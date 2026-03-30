# Executive summary

Current PLC AAS implementation in `PLC_code/` is split into:
- **Active AAS** = OPC UA server methods implemented in FBs under `program_block/AAS/OPC UA Methods/` and instantiated in `Active AAS_DB`.
- **Passive AAS** = runtime state/queue/device execution in `program_block/AAS/Passive_AAS.scl` with data exposed from `PassiveAAS_DB`.

The OPC UA interface is method-based with one input and one output string argument per method (`InputMessage`, `OutputMessage`). Internally, methods convert incoming `WString` to `String`, parse slash-delimited fields (`/`), and execute queue operations and/or transport state changes.

No structured UA method payload (object/struct) is implemented; behavior is driven by positional string tokens and hardcoded support ranges/constants.

---

# PLC AAS architecture overview

## Active AAS (request entry)

- `PLC_code/PLCSeviceProvider.xml` declares OPC UA object `PLCServiceProvider` and method nodes:
  - `FreeFromPosition` (`ns=1;i=7000`)
  - `FreeFromQueue` (`ns=1;i=7001`)
  - `GetStatus` (`ns=1;i=7002`)
  - `GetSupported` (`ns=1;i=7003`)
  - `ReportProduct` (`ns=1;i=7004`)
  - `ReserveAction` (`ns=1;i=7005`)
- Each method is mapped via `si:MethodMapping` to method instance fields in `Active AAS_DB`.
- Method runtime pattern in all FBs:
  1. `OPC_UA_ServerMethodPre`
  2. method logic when `UAMethod_Called = TRUE`
  3. `UAMethod_Result := OpcUa_Good`, `UAMethod_Finished := TRUE`
  4. `OPC_UA_ServerMethodPost`

## Passive AAS (state + execution)

- `Passive_AAS.scl` orchestrates:
  - matching queued and reported items (`CheckAction`)
  - transport/action execution (`DeviceSlow`)
  - finished item cleanup (`RemoveFinishedProduct`)
  - status publishing (`CurrentId`, `CurrentIdPosition`, `ItemCountP`, `ItemCountRP`, `QueueStatus`)
- `PLCSeviceProvider.xml` maps dynamic/static UA variables to `PassiveAAS_DB` fields, including:
  - `ActionStatus`, `CurrentId`, `CurrentPosition`, `ItemCountP`, `ItemCountRP`, `OutputPositionSr`, `QueueHistoryBuffer`, `QueueStatus`
  - `ID`, `InputPosition`, `Name`, `OutputPosition`, `Submodel`, `TotalMaxBuffer`

---

# Relevant blocks / files

- OPC UA mapping:
  - `PLC_code/PLCSeviceProvider.xml`
- Active AAS methods:
  - `program_block/AAS/OPC UA Methods/ReserveAction.scl`
  - `program_block/AAS/OPC UA Methods/GetSupported.scl`
  - `program_block/AAS/OPC UA Methods/GetStatus.scl`
  - `program_block/AAS/OPC UA Methods/ReportProduct.scl`
  - `program_block/AAS/OPC UA Methods/FreeFromQueue.scl`
  - `program_block/AAS/OPC UA Methods/FreeFromPosition.scl`
- Parser and helper:
  - `program_block/AAS/low level functions/GetMessage.scl`
  - `program_block/AAS/low level functions/GetPQueueStatus.scl`
  - `program_block/AAS/low level functions/RemoveFinishedProduct.scl`
  - `program_block/AAS/low level functions/AddItemIdToBuffer.scl`
- Passive runtime:
  - `program_block/AAS/Passive_AAS.scl`
  - `program_block/Device Data/CheckAction.scl`
  - `program_block/Device Data/DeviceSlow.scl`
- Queue internals:
  - `program_block/Priority Queue functions/PQueue_*.scl`
  - `program_block/Queue functions/Queue_*.scl`
- DB/UDT exports (evidence):
  - `program_block/AAS/Active AAS_DB.pdf`
  - `program_block/AAS/Passive AAS_DB.pdf`
  - `plc_data_types/typeOPCUAStatus.pdf`
  - `plc_data_types/typeOPCUAMethodhandling.pdf`
  - `CommBlock_DB.pdf`

