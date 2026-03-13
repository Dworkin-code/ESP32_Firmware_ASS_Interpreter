## 1. Executive summary

The existing Testbed 4.0 codebase already contains a fairly rich notion of cells/stations, process types, and transport/reservation logic, both in the ESP32 reader firmware and in the PLC AAS/OPC UA layer. There is an explicit identity for each reader (`MyCellInfo.IDofCell` + OPC endpoint), recipe-step fields for assigning responsible process/transport cells, and a legacy state machine that decides whether to execute locally or add/request a transport step. The newer AAS path (ReportProduct/GetSupported/ReserveAction) focuses on a single PLC service provider and does not yet expose multi-cell routing decisions explicitly, but it reuses a generic queue/reservation infrastructure that could be extended. Overall, the architecture is **partially prepared** for “this unit belongs to cell X and routes steps accordingly”; most building blocks exist, but they are not yet fully unified around a per-cell routing model for the AAS flow.

## 2. Relevant files and functions found

- **ESP32 firmware – recipes and routing primitives**
  - `NFC_recipes.h` / `NFC_recipes.c`
    - `enum ProcessTypes { ToStorageGlass, StorageAlcohol, StorageNonAlcohol, Shaker, Cleaner, SodaMake, ToCustomer, Transport, Buffer }`
    - `CellInfo` / `Reservation` structs and helper functions:
      - `CellInfo` (ID, OPC endpoint string, supported process types list)
      - `GetCellInfoFromLDS(uint8_t aType, uint16_t *aNumberOfCells)`
      - `GetWinningCell(...)`
      - `ExistType(...)`
      - `AskForValidOffer(...)`, `ReserveAllOfferedReservation(...)`
      - `DoReservation(...)`, `IsDoneReservation(...)`, `OcupancyCell(...)`
    - `GetRecipeStepByNumber`, `GetCardInfoByNumber`, `AddRecipe`, `GetMinule` (step construction and modification).
- **ESP32 firmware – reader state machine and cell identity**
  - `app.c`
    - Global `CellInfo MyCellInfo;`
    - `app_main()`:
      - Loads `ID_Interpretter` from NVS → `MyCellInfo.IDofCell`.
      - Configures `MyCellInfo.IPAdress = "192.168.168.63:4840"` and `MyCellInfo.ProcessTypes = {0,1,2}` (example).
    - `State_Machine()`:
      - Enum-based state machine (`StavovyAutomat` from `NFC_recipes.h`) with states such as `State_Poptavka_Vyroba`, `State_Poptavka_Transporty`, `State_Rezervace`, `State_Transport`, `State_Vyroba_Objeveni`, `State_WaitUntilRemoved`.
      - Decision logic based on `TRecipeStep.TypeOfProcess`, `ProcessCellID`, `TransportCellID`, `NeedForTransport`, `IsProcess`, `IsTransport`, `IsStepDone`, `TimeOfProcess`, `TimeOfTransport`.
      - Legacy routing path uses `GetCellInfoFromLDS`, `GetWinningCell`, `DoReservation`, `IsDoneReservation`, and automatic insertion of `Transport` steps if the card appears at the “wrong” cell.
    - AAS/OPC UA path (guarded by `USE_PLC_AAS_FLOW`):
      - Builds `sr_id` from UID.
      - Calls `OPC_ReportProductEx`, optional `OPC_GetSupported`, and `OPC_ReserveAction` for the current step.
      - Waits for completion via `OPC_AAS_WaitCompletionPoll`, then marks `IsStepDone` and advances `ActualRecipeStep`.
- **ESP32 firmware – reader hardware and tag model**
  - `NFC_reader.c`
    - Tag loading/writing (info + steps, checksum).
    - `NFC_saveUID` stores UID into `TCardInfo.sUid` for later use in AAS flow.
  - `NFC_reader.h`
    - `TRecipeInfo` and `TRecipeStep` layouts:
      - `TRecipeStep` fields: `TypeOfProcess`, `ParameterProcess1/2`, `ProcessCellID`, `TransportCellID`, `ProcessCellReservationID`, `TransportCellReservationID`, `TimeOfProcess`, `TimeOfTransport`, `NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone`, etc.
