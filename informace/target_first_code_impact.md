# Target-First Code Impact

## Likely touched states/functions
- `State_Mimo_Polozena`
  - non-local ownership path should enforce target reservation gate before transport path.
- `State_Vyroba_SpravneProvedeni`
  - existing `reserve_remote_target(...)` invocation should be converted from best-effort side effect to gate-producing result path.
- `State_Inicializace_ZiskaniAdres`
  - keep LDS/cell discovery logic; potentially add gate context initialization/reset.
- `State_Poptavka_Vyroba`
  - keep as-is for winning production cell selection; ensure it is not bypassing gate semantics.
- `State_Poptavka_Transporty`
  - transport request start state must require target reservation success.
- `State_Rezervace`
  - align reservation sequencing to target-first contract.
- `State_Transport`
  - execute transport only when gate still valid (same `sr_id`, step, target).
- `reserve_remote_target(...)`
  - likely central helper to return richer status (supported/rejected/error/timeout).
- Helper usage:
  - target/transport AAS wrappers (`OPC_GetSupported`, `OPC_ReserveAction`, `OPC_GetStatus`).

## Which existing code can be reused
- Endpoint bootstrap and local identity:
  - NVS key `ID_Interpretter` and `assign_local_endpoint_from_cell_id(...)`.
- Target resolution:
  - `resolve_next_target_cell(...)` already computes remote candidate.
- Remote target AAS handshake:
  - `reserve_remote_target(...)` already issues expected call sequence.
- Semaphore and retry style:
  - existing `xEthernet` and `xNFCReader` timeout/retry patterns.
- Existing transport states:
  - reuse chain for minimal structural change, enforce stricter entry conditions.

## Which parts are risky
- State transition race conditions if gate state is not bound to exact `sr_id` and step index.
- Duplicate remote reservations on repeated scans.
- Partial success mismatch:
  - target accepted but transport failed.
- Legacy and new transport semantics running simultaneously.
- Recovery loops that can deadlock state progression if no terminal fail path.

## Recommended smallest implementation scope
1. Add in-memory gate context struct for active item:
   - `sr_id`, `stepIndex`, `targetCellId`, `targetReserved`, `ts`.
2. Set gate only on confirmed remote target reservation success.
3. Check gate before entering/requesting transport states.
4. Reset gate on:
   - successful transport handoff,
   - step change,
   - timeout/failure terminal paths.
5. Keep legacy chain intact; do not refactor broad state machine yet.
6. Add targeted logging only around gate decisions for validation.
