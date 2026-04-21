# Transport Simulation Not Starting - Debug Report

## 1. Executive summary

`ReserveAction` success and simulation start are currently disconnected. The reservation path parses and enqueues the item correctly, but the execution path does not pick and launch it.

First blocking condition (highest confidence): in `PLC_code -Transport/program_block/AAS/Passive_AAS.scl`, `CheckAction` is executed only when `#deviceRunning` is already `TRUE`. While idle (`#deviceRunning = FALSE`), no item is selected, so `#isFoundItem` stays false and `DeviceSlow` never starts.

Additional blockers also exist in the execution chain:
- Handshake model in `DeviceSlow` requires machine status feedback (`StsReady/StsBusy/StsDone/StsIdle`) and command pulses (`CmdSetReady/CmdStart/CmdResetDone`), but the visible `Passive_AAS` call does not show these handshake links.
- `Simulace1` runs its state transitions only when `"Simulace".Sim_BTN = TRUE`; no write to `Sim_BTN` exists in `@PLC_code -Transport`, so simulation can remain permanently gated even after command pulses.

## 2. Expected flow vs actual flow

Expected:
1. Reader sends payload (`sr_id/source/type/source/target`).
2. `ReserveAction` validates and pushes item into `PQueue`.
3. Main logic selects executable item (`CheckAction`), sets `Valid = TRUE`.
4. `DeviceSlow` enters sequencer (`#seqState` 0 -> 10 -> 20 -> ...), drives handshake.
5. Simulation/machine moves through `Idle -> Ready -> Busy -> Done`.

Actual (from code):
1. `ReserveAction` succeeds and inserts item into `PQueue` (works).
2. `Passive_AAS` evaluates `CheckAction` only under `IF #deviceRunning THEN`.
3. When idle, `CheckAction` is skipped, so `#isFoundItem` is not refreshed to true.
4. `DeviceSlow` receives `Valid := #isFoundItem` as false and stays in idle branch (`#seqState := 0`).
5. No execution start occurs.

## 3. Input field mapping

In `PLC_code -Transport/program_block/AAS/OPC UA Methods/ReserveAction.scl`:
- `InputArray := GetMessage(...)`
- `element[0]` -> `tempItem.id`
- `element[1]` -> priority for `PQueue_push`
- `element[2]` -> `tempItem.material`
- `element[3]` -> `tempItem."initialPosition/operation"`
- `element[4]` -> `tempItem."finalPosition/operationParameter"`

For observed payload `1177189415/2/3/2/3`:
- id = `1177189415`
- priority = `2`
- material = `3`
- initialPosition/operation = `2`
- finalPosition/operationParameter = `3`

Conclusion: new source/target payload shape is being parsed and stored into the expected item fields.

## 4. Validation path

Validation happens in two places:

1) `ReserveAction.scl`:
- Checks material/initial/final against support ranges (`SupportMaterial...`, `SupportA...`, `SupportB...`).
- If invalid -> `ErrItemNotSupported`, no queue insert.
- If valid -> `PQueue_push`, returns `Success`.

2) `DeviceSlow.scl`, region `CHECK_ITEM`:
- Re-validates `item.material`, `item."initialPosition/operation"`, `item."finalPosition/operationParameter"` against the same support ranges.
- If failed -> `actionStatus := 'failed'`, `Valid := FALSE`, `seqState := 0`.

Observation:
- Reservation success means `ReserveAction` accepted these values at least once.
- Execution still depends on the second validation in `DeviceSlow`; if symbols/ranges changed at runtime, execution can still fail later.

## 5. Execution path

Path in `PLC_code -Transport/program_block/AAS/Passive_AAS.scl`:
- Calls `CheckAction` (but currently only when `#deviceRunning` is true).
- Passes `Valid := #isFoundItem` into `DeviceSlow`.

`DeviceSlow.scl` handshake sequence:
- Requires `Valid = TRUE` to run.
- Uses `#seqState`:
  - `0`: prepare and set source/target (`"Simulace".Source_Cell`, `"Simulace".Target_Cell`)
  - `10`: pulse `CmdSetReady`
  - `20`: wait `StsReady`
  - `30`: pulse `CmdStart`
  - `40`: wait `StsBusy`
  - `50`: wait `StsDone`
  - `60`: pulse `CmdResetDone`
  - `70`: wait `StsIdle`
  - `80`: finish/reset
  - `100`: error/reset

