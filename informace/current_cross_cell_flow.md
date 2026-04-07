# Current Cross-Cell Runtime Flow

## Trigger point in state machine
1. Tag appears -> `State_Mimo_Polozena` loads recipe and UID.
2. Depending on branch/path, process execution occurs.
3. Cross-cell handover trigger is reached in `State_Vyroba_SpravneProvedeni` when `IsDoneReservation(...)` returns finished (`case 1`).

## Step-by-step flow for currently implemented cross-cell interaction
1. **Local process completion detected**
   - In `State_Vyroba_SpravneProvedeni`, finished process sets step flags (`IsProcess=0`, `IsStepDone=1`) and updates `ActualRecipeStep` or `RecipeDone`.

2. **Condition for remote call**
   - Only if recipe is still not done (`!tempInfo.RecipeDone`) and `sr_id` was built successfully from UID.

3. **Remote target resolution** (`resolve_next_target_cell`)
   - Looks at next recipe step (`NextID`, fallback `current+1`).
   - Queries candidate cells for next process via `GetCellInfoFromLDS(nextStep->TypeOfProcess, ...)`.
   - Excludes local cell ID.
   - Prefers `nextStep->ProcessCellID` when non-zero, otherwise first non-local candidate.

4. **Remote AAS call sequence** (`reserve_remote_target`)
   - Build endpoint from target cell (`normalize_cell_endpoint`).
   - Build InputMessage: `sr_id/localCellId/typeOfProcess/parameter1/parameter2`.
   - Acquire ethernet semaphore.
   - Call `OPC_GetSupported(...)` on target PLC endpoint.
   - If supported, call `OPC_ReserveAction(...)` on target PLC endpoint.
   - If reserve accepted, run short poll loop `poll_remote_target_status(...)` calling `OPC_GetStatus(...)`.
   - Release ethernet semaphore.

5. **Post-call behavior**
   - Function return value is ignored at call site (`(void)reserve_remote_target(...)`).
   - Firmware continues with local write-back/sync and returns to `State_Mimo_Polozena`.

## What happens locally vs remotely
- **Local:** step completion bookkeeping, NFC write-back (`WriteStep`, `WriteInfo`, `Sync`), state transition.
- **Remote:** AAS provider calls (`GetSupported`, `ReserveAction`, `GetStatus`) against another cell's PLC endpoint.

## Is transport involved in this cross-cell call?
- Not directly in this specific remote-handover call.
- This call is a separate best-effort action after local completion, not a transport-state transition decision.

## Parallel newer flow (important for runtime understanding)
In the `USE_PLC_AAS_FLOW` path in `State_Mimo_Polozena`:
- For non-local process owner, firmware directly enters routing/transport states (`State_Inicializace_ZiskaniAdres` -> `State_Poptavka_Vyroba` -> `State_Poptavka_Transporty` ...).
- It does not invoke `reserve_remote_target(...)` beforehand.

This means cross-cell remote reservation exists, but only in a specific branch and timing point, not as universal pre-transport orchestration.