- **ESP32 firmware – OPC UA client / AAS methods**
  - `OPC_klient.c`
    - Low-level per-cell reservation calls (legacy multi-cell path):
      - `Inquire`, `GetInquireIsValid`, `Reserve`, `DoReservation_klient`, `IsFinished`, `Occupancy`.
      - These take a `CellInfo` (with `IDofCell` and `IPAdress`) and drive methods like `"Inquire"`, `"Rezervation"`, `"DoProcess"`, `"IsFinished"`, `"Occupancy"` on the remote OPC UA servers.
    - AAS-style methods against a single PLC service provider (namespace 4):
      - `OPC_BuildSrIdFromUid`, `OPC_WriteCurrentId`.
      - `OPC_ReportProduct` / `OPC_ReportProductEx`.
      - `OPC_GetSupported`, `OPC_ReserveAction`, `OPC_FreeFromPosition`, `OPC_GetStatus`, `OPC_AAS_WaitCompletionPoll`.
- **PLC code – AAS OPC UA methods and queues**
  - `PLC_code/PLCSeviceProvider.xml`
    - OPC UA NodeSet for a single `PLCServiceProvider` object (ns=1), “Methods” folder with:
      - `GetStatus` (7002), `GetSupported` (7003), `ReportProduct` (7004), `ReserveAction` (7005), `FreeFromPosition` (7000), `FreeFromQueue` (7001).
    - “Dynamic” variables mapped to `PassiveAAS_DB`:
      - `ActionStatus`, `CurrentId`, `CurrentPosition`, `ItemCountP`, `ItemCountRP`, `OutputPositionSr`, `QueueHistoryBuffer`, `QueueStatus`.
    - “Static” variables mapped to `PassiveAAS_DB`:
      - `ID`, `InputPosition`, `Name`, `OutputPosition`, `Submodel`, `TotalMaximumBuffer` (per-station identity metadata).
  - `AAS/OPC UA Methods/GetSupported.scl`
    - Parses `InputMessage` → `InputArray.element[0..4]`.
    - Computes `tmpSupportValue` based on `material`/`supportA`/`supportB` ranges.
    - Computes queue position from priority and PQueue sub-element counts.
    - Returns `support:X_position:Y`.
  - `AAS/OPC UA Methods/ReserveAction.scl`
    - Parses `InputMessage` (`sr_id/priority/material/parameterA/parameterB`).
    - Builds `typeProductInQueue` item and inserts it into the priority queue (`PQueue_push`) if supported.
    - Rejects unsupported items with `Error:ErrItemNotSupported`.
  - `AAS/OPC UA Methods/ReportProduct.scl`
    - Registers `sr_id` into a reported-products queue (`ReportedProductsQueue`) with `Queue_find`/`Queue_push`.
  - `AAS/OPC UA Methods/FreeFromPosition.scl`
    - Compares `InputMessage` to `PassiveAAS_DB".OutputPositionSr`; clears it if matching.
  - `Device Data/DeviceSlow.scl`
    - Models a (slow) device executing queued actions:
      - Uses same material/supportA/supportB ranges to compute `waitTime`.
      - Drives `actionStatus` through `idle` → `inProgress` → `idle` and sets `OutputPositionSr` upon completion.
  - `Device Data/CheckAction.scl`
    - Looks for a candidate action:
      - Iterates over `priorityQueue` (`PQueue_get`), checks for a matching reported product in `reportedProductsQueue` (`Queue_find`).
      - Outputs item and its positions in PQueue and reported-products queue.
  - Priority queue utilities:
    - `Priority Queue functions/PQueue_push.scl`
    - `Priority Queue functions/PQueue_get.scl`
    - Plus non-priority `Queue_*` functions (from grep results).

## 3. Existing cell identity mechanisms

- **On the ESP32 reader (firmware-side identity)**
  - `CellInfo` in `NFC_recipes.h`:
    - Fields: `IDofCell`, `IPAdress`, `IPAdressLenght`, `ProcessTypes[]`, `ProcessTypesLenght`.
    - Represents a *cell* as “numeric ID + OPC endpoint + list of supported process types”.
  - `MyCellInfo` in `app.c`:
    - Initialized in `app_main()`:
      - `MyCellInfo.IDofCell` loaded from NVS key `"ID_Interpretter"` (per-reader persistent ID).
      - `MyCellInfo.IPAdress` set to `"192.168.168.63:4840"` (current PLC endpoint used by this reader).
      - `MyCellInfo.ProcessTypes` set to a small array (e.g. `{0,1,2}`) indicating which `ProcessTypes` this reader is associated with.
    - Used for:
      - Logging (`NFC_STATE_DEBUG` includes `MyCellInfo.IDofCell`).
      - Passing interpreter ID into cell-selection (`GetWinningCell(..., MyCellInfo.IDofCell, ...)`).
      - Comparing with step fields (`ProcessCellID`, `TransportCellID`) to know if “this step is mine”.
      - OPC endpoint selection (AAS path uses `MyCellInfo.IPAdress` as the `endpoint` argument).
  - Legacy LDS-style cell directory:
    - `GetCellInfoFromLDS(uint8_t aType, uint16_t *aNumberOfCells)` in `NFC_recipes.c`:
      - Hardcoded array `Vsechny[]` of 3 `CellInfo` entries:
        - Each with `IDofCell` (1,2,3), `IPAdress` (e.g. `"opc.tcp://Lubin-Laptop.local:20000"`), and a `ProcessTypes` array (which `ProcessTypes` this cell supports).
      - Filters cells by `aType` and mandatory inclusion of `Transport` and `ToStorageGlass` process types via `ChooseCell(...)`.
      - Returns an allocated array of `CellInfo` for candidate cells.
    - This is an explicit mapping from process types to cells (IDs + endpoints), although currently hardcoded and not matching the newer AAS layout.

