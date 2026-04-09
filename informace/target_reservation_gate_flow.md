# Target Reservation Gate Runtime Flow

## Non-local runtime flow (current step not owned by local cell)

1. Reader enters AAS decision logic in `State_Mimo_Polozena`.
2. Decision resolves to `REQUEST_TRANSPORT` for current step.
3. Reader executes `reserve_remote_target(...)` before transport request logic.
4. Reservation logic performs:
   - target cell resolution for current step (fallback to next-step resolver if needed)
   - `GetSupported` on resolved target PLC
   - `ReserveAction` on resolved target PLC
5. If reservation succeeds, reader sets gate context.
6. Existing gate check runs.
7. Transport request path executes only if gate is valid.

## Success path

- Conditions:
  - `GetSupported` support value > 0
  - `ReserveAction` accepted (non-error)
- Effects:
  - gate is set with:
    - `sr_id`
    - `stepIndex`
    - `targetCellId`
    - `targetReserved=true`
  - log contains `TRANSPORT_GATE allowed ...`
  - transport log contains `TRANSPORT_REQUEST executed ...`

## Failure path

- Reservation failure causes:
  - gate reset (`target_reserve_failed`)
  - gate mismatch on subsequent check
  - transport skipped (`TRANSPORT_REQUEST skipped reason=gate_blocked`)
- Item remains pending:
  - no step completion/write-back in this non-local branch
  - no transport request execution

## Retry behavior

- On repeated scans/iterations for same `(sr_id, stepIndex, targetCellId)`:
  - reservation attempt is repeated (tolerated by design in this version)
  - if remote target later accepts reservation, gate is set and flow proceeds
  - if remote target still rejects/errors, transport remains blocked

## When transport is now allowed

- Transport is allowed only after:
  1. non-local target reservation attempt succeeds (`GetSupported > 0` and `ReserveAction == success`)
  2. gate context matches runtime card state (`sr_id`, `ActualRecipeStep`, `targetCellId`)
- Any mismatch/failure keeps gate blocked and transport request skipped.
