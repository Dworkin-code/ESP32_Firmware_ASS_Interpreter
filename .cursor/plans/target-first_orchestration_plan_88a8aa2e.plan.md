---
name: Target-first orchestration plan
overview: Minimal first implementation of strict target-first gate in existing ESP32 reader state machine, using current production PLC mapping and fixed shared transport PLC endpoint.
todos:
  - id: analyze-current-flow
    content: Confirm current state transitions and reserve_remote_target behavior in app.c
    status: completed
  - id: define-target-first-runtime
    content: Define strict gate semantics and runtime outcomes for first version
    status: completed
  - id: map-code-impact
    content: Identify minimal touched states/functions for gate integration
    status: completed
  - id: define-endpoint-integration
    content: Keep production endpoint mapping, add fixed transport endpoint usage
    status: completed
  - id: add-required-logging
    content: Add required TARGET_RESERVE, TRANSPORT_GATE, and TRANSPORT_REQUEST logs
    status: completed
isProject: false
---

# Target-First Orchestration Plan (Implementation-Ready, Minimal v1)

## Scope

- Implement strict gate: **no transport request before remote target PLC reservation success**.
- Keep existing state machine and integrate gate into current flow:
  - `State_Mimo_Polozena -> State_Inicializace_ZiskaniAdres -> State_Poptavka_Vyroba -> State_Poptavka_Transporty -> State_Rezervace -> State_Transport`
- Do not redesign flow and do not add new branch architecture.
- Keep endpoint behavior:
  - production PLC mapping stays as is,
  - shared transport PLC endpoint is fixed to `192.168.168.64:4840`.
- No reader-to-reader communication.

## Explicit success criteria

Target reservation is **SUCCESS** only when both are true:

1. `GetSupported` returns support value `> 0`
2. `ReserveAction` returns `Success`

Any other outcome is not success.

## Gate context definition

Gate is bound to:

- `sr_id`
- `stepIndex` (`ActualRecipeStep`)
- `targetCellId`

Gate is valid only if all three values match current runtime values.

## reserve_remote_target(...) required role

`reserve_remote_target(...)` must return a structured status (not best-effort side effect):

- `SUCCESS`
- `REJECTED`
- `ERROR_TIMEOUT`

Only `SUCCESS` may set transport gate to true.

## Hard transport gate rule

- Transport states **must not be entered** unless `gate == TRUE`.
- This check is done **before** entering transport state machine (not only inside transport states).
- If gate is false, transport request path is blocked.

## First implementation scope (minimal)

1. Add in-memory gate context:
  - `sr_id`
  - `stepIndex`
  - `targetCellId`
  - `targetReserved` (bool)
2. Set gate only on `reserve_remote_target(...) == SUCCESS`.
3. Check gate before entering transport states.
4. Reset gate on:
  - step change,
  - successful transport,
  - terminal failure.

## First version runtime behavior

- If target reservation fails: do **not** call transport.
- If transport fails: keep item local and retry later.
- No explicit cancel/release logic in first iteration.
- No lease/TTL enforcement in first iteration.
- No complex reconciliation branch in first iteration.

## Duplicate reservation handling (minimal)

- Duplicate attempts for same (`sr_id`, `stepIndex`, `targetCellId`) are allowed.
- Assume idempotent behavior and tolerate repeated calls.
- Do not implement complex dedup logic in v1.

## Required logging

### TARGET_RESERVE

Log:

- `targetCellId`
- support value
- reserve result (`SUCCESS` / `REJECTED` / `ERROR_TIMEOUT`)

### TRANSPORT_GATE

Log:

- `allowed` or `blocked`
- context tuple (`sr_id`, `stepIndex`, `targetCellId`)

### TRANSPORT_REQUEST

Log:

- `executed` or `skipped`
- reason (e.g., `gate_blocked`, `transport_error`, `retry_later`)

## Acceptance criteria

- Transport state machine is never entered unless gate is true for current (`sr_id`, `stepIndex`, `targetCellId`).
- `reserve_remote_target(...)` result is used as control input, not ignored.
- Production PLC endpoint mapping remains reused.
- Shared transport PLC requests use `192.168.168.64:4840`.
- v1 behavior remains minimal and practical with retries, without cancel/TTL/reconciliation complexity.