---

# OPC UA methods and their meaning

## `ReserveAction`
- Purpose: reserve a new action/product into priority queue.
- Input parsing:
  - `element[0]` -> `id` (`DINT`)
  - `element[1]` -> `priority` (`UINT`)
  - `element[2]` -> `material` (`INT`)
  - `element[3]` -> `initialPosition/operation` (`INT`)
  - `element[4]` -> `finalPosition/operationParameter` (`INT`)
- Logic:
  - reject `id = 0`
  - reject if already in PQueue (`PQueue_find`)
  - compute support score from hardcoded ranges (`SupportMaterial*`, `SupportA*`, `SupportB*`)
  - if supported, enqueue via `PQueue_push`
- Output:
  - success: `Success`
  - failure: `Error:<hex>` or `Error1:<hex>_Error2:<hex>`

## `GetSupported`
- Purpose: report support score + queue position estimate.
- Input parsing uses `element[1]`, `element[2]`, `element[3]`, `element[4]`.
- Logic:
  - support score from same range constants
  - queue position computed by summing `PQueue.subElementCount[0..element[1]]`
- Output:
  - `support:<value>_position:<value>`

## `GetStatus`
- Purpose: return queue state for one product ID.
- Input parsing:
  - `element[0]` -> item id (`DINT`)
- Logic:
  - find in PQueue (`PQueue_find`)
  - if not found or invalid id -> error
  - if found at active position -> `inProgress`
  - else -> `position:<index>`
- Output:
  - `inProgress` or `position:<n>` or `Error:<hex>`

## `ReportProduct`
- Purpose: register product presence into reported-products queue (`RPQueue`).
- Input parsing:
  - `element[0]` -> item id (`DINT`)
- Logic:
  - reject `id = 0`
  - reject duplicate in RPQueue (`Queue_find`)
  - push via `Queue_push`
- Output:
  - `Success` or `Error:<hex>` / `Error1:<hex>_Error2:<hex>`

## `FreeFromQueue`
- Purpose: remove item from priority queue by id.
- Input parsing:
  - `element[0]` -> item id (`DINT`)
- Logic:
  - find in PQueue
  - reject if active item (`PQErrItemAlreadyInProgress`)
  - otherwise remove via `PQueue_removeByPosition`
- Output:
  - `Success` or `Error:<hex>` / `Error1:<hex>_Error2:<hex>`

## `FreeFromPosition`
- Purpose: clear output position marker in passive AAS.
- Input parsing:
  - `element[0]` compared to `PassiveAAS_DB.OutputPositionSr`
- Logic:
  - if equal: set `OutputPositionSr := '0'`
  - else: return `PErrItemNotInPosition` as error string
- Output:
  - `Success` or `Error:<hex>`

---

# Expected request/data format

- UA method signature (from NodeSet): one `InputMessage` (`String`) and one `OutputMessage` (`String`).
- PLC method internals define `InputMessage/OutputMessage` as `WString`, but parse using:
  - `WSTRING_TO_STRING(InputMessage)`
  - `GetMessage(InputMessage: String)`
- Actual message grammar:
  - delimiter: `/`
  - fixed positional tokens
  - no escaping/quoted fields in parser

## Implemented positional fields

- For `ReserveAction`:
  - `0=id`
  - `1=priority`
  - `2=material`
  - `3=initialPosition/operation`
  - `4=finalPosition/operationParameter`
- For `GetSupported`:
  - `1=priority-bucket upper index`
  - `2=material`
  - `3=initialPosition/operation`
  - `4=finalPosition/operationParameter`
- For `GetStatus`, `ReportProduct`, `FreeFromQueue`, `FreeFromPosition`:
  - `0=id` (or output-position id string for `FreeFromPosition`)

Fields explicitly named as `product ID / action / target / source / priority / parameters` are **not modeled as named properties** in payload. Only positional tokens are implemented.

---

# Full request execution flow

1. OPC UA client calls one method (`ReserveAction`, `GetStatus`, etc.) with `InputMessage`.
2. Method FB runs `OPC_UA_ServerMethodPre` and reads `UAMethod_Called`.
3. On call:
   - parse `InputMessage` by `/` using `GetMessage`
   - cast token(s) to numeric/string as needed
   - execute queue/state operation
   - build response string into `OutputMessage`
