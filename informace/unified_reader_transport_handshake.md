# Unified Reader Transport Handshake Proposal

## Executive summary

This proposal defines one AAS-only handshake that works for both production cells and the fixed transport provider, while keeping all NFC readers on identical firmware. The reader makes all decisions from local configuration (`MyCellInfo.IDofCell`, known provider map, timeout/poll settings) and tag data (current step), not from per-cell custom code.

The recommended reservation strategy is:
1. Check and reserve target production cell first.
2. Then reserve transport provider.
3. If transport reservation fails, release the target reservation (`FreeFromQueue`) and retry with backoff.

This order minimizes transport occupancy and avoids moving products without a reserved destination.

## Design goal: identical firmware on all readers

All readers run the same binary and same state machine. Differences between readers are only:
- local `cell_id` (maps to `MyCellInfo.IDofCell`)
- local role flags (from config, not compiled constants)
- provider address map and IDs
- runtime timing settings

No firmware branch should be hardcoded for "transport source", "storage", "shaker", or other specific cell types.

## Generic reader decision model

The reader computes behavior using only:
- `MyCellInfo.IDofCell` (who am I)
- current recipe step (`TypeOfProcess`, `ParameterProcess1`, `ParameterProcess2`, current step index)
- configured map `process_cell_id -> AAS endpoint`
- configured fixed transport provider endpoint/ID

Decision rules:
1. Determine `target_cell_id` from recipe step (existing selection logic / routing table).
2. Compare:
   - if `target_cell_id == MyCellInfo.IDofCell`: local processing path
   - else: remote processing path with transport
3. If local path:
   - query local provider `GetSupported`
   - reserve local action `ReserveAction`
   - monitor with `GetStatus`
4. If remote path:
   - query/reserve remote target first
   - query/reserve fixed transport second
   - monitor both statuses until handover completion

This yields consistent outcomes:
- "I am target cell": ID match
- "I am not target cell": ID mismatch
- "I must request transport": ID mismatch and target reserve successful
- "I should wait": reserve exists but `GetStatus` not yet in-progress/done
- "I should continue locally": ID match and local reserve successful

## Proposed handshake

### Message contract (AAS)

Use existing PLC AAS methods and payload format:
- `GetSupported(sr_id/priority/material/parameterA/parameterB)`
- `ReserveAction(sr_id/priority/material/parameterA/parameterB)`
- `GetStatus(sr_id)`
- optional cleanup:
  - `FreeFromQueue(sr_id)` for reservation cancellation
  - `FreeFromPosition(sr_id)` where output position must be explicitly released

`sr_id` must be stable for one tag/product identity during the whole handshake.

### Reservation order

Recommended two-phase style:
1. **Target pre-check**: `GetSupported` on target provider.
2. **Target reserve**: `ReserveAction` on target.
3. **Transport pre-check**: `GetSupported` on transport provider (transport semantics in `parameterA/B` profile).
4. **Transport reserve**: `ReserveAction` on transport provider.
5. **Execution monitoring**:
   - poll transport `GetStatus(sr_id)` until completion/handover condition
   - then poll target `GetStatus(sr_id)` until `inProgress` or terminal success per agreed semantics

### Status polling strategy

- Poll period: configurable (for example 300-1000 ms).
- Separate timeout budgets:
  - target reserve timeout
  - transport reserve timeout
  - transport execution timeout
  - target takeover timeout
- Stop polling when:
  - success condition reached
  - explicit error returned
  - timeout reached

### Handover completion rule

Recommended logical completion for remote path:
1. transport status confirms done/handover-ready (implementation detail on PLC side)
2. target status confirms item accepted/inProgress
3. reader marks transition complete and advances to next logical step ownership

If target never confirms readiness after transport done, treat as failure and trigger recovery policy.

## Reservation order and justification

### Recommendation: target first, then transport

Why:
- prevents moving item with no destination slot/reservation
- avoids transport resource blocking when target queue is full
- reduces deadlock risk in multi-item scenarios
- aligns with "destination must be ready before dispatch"

Alternative orders and drawbacks:
- transport first: can reserve/occupy transport while target later rejects
- parallel reserve: faster but needs stronger distributed rollback handling and race control

So for minimal-change and robust operation: **target-first sequential reservation** is best.

## Failure handling

### Core failure cases

1. Target `GetSupported` not supported or error:
   - do not contact transport
   - retry target after backoff or wait for next scan cycle

2. Target reserve fails:
   - do not contact transport
   - retry target (bounded retries)

3. Target reserve succeeds, transport reserve fails:
   - cancel target reserve via `FreeFromQueue(sr_id)` on target
   - optional `FreeFromPosition(sr_id)` if PLC queue/position policy requires it
   - retry full sequence after cooldown

4. Transport in progress timeout:
   - query both statuses to confirm last known state
   - attempt cleanup on both providers (`FreeFromQueue`)
   - mark step as pending-retry/error based on policy

5. Target takeover timeout after transport done:
   - keep product in safe hold state (policy)
   - re-poll target for bounded time
   - then cleanup + escalation

### Retry behavior

- Use bounded retries per phase (for example 3 attempts).
- Use exponential or stepped backoff (for example 0.5 s, 1 s, 2 s).
- Persist enough state on tag/local memory to avoid duplicate conflicting reservations after reboot.

### Cancellation / freeing behavior

Minimal rollback contract:
- if only target reserved: release target
- if target + transport reserved and execution not started: release both
- if execution already started: do not blind-release active in-progress item; resolve using `GetStatus` and PLC-defined completion semantics first

## Final recommendation

Adopt a unified AAS-only flow for both production and transport providers with:
- one identical reader firmware
- configuration-driven cell identity and endpoints
- target-first reservation ordering
- explicit rollback with `FreeFromQueue` on partial failure
- status polling via `GetStatus` for transport completion and target takeover

This keeps architecture close to existing PLC AAS methods and removes dependency on legacy `Inquire`/`IsValid`/`Rezervation`/`DoProcess` calls.

## Not confirmed (requires validation)

- Exact transport-specific meaning of `parameterA` and `parameterB` for the fixed transport provider is not confirmed.
- Exact success terminal token(s) from `GetStatus` for "transport done and handover complete" are not confirmed.
- Whether `FreeFromPosition` is mandatory in all rollback paths or only in specific PLC queue/position states is not confirmed.
- Whether `ReserveAction` creates provider-local reservation semantics sufficient for strict two-phase commit behavior is not confirmed.