- **On the PLC (service provider identity)**
  - `PLCSeviceProvider.xml`:
    - Defines a single OPC UA object `PLCServiceProvider` with:
      - Static attributes: `PassiveAAS_DB"."ID`, `"Name"`, `"InputPosition"`, `"OutputPosition"`, `"Submodel"`, `"TotalMaximumBuffer"`.
      - Dynamic attributes and queues: `CurrentId`, `ActionStatus`, `QueueStatus`, etc.
    - This effectively describes a **single logical service provider** (cell or station) on the PLC side, with ID, name, I/O positions, and buffer capacity.
  - There is no explicit multi-cell OPC UA model (no multiple `PLCServiceProvider` instances or separate AAS submodels per cell) in the checked-in PLC NodeSet; routing between multiple cells is currently handled more on the reader side.

**Answer to Q1 – explicit notion “this unit belongs to cell X”:**
- Yes, the reader firmware has an explicit, persistent notion of its own cell identity:
  - `MyCellInfo.IDofCell` (loaded from NVS) and `MyCellInfo.ProcessTypes` represent device/cell role.
  - `MyCellInfo.IPAdress` couples that identity to an OPC endpoint.
  - Recipe steps carry `ProcessCellID` and `TransportCellID`, allowing explicit assignment of steps to responsible cells.
  - On the PLC side, `PassiveAAS_DB"."ID` and `"Name"` model a specific service provider identity, though currently only one instance is defined.

## 4. Existing process-type / routing mechanisms

- **Recipe step typing and ownership fields**
  - `TRecipeStep` (from `NFC_reader.h`) includes:
    - `TypeOfProcess` (byte enum `ProcessTypes`).
    - `ParameterProcess1`, `ParameterProcess2` (type-specific parameters).
    - `ProcessCellID` / `ProcessCellReservationID`.
    - `TransportCellID` / `TransportCellReservationID`.
    - `NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone`.
  - These fields distinguish process execution responsibility vs transport responsibility and allow assigning specific cells (by ID) to each role.

- **Firm-side mapping from process type to candidate cells**
  - `GetCellInfoFromLDS(aType, &BunkyVelikost)`:
    - Constructs `CellInfo Vsechny[]` with:
      - Example cells:
        - ID 1 → endpoint 20000, `ProcessTypes` = `{1,2,3}` (some subset of `ProcessTypes`).
        - ID 2 → endpoint 20001, `ProcessTypes` = `{0,4,5,6}`.
        - ID 3 → endpoint 20002, `ProcessTypes` = `{7}`.
    - Filters these cells using `ChooseCell` with a requested process-types set `{Transport, ToStorageGlass, aType}`.
    - Produces a list of cells that can serve the requested `TypeOfProcess` plus mandatory transport and storage operations.

- **Firm-side routing decision (legacy path)**
  - In `State_Inicializace_ZiskaniAdres` → `State_Poptavka_Vyroba`:
    - Retrieves candidate `CellInfo` array via `GetCellInfoFromLDS` for the current `TypeOfProcess`.
    - Calls `GetWinningCell(Bunky, BunkyVelikost, MyCellInfo.IDofCell, step.TypeOfProcess, step.ParameterProcess1, step.ParameterProcess2, false, &Process);`
      - Internally:
        - Iterates cells whose `ProcessTypes` contain `aProcessType`.
        - Calls `Inquire(...)` OPC UA method on each cell, obtaining a set of `Reservation` objects (with `IDofCell`, `IDofReservation`, `Price`, `TimeOfReservation`).
        - Chooses the “best” cell (currently max `Price`) and returns as `Process`.
    - Updates the recipe step:
      - `ProcessCellID = Process.IDofCell`.
      - `ProcessCellReservationID = Process.IDofReservation`.
      - `NeedForTransport = (MyCellInfo.IDofCell != Process.IDofCell)`.
  - If `NeedForTransport` is true, `State_Poptavka_Transporty` selects a transport cell:
    - Another `GetWinningCell` call with `Transport` as `TypeOfProcess`, using either the target process cell or parameter as payload.
    - Writes `TransportCellID` and `TransportCellReservationID`.

