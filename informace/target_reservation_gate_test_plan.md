# Target Reservation Gate Test Plan

## Test setup prerequisites

- Use a card/tag whose current step is non-local for the reader under test.
- Confirm reader reaches AAS decision in `State_Mimo_Polozena`.
- Ensure target production PLC endpoint is reachable.
- Shared transport PLC remains at `192.168.168.64:4840`.

## Test 1: Non-local step, target reservation accepted

### Goal

Verify that transport is executed only after successful remote target reservation.

### Steps

1. Place tag with non-local current step on reader.
2. Ensure target PLC returns:
   - `GetSupported` with support > 0
   - `ReserveAction` with success
3. Observe logs during the same iteration.

### Expected logs

- `AAS_DECISION: ... => REQUEST_TRANSPORT`
- `TARGET_RESERVE start sr_id=<...> step=<...> targetCellId=<...>`
- `TARGET_RESERVE support=<positive> result=SUCCESS`
- `TRANSPORT_GATE allowed sr_id=<...> step=<...> targetCellId=<...>`
- `TRANSPORT_REQUEST executed ...`

### Expected behavior

- Gate is populated for current runtime tuple `(sr_id, stepIndex, targetCellId)`.
- Transport request path is entered.

## Test 2: Non-local step, target reservation rejected

### Goal

Verify that transport remains blocked when target reservation fails.

### Steps

1. Place tag with non-local current step on reader.
2. Configure target PLC to reject by either:
   - `GetSupported` support <= 0, or
   - `ReserveAction` error/reject response.
3. Observe logs.

### Expected logs

- `AAS_DECISION: ... => REQUEST_TRANSPORT`
- `TARGET_RESERVE start sr_id=<...> step=<...> targetCellId=<...>`
- `TARGET_RESERVE support=<...> result=REJECTED` (or `ERROR`)
- `TRANSPORT_GATE blocked ...`
- `TRANSPORT_REQUEST skipped reason=gate_blocked ...`

### Expected behavior

- Gate stays empty/reset or non-matching.
- No transport request execution.
- Current step remains pending.

## Test 3: Retry behavior after prior rejection

### Goal

Verify repeated attempts are tolerated and eventual success unblocks transport.

### Steps

1. Run Test 2 first (force rejection).
2. Without changing card step, retry scan/iteration.
3. Change target PLC to accept reservation.
4. Retry scan/iteration again.

### Expected behavior

- Repeated reservation attempts occur for same tuple.
- On first accepted attempt:
  - gate becomes valid
  - transport executes

## Regression check (local path unchanged)

- Use a tag where current step is local for this reader.
- Confirm existing local AAS path still performs:
  - local `GetSupported`
  - local `ReserveAction`
  - write-back on success
- Confirm no transport request is forced for local-only step.
