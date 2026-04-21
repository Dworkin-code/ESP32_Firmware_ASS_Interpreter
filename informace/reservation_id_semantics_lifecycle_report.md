# ProcessCellReservationID / TransportCellReservationID - semantic and lifecycle audit

## Short conclusion

`ProcessCellReservationID` and `TransportCellReservationID` are **reservation identifiers (handles/tokens)** coming from OPC UA `Inquire` / `Rezervation` flows, not simple mutex states.  
They are used as opaque IDs passed back into OPC UA methods (`IsValid`, `Rezervation`, `DoProcess`, `IsFinished`).  
Repurposing them directly to enum-like state values (`0/1/2/3`) would break existing reservation logic.

**Final verdict: must not be repurposed.**

---

## 1) Field declaration and data type (exact)

File: `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`  
Struct: `TRecipeStep`

- `uint16_t TransportCellReservationID;`
- `uint16_t ProcessCellReservationID;`

Both are 16-bit unsigned integers in packed NFC step payload.

---

## 2) All read/write/compare/reset/copy/sync locations

Scope analyzed: `ESP32_Firmware_ASS_Interpreter/**/*.c,*.h`

## A) Direct writes to these fields

Only these two direct assignments exist:

1. File: `ESP32_Firmware_ASS_Interpreter/main/app.c`  
   Function/state block: `State_Poptavka_Vyroba`  
   Snippet: `tempStep.ProcessCellReservationID = Process.IDofReservation;`

2. File: `ESP32_Firmware_ASS_Interpreter/main/app.c`  
   Function/state block: `State_Poptavka_Transporty`  
   Snippet: `tempStep.TransportCellReservationID = Process.IDofReservation;`

Then written to NFC by:
- `NFC_Handler_WriteSafeStep(&iHandlerData, &tempStep, ActualRecipeStep)`

## B) Direct comparisons / branching conditions

File: `ESP32_Firmware_ASS_Interpreter/main/app.c` (`State_Mimo_Polozena`)

1. Truthy check (non-zero semantics):
```c
...TransportCellReservationID || (ProcessCellReservationID && !NeedForTransport)
```
2. Truthy check (non-zero semantics):
```c
...ProcessCellReservationID && NeedForTransport
```

No `== specific_id` matching logic; branch decisions are presence/absence (`0` vs non-zero), not ID value interpretation.

## C) Reads (used as reservation handle input)

File: `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`

- `AskForValidOffer(...)`
  - `tempReservation.IDofReservation = ...ProcessCellReservationID;`
  - `tempReservation.IDofReservation = ...TransportCellReservationID;`
  - Used in `GetInquireIsValid(...)`

- `ReserveAllOfferedReservation(...)`
  - reads both IDs into `tempReservation.IDofReservation`
  - used in `Reserve(...)`

- `DoReservation(...)`
  - reads one of the IDs for active step
  - used in `DoReservation_klient(...)`

- `IsDoneReservation(...)`
  - reads one of the IDs for active step
  - used in `IsFinished(...)`

These are all opaque token pass-through uses.

## D) Reset locations

No direct runtime assignment resetting these fields to `0` was found.

Implicit reset/initial zero happens when a step is created from:
- `EmptyRecipeStep` in `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h`
  - `static const TRecipeStep EmptyRecipeStep = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };`
- `GetRecipeStepByNumber(...)` starts with `TRecipeStep tempRecipeStep = EmptyRecipeStep;`

Therefore reset occurs by creating/replacing recipe steps, not by explicit lifecycle cleanup of existing step IDs.

## E) Copy locations (field copied as part of whole struct memory/assignment)

The fields are copied indirectly via full `TRecipeStep` copy:

- `NFC_Handler_WriteStep(...)` (`NFC_handler.c`)
  - `aHandlerData->sWorkingCardInfo.sRecipeStep[aIndex] = *aRecipeStep;`

- `NFC_Handler_Sync(...)` (`NFC_handler.c`)
  - byte-level copy of whole step from working to integrity:
  - `*((uint8_t *)...sIntegrityCardInfo.sRecipeStep + ...) = *((uint8_t *)...sWorkingCardInfo.sRecipeStep + ...);`

- `NFC_LoadTRecipeSteps(...)` / `NFC_LoadTRecipeStep(...)` (`NFC_reader.c`)
  - raw bytes read from tag into `aCardInfo->sRecipeStep` memory

## F) Synchronization back to NFC tag

Paths that persist these two fields to tag:

1. `app.c` write path (immediate safe step):
   - assign reservation ID to `tempStep`
   - `NFC_Handler_WriteSafeStep(...)`  
   (`NFC_Handler_WriteSafeStep` writes step index `aIndex+1` via `NFC_WriteCheck(...)`)

2. Buffered path:
   - `NFC_Handler_WriteStep(...)` -> marks index dirty
   - `NFC_Handler_Sync(...)` -> `NFC_WriteCheck(...)` flushes changed ranges

---

## 3) Exact write semantics (value, source, meaning)

## Write 1: process reservation

