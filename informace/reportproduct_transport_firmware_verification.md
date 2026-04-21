# ESP32 firmware verification: `ReportProduct` in transport workflow

## Scope

This report checks only ESP32 reader firmware usage of `ReportProduct` (`OPC_ReportProductEx` / `OPC_ReportProduct`) and answers whether it is called in transport (`REQUEST_TRANSPORT` / `TRANSPORT_PLC`) flow.

## 1) Exact locations where `ReportProduct` is called

### A. Runtime call site (used by app logic)

File: `ESP32_Firmware_ASS_Interpreter/main/app.c`

```912:933:ESP32_Firmware_ASS_Interpreter/main/app.c
/* PLC AAS: build sr_id from UID (now stored in TCardInfo), write CurrentId, call ReportProductEx to get OutputMessage */
char sr_id_buf[16];
...
if (xSemaphoreTake(Parametry->xEthernet, (TickType_t)5000) == pdTRUE)
{
  OPC_WriteCurrentId(MyCellInfo.IPAdress, uidStr);
  if (have_sr_id)
    report_call_ok = OPC_ReportProductEx(MyCellInfo.IPAdress, sr_id_buf, reportOutBuf, sizeof(reportOutBuf));
  xSemaphoreGive(Parametry->xEthernet);
}
```

Result:
- `ReportProduct` is called through `OPC_ReportProductEx(...)`.
- Endpoint passed is **`MyCellInfo.IPAdress`** (current/local reader cell PLC endpoint).

### B. Wrapper implementations (not additional call sites)

File: `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`

```620:666:ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c
/* PLC contract: ReportProduct expects ONLY a decimal integer string (sr_id). */
bool OPC_ReportProduct(const char *endpoint, const char *sr_id_decimal)
{
    ...
    UA_NodeId methodId = UA_NODEID_NUMERIC(PLC_NODEID_METHOD_NS, PLC_NODEID_REPORTPRODUCT_ID);
    UA_StatusCode ret = UA_Client_call(client, methodId, methodId, 1, &inputVar, &outputSize, &output);
    ...
}
```

```695:742:ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c
/* ReportProductEx: returns call success and fills outBuf with OutputMessage (Success / Error:XXXX). Logs sr_id digits only. */
bool OPC_ReportProductEx(const char *endpoint, const char *sr_id_decimal, char *outBuf, size_t outSize)
{
    ...
    UA_NodeId methodId = UA_NODEID_NUMERIC(PLC_NODEID_METHOD_NS, PLC_NODEID_REPORTPRODUCT_ID);
    UA_StatusCode ret = UA_Client_call(client, methodId, methodId, 1, &inputVar, &outputSize, &output);
    ...
}
```

These are generic client wrappers. They do not prove transport usage by themselves; actual usage is determined by call sites in `app.c`.

## 2) State/path analysis for requested states

## `LOCAL_PROCESS`

Entry decision and local branch:

```1006:1018:ESP32_Firmware_ASS_Interpreter/main/app.c
uint16_t ownerCellId = resolve_owner_cell_id_from_process_type(step->TypeOfProcess);
bool localProcess = (ownerCellId != 0U) && (ownerCellId == MyCellInfo.IDofCell);
...
if (localProcess)
{
  NFC_STATE_DEBUG(GetRafName(RAF), "AAS_DECISION: LOCAL_PROCESS\n");
  ...
```

Inside `LOCAL_PROCESS`, methods called are `GetSupported` and `ReserveAction` on `MyCellInfo.IPAdress`; there is **no additional** `ReportProduct` call in this block:

```1039:1056:ESP32_Firmware_ASS_Interpreter/main/app.c
/* Optional: GetSupported; if response starts with "Error:" abort step */
bool ok_supported = OPC_GetSupported(MyCellInfo.IPAdress, msg5, outBuf, sizeof(outBuf));
...
/* ReserveAction */
outBuf[0] = '\0';
bool ok_reserve = OPC_ReserveAction(MyCellInfo.IPAdress, msg5, outBuf, sizeof(outBuf));
```

## `REQUEST_TRANSPORT`

Non-local branch:

```1187:1196:ESP32_Firmware_ASS_Interpreter/main/app.c
NFC_STATE_DEBUG(GetRafName(RAF),
                "AAS_DECISION: REQUEST_TRANSPORT (TypeOfProcess=%u owner_cell_id=%u local_cell_id=%u)\n",
                ...
TargetReserveResult reserveResult = reserve_remote_target(&iHandlerData, sr_id_buf, MyCellInfo.IDofCell, Parametry->xEthernet,
                                                          &targetCellId, &targetStepIndex);
```