4. Method sets:
   - `tempUAM_MethodHandling.UAMethod_Result := OpcUa_Good`
   - `tempUAM_MethodHandling.UAMethod_Finished := TRUE`
5. Method FB runs `OPC_UA_ServerMethodPost`, publishing output and method completion status.
6. Independently, cyclic passive logic (`Passive_AAS.scl`) processes queued/reported matches:
   - `CheckAction` finds actionable item
   - `DeviceSlow` performs handshake with machine states (`Idle/Ready/Busy/Done/Error`)
   - on falling edge of `deviceRunning`, `RemoveFinishedProduct` removes completed item from RPQueue and PQueue and updates history
   - passive status strings are refreshed for OPC UA variable reads

---

# Transport-related capabilities already present

- `ReserveAction` exists and is functional (queue reservation).
- `DeviceSlow` implements handshake-based execution:
  - output commands: `CmdSetReady`, `CmdStart`, `CmdResetDone`, `CmdProductID`
  - input states: `StsIdle`, `StsReady`, `StsBusy`, `StsDone`, `StsError`
- `CommBlock_DB.pdf` contains matching communication fields:
  - status bits: `Idle`, `Ready`, `Busy`, `Done`, `Error`
  - command bits: `SetReady`, `Start`, `ResetDone`
  - `ProductID` dword
- `ActionStatus` strings used: `idle`, `inProgress`, `failed`.

No separate method explicitly named `ReserveTransport` is exposed in NodeSet; exported method name is `ReserveAction`.

---

# Limitations / assumptions

- Input protocol is rigid positional string format with `/` delimiter.
- `id = 0` is invalid in multiple methods.
- Methods return business errors in output strings while still setting UA method result to `OpcUa_Good`.
- Support logic is hardcoded via constants/ranges:
  - `SupportMaterialLow100..High100`, `SupportMaterialLow60..High60`
  - `SupportALow100..High100`, `SupportALow60..High60`
  - `SupportBLow100..High100`, `SupportBLow60..High60`
- Queueing model:
  - supports multiple queued items in PQueue/RPQueue
  - one active item tracked via `activeItemPosition`
  - no evidence of parallel execution of multiple active items in one service provider instance
- Hardcoded static metadata in passive DB defaults:
  - `ID='11'`, `Name='man1'`, `Submodel='Manipulator'`, etc.
- NodeSet namespace URI/endpoint is static in export (`opc.tcp://192.168.168.63`).

---

# Diagnostics / error handling

- Output success strings:
  - `Success`
  - `inProgress`
  - `position:<n>`
  - `support:<v>_position:<p>`
- Output error strings:
  - `Error:<hex>`
  - `Error1:<hex>_Error2:<hex>`
- Frequently referenced status identifiers:
  - `PQErrItemAlreadyInQueue`
  - `ErrItemNotSupported`
  - `PQErrInvalidItemId`
  - `PQErrItemNotFound`
  - `RQErrIdAlreadyKnown`
  - `PQErrItemAlreadyInProgress`
  - `PErrItemNotInPosition`
  - `PQNoError`
- OPC UA pre/post diagnostic structure:
  - `typeOPCUAStatus` = `done`, `busy`, `error`, `status`
  - `typeOPCUAMethodhandling` = `UAMethod_Called`, `UAMethod_Result`, `UAMethod_Finished`

---

# Uncertainties / not confirmed items

- Exact numeric values of many constants/status words (e.g. `PQErr*`, `ErrItemNotSupported`, `Support*` boundaries, `WordNumberSpaces`, `QueueCountSmall`, `SubQueueCount`) are **not confirmed from code/project files** available as SCL/XML/PDF text exports.
- Exact OB/program call chain that invokes `Passive_AAS` and method instance FBs in runtime scan is **not confirmed from code/project files** in `PLC_code/`.
- Exact physical IO/profinet binding between `DeviceSlow` command/status pins and real machine signals is **not confirmed from code/project files** (only `CommBlock_DB` field definitions are visible).
