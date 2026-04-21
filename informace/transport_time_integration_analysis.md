# TransportTime Integration Analysis

## 1. Queue Handling
- **Where items are taken from PQueue:** `CheckAction` iterates through `priorityQueue` and reads items using `PQueue_get(position := i, item => #item, ...)`.
- **Current item variable:** `CheckAction` output variable `item` (`typeProductInQueue`), with queue index in `itemPositionP`.
- **Exact file + block/function:**
  - `PLC_code/program_block/Device Data/CheckAction.scl` (`REGION CheckAction`)
  - `PLC_code/program_block/Priority Queue functions/PQueue_get.scl` (`REGION get`)
- **Queue removal (pop-equivalent) occurs after completion:** `RemoveFinishedProduct` calls `PQueue_removeByPosition(position := itemPositionP, ...)`, so item selection and item removal are separated in time.

## 2. Processing Start Point
- **Where a product begins execution:**
  1. `Passive_AAS` passes selected `item` + `itemPositionP` + `isFoundItem` into `DeviceSlow`.
  2. In `DeviceSlow`, when state machine is in idle (`seqState = 0`) and `Valid = TRUE`, product ID is latched (`CmdProductID := DINT_TO_DWORD(item.id)`) and `seqState` moves to `10` (start of handshake).
- **What triggers start:** `Valid = TRUE` for the currently selected queue item while `DeviceSlow` is in idle state.
- **Exact file + block/function:**
  - `PLC_code/program_block/AAS/Passive_AAS.scl` (call of `DeviceSlow_Instance`)
  - `PLC_code/program_block/Device Data/DeviceSlow.scl` (`CASE seqState OF`, state `0`)

## 3. Simulation Control
- **Where `Simulace` is used:** No direct usage of `Simulace` was found in entire `PLC_code` search scope.
- **Where `Set_time_simulace` is used:** No direct usage of `Set_time_simulace` was found in entire `PLC_code` search scope.
- **Relevant finding:** `DeviceSlow` contains comment that timer-based simulation was replaced by handshake-based real machine control; therefore simulation timing is currently not configured in visible PLC blocks.
- **Closest current control point:** `DeviceSlow` state machine is now the effective execution controller (idle/ready/start/busy/done flow).

## 4. Recommended Integration Point
- **Exact place:** `PLC_code/program_block/Device Data/DeviceSlow.scl`, in state `0` of `seqState` (idle -> start transition), immediately before or together with transition to `seqState := 10` when `Valid = TRUE`.
- **Why this is correct location:**
  - This is the first deterministic moment when one concrete queue item becomes the active processed item.
  - `item` is already resolved from PQueue and passed in from orchestration.
  - Assignment here executes once per item start (not continuously every scan), avoiding repeated overwrite.
- **Current item availability:** Yes. Full current item record (`item`) is available, including `item."finalPosition/operationParameter"` (TransportTime source as described).

## 5. Implementation Proposal (NO CODE CHANGE)
- Add a single assignment step (conceptually) at the `DeviceSlow` start transition (state `0`, `Valid = TRUE`) to set simulation duration from current item TransportTime.
- The assignment should map current item parameter to simulation time:
  - source: `item."finalPosition/operationParameter"` (INT)
  - target: `Simulace.Set_time_simulace` (TIME)
  - conversion: `INT` value interpreted as milliseconds -> convert to `TIME` in ms.
- Apply this only at item activation time (idle -> active transition), not in waiting states (`20/40/50/70`) and not globally each cycle.