`reserve_remote_target(...)` performs `GetSupported` + `ReserveAction` on target endpoint, not `ReportProduct`:

```561:571:ESP32_Firmware_ASS_Interpreter/main/app.c
bool supportedCallOk = OPC_GetSupported(endpoint, inputMsg, outBuf, sizeof(outBuf));
...
bool reserveCallOk = OPC_ReserveAction(endpoint, inputMsg, outBuf, sizeof(outBuf));
```

## `TRANSPORT_PLC`

Transport request function:

```634:672:ESP32_Firmware_ASS_Interpreter/main/app.c
printf("TRANSPORT_PLC start\n");
printf("TRANSPORT_PLC endpoint=%s\n", TRANSPORT_PLC_ENDPOINT);
...
bool supportedCallOk = OPC_GetSupported(TRANSPORT_PLC_ENDPOINT, inputMsg, outBuf, sizeof(outBuf));
...
bool reserveCallOk = OPC_ReserveAction(TRANSPORT_PLC_ENDPOINT, inputMsg, outBuf, sizeof(outBuf));
```

Then polls `GetStatus` on transport PLC:

```683:687:ESP32_Firmware_ASS_Interpreter/main/app.c
for (int i = 0; i < TRANSPORT_STATUS_POLLS; i++)
{
  ...
  bool statusOk = OPC_GetStatus(TRANSPORT_PLC_ENDPOINT, sr_id, outBuf, sizeof(outBuf));
```

No `OPC_ReportProductEx` call in `request_transport_plc(...)`.

## `TARGET_RESERVE`

Within `reserve_remote_target(...)` (the target reserve phase), only `GetSupported` and `ReserveAction` are used on target PLC endpoint:

```541:571:ESP32_Firmware_ASS_Interpreter/main/app.c
printf("TARGET_RESERVE resolved endpoint=%s\n", endpoint);
...
bool supportedCallOk = OPC_GetSupported(endpoint, inputMsg, outBuf, sizeof(outBuf));
...
bool reserveCallOk = OPC_ReserveAction(endpoint, inputMsg, outBuf, sizeof(outBuf));
```

No `ReportProduct` call in target reserve path.

## 3) Where `ReportProduct` is called (local vs target vs transport)

- **Local PLC:** **Yes**. Called once via `OPC_ReportProductEx(MyCellInfo.IPAdress, ...)` in `app.c`.
- **Target PLC:** **No** direct `ReportProduct` call found.
- **Transport PLC:** **No** direct `ReportProduct` call found.

## 4) Transport workflow step-by-step (methods called)

For non-local step (`REQUEST_TRANSPORT`):

1. `ReportProductEx` is called earlier in common AAS entry path, with endpoint `MyCellInfo.IPAdress` (local/current reader cell).
2. `reserve_remote_target(...)`:
   - `OPC_GetSupported(targetEndpoint, inputMsg, ...)`
   - `OPC_ReserveAction(targetEndpoint, inputMsg, ...)`
   - optional `OPC_GetStatus(targetEndpoint, sr_id, ...)` polling helper
3. `request_transport_plc(...)`:
   - `OPC_GetSupported(TRANSPORT_PLC_ENDPOINT, inputMsg, ...)`
   - `OPC_ReserveAction(TRANSPORT_PLC_ENDPOINT, inputMsg, ...)`
   - `OPC_GetStatus(TRANSPORT_PLC_ENDPOINT, sr_id, ...)` polling

There is no `ReportProduct` call inserted in either target-reserve phase or transport-PLC phase.

## Explicit answer

**ReportProduct is NOT called during transport-specific phases (`TARGET_RESERVE`, `REQUEST_TRANSPORT` internals, `TRANSPORT_PLC`) on target PLC or transport PLC.**

It is called only once on the local/current endpoint (`MyCellInfo.IPAdress`) before branching into local/transport decision logic.

## Missing points (if expected per PLC contract)

If `reportedProductsQueue` must be populated on:
- the **target PLC** before target `ReserveAction`, or
- the **transport PLC** before transport `ReserveAction`,

then firmware currently lacks `OPC_ReportProductEx` calls in:
- `reserve_remote_target(...)` (before its `OPC_ReserveAction(endpoint, ...)`)
- `request_transport_plc(...)` (before its `OPC_ReserveAction(TRANSPORT_PLC_ENDPOINT, ...)`)

