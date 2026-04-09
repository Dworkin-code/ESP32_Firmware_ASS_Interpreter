# Target Reservation Gate Implementation

## Files and functions changed

- Changed file: `ESP32_Firmware_ASS_Interpreter/main/app.c`
- Functions updated:
  - `reserve_remote_target(...)`
  - `State_Machine(...)` in `State_Mimo_Polozena` non-local `REQUEST_TRANSPORT` branch
- New helper function:
  - `resolve_target_cell_for_step(...)`

## Where remote target reservation was inserted

- In `State_Mimo_Polozena`, inside the AAS flow branch where decision is:
  - `AAS_DECISION: REQUEST_TRANSPORT`
- Before transport gate check / transport request path, firmware now executes:
  - `reserve_remote_target(&iHandlerData, sr_id_buf, MyCellInfo.IDofCell, Parametry->xEthernet, &targetCellId, &targetStepIndex)`

## How gate is now set

- On non-local branch:
  1. `reserve_remote_target(...)` is called first.
  2. On `TARGET_RESERVE_RESULT_SUCCESS`, firmware calls:
     - `transport_gate_set(sr_id_buf, targetStepIndex, targetCellId)`
  3. Then existing gate validation remains:
     - `transport_gate_matches_runtime(...)`
  4. If gate matches, transport request path is executed.
- On reservation failure:
  - firmware calls `transport_gate_reset("target_reserve_failed")`
  - transport remains blocked by existing gate check

## Reservation success semantics implemented

- Target reservation is considered successful only if:
  - `OPC_GetSupported(...)` indicates support (`support > 0`)
  - and `OPC_ReserveAction(...)` is accepted (not error)
- This is reflected in logs:
  - `TARGET_RESERVE support=<...> result=SUCCESS|REJECTED|ERROR`

## Additional behavior in `reserve_remote_target(...)`

- Function now resolves target in this order:
  1. Try current step target resolution (`resolve_target_cell_for_step(...)`)
  2. Fallback to previous next-step logic (`resolve_next_target_cell(...)`)
- This keeps compatibility with existing call site after local process completion while enabling non-local current-step reservation.

## What remained unchanged

- Local AAS processing path (`LOCAL_PROCESS`) is unchanged.
- Existing transport gate data model and matching logic are unchanged:
  - `s_transportGate`
  - `transport_gate_set(...)`
  - `transport_gate_reset(...)`
  - `transport_gate_matches_runtime(...)`
- Existing transport request gating points in transport states remain unchanged.
- No cancel/release/lease logic was added in this version.
