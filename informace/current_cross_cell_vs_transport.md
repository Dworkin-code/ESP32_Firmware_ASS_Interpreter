# Cross-Cell vs Transport: Current State

## Relationship between cross-cell communication and transport logic
- Cross-cell communication exists as remote AAS calls to another production cell PLC (`GetSupported`, `ReserveAction`, `GetStatus`).
- Transport orchestration exists as a separate state chain (`State_Poptavka_Vyroba` -> `State_Poptavka_Transporty` -> `State_Rezervace` -> `State_Transport`).
- These two mechanisms are currently only loosely coupled.

## Is target reservation already used before transport request?
Short answer: **not as a required gate in the main flow**.

Details:
- In the newer AAS decision branch (`State_Mimo_Polozena` + `USE_PLC_AAS_FLOW`):
  - If step owner is non-local, firmware directly routes to transport/routing logic.
  - It does not first require successful remote target `ReserveAction`.

- The remote target reservation helper is called in `State_Vyroba_SpravneProvedeni` after local step completion, and its result is not used to block/allow transport states.

Therefore transport can still be requested without confirmed target reservation from the remote cell in the primary routing path.

## What is already implemented
- Resolution of next non-local target cell from recipe/LDS data (`resolve_next_target_cell`).
- Remote AAS method invocation to target PLC endpoint (`reserve_remote_target`).
- Basic status polling via remote `GetStatus` (`poll_remote_target_status`).
- Existing transport reservation/execution states independent of that remote helper.

## What is missing for full target-first transport orchestration
To be fully target-first, the firmware would need (not currently present):
- A single canonical flow where remote target reservation outcome is consumed by state transitions.
- A hard gating condition: no transport request unless target reservation is confirmed.
- Failure handling path when remote target rejects or cannot reserve (retry/alternate target/abort semantics tied to transport decisions).
- Unified behavior across newer AAS path and legacy completion path so cross-cell handshake is not branch-specific.

## Classification of current implementation
- **Not absent:** cross-cell communication is real and functional at call level.
- **Not fully integrated:** it behaves like a handover/reservation side-flow (best effort) rather than strict orchestration gate.
- **Overall:** partially integrated cross-cell capability, transport flow still independently executable.
