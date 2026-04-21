# TransportReservation / ProcessReservation Firmware Audit

## Executive summary

- Exact symbols `TransportReservation` and `ProcessReservation` are **not present** in the ESP32 firmware codebase.
- Equivalent behavior is **partially implemented under different names**, mainly via:
  - `ProcessCellReservationID`
  - `TransportCellReservationID`
  - `ProcessCellID`
  - `TransportCellID`
- Reservation IDs are stored in `TRecipeStep`, propagated through state-machine logic, used for OPC UA reservation lifecycle calls, and written back to NFC tag memory through generic struct-level NFC sync/write functions.
- There is already reader-side anti-duplicate / anti-repeat behavior for remote reservation requests (`re-scan guard`, `transport gate`, `legacy flow guard`), but it is **flow-level**, not a dedicated generic mutex abstraction for reservation objects.
- Final conclusion: **partially implemented**.

## Exact matches found

### Requested identifiers

- `TransportReservation`: no matches in firmware source.
- `ProcessReservation`: no matches in firmware source.
- `PriceForTransport`: present in:
  - `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
  - `ESP32_Firmware_ASS_Interpreter/main/app.c`
- `PriceForProcess`: present in:
  - `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
  - `ESP32_Firmware_ASS_Interpreter/main/app.c`
