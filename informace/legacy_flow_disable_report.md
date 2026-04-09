# Legacy Flow Disable Report

## Legacy flow trigger location

- File: `ESP32_Firmware_ASS_Interpreter/main/app.c`
- Function: `State_Machine()`
- Trigger chain for non-local step after target-first transport request:
  1. In `State_Mimo_Polozena` (AAS non-local branch), after `TARGET_RESERVE` and gate-allowed `TRANSPORT_REQUEST`, the state machine sets:
     - `RAF = State_Inicializace_ZiskaniAdres`
     - `RAFnext = State_Poptavka_Vyroba`
  2. `State_Inicializace_ZiskaniAdres` calls `GetCellInfoFromLDS(...)`
  3. `State_Poptavka_Vyroba` calls `GetWinningCell(...)`

This is the legacy LDS-based branch that could still run after the new target-first path.

## Disable mechanism (condition/flag)

- Added step-scoped guard state:
  - `LegacyFlowGuard s_legacyFlowGuard`
  - fields:
    - `sr_id`
    - `stepIndex`
    - `targetReserveSuccessful`
    - `transportRequestExecuted`
- Added helper functions:
  - `legacy_flow_guard_reset()`
  - `legacy_flow_guard_mark_target_reserve_success(sr_id, stepIndex)`
  - `legacy_flow_guard_mark_transport_request_executed(sr_id, stepIndex)`
  - `legacy_flow_guard_should_skip(handler, &sr_id_out, &stepIndexOut)`

Exact effective skip condition:

- `targetReserveSuccessful == true`
- `transportRequestExecuted == true`
- runtime `sr_id` (from UID) matches guard `sr_id`
- runtime `ActualRecipeStep` matches guard `stepIndex`

## Guard insertion point

- Inserted at the earliest entry point to legacy resolution:
  - `State_Inicializace_ZiskaniAdres` in `State_Machine()`
- Behavior when condition matches:
  - log:
    - `LOGI("LEGACY_FLOW skipped due to target-first orchestration (sr_id=%u step=%u)", sr_id, stepIndex);`
  - state transition:
    - `RAF = State_Mimo_Polozena`
    - `continue;`
- This prevents entering LDS-based `GetCellInfoFromLDS` and downstream `State_Poptavka_Vyroba`/`GetWinningCell` for that non-local, already-transport-requested step.

## Confirmations

- Legacy code was **not deleted**:
  - `GetCellInfoFromLDS` remains unchanged.
  - `GetWinningCell` invocation remains unchanged.
  - `State_Inicializace_ZiskaniAdres` and `State_Poptavka_Vyroba` logic remains present.
- New target-first orchestration remains active and unaffected:
  - target reservation flow unchanged.
  - gate logic unchanged.
  - transport request logging/execution unchanged.
  - only legacy branch entry is conditionally skipped for the specific handled non-local step.

## Example log sequence

```text
TARGET_RESERVE start sr_id=12345678 step=2 targetCellId=3
TARGET_RESERVE support=1 result=SUCCESS
[TARGET_RESERVE] targetCellId=3 support=1 reserveResult=SUCCESS
[TRANSPORT_REQUEST] executed reason=gate_allowed endpoint=192.168.168.64:4840
I (12345) APP: LEGACY_FLOW skipped due to target-first orchestration (sr_id=12345678 step=2)
```

