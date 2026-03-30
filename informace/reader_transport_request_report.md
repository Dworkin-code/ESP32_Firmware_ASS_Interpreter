# Reader Transport Request Report (Current Implementation)

## Executive summary

The current reader firmware has two transport-related mechanisms:

1. **Legacy state-machine transport reservation/execution** in `main/app.c` + `components/NFC_Recipes/NFC_recipes.c` + `components/OPC_Klient/OPC_klient.c` (methods `Inquire`, `IsValid`, `Rezervation`, `DoProcess`, `IsFinished`).
2. **PLC AAS flow** in `main/app.c` + `components/OPC_Klient/OPC_klient.c` (`ReportProductEx`, `GetSupported`, `ReserveAction`, `GetStatus`), where non-`ToStorageGlass` steps are explicitly routed to the legacy transport/production branch.

In the current code, transport is requested when the chosen process cell is not the current cell, and the request is represented primarily by reservation fields written to NFC step data (`NeedForTransport`, `TransportCellID`, `TransportCellReservationID`) plus OPC UA `Inquire` calls to transport cells.

## Trigger condition for transport request

### Primary trigger after process cell selection

In `main/app.c` (`State_Poptavka_Vyroba`):

- Firmware calls `GetWinningCell(...)` for the step's `TypeOfProcess`.
- It stores selected process reservation into the current step:
  - `ProcessCellID = Process.IDofCell`
  - `ProcessCellReservationID = Process.IDofReservation`
- Then transport need is decided by:
  - `if (MyCellInfo.IDofCell != Process.IDofCell) tempStep.NeedForTransport = true;`

So transport is required when **current reader cell ID differs from selected process cell ID**.

### Additional routing conditions already present on tag/state

In `main/app.c` (`State_Mimo_Polozena` decision branch), transport path is also entered when recipe step flags already indicate transport context, for example:

- `ProcessCellReservationID && NeedForTransport` -> routes to `State_Poptavka_Transporty`.
- `TimeOfTransport > 0 && MyCellInfo.IDofCell != ProcessCellID` -> routes to `State_Transport`.
- `IsTransport == true` -> routes via `State_Vyroba_Objeveni`.

### AAS decision to legacy transport branch

In `main/app.c` (AAS flow block):

- If `step->TypeOfProcess == ToStorageGlass`, AAS methods are called directly (`GetSupported`/`ReserveAction`).
- Else firmware logs `AAS_DECISION: REQUEST_TRANSPORT` and routes to legacy branch:
  - `RAF = State_Inicializace_ZiskaniAdres;`
  - `RAFnext = State_Poptavka_Vyroba;`

This means non-`ToStorageGlass` operations currently use legacy process/transport routing.

## Full call flow

### Legacy transport request flow (reservation stage)

1. `State_Mimo_Polozena` loads NFC data and routes.
2. `State_Inicializace_ZiskaniAdres` builds candidate cells with `GetCellInfoFromLDS(step->TypeOfProcess, &BunkyVelikost)`.
3. `State_Poptavka_Vyroba`:
   - Calls `GetWinningCell(...)` for process.
   - Writes `ProcessCellID`, `ProcessCellReservationID`.
   - Sets `NeedForTransport` based on cell mismatch.
   - Writes updated step via `NFC_Handler_WriteSafeStep(...)`.
   - Moves to `State_Poptavka_Transporty`.
4. `State_Poptavka_Transporty`:
   - If transport not needed and not transport operation: go to `State_Rezervace`.
   - Else calls `GetWinningCell(...)` for `Transport`.
   - Writes `NeedForTransport = true`, `TransportCellID`, `TransportCellReservationID`.
   - Writes step via `NFC_Handler_WriteSafeStep(...)`.
   - Goes to `State_Rezervace`.
5. `State_Rezervace`:
   - `AskForValidOffer(...)` -> per-reservation `GetInquireIsValid(...)` -> OPC UA method `IsValid`.
   - `ReserveAllOfferedReservation(...)` -> per-reservation `Reserve(...)` -> OPC UA method `Rezervation`.
   - Reservation times are written back (`TimeOfProcess`, `TimeOfTransport`), then `NFC_Handler_Sync(...)`.
6. `State_Transport`:
   - Validity check call path includes `AskForValidReservation(...)` (currently TODO/returns 0).
   - Writes `IsTransport=1`, adjusts budget, writes NFC.
   - Starts transport execution: `DoReservation(..., false)` -> `DoReservation_klient(...)` -> OPC UA method `DoProcess`.

### Functions directly involved (ordered by core request path)

- `State_Machine` (task function in `main/app.c`)
- `GetCellInfoFromLDS`
- `GetWinningCell`
- `Inquire` (OPC UA `Inquire` call)
- `NFC_Handler_WriteSafeStep`
- `AskForValidOffer` -> `GetInquireIsValid` (OPC UA `IsValid`)
- `ReserveAllOfferedReservation` -> `Reserve` (OPC UA `Rezervation`)
- `DoReservation` -> `DoReservation_klient` (OPC UA `DoProcess`)

## Current payload / message format

## Legacy transport request payload (actual request)