- **Per-step “belongs here?” decision**
  - In `State_Mimo_Polozena`, after loading tag and skipping the AAS path (or when legacy is active), the state machine uses:
    - `MyCellInfo.IDofCell` vs `ProcessCellID` / `TransportCellID`.
    - Timers `TimeOfProcess` and `TimeOfTransport`.
  - Key branches:
    - If `IsProcess == true` or `IsTransport == true`: the reader understands that the step has been processed or transported and moves to the respective follow-up states.
    - If `TimeOfProcess > 0` and `MyCellInfo.IDofCell == ProcessCellID`: “process reservation exists for this cell” → go to `State_Vyroba_Objeveni`.
    - If `TimeOfTransport > 0` and `MyCellInfo.IDofCell != ProcessCellID`: “card appears at a non-process cell with a reserved transport” → go to `State_Transport`.
  - In `State_Vyroba_Objeveni`:
    - If `TypeOfProcess == Transport`: skips process and just marks the step done and advances to next.
    - Else, if `ProcessCellID != MyCellInfo.IDofCell`:
      - Recognizes that the tag is at the “wrong” cell.
      - Uses `GetMinule` + `AddRecipe(GetRecipeStepByNumber(10, ProcessCellID), ...)` to **insert a `Transport` step** before the current step, pointing at the correct process cell.
      - Adjusts `NextID` and `ActualRecipeStep` accordingly.
    - If `ProcessCellID == MyCellInfo.IDofCell`:
      - Proceeds to `State_Vyroba_OznameniOProvedeni` where the local device performs the process, updates `IsProcess`, `IsStepDone`, and advances `ActualRecipeStep`.

**Answer to Q2 – mapping from process type to responsible cell:**
- Yes, but primarily in the **legacy LDS-based path**:
  - `GetCellInfoFromLDS` encodes a mapping from `TypeOfProcess` values to a list of candidate `CellInfo` (cell IDs + endpoints) via process-type arrays.
  - `GetWinningCell` selects a specific cell based on OPC `Inquire` responses, effectively mapping from process type (and parameters) to a concrete cell.
  - Recipe steps then persist this mapping via `ProcessCellID` and `TransportCellID`.
- In the newer AAS flow:
  - The mapping is implicitly pushed into the PLC: the reader encodes `TypeOfProcess` (and parameters) into the 5-field message for `GetSupported`/`ReserveAction`, and the PLC decides support/queue placement.
  - However, there is currently only one `PLCServiceProvider` instance and no explicit per-cell AAS-level mapping beyond support ranges.

## 5. Existing transport / reservation mechanisms

- **Firmware-side reservation and transport**
  - `Reservation` struct in `NFC_recipes.h`:
    - `IDofCell`, `IDofReservation`, `ProcessType`, `Price`, `TimeOfReservation`.
  - `NFC_recipes.c`:
    - `GetWinningCell(...)`:
      - Calls `Inquire(...)` for each candidate cell and process type; chooses “winning” reservation based on `Price`.
    - `AskForValidOffer(...)`:
      - Iterates over pending recipe steps, for each step with `ProcessCellID` or `TransportCellID` set.
      - Verifies each reservation (`ProcessCellReservationID` / `TransportCellReservationID`) with `GetInquireIsValid`.
      - Tracks `aLastGoodReserved` index as last consistent reservation.
    - `ReserveAllOfferedReservation(...)`:
      - For all steps with `ProcessCellID` or `TransportCellID` set:
        - Calls `Reserve(...)` to confirm the reservation with each cell.
        - When writing process/transport times, uses `NFC_Handler_GetRecipeStep` / `NFC_Handler_WriteStep` and updates `TimeOfProcess` / `TimeOfTransport`.
    - `DoReservation(...)`:
      - For the current step, either process or transport:
        - Retrieves the correct `CellInfo` by matching `ProcessCellID` or `TransportCellID` to `aCellInfo[j].IDofCell`.
        - Calls `DoReservation_klient(...)` to trigger remote execution (`DoProcess`).
    - `IsDoneReservation(...)`:
      - Similar selection of cell and reservation.
      - Calls `IsFinished(...)` to query completion status.
  - `app.c` states:
    - `State_Rezervace`:
      - Runs `AskForValidOffer` and `ReserveAllOfferedReservation`, then moves to `State_Transport`.
    - `State_Transport`:
      - Ensures both process and transport reservations are valid (`AskForValidReservation` TODO in C, but called).
      - Writes `IsTransport`, decrements `ActualBudget` by `PriceForTransport`, and then calls `DoReservation(..., process=false)` (transport execution) before waiting for removal.
    - `State_Vyroba_OznameniOProvedeni` / `State_Vyroba_SpravneProvedeni`:
      - `DoReservation(..., process=true)` and `IsDoneReservation` for process cells, update step and recipe info accordingly.

