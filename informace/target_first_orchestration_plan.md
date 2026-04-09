# Target-First Orchestration Plan

## Executive summary
This plan enforces a strict orchestration rule in the existing firmware architecture: transport is requested only after remote target production PLC reservation succeeds. The implementation should reuse existing state-machine flow and existing production endpoint mapping, with minimal invasive changes.

## Chosen architecture
- Each production cell has one reader and one local production PLC (AAS provider).
- One shared transport PLC serves all cells.
- Readers do not communicate directly with each other.
- Every reader can call:
  - its local production PLC,
  - all production PLCs (remote included),
  - shared transport PLC.
- Local identity is loaded from `ID_Interpretter` into `MyCellInfo.IDofCell`.
- Local endpoint is assigned by `assign_local_endpoint_from_cell_id(...)`.

## Final intended flow
1. Reader A performs local step with PLC_A.
2. On local completion, Reader A resolves next target production cell from recipe/LDS.
3. Reader A calls target PLC_B:
   - `GetSupported`
   - `ReserveAction`
4. Only if target reservation succeeds, Reader A calls shared transport PLC.
5. Physical transport occurs; product arrives to cell B.
6. Reader B reads arriving tag and continues local orchestration.

## What already exists
- Production endpoint mapping by cell ID in `assign_local_endpoint_from_cell_id(...)`.
- Next-target resolution for remote production handover:
  - `resolve_next_target_cell(...)`
- Cross-cell remote AAS helper:
  - `reserve_remote_target(...)` with `GetSupported -> ReserveAction -> GetStatus`
- Legacy transport state chain:
  - `State_Poptavka_Vyroba -> State_Poptavka_Transporty -> State_Rezervace -> State_Transport`
- Local-vs-non-local ownership decision in `State_Mimo_Polozena` AAS path.

## What is missing
- Strict gate enforcement in main flow:
  - remote target reservation result currently does not gate transport chain entry.
- Explicit runtime state for target reservation outcome (for same `sr_id` and step).
- Clear failure/recovery policy for:
  - target reserved but transport failed,
  - stale reservations,
  - mismatched target vs transport state.
- Integration of fixed shared transport endpoint `192.168.168.64:4840` as canonical transport destination.

## Recommended implementation strategy
### Phase 1: enforce target-first gate
- Add a pre-transport gate check in non-local path before entering `State_Poptavka_Transporty`.
- Reuse `resolve_next_target_cell(...)` and `reserve_remote_target(...)`.
- Persist in-memory gate context for active item:
  - `sr_id`,
  - target cell id,
  - current recipe step index,
  - target reservation status and timestamp.

### Phase 2: transport endpoint integration
- Keep production endpoint mapping untouched.
- Add fixed transport endpoint constant `192.168.168.64:4840`.
- Route transport AAS interactions to this endpoint only.

### Phase 3: failure and recovery hardening
- Define bounded retries for target reserve and transport reserve.
- If target reserved but transport fails:
  - keep reservation only within short timeout window,
  - then release/expire according to PLC API capability.
- Add timeout rule for “never arrived” cases to prevent stale locks.

### Phase 4: stabilization and optional cleanup
- Keep legacy transport chain as shell for minimal regression risk.
- Remove dead legacy internals only after stable runtime evidence.

## Recommendation on branching approach
- Preferred now: integrate target-first gate into existing transport state chain.
- Do not introduce a fully new branch in first implementation.
- Reason: smallest risk, maximum reuse, minimal impact to proven NFC/state timing behavior.
