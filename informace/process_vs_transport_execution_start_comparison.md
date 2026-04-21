# Execution Start Comparison: `PLC_code` vs `PLC_code -Transport`

## 1. Executive summary
Transport execution does not start because `CheckAction` is gated by `IF #deviceRunning THEN`, so with `#deviceRunning = FALSE` at idle, `#isFoundItem` never becomes `TRUE`, `DeviceSlow.Valid` stays `FALSE`, and `seqState` never leaves `0`.

## 2. Side-by-side flow comparison (process vs transport)

### ReserveAction -> queue storage
- **Process (`PLC_code`)**: `ReserveAction` pushes reserved item to `"PQueue".PQueue` via `"PQueue_push"(...)`.
- **Transport (`PLC_code -Transport`)**: same logic and same destination (`"PQueue".PQueue`).
- **Observation**: reservation success only inserts into priority queue; it does not directly set execution-valid flag.

### Item selection for execution
- **Process**: `CheckAction` scans `priorityQueue`, then `Queue_find` checks whether the same ID exists in `reportedProductsQueue`; output `foundItem => #isFoundItem`.
- **Transport**: identical `CheckAction` logic.
- **Critical precondition in both**: `CheckAction` call is wrapped in `IF #deviceRunning THEN ... END_IF;` inside `Passive_AAS`.

### `DeviceSlow` input wiring
- **Process**: `Valid := #isFoundItem` to `DeviceSlow_Instance(...)`.
- **Transport**: `Valid := #isFoundItem` to `#DeviceSlow_Instance(...)`.
- **Effect in both**: execution starts only if `#isFoundItem = TRUE`.

### State machine start condition
- In both `DeviceSlow.scl`: state transition is `IF #Valid = TRUE THEN ... #seqState := 10;` in state `0`.
- If `#Valid = FALSE`, block executes ELSE branch (`#deviceRunning := FALSE`, `#seqState := 0`).

## 3. First blocking condition
First blocker is in `Passive_AAS.scl`:
- `IF #deviceRunning THEN #CheckAction_Instance(... foundItem => #isFoundItem ...) END_IF;`

Because `#deviceRunning` is `FALSE` at idle:
- `CheckAction` is not executed
- `#isFoundItem` is not refreshed to `TRUE`
- `Valid := #isFoundItem` stays `FALSE`
- `DeviceSlow` remains in `seqState = 0`
- no `CmdStart` pulse, therefore no simulation start trigger

## 4. Exact code difference causing it
For the execution-trigger path itself, there is **no functional process-vs-transport difference** in the gating logic:
- both variants gate `CheckAction` by `#deviceRunning`
- both feed `DeviceSlow.Valid` from `#isFoundItem`
- both start `seqState` only on `Valid = TRUE`

So the non-start behavior is caused by the same bootstrap dependency in transport being active in runtime (`#deviceRunning` false at idle), not by a different ReserveAction/CheckAction/Valid state-machine condition between these two source trees.

## 5. Minimal fix recommendation
Break the bootstrap dependency by evaluating `CheckAction` also when the device is idle.

Minimal change:
- In `Passive_AAS.scl`, remove or relax the `IF #deviceRunning THEN` gate around `#CheckAction_Instance(...)`.

Expected result after fix:
- once reservation/report conditions are met, `#isFoundItem` can become `TRUE` while idle
- `DeviceSlow.Valid` becomes `TRUE`
- `seqState` transitions `0 -> 10 -> ...`
- start pulse is emitted and simulation can run (`Busy -> Done`)