- **PLC-side reservation, queueing, and execution**
  - `ReserveAction.scl`:
    - Interprets the `InputMessage` fields as `id`, `priority`, `material`, `initialPosition/operation`, and `finalPosition/operationParameter`.
    - Checks support (same ranges as `GetSupported`) and rejects unsupported items.
    - Inserts a `typeProductInQueue` into the appropriate priority subqueue (`PQueue_push`).
  - `GetSupported.scl`:
    - Computes `support` and queue position for a hypothetical item without inserting it.
  - `DeviceSlow.scl`:
    - Performs a simulated device run:
      - Uses support ranges to compute `waitTime`.
      - Uses `TON_TIME` timer to change `actionStatus` to `inProgress` and then back to `idle` when done.
      - On completion, fills `OutputPositionSr` with the `id` of the processed item and clears `Valid`.
  - `CheckAction.scl`:
    - Scans `priorityQueue` for the next item whose `id` is present in `reportedProductsQueue`, to decide which queued product to run.
  - `FreeFromPosition.scl`:
    - Clears `OutputPositionSr` when a cell (or external entity) acknowledges removal of the product.

**Answer to Q3 – existing “execute locally vs request transport” logic:**
- Yes, with quite a bit of detail in the legacy path:
  - **Local vs remote process**:
    - `GetWinningCell` chooses the best process cell; if `ProcessCellID != MyCellInfo.IDofCell`, the reader knows that the process should not be executed locally.
  - **Need for transport**:
    - `NeedForTransport` is set to true when the winning process cell differs from the current cell.
    - `State_Poptavka_Transporty` chooses a transport cell and writes `TransportCellID` + reservation.
  - **Runtime decision when tag appears**:
    - In `State_Vyroba_Objeveni`, if the card appears at a cell where `ProcessCellID != MyCellInfo.IDofCell`, the firmware inserts a `Transport` step to move the product to the correct process cell.
    - If the card appears at the correct process cell (or with a transport reservation), the state machine routes to process execution or transport execution respectively.
  - The newer AAS path does **not** currently re-express this local-vs-remote decision around `MyCellInfo.IDofCell`; it assumes the PLC service provider handles the actual operation and uses queueing status to determine completion, but this can be layered on top of the existing process/transport fields if desired.

## 6. Existing AAS / OPC UA support for this architecture

- **Firmware-side AAS support**
  - `OPC_klient.c` implements:
    - `OPC_WriteCurrentId(endpoint, uidHex)`: writes to `CurrentId` (ns=4;i=6101).
    - `OPC_ReportProduct` / `OPC_ReportProductEx(endpoint, sr_id_decimal, outBuf, outSize)`: call `ReportProduct` (ns=4;i=7004) with `InputMessage = sr_id`.
    - `OPC_GetSupported(endpoint, msg5, outBuf, outSize)`: generic call to `GetSupported` (ns=4;i=7003).
    - `OPC_ReserveAction(endpoint, msg5, outBuf, outSize)`: calls `ReserveAction` (ns=4;i=7005).
    - `OPC_FreeFromPosition(endpoint, sr_id, outBuf, outSize)`: calls `FreeFromPosition` (ns=4;i=7000).
    - `OPC_GetStatus(endpoint, sr_id, outBuf, outSize)` and `OPC_AAS_WaitCompletionPoll(endpoint, sr_id, timeout_ms, poll_interval_ms)`.
  - `app.c` AAS flow:
    - For each tag:
      - Build `sr_id` from UID and write `CurrentId`.
      - Call `ReportProductEx`; treat `"Success"` or `"Error:8501"` as idempotent success.
      - Build `msg5 = sr_id/0/TypeOfProcess/ParameterProcess1/ParameterProcess2`.
      - Optionally call `GetSupported`; if output starts with `"Error:"`, mark recipe as done and stop.
      - Call `ReserveAction`; if error, mark done and stop.
      - Wait for completion via `OPC_AAS_WaitCompletionPoll` (GetStatus-based handshake).
      - On success, set `IsStepDone`, advance `ActualRecipeStep`, and write back to tag.

