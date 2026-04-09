# Transport PLC integration

## Where transport PLC flow was added

Transport PLC execution was added to `ESP32_Firmware_ASS_Interpreter/main/app.c` in the non-local branch of the AAS decision flow inside `State_Mimo_Polozena` (the branch that already performs target PLC `GetSupported/ReserveAction/GetStatus` through `reserve_remote_target(...)`).

## Modified function(s)

- Added new function: `request_transport_plc(const char *sr_id, uint16_t localCellId, const TRecipeStep *step, SemaphoreHandle_t xEthernet)`
- Updated non-local AAS branch in `State_Mimo_Polozena` to:
  - call `request_transport_plc(...)` after target reserve success + gate allow,
  - on transport success set `RAF = State_WaitUntilRemoved`.
- Added transport wait logs in `case State_WaitUntilRemoved` only (logic unchanged).

## Exact transport PLC call sequence

Transport endpoint is fixed and used directly by the new function:

- `opc.tcp://192.168.168.64:4840`

Sequence performed by `request_transport_plc(...)`:

1. Build InputMessage in 5-field format:
   - `sr_id/localCellId/TypeOfProcess/ParameterProcess1/ParameterProcess2`
2. Call `OPC_GetSupported(endpoint, inputMsg, outBuf, ...)`
3. If supported, call `OPC_ReserveAction(endpoint, inputMsg, outBuf, ...)`
4. Poll status via `OPC_GetStatus(endpoint, sr_id, outBuf, ...)`
5. On status success -> return success, otherwise fail

## How legacy LDS routing is avoided

After successful target reserve and successful transport PLC execution, flow does **not** continue to:

- `State_Inicializace_ZiskaniAdres`
- `State_Poptavka_Vyroba`
- `GetWinningCell(...)` path
- LDS transport request path

Instead, it directly transitions to:

- `RAF = State_WaitUntilRemoved`

This bypasses legacy LDS decision/routing path for this orchestration branch.

## Where `RAF = State_WaitUntilRemoved` is set

In the non-local AAS branch in `State_Mimo_Polozena`, immediately after:

- target PLC reserve/gate success,
- transport PLC `GetSupported/ReserveAction/GetStatus` success.

Also, existing wait state transition remains unchanged in `State_WaitUntilRemoved`:

- if `!Parametry->CardOnReader` then `RAF = State_Mimo_Polozena`.

## Confirmation of required constraints

- `State_Transport` is **NOT** used by the new target-first transport PLC branch.
- `State_WaitUntilRemoved` is **reused** as-is for tag removal handling.
- Legacy wait behavior (`!CardOnReader -> State_Mimo_Polozena`) remains unchanged.

## Example expected log

```text
TRANSPORT_PLC start
TRANSPORT_PLC endpoint=opc.tcp://192.168.168.64:4840
TRANSPORT_PLC GetSupported InputMessage=123456/2/7/0/0
TRANSPORT_PLC GetSupported callOk=1 response=Support:1 support=1
TRANSPORT_PLC ReserveAction InputMessage=123456/2/7/0/0
TRANSPORT_PLC ReserveAction callOk=1 response=Success
TRANSPORT_PLC GetStatus poll=1 callOk=1 response=inProgress
TRANSPORT_PLC GetStatus poll=2 callOk=1 response=finished
TRANSPORT_PLC result=SUCCESS
TRANSPORT_PLC success -> entering State_WaitUntilRemoved
TRANSPORT_WAIT waiting for tag removal
TRANSPORT_WAIT tag disappeared
```