- `TransportCellID`: present in:
  - `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
  - `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`
  - `ESP32_Firmware_ASS_Interpreter/main/app.c`
- `ProcessCellID`: present in:
  - `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h`
  - `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`
  - `ESP32_Firmware_ASS_Interpreter/main/app.c`

### Reservation keyword family

- `reservation/reserve/reserved/consume/consumed` hits are dominated by:
  - Reservation lifecycle functions in `NFC_recipes.c` and `OPC_klient.c`
  - Cross-cell and PLC AAS reserve flow in `main/app.c`
- `consume/consumed` semantics specific to recipe step payload fields were **not found**.

### Byte offsets requested (`0x08, 0x09, 0x0C, 0x0D`)

- These hex constants are found in PN532 / protocol / third-party code (e.g. `pn532.c`, `pn532.h`, `mdns_private.h`, `open62541.c`) but **not as recipe-step field offsets** for `TransportReservation`/`ProcessReservation`.

## Similar / possibly related logic

## Existing structs and offsets

### Struct carrying equivalent reservation fields

- `TRecipeStep` in `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.h` contains:
  - `PriceForTransport`
  - `TransportCellID`
  - `TransportCellReservationID`
  - `PriceForProcess`
  - `ProcessCellID`
  - `ProcessCellReservationID`
  - `TimeOfProcess`
  - `TimeOfTransport`
  - flags (`NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone`)

Interpretation:
- `TransportCellReservationID` and `ProcessCellReservationID` are the closest existing equivalents to requested `TransportReservation` and `ProcessReservation`.

### Parsing from NFC/tag memory

- Recipe step parsing is done by raw struct-byte transfer, not per-field parsers:
  - `NFC_LoadTRecipeSteps()` in `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`
  - `NFC_LoadTRecipeStep()` in `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`
- Because the full `TRecipeStep` bytes are loaded, reservation fields are loaded implicitly as part of the struct.

### Writing back to NFC/tag memory

- Fields are persisted via struct write + sync pipeline:
  - `NFC_Handler_WriteStep()`
  - `NFC_Handler_WriteSafeStep()`
  - `NFC_Handler_WriteSafeInfo()`
  - `NFC_Handler_Sync()`
  - `NFC_WriteCheck()`
  - `NFC_WriteStructRange()`
- Relevant files:
  - `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`
  - `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`

### State-machine usage

- Reservation IDs are used in branching and lifecycle handling in `ESP32_Firmware_ASS_Interpreter/main/app.c`, e.g. conditions around:
  - `TransportCellReservationID`
  - `ProcessCellReservationID`
  - `NeedForTransport`
  - calls to reservation lifecycle functions.
- Reservation request/finish functions are implemented in `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`:
  - `AskForValidOffer()`
  - `ReserveAllOfferedReservation()`
  - `DoReservation()`
  - `IsDoneReservation()`

### OPC UA payload building / transport

- Reservation objects and IDs are used in OPC UA method calls through:
  - `Inquire()`
  - `GetInquireIsValid()`
  - `Reserve()`
  - `DoReservation_klient()`
  - `IsFinished()`
  - file: `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`
- PLC AAS path in `main/app.c` builds 5-field string payloads (`sr_id/priority/material/parameterA/parameterB`) and uses:
  - `OPC_GetSupported()`
  - `OPC_ReserveAction()`
  - `OPC_GetStatus()`
- These payload builders do **not** expose named fields `TransportReservation` / `ProcessReservation`; reservation linkage is handled by reservation IDs and state logic.

## Existing read/write helpers

### Generic helpers for recipe-step byte handling

- Present:
  - `NFC_LoadTRecipeSteps()`
  - `NFC_LoadTRecipeStep()`
  - `NFC_WriteStruct()`
  - `NFC_WriteStructRange()`
  - `NFC_WriteCheck()`
  - `NFC_CheckStructArrayIsSame()`
- They operate by copying serialized bytes of `TRecipeInfo` / `TRecipeStep` structures into/out of tag memory pages/blocks.

### Helpers for updating step data on tag

- Present:
  - `NFC_Handler_WriteStep()`
  - `NFC_Handler_WriteSafeStep()`
  - `NFC_Handler_Sync()`
  - `NFC_Handler_GetRecipeStep()`
  - `NFC_Handler_GetRecipeInfo()`

### Helpers for persisting state after OPC UA communication

- Present and used in `main/app.c` AAS/legacy flow:
  - `NFC_Handler_WriteStep()`
  - `NFC_Handler_WriteSafeInfo()`
  - `NFC_Handler_Sync()`
- This confirms persistence back into tag after remote calls/status transitions is already implemented.

## Existing anti-duplicate / mutex-like behavior

### Found logic

- `main/app.c` has multiple guards against duplicate reservation/action requests:
  - re-scan guard (`s_lastSeenSrId`, `s_lastActionTimestampMs`, `AAS_RESCAN_GUARD_MS`)
  - transport gate (`s_transportGate`, `transport_gate_set()`, `transport_gate_matches_runtime()`, `transport_gate_reset()`)
  - legacy flow guard (`s_legacyFlowGuard`, `legacy_flow_guard_*`)
  - comment explicitly states idempotent handling around `ReportProduct`.

### Assessment

- This is effectively a mutex/idempotency layer at orchestration/state level.
- It is **not** implemented as a dedicated generic reservation mutex object/API specifically named around `TransportReservation` / `ProcessReservation`.

## Recommendation

- Reuse existing data model and flow where possible:
  - `ProcessCellReservationID` and `TransportCellReservationID` should be treated as canonical existing reservation identifiers.
  - Existing NFC read/write + sync infrastructure should be reused (do not add low-level byte write logic unless absolutely necessary).
  - Existing anti-duplicate guards in `main/app.c` should be extended rather than replaced.
- If new fields must exactly match names `TransportReservation` / `ProcessReservation` (for external contract/spec reasons), add them as explicit mapping aliases to current fields to avoid duplicate state sources.
- For offset-specific integration (0x08/0x09/0x0C/0x0D), introduce explicit documented mapping only if required by external tag format spec; current code does not express those offsets for recipe reservations.

## Final conclusion

**partially implemented**

- Not implemented under the exact names `TransportReservation` / `ProcessReservation`.
- Implemented in equivalent form via `TransportCellReservationID` / `ProcessCellReservationID`, including parse/store/use/writeback and OPC-UA-driven lifecycle.