- **PLC-side AAS support**
  - `PLCSeviceProvider.xml` + FBs:
    - The NodeSet and SCL blocks implement a classical AAS-style contract:
      - `ReportProduct`: register product ID in a reported-products queue.
      - `GetSupported`: compute support and queue position for a hypothetical operation.
      - `ReserveAction`: actually enqueue the operation for execution (with priority).
      - `GetStatus`: return human-readable status (e.g. `running`, `finished`, `position:N`, `inProgress`, or `error:XXXX`).
      - `FreeFromPosition` and `FreeFromQueue`: cleanup once a product leaves the device.
    - The `PassiveAAS_DB` holds configuration and status for the service provider; `DeviceSlow` and `CheckAction` implement the service-provider logic on top of priority queues.

**Answer to Q4 & Q5 – transport-request infrastructure and AAS/OPC routing actions:**
- **Transport-request infrastructure:**
  - On the firmware side, yes: `Reservation`, `GetWinningCell`, `AskForValidOffer`, `ReserveAllOfferedReservation`, `DoReservation`, `IsDoneReservation`, and `OcupancyCell` together implement a complete transport/process reservation and occupancy protocol across multiple cells identified by `CellInfo`.
  - On the PLC side, yes: `ReserveAction`, `ReportProduct`, the priority queue functions, `DeviceSlow`, `CheckAction`, and status variables provide the infrastructure to queue and execute operations per service provider.
- **AAS/OPC UA methods for routing:**
  - The **AAS method set is in place** for a single PLC service provider. It already separates:
    - Product identity (`ReportProduct`, `CurrentId`).
    - Capability / support (`GetSupported`).
    - Reservation/queueing (`ReserveAction`).
    - Execution and completion (`DeviceSlow`, `GetStatus`, `ActionStatus`, `OutputPositionSr`).
  - What is **not yet explicit** is a multi-cell AAS model where each physical cell has its own AAS instance and where routing between them is modelled at the AAS level (as opposed to being handled by the reader’s legacy LDS + `CellInfo` logic).

## 7. What is already prepared

Summarizing what exists and can be reused directly:

- **Explicit cell identity and per-device role**
  - `MyCellInfo` on the reader: numeric cell ID, OPC endpoint, and list of supported `ProcessTypes`.
  - Per-reader ID persisted in NVS (`ID_Interpretter`), allowing multiple readers to behave as different cells.
- **Per-step ownership fields on the tag**
  - `ProcessCellID` / `TransportCellID` and their reservation IDs are already present and used.
  - `NeedForTransport` flags steps that require a transfer after process.
  - The same tag format supports arbitrary future cells by ID (no schema change needed).
- **Legacy multi-cell routing logic**
  - `GetCellInfoFromLDS` → `GetWinningCell` provides a generic mechanism to discover candidate cells (by process type) and select the “best” one based on OPC `Inquire` responses.
  - `State_Poptavka_Vyroba` / `State_Poptavka_Transporty` / `State_Rezervace` / `State_Transport` implement a complete request–reserve–execute–confirm cycle for both process and transport.
  - `State_Vyroba_Objeveni` contains explicit logic for “we are at the wrong cell” and automatically injects a transport step to route the glass to the correct process cell.
- **Transport and queue infrastructure**
  - On firmware: C-side reservation and transport functions with clear separation between process and transport cells.
  - On PLC: priority queues (`typePQueueData`, `PQueue_*`), simple device execution model (`DeviceSlow`), and AAS methods with queue and status integration.
- **AAS/OPC abstraction**
  - Reader-side wrappers around `ReportProduct`, `GetSupported`, `ReserveAction`, `GetStatus`, and `FreeFromPosition` with validated message formats.
  - PLC-side AAS FBs that already work with `sr_id` and a 5-field operation description, including support computation and error reporting.
- **Configurability hooks**
  - Cell configuration via `CellInfo` (process-type capabilities per cell).
  - Reader identity via `ID_Interpretter` in NVS and `MyCellInfo.ProcessTypes`.
  - PLC `PassiveAAS_DB` static fields for ID, name, input/output position, and buffer, which can be used to distinguish different service providers if more are added.

## 8. What is missing

