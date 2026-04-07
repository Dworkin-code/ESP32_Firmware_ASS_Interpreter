# Unified Reader State Machine Proposal

## Proposed logical states

1. `IDLE_WAIT_TAG`
2. `TAG_READ_AND_PARSE`
3. `RESOLVE_TARGET_CELL`
4. `DECIDE_LOCAL_OR_REMOTE`
5. `LOCAL_GETSUPPORTED`
6. `LOCAL_RESERVE`
7. `LOCAL_WAIT_STATUS`
8. `REMOTE_TARGET_GETSUPPORTED`
9. `REMOTE_TARGET_RESERVE`
10. `REMOTE_TRANSPORT_GETSUPPORTED`
11. `REMOTE_TRANSPORT_RESERVE`
12. `REMOTE_WAIT_TRANSPORT`
13. `REMOTE_WAIT_TARGET_TAKEOVER`
14. `COMPLETE_AND_WRITEBACK`
15. `ROLLBACK_PARTIAL_RESERVATION`
16. `RETRY_BACKOFF`
17. `FAIL_AND_PERSIST`

## Transition conditions

- `IDLE_WAIT_TAG -> TAG_READ_AND_PARSE`: tag detected.
- `TAG_READ_AND_PARSE -> RESOLVE_TARGET_CELL`: recipe step valid.
- `TAG_READ_AND_PARSE -> FAIL_AND_PERSIST`: invalid data, missing step, parse failure.
- `RESOLVE_TARGET_CELL -> DECIDE_LOCAL_OR_REMOTE`: target resolved from recipe/config.
- `RESOLVE_TARGET_CELL -> FAIL_AND_PERSIST`: target unknown/missing mapping.
- `DECIDE_LOCAL_OR_REMOTE -> LOCAL_GETSUPPORTED`: `target_cell_id == local_cell_id`.
- `DECIDE_LOCAL_OR_REMOTE -> REMOTE_TARGET_GETSUPPORTED`: `target_cell_id != local_cell_id`.

Local branch:
- `LOCAL_GETSUPPORTED -> LOCAL_RESERVE`: supported.
- `LOCAL_GETSUPPORTED -> RETRY_BACKOFF`: transient error.
- `LOCAL_GETSUPPORTED -> FAIL_AND_PERSIST`: unsupported (policy dependent).
- `LOCAL_RESERVE -> LOCAL_WAIT_STATUS`: reserve success.
- `LOCAL_RESERVE -> RETRY_BACKOFF`: reserve transient fail.
- `LOCAL_WAIT_STATUS -> COMPLETE_AND_WRITEBACK`: done/inProgress->done criteria reached.
- `LOCAL_WAIT_STATUS -> RETRY_BACKOFF`: timeout.

Remote branch:
- `REMOTE_TARGET_GETSUPPORTED -> REMOTE_TARGET_RESERVE`: target supported.
- `REMOTE_TARGET_GETSUPPORTED -> RETRY_BACKOFF`: transient fail.
- `REMOTE_TARGET_GETSUPPORTED -> FAIL_AND_PERSIST`: unsupported.
- `REMOTE_TARGET_RESERVE -> REMOTE_TRANSPORT_GETSUPPORTED`: target reserved.
- `REMOTE_TARGET_RESERVE -> RETRY_BACKOFF`: reserve fail.
- `REMOTE_TRANSPORT_GETSUPPORTED -> REMOTE_TRANSPORT_RESERVE`: transport supported.
- `REMOTE_TRANSPORT_GETSUPPORTED -> ROLLBACK_PARTIAL_RESERVATION`: transport unsupported/fail after target reserved.
- `REMOTE_TRANSPORT_RESERVE -> REMOTE_WAIT_TRANSPORT`: transport reserved.
- `REMOTE_TRANSPORT_RESERVE -> ROLLBACK_PARTIAL_RESERVATION`: reserve fail after target reserved.
- `REMOTE_WAIT_TRANSPORT -> REMOTE_WAIT_TARGET_TAKEOVER`: transport completion condition met.
- `REMOTE_WAIT_TRANSPORT -> ROLLBACK_PARTIAL_RESERVATION`: transport timeout/fail.
- `REMOTE_WAIT_TARGET_TAKEOVER -> COMPLETE_AND_WRITEBACK`: target confirms takeover.
- `REMOTE_WAIT_TARGET_TAKEOVER -> ROLLBACK_PARTIAL_RESERVATION`: timeout/fail.

Recovery:
- `ROLLBACK_PARTIAL_RESERVATION -> RETRY_BACKOFF`: cleanup success.
- `ROLLBACK_PARTIAL_RESERVATION -> FAIL_AND_PERSIST`: cleanup failed or inconsistent status.
- `RETRY_BACKOFF -> <branch entry state>`: retry budget remaining.
- `RETRY_BACKOFF -> FAIL_AND_PERSIST`: retries exhausted.
- `COMPLETE_AND_WRITEBACK -> IDLE_WAIT_TAG`: writeback success and session closed.
- `FAIL_AND_PERSIST -> IDLE_WAIT_TAG`: failure stored/logged, wait next interaction.

## Success path

### Scenario requested: local cell is not target

1. Reader reads tag and resolves `target_cell_id`.
2. Compare IDs: local != target, choose remote path.
3. Call target `GetSupported`.
4. Call target `ReserveAction` (must succeed first).
5. Call fixed transport `GetSupported`.
6. Call fixed transport `ReserveAction`.
7. Poll transport `GetStatus(sr_id)` until transport completion condition.
8. Poll target `GetStatus(sr_id)` until takeover/inProgress condition.
9. Mark handover complete, persist recipe progress, return to idle.

This sequence is identical on every reader because only IDs/endpoints differ.

## Failure / retry paths

Main retry rules:
- retry only transient communication/timeouts
- no infinite loops; bounded retry counter
- backoff between retries

Rollback rules:
- if target reserved but transport not reserved: release target (`FreeFromQueue`)
- if both reserved but not started: release both (`FreeFromQueue` on both)
- if one side in progress: query status first, avoid unsafe immediate free

Escalation rules:
- after retry exhaustion, persist failure marker and keep step pending/error for operator or next recovery cycle

## Notes for later implementation

- Keep one shared event loop and data-driven state table; avoid per-cell `if/else` trees.
- Persist `sr_id`, reservation ownership info, and retry counters so restart can reconcile in-flight work.
- Add clear reason codes for transitions (`unsupported`, `reserve_fail`, `timeout`, `rollback_fail`).
- Prefer centralized AAS response parsing to avoid inconsistent string handling.
- Guard against duplicate reservations on repeated tag reads by checking existing in-flight state for same `sr_id`.

## PLC-side blockers / ambiguities (not confirmed)

- Not confirmed: exact `GetStatus` token(s) that unambiguously indicate "transport done and physically handed over".
- Not confirmed: whether `ReserveAction` semantics are strictly idempotent for repeated same `sr_id` requests across retries.
- Not confirmed: mandatory use cases for `FreeFromPosition` versus `FreeFromQueue` in all rollback branches.
- Not confirmed: canonical encoding of transport source/target in `parameterA/parameterB` expected by transport provider.
- Not confirmed: whether target and transport providers share identical timeout assumptions for queue movement to `inProgress`.
