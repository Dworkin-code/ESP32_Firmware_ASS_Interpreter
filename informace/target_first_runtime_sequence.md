# Target-First Runtime Sequence

## Step-by-step runtime sequence
1. Reader detects tag and loads active recipe step.
2. Reader executes local step against local production PLC if owner is local.
3. On local success, reader resolves next target production cell.
4. Reader sends target reservation calls to target PLC:
   - `GetSupported`
   - `ReserveAction`
5. If target reservation accepted, reader requests transport on shared transport PLC.
6. Reader records transport request state and waits for removal/arrival events.
7. At destination, next reader continues normal local flow.

## Success path
1. Local step finished and synced on tag.
2. Next target cell resolved (non-local).
3. Target `GetSupported` returns positive.
4. Target `ReserveAction` accepted.
5. Transport PLC accepts transport request.
6. Physical transfer succeeds.
7. Destination reader reads same tag and continues with its local step.

## Failure path
### Target reservation fail
- If target `GetSupported` fails or rejects:
  - do not request transport,
  - keep item local and retry later.
- If target `ReserveAction` fails:
  - do not request transport,
  - retry with bounded backoff.

### Transport reservation fail
- If transport call fails after target success:
  - keep item local,
  - mark mismatch state: target reserved, transport not reserved.
- Trigger recovery policy:
  - retry transport within short window,
  - if still failing, release/expire target reservation if supported.

### Arrival fail / never arrives
- If destination does not observe arrival within timeout:
  - mark stale transport in monitoring/log path,
  - force reconciliation on next source read.

## Retry/recovery path
- Retry tiers:
  1. Immediate short retries for transient network errors.
  2. Exponential or fixed-interval retries for service busy.
  3. Terminal fail after max attempts.
- Recovery actions:
  - clear local in-memory gate context on terminal fail,
  - optionally send release to target/transport queue endpoint if available,
  - require fresh target reservation before any new transport attempt.
- Idempotency guard:
  - keep per-`sr_id` recent action window to avoid duplicate `ReserveAction`.

## Transport gating rule
Transport request is allowed only if all are true for the same in-flight item:
- local step succeeded,
- target cell resolved,
- target `GetSupported` accepted,
- target `ReserveAction` accepted and not stale/expired.

If any condition is false, reader must not call transport PLC.
