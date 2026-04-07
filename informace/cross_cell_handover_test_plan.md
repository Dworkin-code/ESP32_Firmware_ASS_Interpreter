# Cross-cell handover test plan

## Test objective

Verify simulation-level behavior:

- reader on cell A finishes local production step
- reader resolves next production cell B
- reader calls B over AAS using:
  - `GetSupported(InputMessage)`
  - `ReserveAction(InputMessage)`
  - `GetStatus(sr_id)`
- reservation on B succeeds and is visible in logs

Transport cell is not part of this test.

## Test setup (two readers / two production cells)

1. Deploy same firmware binary to both reader devices (no per-cell fork).
2. Ensure each reader has different `MyCellInfo.IDofCell` in runtime config/NVS.
3. Ensure endpoint discovery data (`GetCellInfoFromLDS`) contains both production cells and reachable OPC endpoints.
4. Prepare NFC recipe where:
   - current step runs on A (local cell)
   - next step type can be handled by B (remote cell)
5. Place tag on reader A and let local processing finish.

## Expected logs on reader A

Look for the new `[CROSS_CELL]` messages:

- `local cell finished, next target resolved: ...`
- `remote GetSupported request InputMessage=...`
- `remote GetSupported callOk=... response=...`
- `remote ReserveAction request InputMessage=...`
- `remote ReserveAction callOk=... response=...`
- `remote GetStatus poll=... sr_id=... status=...` (optional)
- `remote reservation SUCCESS targetCell=... sr_id=...`

If unsuccessful, expect explicit failure logs:

- `remote target not supported ...`
- `remote reservation FAILED ...`
- semaphore/endpoint/message-build errors

## Verification checklist

- `sr_id` used for remote handover equals UID-derived `sr_id` from the same product/tag.
- target cell ID in logs is different from local `MyCellInfo.IDofCell`.
- `GetSupported` is called before `ReserveAction`.
- `ReserveAction` response is non-error on success scenario.
- at least one `GetStatus(sr_id)` response is logged when reservation succeeded.

## Negative tests

1. **No remote candidate**
   - configure recipe/type so only local cell matches.
   - expect "skip/no remote candidate" style logs.

2. **Unsupported reservation**
   - make remote `GetSupported` return unsupported/error.
   - expect no successful `ReserveAction`.

3. **ReserveAction reject**
   - remote returns `Error:*` for `ReserveAction`.
   - expect failure log and no success line.

4. **Endpoint format mismatch**
   - verify endpoint normalization (`opc.tcp://` prefix handling) by testing both endpoint styles.

## Notes

- This milestone verifies remote production-cell reservation only.
- No transport reservation/execution is expected in this test phase.
