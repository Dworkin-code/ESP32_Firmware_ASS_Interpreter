# Transport PLC migration report: time -> source/target

## 1. Files changed

- `ESP32_Firmware_ASS_Interpreter/main/app.c`
- `informace/transport_source_target_migration_report.md`

## 2. Exact functions changed

- `request_transport_plc()`
- `State_Machine()` (transport-entry branch selection and `State_Transport` branch)

## 3. Old transport payload semantics

In `request_transport_plc()` the 5-token message was built as:

- token0: `sr_id`
- token1: `localCellId`
- token2: `TypeOfProcess`
- token3: `ParameterProcess1`
- token4: `transportTimeSeconds` (derived from `TimeOfTransport`)

Additional behavior:

- `TimeOfTransport` was converted to `uint16` seconds (with special handling for `UA_DATETIME_SEC` scale).
- Debug output emphasized `transportTime`.

## 4. New transport payload semantics

`request_transport_plc()` now receives explicit `targetCellId` and builds:

- token0: `sr_id`
- token1: `localCellId` (unchanged metadata position)
- token2: `TypeOfProcess` (unchanged)
- token3: source/current cell (`localCellId`) as transport `P1`
- token4: target/destination cell (`targetCellId`) as transport `P2`

`TimeOfTransport` is no longer used for transport PLC payload construction.

Runtime safety checks added in `request_transport_plc()`:

- fail if `targetCellId == 0` (unresolved target)
- skip/fail if `source == target` (meaningless transport request)

Debug output now prints:

- `sr_id`
- source cell
- target cell
- process type
- full request payload string

## 5. Any assumptions made

- The existing 5-token contract must remain intact, so token count/order was preserved.
- `token1` is still kept as existing metadata field (`localCellId`), while transport parameters are represented by tokens 3 and 4.
- `targetCellId` returned by `reserve_remote_target()` is the resolved destination already aligned with `transport_gate_set()` and reservation logic.

## 6. Any risky or uncertain points

- If downstream transport PLC currently interprets token3 as legacy `ParameterProcess1`, behavior must be aligned there to treat token3/token4 as source/target.
- The legacy branch condition was changed from `TimeOfTransport > 0` to `NeedForTransport`; if any tag relies on non-standard combinations (`NeedForTransport=0` with remote target), transport trigger may differ.
- `request_transport_plc()` returns `false` on `source == target`, which keeps existing failure handling path (no forced success shortcut).

## 7. Suggested runtime test scenario

1. Use a recipe step that requires remote execution (`ownerCellId != local cell`) and confirms target reservation succeeds.
2. Verify log line from `request_transport_plc()` includes:
   - `sr_id=<id>`
   - `source=<localCellId>`
   - `target=<resolvedTargetCellId>`
   - `message=<sr_id/source/type/source/target>`
3. Confirm no `transportTime` debug lines are emitted.
4. Confirm transport PLC receives and accepts request with source/target in final two tokens.
5. Negative tests:
   - force unresolved target (`targetCellId=0`) and verify safe failure log.
   - use a case where source equals target and verify request is skipped with explicit log.