Key gaps relative to the desired “per-cell routing / SkladSklenicek first” architecture:

1. **Unified routing strategy between AAS path and legacy cell-routing path**
   - The AAS path currently:
     - Talks to a **single** PLC `PLCServiceProvider` endpoint via `MyCellInfo.IPAdress`.
     - Does not consult or update `ProcessCellID`, `TransportCellID`, or `NeedForTransport`.
     - Treats each step as an abstract operation executed at this one service provider, without modelling which **physical cell** is actually responsible.
   - The legacy path:
     - Has detailed multi-cell selection and transport logic, but does not use the PLC AAS methods (other than the older Inquire/Rezervation/DoProcess/IsFinished protocol).
   - There is no single, consolidated “routing engine” that:
     - Uses `MyCellInfo` to represent the current cell.
     - Uses recipe step ownership (`ProcessCellID`, `TransportCellID`) consistently for both legacy and AAS flows.
     - Decides in one place “execute here vs request transport via AAS/OPC”.

2. **Per-cell AAS models on the PLC**
   - The checked-in NodeSet and SCL code describe one `PLCServiceProvider` with one set of queues and one `PassiveAAS_DB`.
   - To fully realize “routing between SkladSklenicek and other cells” at the AAS level, you would likely want:
     - Multiple AAS instances (one per cell) or at least:
       - A richer “position/cell” model in `PassiveAAS_DB` that captures which cell performs which operation.
     - Clear mapping from `TypeOfProcess` (or higher-level process types) to “which AAS instance / PLC method set handles this”.
   - Today, this mapping is still encoded in:
     - Firmware’s `GetCellInfoFromLDS` and `CellInfo.ProcessTypes`.
     - PLC support ranges (material/supportA/supportB) in GetSupported/ReserveAction/DeviceSlow.

3. **Explicit mapping from recipe step to named cell roles (e.g. "SkladSklenicek")**
   - On the tag and in the firmware:
     - Steps carry numeric `ProcessCellID` / `TransportCellID`, but there is no symbolic name mapping (e.g. SkladSklenicek vs Shaker vs Storage) persisted with the recipe.
     - Names exist only in comments or external docs; in code, they are represented as `ProcessTypes` or numeric cell IDs.
   - On the PLC:
     - `PassiveAAS_DB"."Name` can hold a cell name, but there is only one instance in the provided NodeSet.

4. **Stabilized configuration for multiple real cells**
   - `GetCellInfoFromLDS` currently uses example OPC endpoints on a laptop and a fixed set of 3 cells.
   - `MyCellInfo.IPAdress` is a single endpoint string, not a dynamic selection based on `ProcessCellID` or `TransportCellID`.
   - For a multi-cell deployment:
     - This configuration must be externalized (e.g. compiled-time config or runtime NVS) and aligned with physical PLC(s) and AAS instances.
     - `MyCellInfo.ProcessTypes` must be set per device to match its actual role(s).

5. **AAS-level concept of supported operations per cell**
   - While GetSupported/ReserveAction compute “support” based on numeric ranges and queue state, they do not yet:
     - Distinguish multiple cells or AAS submodels per process type.
     - Encode a mapping from `ProcessTypes` to these AAS instances; instead, the mapping is currently numeric and implicit (material/supportA/supportB ranges).

6. **Process-type mapping for all planned cells**
   - Existing `ProcessTypes` cover many planned roles (storage, shaker, sodamaker, etc.), but ice crusher and any additional cells require:
     - Either new enum values, or conventions in `ParameterProcess1/2` – which are not yet defined in code for those cells.
   - For SkladSklenicek specifically, `ToStorageGlass` (0) exists, but a stable mapping from this type to that cell’s identity in both firmware and PLC must be agreed.

## 9. Assessment

- **Is there already an explicit notion of “this unit belongs to cell X”?**
  - **Yes.**
  - `MyCellInfo.IDofCell` loaded from NVS, plus `MyCellInfo.ProcessTypes` and `MyCellInfo.IPAdress`, give each reader a concrete cell identity and role.
- **Is there a mapping from process type to responsible cell?**
  - **Partially.**
  - Legacy: `GetCellInfoFromLDS` + `GetWinningCell` give a hardcoded mapping from `TypeOfProcess` to cell candidates and then to a specific cell (ID + endpoint).
  - AAS: mapping is implicit in support ranges and not yet explicit or multi-cell aware.