`Simulace1.scl` execution side:
- Entire simulation logic is inside `IF "Simulace".Sim_BTN THEN`.
- `SetReady + Idle` -> `Ready`
- `Ready + Start` -> computes time from `TransportMatrix[source,target]`, sets `Sim_start`
- `Sim_start` -> `Busy`
- `Sim_end` -> `Done`

## 6. First blocking condition

First exact blocker after reservation (deterministic from source):

In `PLC_code -Transport/program_block/AAS/Passive_AAS.scl`:
- `CheckAction` is wrapped by `IF #deviceRunning THEN ... END_IF;`
- While system is idle (`#deviceRunning = FALSE`), no item is searched in queues.
- Therefore `#isFoundItem` remains false, and `DeviceSlow` receives `Valid := FALSE`.
- In `DeviceSlow`, `IF #Valid = TRUE THEN ... ELSE ... #seqState := 0;` keeps the state machine idle forever.

This is the first condition that prevents transition into execution.

## 7. Old time-based assumptions still present

Not fully removed; still present in simulation layer:

- `Simulace1.scl` still computes duration from `"Simulace".TransportMatrix[source,target]` and writes `"Simulace".Set_time_simulace`.
- `Simulace1` depends on timer-based `Sim_start/Sim_end`.
- This is compatible with source/target transport, but it is still time simulation logic (source/target -> matrix time).

Also, execution still uses old support-range semantics naming:
- `material`, `initialPosition/operation`, `finalPosition/operationParameter`
- Checked against `SupportMaterial*`, `SupportA*`, `SupportB*` constants.

## 8. Minimal fix recommendation

Do not change reservation logic first. Apply the smallest execution-path fix:

1. In `Passive_AAS.scl`, run `CheckAction` when idle / always, not only when `#deviceRunning = TRUE`.
   - Minimal change concept: remove or invert the `IF #deviceRunning THEN` gate around `CheckAction`.
2. Ensure `DeviceSlow` handshake I/O is actually wired in the caller:
   - Inputs: `StsIdle`, `StsReady`, `StsBusy`, `StsDone`, `StsError`
   - Outputs: `CmdSetReady`, `CmdStart`, `CmdResetDone`, `CmdProductID`
3. Ensure simulation enable path sets `"Simulace".Sim_BTN` true in runtime (or remove this gate for automatic mode).

Priority order:
- First fix item selection gate (`CheckAction` condition).
- Then verify handshake wiring and `Sim_BTN`.

## 9. Suggested watch table variables for live PLC debugging

Reservation / queue:
- `"PQueue".PQueue.queueProductCount`
- `"PQueue".PQueue.activeItemPosition`
- `"RPQueue".ReportedProductsQueue.staticQueueVariables.statElementCount`
- `ReserveAction` method output message (`Success`/error)

Passive AAS selection:
- `PassiveAAS_DB.deviceRunning` (or equivalent internal)
- `PassiveAAS_DB.isFoundItem` (or mapped internal)
- `PassiveAAS_DB.item.id`
- `PassiveAAS_DB.itemPositionP`
- `PassiveAAS_DB.itemPositionRP`

DeviceSlow execution:
- `DeviceSlow_Instance.Valid`
- `DeviceSlow_Instance.tempFailed`
- `DeviceSlow_Instance.seqState`
- `DeviceSlow_Instance.CmdSetReady`
- `DeviceSlow_Instance.CmdStart`
- `DeviceSlow_Instance.CmdResetDone`
- `DeviceSlow_Instance.StsIdle`
- `DeviceSlow_Instance.StsReady`
- `DeviceSlow_Instance.StsBusy`
- `DeviceSlow_Instance.StsDone`
- `DeviceSlow_Instance.StsError`
- `PassiveAAS_DB.ActionStatus`

Simulation internals:
- `"Simulace".Sim_BTN`
- `"Simulace".Source_Cell`
- `"Simulace".Target_Cell`
- `"Simulace".Set_time_simulace`
- `"Simulace".Sim_start`
- `"Simulace".Sim_end`
- `"Simulace".State_Int`
- `"Simulace".run_time`

Uncertainty notes:
- Numeric values of support constants (`SupportMaterial*`, `SupportA*`, `SupportB*`) are not defined in checked-in text sources; if these symbols were changed in PLC project constants, `ReserveAction` and `DeviceSlow` could validate differently at runtime.
- `Passive_AAS.scl` visible source body does not show full declaration/signature context; if graphical LAD/FBD networks exist outside exported SCL text, verify effective online wiring there too.
