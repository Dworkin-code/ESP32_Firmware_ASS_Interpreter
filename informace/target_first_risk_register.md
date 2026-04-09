# Target-First Risk Register

## Duplicate reservation
- Risk: repeated scans call target reservation multiple times for same `sr_id`.
- Impact: blocked capacity, inconsistent downstream state.
- Mitigation:
  - preserve and extend re-scan guard per `sr_id`,
  - bind gate context to `(sr_id, stepIndex, targetCellId)`,
  - ignore duplicate reserve attempts inside validity window.

## Target reserved but transport failed
- Risk: destination capacity held while item remains at source.
- Impact: starvation, stale queue entries.
- Mitigation:
  - bounded retries for transport request,
  - explicit release/expiry path after max retries,
  - log mismatch event with correlation keys.

## Product never arrives
- Risk: transport reported started but destination never reads tag.
- Impact: long-lived stale reservation and recipe deadlock.
- Mitigation:
  - arrival timeout policy,
  - source-side reconciliation on next read,
  - destination-side stale detection metric.

## Stale reservation
- Risk: target reservation remains valid after source state reset/restart.
- Impact: hidden inconsistency and future false rejects.
- Mitigation:
  - reservation TTL policy,
  - revalidation before transport execution,
  - startup cleanup for unresolved in-flight contexts.

## Reconnect/session errors
- Risk: OPC UA reconnect instability causes partial call sequence.
- Impact: unknown true state after network break.
- Mitigation:
  - strict call result classification,
  - idempotent retry with backoff,
  - fail-closed rule (no transport without confirmed target reserve).

## Target vs transport state mismatch
- Risk: target and transport PLC hold different realities for same item.
- Impact: orchestration drift across cells.
- Mitigation:
  - correlation fields in logs (`sr_id`, target cell, step index),
  - reconciliation branch in runtime sequence,
  - single source of truth gate in reader before transport calls.