- **Is there decision logic “execute locally vs request transport”?**
  - **Yes (in legacy flow).**
  - The state machine uses `ProcessCellID`, `TransportCellID`, `NeedForTransport`, and `MyCellInfo.IDofCell` to decide whether to execute locally, insert a transport step, or execute a reserved transport.
  - The AAS path currently operates at the level of “step vs PLC service provider” and does not yet exploit this per-cell decision logic.
- **Is there transport-request infrastructure that can be reused?**
  - **Yes.**
  - Firmware and PLC both have mature queue/reservation/transport abstractions that can be aligned.
- **Is the current architecture suitable for implementing SkladSklenicek first and other cells later?**
  - **Yes, but with some integration work.**
  - The tag format and firmware state machine are already generic enough to assign steps to arbitrary cells (by numeric ID) and to decide local vs transported execution.
  - The AAS path will need to be extended or integrated with the existing multi-cell logic to avoid duplicating concepts and to allow per-cell AAS endpoints or submodels.

**Overall assessment:**  
The routing / cell-identity architecture is **partially prepared**:
- The **data model** (tag structs, `CellInfo`, `Reservation`, PLC queues) and **legacy code paths** already implement most of the conceptual architecture you need.
- The **newer AAS-based flow** has the right building blocks at the method level but does not yet expose multi-cell routing or per-cell decision-making; it effectively treats the PLC service provider as a single “black box” cell.

## 10. Recommended minimum next step

To move towards the desired architecture (starting with SkladSklenicek) with minimal change and maximal reuse:

1. **Stabilize per-reader cell identity and role**
   - Decide and configure, per hardware reader:
     - `ID_Interpretter` (numeric cell ID for each physical cell).
     - `MyCellInfo.ProcessTypes` to reflect the process types that this cell is allowed to execute (e.g. `{ToStorageGlass}` for SkladSklenicek).
   - Store these in NVS or a small configuration layer so they can be set per device without code changes.

2. **Explicitly adopt the existing tag-side ownership fields as the primary routing contracts**
   - Treat `ProcessCellID` and `TransportCellID` in `TRecipeStep` as the **authoritative* cell assignment for each step.
   - When the AAS flow is active:
     - Ensure that after `ReserveAction` succeeds, the step’s `ProcessCellID` (and `TransportCellID` if needed) are set consistently (using either legacy `GetWinningCell` or new AAS feedback).
     - In `State_Mimo_Polozena` and `State_Vyroba_Objeveni`, always decide “execute here vs request/insert transport” by comparing these IDs with `MyCellInfo.IDofCell`, regardless of whether the operation was reserved via the legacy or AAS path.

3. **Bridge AAS flow to existing multi-cell mechanics instead of duplicating logic**
   - Short-term minimal integration:
     - Keep using AAS methods for reservation/execution but:
       - After a successful AAS reservation, set `ProcessCellID` to `MyCellInfo.IDofCell` (for “local” cells) or to a configured target cell ID if the AAS method represents a remote cell.
       - Use `NeedForTransport` and `TransportCellID` to trigger transport steps when the tag arrives at a cell different from the process cell.
   - Longer-term:
     - Introduce per-cell AAS endpoints (or submodels) and extend `MyCellInfo`/`CellInfo` so that `ProcessCellID` maps to a specific AAS instance or OPC endpoint.

4. **For SkladSklenicek specifically**
   - Define a cell ID (e.g. `ID=1`) and map it to the process type(s) that SkladSklenicek should handle:
     - At minimum, `ProcessTypes::ToStorageGlass` (0) for “glass storage”.
   - Configure the reader deployed at SkladSklenicek:
     - `ID_Interpretter = 1`, `MyCellInfo.ProcessTypes = {ToStorageGlass}`.
   - Ensure recipes steps that belong to SkladSklenicek either:
     - Have `ProcessCellID = 1` set at authoring time, or
     - Have `TypeOfProcess = ToStorageGlass` and rely on existing mapping (`GetCellInfoFromLDS` / AAS) plus the state machine to set `ProcessCellID` appropriately.

5. **Document and align PLC-side AAS configuration with cell IDs**
   - Use `PassiveAAS_DB"."ID` / `"Name"` to represent SkladSklenicek and other cells explicitly (even if using a single PLC).
   - For future multiple AAS instances:
     - Mirror the `CellInfo` list (IDs, names, endpoints, supported `ProcessTypes`) on the PLC side so that AAS methods and the reader’s LDS mapping talk about the same set of cell IDs and roles.

With these steps, you can implement “this reader/PLC belongs to SkladSklenicek; execute local steps here and request/insert transports for others” **without changing the NFC tag layout**, while reusing the substantial existing code for cell identity, routing, transport, and AAS/OPC interactions.

