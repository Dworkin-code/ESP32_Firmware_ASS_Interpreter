| Method | Purpose | Inputs | Outputs | Internal handler | Response format | Notes |
|---|---|---|---|---|---|---|
| `FreeFromPosition` | Clears output-position marker when requested ID matches current output slot | `InputMessage` (`String` in UA, `WString` in FB), token `[0]` string compared to `PassiveAAS_DB.OutputPositionSr` | `OutputMessage` | `program_block/AAS/OPC UA Methods/FreeFromPosition.scl` | `Success` or `Error:<hex>` | Sets `PassiveAAS_DB.OutputPositionSr := '0'` on success; error from `PErrItemNotInPosition`; UA result still `OpcUa_Good` |
| `FreeFromQueue` | Removes item from priority queue by item ID | `InputMessage`, token `[0]` -> `DINT id` | `OutputMessage` | `program_block/AAS/OPC UA Methods/FreeFromQueue.scl` | `Success` or `Error:<hex>` / `Error1:<hex>_Error2:<hex>` | Uses `PQueue_find`, blocks removal if `activeItemPosition` (`PQErrItemAlreadyInProgress`) |
| `GetStatus` | Returns queue position or in-progress state for item ID | `InputMessage`, token `[0]` -> `DINT id` | `OutputMessage` | `program_block/AAS/OPC UA Methods/GetStatus.scl` | `position:<n>` or `inProgress` or `Error:<hex>` | Uses `PQueue_find`; `id=0` invalid |
| `GetSupported` | Calculates support score and queue-position estimate | `InputMessage`, tokens `[1]` (priority index), `[2]` material, `[3]` initial/op, `[4]` final/opParam | `OutputMessage` | `program_block/AAS/OPC UA Methods/GetSupported.scl` | `support:<v>_position:<p>` | Support based on hardcoded `Support*` ranges; no explicit error output path in FB |
| `ReportProduct` | Registers product ID into reported-products queue (RPQueue) | `InputMessage`, token `[0]` -> `DINT id` | `OutputMessage` | `program_block/AAS/OPC UA Methods/ReportProduct.scl` | `Success` or `Error:<hex>` / `Error1:<hex>_Error2:<hex>` | Duplicate ID => `RQErrIdAlreadyKnown`; uses `Queue_find` + `Queue_push` |
| `ReserveAction` | Reserves/enqueues action request to priority queue | `InputMessage`, tokens `[0]=id`, `[1]=priority`, `[2]=material`, `[3]=initialPosition/operation`, `[4]=finalPosition/operationParameter` | `OutputMessage` | `program_block/AAS/OPC UA Methods/ReserveAction.scl` | `Success` or `Error:<hex>` / `Error1:<hex>_Error2:<hex>` | Checks duplicate via `PQueue_find`; support scoring from `Support*` ranges; enqueue via `PQueue_push` |

## Common method wrapper behavior

- All six methods are mapped in `PLC_code/PLCSeviceProvider.xml` via `si:MethodMapping` to `Active AAS_DB` instances.
- All use:
  - `OPC_UA_ServerMethodPre`
  - method logic when `UAMethod_Called = TRUE`
  - `UAMethod_Result := OpcUa_Good`
  - `UAMethod_Finished := TRUE`
  - `OPC_UA_ServerMethodPost`
- Payload parser: `GetMessage.scl` splits `String` by `/` into positional elements.
