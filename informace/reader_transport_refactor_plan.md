# Reader Transport Refactor Plan (Legacy -> AAS)

## Scope and constraints

- Scope is limited to existing firmware in `ESP32_Firmware_ASS_Interpreter`.
- Keep one identical firmware for all readers.
- Behavior must stay configuration/data driven (`MyCellInfo.IDofCell`, endpoints/IDs, recipe data).
- No architecture redesign; only replace transport call flow in existing states.
- Final transport payload contract: `<sr_id>/0/0/0/0`.

## Where to change code

Primary file:
- `ESP32_Firmware_ASS_Interpreter/main/app.c`
  - `case State_Poptavka_Transporty`
  - `case State_Rezervace`
  - `case State_Transport`
  - route selection in `case State_Mimo_Polozena` (conditions using transport reservation IDs)

Supporting files:
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`
  - legacy helper usage currently wired from the three states
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.h`
  - legacy helper declarations
- `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.h`
  - keep AAS methods (`OPC_GetSupported`, `OPC_ReserveAction`, `OPC_GetStatus`)
  - add alias/wrapper for rollback name `FreeFromQueue` mapped to existing `OPC_FreeFromPosition`

## Legacy functions/calls to remove from transport path

Remove usage from transport branch (states above):
- `GetWinningCell(...)` when called with `Transport` from `State_Poptavka_Transporty`
- `AskForValidOffer(...)` in `State_Rezervace`
- `ReserveAllOfferedReservation(...)` in `State_Rezervace`
- `AskForValidReservation(...)` in `State_Transport`
- `DoReservation(..., false)` in `State_Transport`

Legacy OPC methods no longer used by transport flow:
- `Inquire`
- `GetInquireIsValid` / OPC `IsValid`
- `Reserve` / OPC `Rezervation`
- `DoReservation_klient` / OPC `DoProcess`

Note:
- Keep these symbols compiled if still needed by non-transport legacy paths, but detach transport states from them.

## AAS replacements to insert

Transport branch call order (same endpoint currently in `MyCellInfo.IPAdress`, later config-driven endpoint):
1. Build input message: `<sr_id>/0/0/0/0`.
2. `OPC_GetSupported(endpoint, msg, outBuf, outSize)`.
3. `OPC_ReserveAction(endpoint, msg, outBuf, outSize)`.
4. Poll `OPC_GetStatus(endpoint, sr_id, outBuf, outSize)` until terminal success/error/timeout.
5. On rollback path call `FreeFromQueue(sr_id)` (firmware-side wrapper to existing `OPC_FreeFromPosition` unless PLC API already renamed).

## State-level minimal modifications

### `State_Poptavka_Transporty`

Current behavior:
- Selects transport cell via `GetWinningCell(..., Transport, ...)`.
- Writes `TransportCellID` and `TransportCellReservationID`.

Replace with:
- Keep branch decision based on `NeedForTransport` and step type.
- Do not select transport provider via `GetWinningCell`.
- Build transport message from current tag `sr_id`.
- Call `OPC_GetSupported` only.
- If supported -> proceed to `State_Rezervace`.
- If unsupported/error -> stay/retry (existing retry style) or route to failure state.
- Stop writing `TransportCellID` / `TransportCellReservationID` for new flow.

### `State_Rezervace`

Current behavior:
- Validates old offers (`AskForValidOffer`), then reserves all (`ReserveAllOfferedReservation`).

Replace with:
- Keep state entry and semaphore pattern.
- Call transport reserve helper (`OPC_ReserveAction` with `<sr_id>/0/0/0/0`).
- On success -> go `State_Transport`.
- On failure -> call rollback `FreeFromQueue(sr_id)` only if target reservation had been done earlier in sequence; then retry/fail path.
- Remove dependency on reservation IDs in step.

### `State_Transport`

Current behavior:
- legacy reservation validity + `DoReservation(..., false)` (DoProcess transport start).

Replace with:
- Keep NFC write-back of local flags/budget if needed by existing state machine.
- Replace execution trigger with polling loop:
  - `OPC_GetStatus(endpoint, sr_id, ...)`
  - interpret terminal success vs error vs timeout
- On success -> `RAF = State_WaitUntilRemoved` (existing behavior preserved).
- On error/timeout -> rollback via `FreeFromQueue(sr_id)` and route to existing failure/retry path.

## Integration details for one-firmware behavior

- Endpoint/ID source remains runtime configuration; no hardcoded role branch.
- Decision to enter transport branch stays data driven:
  - `NeedForTransport`
  - `MyCellInfo.IDofCell` vs target process cell
  - recipe step contents
- Transport provider remains "cell without reader"; only production-reader performs calls.

## Data model impact in NFC step

Keep:
- `NeedForTransport` (still needed to branch state machine)

Likely obsolete in new flow:
- `TransportCellID`
- `TransportCellReservationID`
- transport reservation times tightly coupled to legacy reserve IDs

Transition-safe approach:
- stop using these fields in logic first;
- keep writing zeros/defaults for compatibility during migration.

## Rollback mapping

Requested method name:
- `FreeFromQueue`

Current firmware API:
- `OPC_FreeFromPosition(endpoint, sr_id, ...)`

Minimal-change recommendation:
- add `OPC_FreeFromQueue(...)` wrapper in `OPC_klient` calling existing node/method until PLC naming is aligned.
- update transport flow docs/call sites to use `FreeFromQueue` semantic name.

## Risks and edge cases

- `sr_id` missing/invalid: must fail early before `GetSupported`.
- duplicate scans: keep existing re-scan guard to prevent duplicate `ReserveAction`.
- status token mismatch (`finished` vs `inProgress`/`position:N`): parser must treat only agreed terminal values as success.
- timeout after successful reserve: rollback must be bounded/retriable; avoid infinite hold.
- mixed old/new tags containing reservation IDs: new logic must ignore legacy reservation ID fields.
- semaphore contention (`xEthernet`, `xNFCReader`): preserve existing timeout/retry pattern to avoid deadlocks.

