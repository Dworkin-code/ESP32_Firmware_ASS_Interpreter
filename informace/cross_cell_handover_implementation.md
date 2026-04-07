# Cross-cell handover implementation

## Scope implemented

This change implements the first cross-cell AAS handover milestone in `ESP32_Firmware_ASS_Interpreter`:

- local production step completes on current reader/cell
- reader resolves next production cell from recipe + runtime-discovered cells
- reader calls remote production-cell AAS methods:
  - `GetSupported(InputMessage)`
  - `ReserveAction(InputMessage)`
  - `GetStatus(sr_id)` (optional polling/log)
- transport cell behavior is not implemented or required in this step

## Changed code

Main implementation was added in:

- `ESP32_Firmware_ASS_Interpreter/main/app.c`

No transport architecture redesign was done.
Existing legacy flow and existing AAS wrappers were preserved.

## State/function changes

### State machine insertion point

The remote handover is triggered in:

- `State_Vyroba_SpravneProvedeni`
- branch `IsDoneReservation(...) == 1` ("Process je hotov")

This is the local completion point of production on current cell.  
After local step success is detected (and before NFC info write-back finalization), the new remote reservation attempt is executed.

### New helper functions added (in `app.c`)

- `normalize_cell_endpoint(...)`
  - normalizes endpoint format (removes `opc.tcp://` prefix when present), because OPC client builder in `ClientStart` already prepends protocol.

- `parse_supported_positive(...)`
  - checks whether `GetSupported` response allows reservation (`Support:X`, `X > 0`, and non-error fallback handling for simulation).

- `resolve_next_target_cell(...)`
  - resolves next recipe step target cell from recipe flow and LDS-discovered runtime cell list.
  - skips local cell (`MyCellInfo.IDofCell`).
  - prefers `nextStep.ProcessCellID` when already set.

- `build_target_action_message(...)`
  - builds PLC-compatible 5-field `InputMessage`:
    - `sr_id/priority/material/parameterA/parameterB`
  - reuses same `sr_id` for local/remote calls.

- `poll_remote_target_status(...)`
  - optional `GetStatus` polling/logging for simulation visibility.

- `reserve_remote_target(...)`
  - orchestration helper:
    1. resolve next target
    2. skip if target is local
    3. call remote `GetSupported`
    4. if supported, call remote `ReserveAction`
    5. poll/log remote `GetStatus`
    6. log success/failure

## Configuration/runtime assumptions used

- Firmware remains generic (same binary for all readers).
- Behavior depends on:
  - `MyCellInfo.IDofCell`
  - runtime-discovered cell endpoints (`GetCellInfoFromLDS`)
  - recipe data on NFC tag (`TRecipeInfo`, `TRecipeStep`, `NextID`)
- No hardcoded `cell A -> cell B` branch logic.
- Same `sr_id` is generated from NFC UID using existing `OPC_BuildSrIdFromUid(...)`.
- Existing OPC/AAS wrapper layer is used (`OPC_GetSupported`, `OPC_ReserveAction`, `OPC_GetStatus`).

## Logging added

New `[CROSS_CELL]` logs cover:

- local completion reached
- next target resolved
- remote `GetSupported` request/response
- remote `ReserveAction` request/response
- remote reservation success/failure
- remote `GetStatus` poll output

## Uncertainties documented

- `GetCellInfoFromLDS(...)` currently returns static simulation endpoints; production endpoint provisioning may need alignment.
- `GetSupported` output format is interpreted with tolerant parsing for simulation (`Support:X` preferred; non-error non-empty accepted).
- This step reserves next production cell only; no transport reservation/execution is included.
