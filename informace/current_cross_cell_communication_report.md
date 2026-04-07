# Current Cross-Cell Communication Report

## Executive summary
- Cross-cell communication is already implemented in firmware, but only as a specific handover flow after local process completion in the legacy state path.
- The implemented cross-cell mechanism is not reader-to-reader; it is reader A calling OPC UA/AAS methods on the remote PLC (target production cell provider).
- The remote methods used are `GetSupported`, `ReserveAction`, and a short `GetStatus` polling sequence.
- This remote reservation is currently not used as a hard gate for transport request in the main transport orchestration flow.

## Does cross-cell communication already exist?
Yes. There is explicit code for remote target-cell interaction:
- `resolve_next_target_cell(...)`
- `reserve_remote_target(...)`
- `poll_remote_target_status(...)`

All are defined in `ESP32_Firmware_ASS_Interpreter/main/app.c` and called from the production completion branch.

## Communication form
- **Implemented form:** reader -> remote PLC/AAS provider of another production cell.
- **Not implemented form:** direct reader-to-reader protocol.

The remote endpoint is taken from `CellInfo.IPAdress` (resolved via `GetCellInfoFromLDS(...)`) and normalized by `normalize_cell_endpoint(...)`. Calls then go through `OPC_GetSupported(...)`, `OPC_ReserveAction(...)`, `OPC_GetStatus(...)` in `OPC_klient.c`.

## Exact methods currently used for remote production-cell interaction
In remote-target reservation flow (`reserve_remote_target`):
1. `OPC_GetSupported(endpointBuf, inputMsg, outBuf, ...)`
2. `OPC_ReserveAction(endpointBuf, inputMsg, outBuf, ...)`
3. `poll_remote_target_status(endpointBuf, sr_id)` -> internally calls `OPC_GetStatus(...)` up to `CROSS_CELL_STATUS_POLLS` times.

Message format for remote target calls is built by `build_target_action_message(...)` as:
- `sr_id/localCellId/typeOfProcess/parameter1/parameter2`

## Where cross-cell logic starts and how it is triggered
Primary trigger point is in `State_Vyroba_SpravneProvedeni` when local process is reported finished (`case 1` from `IsDoneReservation(...)`):
- Step completion and next-step update happen.
- If recipe is not done and `sr_id` is available:
  - `(void)reserve_remote_target(&iHandlerData, finishedSrId, MyCellInfo.IDofCell, Parametry->xEthernet);`

Important properties of this trigger:
- It happens **after local process completion**.
- It is a best-effort side call (`(void)` return ignored).
- Failure does not block further flow.

## Relation to AAS/local flow and transport routing
There are two distinct runtime paths:

1. **Newer local AAS decision path** (inside `State_Mimo_Polozena`, under `USE_PLC_AAS_FLOW`):
   - Local owner cell runs local `GetSupported`/`ReserveAction` + `OPC_AAS_WaitCompletionPoll`.
   - Non-local owner cell directly routes to existing routing states:
     - `State_Inicializace_ZiskaniAdres`
     - `State_Poptavka_Vyroba`
     - transport selection/reservation chain.
   - This path does **not** call `reserve_remote_target(...)` before requesting transport.

2. **Legacy completion branch** (`State_Vyroba_SpravneProvedeni`):
   - After successful local process completion, firmware may call `reserve_remote_target(...)` for next step.
   - This is where cross-cell AAS handover is currently active.

## Current limitations
- Remote target reservation is not integrated as mandatory prerequisite for transport request.
- Remote handover call result is not consumed for state transition decisions (logging + best effort).
- `poll_remote_target_status(...)` confirms only that `GetStatus` call returned; it does not enforce terminal status (`finished`) before continuing.
- Because the codebase contains both newer AAS decision flow and legacy reservation/transport flow, behavior is partially split; cross-cell remote reservation is only present in one branch.

## Current-state conclusion
- Cross-cell communication **exists**.
- It is implemented as **reader-to-remote-PLC AAS calls**, not reader-to-reader.
- It is currently **partially integrated**: present as post-completion remote handover/test-like behavior, but not connected as strict gate in full target-first transport orchestration.
