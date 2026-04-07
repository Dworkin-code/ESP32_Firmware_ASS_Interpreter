# Reader Transport New Flow (AAS Contract)

## Final transport message

For transport provider calls, use fixed payload:

- InputMessage: `<sr_id>/0/0/0/0`
- Status input: `<sr_id>`

`sr_id` is built from NFC UID using existing `OPC_BuildSrIdFromUid(...)`.

## Step-by-step flow in existing state machine

## 1) Enter transport branch

Entry remains exactly from current branch rules in `main/app.c`:
- `State_Mimo_Polozena` routes to transport path when recipe indicates remote target / transport needed.
- `State_Poptavka_Transporty` is still the first transport-specific state.

Decision source must remain:
- `MyCellInfo.IDofCell`
- recipe step data (`NeedForTransport`, process target semantics)
- configuration (endpoint mapping)

## 2) `State_Poptavka_Transporty` (pre-check)

Code-level sequence:
1. If no transport needed (`NeedForTransport == 0` and non-transport step), jump to `State_Rezervace` (unchanged).
2. Build `sr_id` from current card UID (already done in app flow, reuse value).
3. Build transport message: `msg = "<sr_id>/0/0/0/0"`.
4. Call `OPC_GetSupported(transportEndpoint, msg, outBuf, sizeof(outBuf))`.
5. Parse output:
   - supported -> `RAF = State_Rezervace`
   - not supported / error -> retry or fail path (existing pattern with `continue`/failure state)

Remove from this state:
- transport `GetWinningCell(...)`
- writing `TransportCellID`, `TransportCellReservationID`

## 3) `State_Rezervace` (reservation)

Code-level sequence:
1. Keep semaphore take/release around Ethernet calls.
2. Call `OPC_ReserveAction(transportEndpoint, msg, outBuf, sizeof(outBuf))`.
3. If output is success -> `RAF = State_Transport`.
4. If reserve fails:
   - if there is already reserved target in same handshake, call rollback `FreeFromQueue(sr_id)`;
   - route to retry/failure branch.

Remove from this state:
- `AskForValidOffer(...)`
- `ReserveAllOfferedReservation(...)`

## 4) `State_Transport` (execution monitoring)

Code-level sequence:
1. Keep existing local NFC update block if needed (`IsTransport`, budget update).
2. Poll status in loop:
   - `OPC_GetStatus(transportEndpoint, sr_id, outBuf, sizeof(outBuf))`
   - every configured interval (for example 300-1000 ms)
   - until terminal status or timeout
3. Terminal handling:
   - success -> `RAF = State_WaitUntilRemoved` (same end state as now)
   - error/timeout -> rollback `FreeFromQueue(sr_id)` then failure/retry route

Remove from this state:
- `AskForValidReservation(...)`
- `DoReservation(..., false)` / `DoReservation_klient(...)`

## 5) Exit and continuation

After successful transport completion:
- state continues with existing chain:
  - `State_WaitUntilRemoved` (wait for tag removal)
  - later `State_Vyroba_Objeveni` and normal recipe progression

No redesign is required; only transport trigger/execute mechanics change.

## Rollback behavior

Rollback API for this refactor:
- logical name: `FreeFromQueue(sr_id)`
- minimal firmware implementation can wrap current `OPC_FreeFromPosition(...)`

When to rollback:
- reserve failed after partial success in multi-provider handshake
- status polling timeout/error after reserve

When not to rollback blindly:
- if provider reports already in-progress and ownership ambiguous; first re-check `GetStatus`.

## Mapping to existing states (quick map)

- `State_Mimo_Polozena`: unchanged entry logic.
- `State_Poptavka_Transporty`: legacy inquiry -> AAS support pre-check.
- `State_Rezervace`: legacy validate/reserve IDs -> AAS reserve action.
- `State_Transport`: legacy DoProcess -> AAS GetStatus polling.
- `State_WaitUntilRemoved`: unchanged successful exit.

## Edge conditions to implement explicitly

- missing `sr_id`: do not call AAS methods, route to failure/retry.
- duplicate scan of same tag: keep re-scan guard (already present in app flow).
- non-terminal status forever: hard timeout + rollback.
- stale legacy reservation fields on old cards: ignore for control flow in new transport path.