Transport "request" is made by OPC UA method `Inquire` in `Inquire(...)` with **5 scalar arguments**:

1. `IDInterpreter` (`UA_TYPES_UINT16`) - passed as `MyCellInfo.IDofCell` in call sites.
2. `TypeOfProcess` (`UA_TYPES_BYTE`) - `Transport`.
3. `priority` (`UA_TYPES_BOOLEAN`) - currently `false` in shown call sites.
4. `param1` (`UA_TYPES_BYTE`) - depends on branch:
   - if `ProcessCellID == Transport`: uses `ParameterProcess1`
   - else uses `ProcessCellID` (target-like selector)
5. `param2` (`UA_TYPES_UINT16`) - currently `0` in transport inquiry calls.

Returned output (when valid):

- `output[0]` -> reservation ID (`IDofReservation`)
- `output[1]` -> `Price`
- `output[2]` -> `TimeOfReservation`

Stored into recipe step:

- `NeedForTransport`
- `TransportCellID`
- `TransportCellReservationID`
- later `TimeOfTransport`

### AAS message format (separate path)

For AAS branch (`ToStorageGlass` only), string format is:

- `sr_id/priority/material/parameterA/parameterB`

Built in `app.c` as:

- `"%s/0/%u/%u/%u"` (`sr_id`, `TypeOfProcess`, `ParameterProcess1`, `ParameterProcess2`)

Used in:

- `OPC_GetSupported` (method node `ns=4;i=7003`)
- `OPC_ReserveAction` (method node `ns=4;i=7005`)

This is not the same transport reservation payload as legacy `Inquire`.

## OPC UA interaction used

### Legacy transport-related OPC UA methods

Defined in `components/OPC_Klient/OPC_klient.c`:

- `Inquire` (called as `UA_Client_call(..., UA_NODEID_STRING(1, "Inquire"), 5, ...)`)
- `IsValid` (`UA_NODEID_STRING(1, "IsValid"), 1 arg`)
- `Rezervation` (`UA_NODEID_STRING(1, "Rezervation"), 1 arg`)
- `DoProcess` (`UA_NODEID_STRING(1, "DoProcess"), 1 arg`)
- `IsFinished` (`UA_NODEID_STRING(1, "IsFinished"), 1 arg`)

Object node used for these calls:

- `UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER)`

### AAS OPC UA methods (parallel mechanism)

Defined by numeric node IDs in ns=4:

- `CurrentId`: `ns=4;i=6101` (write)
- `ReportProduct`: `ns=4;i=7004`
- `GetSupported`: `ns=4;i=7003`
- `ReserveAction`: `ns=4;i=7005`
- `GetStatus`: `ns=4;i=7002`
- `FreeFromPosition`: `ns=4;i=7000`

Call style for AAS methods:

- `UA_Client_call(methodId, methodId, 1, STRING InputMessage, ...)`

## Logging / diagnostics

### State machine / transport logs

In `main/app.c`:

- `"Je potreba transport(Aktualne %d- cil %d\n"`
- `"Poptavka transportu"`
- `"Neni potreba transport"`
- `"Je potreba transport(%d)"`
- `"Nelze vybrat transportni bunku"`
- `"Zapisuji data o vyherni transportni bunce"`
- `"Stav transport"`
- `"Moznost provedeni transportu"`
- `"Cekam nez tag zmizi po odebrani transportem"`
- `"Jsme u spatne bunky, pridavam transport"`
- `"Transport se zapsal"`

### OPC UA/AAS logs

In `OPC_klient.c` and `app.c`:

- `OPC_GetSupported: InputMessage=...`
- `OPC_ReserveAction: InputMessage=...`
- `OPC_CallAasMethod(...): OutputMessage=...`
- `AAS_DECISION: REQUEST_TRANSPORT (...)`
- `AAS: ReserveAction failed or Error -> ...`

### Error handling behavior

- If transport cell selection fails (`Error != 0` from `GetWinningCell`), branch logs and retries/continues.
- Ethernet semaphore acquisition failures log and continue.
- AAS branch errors may set `RecipeDone=true` and write back to NFC.

## Relationship to current AAS logic

- Transport logic is **not fully replaced** by AAS in current implementation.
- AAS flow directly handles `ToStorageGlass` via string-based AAS methods.
- For other step types, AAS branch explicitly routes into legacy process/transport state machine (`REQUEST_TRANSPORT` log + `State_Poptavka_Vyroba` path).
- Legacy transport uses reservation-centric OPC UA methods (`Inquire`, `Rezervation`, etc.) with typed arguments, not the 5-field AAS string.

## Uncertainties / not confirmed items

- Exact semantic meaning of `param1` and `param2` for transport `Inquire` on PLC side is **not confirmed from code**.
- Whether `Price` selection logic in `GetWinningCell` is intended (current code selects higher `Price`) is **not confirmed from code**.
- `AskForValidReservation(...)` is TODO and currently returns `0`; detailed validation in `State_Transport` is therefore **not confirmed from code**.
- Whether any PLC-side mapping converts legacy `Inquire` calls into AAS entities is **not confirmed from code** (reader-side code does not prove it).
