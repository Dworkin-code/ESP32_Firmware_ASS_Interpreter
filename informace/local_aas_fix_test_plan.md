# Local AAS Fix Test Plan

## Test setup
- Device under test: Shaker reader (`ID_Interpretter` = 3)
- Expected local endpoint assignment: `192.168.168.150:4840`
- Use recipe where current step has `TypeOfProcess = 3` (Shaker)
- Ensure NFC tag has valid recipe with current step not done and `RecipeDone = false`

## Test 1: Local owner path and payload correctness (Shaker local)
1. Place tag on Shaker reader.
2. Observe decision log.
3. Observe local input payload log.
4. Observe `GetSupported` and `ReserveAction` responses.

Expected logs:
- `AAS_DECISION: TypeOfProcess=3 owner_cell_id=3 local_cell_id=3 => LOCAL_PROCESS`
- `AAS: local InputMessage=<sr_id>/0/3/<P1>/<P2> [id=<sr_id> priority=0 material=3 pA=<P1> pB=<P2>]`

Expected behavior:
- Local payload token1 is `0` (priority), not cell ID.
- `GetSupported` is evaluated against PLC-compatible message.

## Test 2: Local success path
Precondition: PLC returns support and accepts reserve for that step.

Expected behavior:
- `ReserveAction` succeeds
- completion poll succeeds
- current step is marked done (`IsStepDone=1`)
- `ActualRecipeStep` increments by 1
- `RecipeDone` is set only if new step index reaches `RecipeSteps`
- state transitions to wait/remove flow (no false failures)

## Test 3: Local reserve failure must stay recoverable
Precondition: force local reserve failure (e.g. PLC returns `Error:8502` or call failure).

Expected logs:
- `AAS_FAIL_LOCAL: ReserveAction callOk=<0|1> response=<...> -> keep step=<idx> pending (RecipeDone=<current>)`

Expected behavior:
- No false `RECIPE_FINISHED` on next iteration
- `ActualRecipeStep` unchanged
- `RecipeDone` unchanged (typically remains `false`)
- Same step is retried on next scan/iteration

## Test 4: Local GetSupported error must stay recoverable
Precondition: PLC returns `Error:*` for `GetSupported`.

Expected logs:
- `AAS_FAIL_LOCAL: GetSupported Error response=<...> -> keep step=<idx> pending (RecipeDone=<current>)`

Expected behavior:
- recipe is not marked done
- no completion write-back to tag for this failure
- subsequent iteration retries same step

## Test 5: Negative routing test (owner != local cell)
Precondition: run on cell ID 3 with step `TypeOfProcess` owned by another cell (e.g. SodaMake => owner 2).

Expected logs:
- `AAS_DECISION: ... => REQUEST_TRANSPORT`

Expected behavior:
- local AAS process branch is not used
- existing cross-cell/transport routing path remains active
- no regression in handover logic
