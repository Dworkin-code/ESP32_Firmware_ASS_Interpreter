# Local AAS Payload Fix

## What was wrong
In local processing (`LOCAL_PROCESS`) the firmware built local PLC AAS payload as:
`sr_id/MyCellInfo.IDofCell/TypeOfProcess/ParameterProcess1/ParameterProcess2`.

This changed token1 from priority to local cell ID. PLC AAS expects positional tokens:
- token0 = id
- token1 = priority
- token2 = material/classifier
- token3 = parameterA
- token4 = parameterB

Because of that mismatch, local `GetSupported` could return unsupported (e.g. `support:0_position:0`) even for a valid local step.

## What was changed
A dedicated helper was added to build local AAS message with PLC-compatible semantics:
- `build_local_aas_action_message(...)`
- message format now is: `sr_id/0/TypeOfProcess/ParameterProcess1/ParameterProcess2`

So ownership still uses local cell ID, but payload token1 is again priority (`0`).

## Token meanings after fix
For local processing payload:
- token0 = `sr_id`
- token1 = `priority` (fixed to `0`)
- token2 = `TypeOfProcess` (material/classifier for PLC logic)
- token3 = `ParameterProcess1`
- token4 = `ParameterProcess2`

## Ownership decision remains unchanged
Routing decision still uses:
- resolved owner from `resolve_owner_cell_id_from_process_type(step->TypeOfProcess)`
- compared against `MyCellInfo.IDofCell`

Therefore:
- same-cell owner => `LOCAL_PROCESS`
- different owner => `REQUEST_TRANSPORT`

## Exact changed places
File: `ESP32_Firmware_ASS_Interpreter/main/app.c`

1. Added helper function:
- `build_local_aas_action_message(const TRecipeStep *step, const char *sr_id, char *outMsg, size_t outMsgSize)`

2. Local AAS branch in `State_Machine` (`LOCAL_PROCESS` path):
- replaced inline `snprintf(..., MyCellInfo.IDofCell, ...)` with `build_local_aas_action_message(...)`
- added explicit debug log with parsed payload meaning:
  - `AAS: local InputMessage=... [id=... priority=0 material=... pA=... pB=...]`