- Function/block: `State_Poptavka_Vyroba` in `main/app.c`
- Assignment: `tempStep.ProcessCellReservationID = Process.IDofReservation;`
- Source chain:
  1. `GetWinningCell(...)` fills `Reservation Process`
  2. `GetWinningCell` calls `Inquire(...)` for candidate cells
  3. `Inquire(...)` (`OPC_klient.c`) assigns:
     - `aRezervace->IDofReservation = *(UA_UInt16 *)output[0].data;`
- Meaning: ID/token returned by OPC UA `Inquire` response.
- Classification: **real reservation ID / handle**, not state enum.

## Write 2: transport reservation

- Function/block: `State_Poptavka_Transporty` in `main/app.c`
- Assignment: `tempStep.TransportCellReservationID = Process.IDofReservation;`
- Source chain identical:
  - `GetWinningCell(...)` -> `Inquire(...)` -> OPC UA output[0] -> reservation ID
- Classification: **real reservation ID / handle**, not state enum.

---

## 4) Comparison semantics

Comparisons do **not** test specific ID values; they test zero/non-zero existence:

- `if (TransportCellReservationID || (ProcessCellReservationID && !NeedForTransport))`
- `else if (ProcessCellReservationID && NeedForTransport)`

So branch control expects "have any reservation token?" semantics, not token equality semantics.

However, later logic requires true token meaning because the same fields are passed into OPC UA APIs expecting valid reservation IDs.

---

## 5) Reconstructed lifecycle (step-by-step)

Plain-text lifecycle for both fields (per recipe step):

1. **Initial value**
   - New/generated step from `EmptyRecipeStep` -> both IDs = `0`.
   - Existing card load -> values read from NFC bytes (`NFC_LoadTRecipeSteps`).

2. **After inquiry**
   - `State_Poptavka_Vyroba`: `ProcessCellReservationID <- Inquire(...).IDofReservation`
   - `State_Poptavka_Transporty`: `TransportCellReservationID <- Inquire(...).IDofReservation`
   - Values persisted to tag by `NFC_Handler_WriteSafeStep`.

3. **After reservation confirmation**
   - `State_Rezervace` -> `ReserveAllOfferedReservation(...)` reads these IDs, calls OPC `Rezervation`.
   - Time fields are updated (`TimeOfProcess`, `TimeOfTransport`); reservation ID fields remain IDs.

4. **During execution**
   - `DoReservation(...)` reads ID and calls OPC `DoProcess`.
   - `IsDoneReservation(...)` reads ID and calls OPC `IsFinished`.
   - Routing in `State_Mimo_Polozena` uses non-zero presence of IDs.

5. **After finish / done / cancel**
   - No explicit ID cleanup/reset observed.
   - Step completion toggles flags (`IsProcess`, `IsTransport`, `IsStepDone`) and recipe indices.
   - IDs remain until step data is replaced/reset by new recipe step content.

---

## 6) Risk if converted to pure mutex state (`NOT_RESERVED/RESERVED/CONSUMED/ERROR`)

High breakage risk:

- Current code forwards field values to OPC UA methods as reservation handle input:
  - `IsValid`
  - `Rezervation`
  - `DoProcess`
  - `IsFinished`
- Replacing IDs with enum states would send invalid IDs to those methods.
- This breaks:
  - offer validity checks (`AskForValidOffer`)
  - reservation confirmation (`ReserveAllOfferedReservation`)
  - start execution (`DoReservation`)
  - completion polling (`IsDoneReservation`)

Even though branch checks only need zero/non-zero, downstream calls need actual token identity.

---

## 7) OPC UA relation (sent to / derived from OPC UA)

Directly derived from OPC UA:
- `Inquire(...)` fills `Reservation.IDofReservation` from OPC UA output variant.

Directly sent to OPC UA:
- `GetInquireIsValid(...)`: sends `IDofReservation` as input scalar
- `Reserve(...)`: sends `IDofReservation`
- `DoReservation_klient(...)`: sends `IDofReservation`
- `IsFinished(...)`: sends `IDofReservation`

Therefore these fields are part of an OPC-UA-coupled reservation protocol, not standalone local state.

---

## 8) Safe mapping point for persistent mutex state (without breaking behavior)

Safe approach is **not in-place repurpose** of these two fields.  
A safe mapping point exists only as an **additional derived state** layer:

- Keep existing IDs unchanged for OPC UA calls.
- Derive mutex-like state from existing lifecycle events:
  - `0` ID -> NOT_RESERVED
  - ID assigned after inquiry -> RESERVED_OFFERED
  - successful `Reserve(...)` + time set -> RESERVED_CONFIRMED
  - `IsFinished == true` -> CONSUMED/DONE
  - reservation/transport/process failures -> ERROR

Best technical insertion point:
- In state-machine transitions around:
  - `State_Poptavka_Vyroba` / `State_Poptavka_Transporty` (ID assignment)
  - `State_Rezervace` (ReserveAllOfferedReservation success/fail)
  - `State_Vyroba_SpravneProvedeni` (IsDoneReservation result)
- But this should use separate field(s) or derived runtime value; do not overwrite reservation ID fields.

---

## Real semantic meaning

- `ProcessCellReservationID`: reservation handle for selected process cell operation.
- `TransportCellReservationID`: reservation handle for selected transport cell operation.
- They are **persistent NFC-carried identifiers** used to continue reservation protocol across reader cycles.
- They are not abstract mutex state slots.

---

## Final verdict

**must not be repurposed**

