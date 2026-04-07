# Reader Transport Helper Functions (Minimal Additions)

## Goal

Define minimal helper functions for replacing legacy transport calls with AAS calls, without changing architecture.

All helpers are intended for use from existing state handlers in `main/app.c`:
- `State_Poptavka_Transporty`
- `State_Rezervace`
- `State_Transport`

## 1) `build_transport_message()`

Suggested signature:

```c
bool build_transport_message(const uint8_t *uid, uint8_t uid_len,
                             char *sr_id_out, size_t sr_id_out_size,
                             char *msg_out, size_t msg_out_size);
```

Purpose:
- Build decimal `sr_id` from UID via existing `OPC_BuildSrIdFromUid(...)`.
- Build final transport payload: `<sr_id>/0/0/0/0`.

Inputs:
- `uid`, `uid_len`: current NFC tag UID bytes.
- output buffers for `sr_id` and `msg`.

Outputs:
- returns `true` on complete success.
- `sr_id_out`: decimal sr_id string.
- `msg_out`: exact AAS payload string.

Failure conditions:
- null pointers
- invalid UID length
- sr_id conversion failure
- output buffer overflow (`snprintf` length check)

## 2) `call_transport_reserve()`

Suggested signature:

```c
typedef enum {
  TRANSPORT_RESERVE_OK = 0,
  TRANSPORT_RESERVE_NOT_SUPPORTED,
  TRANSPORT_RESERVE_ERROR
} transport_reserve_result_t;

transport_reserve_result_t call_transport_reserve(const char *endpoint,
                                                  const char *transport_msg,
                                                  char *diag_out,
                                                  size_t diag_out_size);
```

Purpose:
- Execute the two reservation-phase AAS calls:
  1. `OPC_GetSupported(endpoint, transport_msg, ...)`
  2. `OPC_ReserveAction(endpoint, transport_msg, ...)`

Expected behavior:
- Parse output (`Error:*`, support indication, `Success`).
- Return explicit result enum for state machine branch decisions.
- Store last response in `diag_out` for logging.

Used by:
- `State_Poptavka_Transporty` (support pre-check)
- `State_Rezervace` (reserve execution)

Minimal-change note:
- can be split internally into precheck+reserve booleans if keeping current coding style.

## 3) `poll_transport_status()`

Suggested signature:

```c
typedef enum {
  TRANSPORT_STATUS_DONE = 0,
  TRANSPORT_STATUS_TIMEOUT,
  TRANSPORT_STATUS_ERROR
} transport_poll_result_t;

transport_poll_result_t poll_transport_status(const char *endpoint,
                                              const char *sr_id,
                                              uint32_t timeout_ms,
                                              uint32_t poll_interval_ms,
                                              char *last_status_out,
                                              size_t last_status_out_size);
```

Purpose:
- Poll `OPC_GetStatus(endpoint, sr_id, ...)` until terminal condition.

Terminal rules:
- done: agreed success token(s) from PLC (`finished` or project-defined equivalent)
- error: `Error:*` or explicit fail token
- timeout: no terminal state in `timeout_ms`

Used by:
- `State_Transport`

Implementation option:
- wrap existing `OPC_AAS_WaitCompletionPoll(...)` for first iteration, then unify parser behavior.

## Rollback helper (`FreeFromQueue`)

Requested contract includes rollback by `FreeFromQueue`.

Minimal firmware addition:

```c
bool OPC_FreeFromQueue(const char *endpoint, const char *sr_id_decimal,
                       char *outBuf, size_t outSize);
```

Implementation:
- wrapper to existing `OPC_FreeFromPosition(...)` until PLC API naming aligns.

Used when:
- partial reservation must be cancelled
- status polling ends with timeout/error and reservation should be released

## State usage matrix

- `State_Poptavka_Transporty`
  - `build_transport_message()`
  - support precheck part of `call_transport_reserve()` (or direct `OPC_GetSupported`)

- `State_Rezervace`
  - reserve part of `call_transport_reserve()` (or direct `OPC_ReserveAction`)
  - rollback via `OPC_FreeFromQueue(...)` when needed

- `State_Transport`
  - `poll_transport_status()`
  - rollback via `OPC_FreeFromQueue(...)` on timeout/error

## NFC step data still needed vs obsolete

Still needed:
- `NeedForTransport` (transport branch decision)
- existing process-target fields used to detect local vs remote step ownership

Likely obsolete for new transport contract:
- `TransportCellID`
- `TransportCellReservationID`
- transport reservation time derived from legacy reservation IDs

Migration-safe behavior:
- stop reading obsolete fields for new transport decisions;
- optionally keep writing default/zero values for backward compatibility.

## Risks for helper implementation

- inconsistent endpoint selection (must be config-driven transport provider endpoint)
- status token drift between PLC versions
- semaphore ownership around helper calls (`xEthernet`) must match current state-machine pattern
- duplicate reserve calls on re-scan if guard is bypassed

