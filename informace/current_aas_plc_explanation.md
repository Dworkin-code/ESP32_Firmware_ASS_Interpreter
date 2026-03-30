# 1) AAS blocks overview

- **Main OPC UA AAS method FBs (Active AAS side, implemented as SCL FBs)**
  - `PLC_code/program_block/AAS/OPC UA Methods/ReportProduct.scl` – FB `ReportProduct`
  - `PLC_code/program_block/AAS/OPC UA Methods/GetSupported.scl` – FB `GetSupported`
  - `PLC_code/program_block/AAS/OPC UA Methods/ReserveAction.scl` – FB `ReserveAction`
  - `PLC_code/program_block/AAS/OPC UA Methods/GetStatus.scl` – FB `GetStatus`
  - `PLC_code/program_block/AAS/OPC UA Methods/FreeFromQueue.scl` – FB `FreeFromQueue`
  - `PLC_code/program_block/AAS/OPC UA Methods/FreeFromPosition.scl` – FB `FreeFromPosition`

- **Low-level AAS helper FC/FBs**
  - `PLC_code/program_block/AAS/low level functions/GetMessage.scl` – FC `GetMessage` (parses slash-separated input message into array)
  - `PLC_code/program_block/AAS/low level functions/GetPQueueStatus.scl` – FC `GetPQueueStatus` (builds string representation of queue fill per priority)
  - `PLC_code/program_block/AAS/low level functions/RemoveFinishedProduct.scl` – FB `RemoveFinishedProduct` (removes finished products from both queues, updates history)
  - `PLC_code/program_block/AAS/low level functions/AddItemIdToBuffer.scl` – FC `AddItemIdToBuffer` (maintains history string of last finished products)

- **Queue/priority-queue infrastructure**
  - `PLC_code/program_block/Queue functions/Queue_find.scl` – FC `Queue_find` (find item by ID in simple queue)
  - `PLC_code/program_block/Queue functions/Queue_remove.scl` – FC `Queue_remove` (remove item at position from simple queue)
  - `PLC_code/program_block/Priority Queue functions/PQueue_push.scl` – FC `PQueue_push` (push into priority queue, updates counts, buffer/full/empty flags)
  - `PLC_code/program_block/Priority Queue functions/PQueue_find.scl` – FC `PQueue_find` (search ID across all priority sub-queues)
  - `PLC_code/program_block/Priority Queue functions/PQueue_get.scl` – FC `PQueue_get` (read item at global queue position using `Queue_get`)

- **Device / action execution simulation (timeout-based “machine”)**
  - `PLC_code/program_block/Device Data/CheckAction.scl` – FB `CheckAction`
  - `PLC_code/program_block/Device Data/DeviceSlow.scl` – FB `DeviceSlow`

- **DBs and OPC UA mapping (AAS-side data structures)**
  - Passive AAS DB (data-only, no SCL):
    - `PLC_code/program_block/AAS/Passive AAS_DB.pdf` – DB `PassiveAAS_DB [DB2]` structure
  - Active AAS DB (method orchestration, UA glue):
    - `PLC_code/program_block/AAS/Active AAS_DB.pdf` – DB `Active AAS_DB [DB1]` structure
  - OPC UA model and method-variable mappings:
    - `PLC_code/PLCSeviceProvider.xml`
      - Maps OPC UA methods (`ReportProduct`, `GetSupported`, `ReserveAction`, `GetStatus`, `FreeFromQueue`, `FreeFromPosition`) to instances inside `Active AAS_DB`
      - Maps OPC UA variables like `ActionStatus`, `CurrentId`, `CurrentPosition`, `ItemCountP`, `ItemCountRP`, `OutputPositionSr`, `QueueHistoryBuffer`, `QueueStatus`, etc., to fields in `PassiveAAS_DB`


# 2) Current method-by-method behavior

## 2.1 ReportProduct

- **File path**
  - `PLC_code/program_block/AAS/OPC UA Methods/ReportProduct.scl`

- **Purpose**
  - OPC UA method that **registers a product** as “reported” to this Service Provider (SP) by inserting it into the **reported products queue** `RPQueue`.ReportedProductsQueue.
  - It does **not** start any action; it only records that a product with a given ID exists in the SP’s sphere of influence.

- **Input format**
  - OPC UA-side: one `WString` argument named `InputMessage` (see `PLCSeviceProvider.xml` `InputArguments` section for `ReportProduct`).
  - Internally:
    - `UAMethod_InParameters.InputMessage : WString` (lines 9–11 in `ReportProduct.scl`).
    - Converted to `String` and parsed by `GetMessage`:

      ```12:45:PLC_code/program_block/AAS/OPC UA Methods/ReportProduct.scl
      #InputArray := "GetMessage"(WSTRING_TO_STRING(#UAMethod_InParameters.InputMessage));
      #tempItem.id := STRING_TO_DINT(#InputArray.element[0]);
      ```

    - The code only uses `element[0]` as the product ID for this method.

- **Output format**
  - One `WString` output `UAMethod_OutParameters.OutputMessage`.
  - Values:
    - `WSTRING#'Success'` on success (line 83).
    - On error, a formatted error string using status/substatus encoded to hex via `HTA` (lines 68–81).

- **Current behavior**
  - Uses standard UA method pre/post wrappers:

    ```31:38:PLC_code/program_block/AAS/OPC UA Methods/ReportProduct.scl
    #OPC_UA_ServerMethodPre_Instance(..., UAMethod_Called => #tempUAM_MethodHandling.UAMethod_Called, ..., UAMethod_InParameters := #UAMethod_InParameters);
    ...
    IF #tempUAM_MethodHandling.UAMethod_Called THEN
      ...
    END_IF;
    ...
    #OPC_UA_ServerMethodPost_Instance(..., UAMethod_OutParameters := #UAMethod_OutParameters);
    ```

  - When called:
    1. Parses `InputMessage` into `InputArray`.
    2. Extracts `id` from `element[0]`.
    3. If `id = 0` → error: `PQErrInvalidItemId`.
    4. Else calls `Queue_find` to check whether the product is already in the **reported products queue**:

       ```46:51:PLC_code/program_block/AAS/OPC UA Methods/ReportProduct.scl
       "Queue_find"(id := #tempItem.id,
                    error => #tempError,
                    status => #tempStatus,
                    found => #tempFound,
                    productPosition => #tempPosition,
                    QueueData:="RPQueue".ReportedProductsQueue);
       ```

    5. If found → error: `RQErrIdAlreadyKnown` (id already reported).
    6. If not found → pushes the item into `RPQueue`.ReportedProductsQueue using `Queue_push` (lines 57–61).
    7. Builds an error or success message in `OutputMessage`.

- **Important variables / queues / DBs**
  - `RPQueue`.ReportedProductsQueue – **queue of all reported product IDs**.
  - `typeProductInQueue` – structure containing at least `.id` (and usually material/positions, but `ReportProduct` only fills ID).
  - Error/status constants: `"RQErrIdAlreadyKnown"`, `"PQErrInvalidItemId"`, `"PQNoError"`.


## 2.2 GetSupported

- **File path**
  - `PLC_code/program_block/AAS/OPC UA Methods/GetSupported.scl`

- **Purpose**
  - Given a product **material** and two “supports” (A and B), and a **priority index**, **computes a support score** from 0 to 100 and a **queue position estimate**.
  - Used by the AAS to tell the caller whether this SP **can support** the requested action and give a position in the queue.

- **Input format**
  - `UAMethod_InParameters.InputMessage : WString` – slash-separated string.
  - The method parses:

    ```38:41:PLC_code/program_block/AAS/OPC UA Methods/GetSupported.scl
    #InputArray := "GetMessage"(WSTRING_TO_STRING(#UAMethod_InParameters.InputMessage));
    #tmpSupportValue := 100;
    ...
    CASE STRING_TO_INT(#InputArray.element[2]) OF   // material
    ...
    CASE STRING_TO_INT(#InputArray.element[3]) OF   // support A
    ...
    CASE STRING_TO_INT(#InputArray.element[4]) OF   // support B
    ```

  - Implicit structure of `InputArray`:
    - `element[0]` – ID (not used here).
    - `element[1]` – **priority index** (used below for queue position).
    - `element[2]` – material code.
    - `element[3]` – support A.
    - `element[4]` – support B.

- **Output format**
  - `OutputMessage` is a `WString` composed as:

    ```78:80:PLC_code/program_block/AAS/OPC UA Methods/GetSupported.scl
    #UAMethod_OutParameters."OutputMessage" := CONCAT_WSTRING(IN1 := WSTRING#'support:', IN2 := DELETE(IN := DINT_TO_WSTRING(#tmpSupportValue), L := 1, P := 1));
    #UAMethod_OutParameters."OutputMessage" := CONCAT_WSTRING(IN1 := #UAMethod_OutParameters."OutputMessage", IN2 := WSTRING#'_position:');
    #UAMethod_OutParameters."OutputMessage" := CONCAT_WSTRING(IN1 := #UAMethod_OutParameters."OutputMessage", IN2 := DELETE(IN := DINT_TO_WSTRING(#tmpQueuePosition), L := 1, P := 1));
    ```

  - `supportValue` 0–100, `position` is integer queue position computed from counts in `"PQueue".PQueue`.

- **Current behavior**
  - Starts with support 100.
  - For each of material/supportA/supportB, it uses **range-encoded constants** like `"SupportMaterialLow100".."SupportMaterialHigh100"`, `"SupportALow60".."SupportAHigh60"`, etc.
    - Good range → keep support.
    - “60 range” → subtract 40.
    - Else → set support to 0.
  - Ensures support is not negative (lines 68–70).
  - Computes **queue position** by summing `PQueue.PQueue.subElementCount` up to the requested priority index:

    ```72:75:PLC_code/program_block/AAS/OPC UA Methods/GetSupported.scl
    #tmpQueuePosition := 0;
    FOR #i := 0 TO STRING_TO_INT(#InputArray.element[1]) DO
        #tmpQueuePosition := #tmpQueuePosition + "PQueue".PQueue.subElementCount[#i];
    END_FOR;
    ```

- **Important variables / queues / DBs**
  - `"PQueue".PQueue.subElementCount[]` – number of elements per priority level.
  - Range constants for supports/materials: `"SupportMaterialLow100".."SupportMaterialHigh100"`, etc.
  - No direct access here to `PassiveAAS_DB` or `ActionStatus`; this method is purely **capability + queue load calculation**.


## 2.3 ReserveAction

- **File path**
  - `PLC_code/program_block/AAS/OPC UA Methods/ReserveAction.scl`

- **Purpose**
  - OPC UA method that **reserves an action** for a given product by **enqueuing it into the priority queue** `"PQueue".PQueue` with a given priority.
  - It ensures the product is supported and not already in the queue.
  - It does **not directly execute** the action; it **only pushes to the priority queue**.

- **Input format**
  - `UAMethod_InParameters.InputMessage : WString`, parsed by `GetMessage`:

    ```44:49:PLC_code/program_block/AAS/OPC UA Methods/ReserveAction.scl
    #InputArray := "GetMessage"(WSTRING_TO_STRING(#UAMethod_InParameters.InputMessage));
    #tempItem.id := STRING_TO_DINT(#InputArray.element[0]);
    #tempItem.material := STRING_TO_INT(#InputArray.element[2]);
    #tempItem."initialPosition/operation" := STRING_TO_INT(#InputArray.element[3]);
    #tempItem."finalPosition/operationParameter" := STRING_TO_INT(#InputArray.element[4]);
    ```

  - Structure:
    - `element[0]` – product ID.
    - `element[1]` – **priority**.
    - `element[2]` – material.
    - `element[3]` – initial position / operation.
    - `element[4]` – final position / operation parameter.

- **Output format**
  - `OutputMessage : WString`
    - `WSTRING#'Success'` on success.
    - Error string – similar logic to `ReportProduct` based on error/status codes and `HTA` conversion (lines 107–120).

- **Current behavior**
  - Standard UA pre/post handling (lines 33–40; 129–137).
  - Main logic:
    1. Parse inputs, fill a `typeProductInQueue` `tempItem`.
    2. If `id = 0` → error `PQErrInvalidItemId`.
    3. Calls `PQueue_find` to check if item already exists in **priority queue** `"PQueue".PQueue`:

       ```51:56:PLC_code/program_block/AAS/OPC UA Methods/ReserveAction.scl
       "PQueue_find"(id:=#tempItem.id,
                     error=>#tempError,
                     status=>#tempStatus,
                     found=>#tempFound,
                     productPosition=>#tempPosition,
                     PQueueData:="PQueue".PQueue);
       ```

    4. If found → error: `PQErrItemAlreadyInQueue`.
    5. If not found → recomputes a **support value** similarly to `GetSupported`:

       ```62:88:PLC_code/program_block/AAS/OPC UA Methods/ReserveAction.scl
       #tmpSupportValue := 100;
       CASE STRING_TO_INT(#InputArray.element[2]) OF   // check material
         "SupportMaterialLow100".."SupportMaterialHigh100": ...
         "SupportMaterialLow60".."SupportMaterialHigh60": ...
         ELSE #tmpSupportValue := 0;
       END_CASE;
       ...
       IF #tmpSupportValue = 0 THEN
           #tempError := TRUE;
           #tempStatus := "ErrItemNotSupported";
       ELSE
           "PQueue_push"(item := #tempItem,
                         priority := STRING_TO_UINT(#InputArray.element[1]),
                         error => #tempError,
                         status => #tempStatus,
                         subFunctionStatus => #tempSubStatus,
                         PQueueData := "PQueue".PQueue);
       END_IF;
       ```

    6. On success, pushes item into `"PQueue".PQueue` with given priority.
    7. Returns `Success` or an error string via `OutputMessage`.

- **Important variables / queues / DBs**
  - `"PQueue".PQueue` – main **priority queue** where actions are reserved.
  - Uses `PQueue_find`, `PQueue_push`.
  - Error/status enums: `"PQErrItemAlreadyInQueue"`, `"ErrItemNotSupported"`, `"PQErrInvalidItemId"`, etc.
  - **No direct use** of `PassiveAAS_DB`, `ActionStatus`, or any machine I/O; it only enqueues.

- **Reserve vs execute**
  - From the code, **ReserveAction only reserves**:
    - It writes to the PQueue.
    - It does not depend on `DeviceSlow`, timers, or `ActionStatus`.
    - Execution is delegated to separate logic (in `PassiveAAS_DB` + `DeviceSlow` and `CheckAction`) that consumes the queue.


## 2.4 FreeFromPosition

- **File path**
  - `PLC_code/program_block/AAS/OPC UA Methods/FreeFromPosition.scl`

- **Purpose**
  - OPC UA method used **after the physical device / operator removes a finished product from the output position**.
  - It checks that the provided position matches `PassiveAAS_DB`.OutputPositionSr, and if so, resets it to `'0'`.
  - This is effectively an **acknowledgment / clearing** of the “product at output position” flag.

- **Input format**
  - `InputMessage : WString`, parsed with `GetMessage` (line 41).
  - Used only as `InputArray.element[0]` representing the **output position / SR ID**.

- **Output format**
  - `OutputMessage : WString`
    - `WSTRING#'Success'` if the given SR ID matches `PassiveAAS_DB`.OutputPositionSr.
    - Otherwise, an error string derived from `PErrItemNotInPosition` (lines 46–48).

- **Current behavior**

  ```41:49:PLC_code/program_block/AAS/OPC UA Methods/FreeFromPosition.scl
  #InputArray := "GetMessage"(WSTRING_TO_STRING(#UAMethod_InParameters.InputMessage));
  IF "PassiveAAS_DB".OutputPositionSr = STRING_TO_WSTRING(#InputArray.element[0]) THEN
      "PassiveAAS_DB".OutputPositionSr := WSTRING#'0';
      #UAMethod_OutParameters."OutputMessage" := WSTRING#'Success';
  ELSE
      #tempErrValue := HTA(IN := WORD_TO_DINT("PErrItemNotInPosition"), N := 4, OUT => #UAMethod_OutParameters."OutputMessage");
      #UAMethod_OutParameters."OutputMessage" := DELETE(IN := #UAMethod_OutParameters."OutputMessage", L := 4, P := 1);
      #UAMethod_OutParameters."OutputMessage" := CONCAT_WSTRING(IN1 := WSTRING#'Error:', IN2 := #UAMethod_OutParameters."OutputMessage");
  END_IF;
  ```

  - No queue / PQueue manipulation here; it purely toggles the **output position flag** in `PassiveAAS_DB`.

- **Important variables / queues / DBs**
  - `PassiveAAS_DB`.OutputPositionSr – mapped to OPC UA variable `OutputPositionSr` in `PLCSeviceProvider.xml`.
  - Error constant: `PErrItemNotInPosition`.


## 2.5 Other relevant blocks

### 2.5.1 FreeFromQueue

- **File path**
  - `PLC_code/program_block/AAS/OPC UA Methods/FreeFromQueue.scl`

- **Purpose**
  - OPC UA method to **remove a product from the priority queue** by ID, unless it is currently active.
  - Used to “unreserve” actions or cancel them before they start (or if they are pending).

- **Behavior summary**

  ```42:59:PLC_code/program_block/AAS/OPC UA Methods/FreeFromQueue.scl
  #InputArray := "GetMessage"(WSTRING_TO_STRING(#UAMethod_InParameters.InputMessage));
  #tempItemId := STRING_TO_DINT(#InputArray.element[0]);
  ...
  "PQueue_find"(id := #tempItemId,
                error => #tempError,
                status => #tempStatus,
                found => #tempFound,
                productPosition => #tempPosition,
                PQueueData := "PQueue".PQueue);

  IF #tempFound THEN
      IF #tempPosition <> "PQueue".PQueue.activeItemPosition THEN
          "PQueue_removeByPosition"(position := #tempPosition,
                                    error => #tempError,
                                    status => #tempStatus,
                                    subFunctionStatus => #tempSubStatus,
                                    PQueueData := "PQueue".PQueue);
      ELSE
          #tempError := TRUE;
          #tempStatus := "PQErrItemAlreadyInProgress";
          #tempSubStatus := "PQNoError";
      END_IF;
  ELSE
      #tempError := TRUE;
      #tempStatus := "PQErrItemNotFound";
      #tempSubStatus := "PQNoError";
  END_IF;
  ```

- **Key points**
  - Guards against removing the **currently active item** (position equal to `PQueue.PQueue.activeItemPosition`) – that is considered “already in progress”.
  - Therefore:
    - **Before** an action starts, `FreeFromQueue` can cancel it.
    - **After** it starts, you must wait until the executor completes and `RemoveFinishedProduct` runs.


### 2.5.2 GetStatus

- **File path**
  - `PLC_code/program_block/AAS/OPC UA Methods/GetStatus.scl`

- **Purpose**
  - OPC UA method that **reports the status of a specific product ID in the priority queue**.
  - It tells whether:
    - the item is missing, invalid ID, or
    - the item is in queue at some position, or
    - the item is the **currently active** item.

- **Input / Output**
  - Input: `InputMessage` → `InputArray.element[0]` = ID.
  - Output: `OutputMessage` one of:
    - `"Error:<hexStatus>"` if item not found / invalid ID.
    - `"position:<n>"` if found **and not active**.
    - `"inProgress"` if found **and at position = activeItemPosition**.

- **Core logic**

  ```40:69:PLC_code/program_block/AAS/OPC UA Methods/GetStatus.scl
  #InputArray := "GetMessage"(WSTRING_TO_STRING(#UAMethod_InParameters.InputMessage));
  #tempItemId := STRING_TO_DINT(#InputArray.element[0]);
  IF #tempItemId <> 0 THEN
      "PQueue_find"(id := #tempItemId,
                    error => #tempError,
                    status => #tempStatus,
                    found => #tempFound,
                    productPosition => #tempPosition,
                    PQueueData := "PQueue".PQueue);

      IF #tempFound = FALSE THEN
          #tempError := TRUE;
          #tempStatus := "PQErrItemNotFound";
      END_IF;
  ELSE
      #tempError := TRUE;
      #tempStatus := "PQErrInvalidItemId";
  END_IF;

  IF #tempError THEN
      #tempErrValue := HTA(IN := #tempStatus, N := 4, OUT => #UAMethod_OutParameters."OutputMessage");
      #UAMethod_OutParameters."OutputMessage" := DELETE(IN := #UAMethod_OutParameters."OutputMessage", L := 4, P := 1);
      #UAMethod_OutParameters."OutputMessage" := CONCAT_WSTRING(IN1 := WSTRING#'Error:', IN2 := #UAMethod_OutParameters."OutputMessage");
  ELSE
      IF #tempPosition <> "PQueue".PQueue.activeItemPosition THEN
          #UAMethod_OutParameters."OutputMessage" := CONCAT_WSTRING(IN1 := WSTRING#'position:', IN2 := DELETE(IN := DINT_TO_WSTRING(#tempPosition), L := 1, P := 1));
      ELSE
          #UAMethod_OutParameters."OutputMessage" := WSTRING#'inProgress';
      END_IF;
  END_IF;
  ```

- **Important states / structures**
  - `"PQueue".PQueue.activeItemPosition` – global index of the **currently executing item** in the PQueue.
    - Maintained somewhere in the PQueue/PassiveAAS logic (not in the SCL we see, probably in DB logic).
  - Note that **GetStatus does not read `ActionStatus` or `PassiveAAS_DB` directly**; it distinguishes only between “in queue”, “in progress”, and basic errors.


### 2.5.3 GetMessage

- **File path**
  - `PLC_code/program_block/AAS/low level functions/GetMessage.scl`

- **Purpose**
  - Utility FC to **split the slash-separated `InputMessage` string** into `typeArrayMessage.element[]`.

- **Behavior**
  - Cleans the result array, then repeatedly searches for `'/'`, uses `LEFT` and `RIGHT` to isolate tokens.
  - Up to `"WordNumberSpaces" + 1` elements.
  - Used by all OPC UA methods (`ReportProduct`, `GetSupported`, `ReserveAction`, `FreeFromQueue`, `GetStatus`, `FreeFromPosition`) to interpret their `InputMessage`.


### 2.5.4 Priority queue and simple queue functions

- **`PQueue_push`** – `PLC_code/program_block/Priority Queue functions/PQueue_push.scl`
  - Pushes item into `PQueueData.buffer[priority]`, updates `queueProductCount`, `subElementCount`, `isFullBuffer[]`, `isEmptyBuffer[]`.
  - If the buffer for a priority is full → `PQErrBufferIsFull`.

- **`PQueue_find`** – `PLC_code/program_block/Priority Queue functions/PQueue_find.scl`
  - Iterates all sub-queues 0..`QueueCountSmall`, calling `Queue_find` in each.
  - On success: `found = TRUE`, `productPosition` = 0-based global position; if not found but no error, sets `productPosition = 0`, `status = PQErrItemNotFound`.

- **`PQueue_get`** – `PLC_code/program_block/Priority Queue functions/PQueue_get.scl`
  - For a global position, identifies sub-queue (`tempTableIndex`) and local position, calls `Queue_get` on that sub-queue to obtain the item.
  - Used by `CheckAction`.

- **`Queue_find`** – `PLC_code/program_block/Queue functions/Queue_find.scl`
  - Validates indices and emptiness; if queue empty → `PQErrMissingProduct`.
  - Otherwise scans entries (ring buffer) for given ID; on success returns position and `PQNoError`, else error `PQErrMissingProduct`.

- **`Queue_remove`** – `PLC_code/program_block/Queue functions/Queue_remove.scl`
  - Validates types and indices, removes an item at a given local queue position, compacts items with `MOVE_BLK_VARIANT`, and updates ring buffer variables.
  - Used by `RemoveFinishedProduct` to clear `reportedProductsQueue`.


### 2.5.5 Device-side flow (CheckAction & DeviceSlow)

#### CheckAction

- **File**
  - `PLC_code/program_block/Device Data/CheckAction.scl`

- **Purpose**
  - Scans the **priority queue** for items that also exist in the **reported products queue**.
  - Outputs the **first “actionable” item** and its positions in both queues.

- **Inputs/Outputs**

  ```4:14:PLC_code/program_block/Device Data/CheckAction.scl
  VAR_OUTPUT
    foundItem : Bool;
    item : "typeProductInQueue";
    itemPositionRP : DInt;
    itemPositionP : DInt;
  VAR_IN_OUT
    priorityQueue : "typePQueueData";
    reportedProductsQueue : "typeArrayOfProducts";
  ```

- **Behavior**
  - For each position `i` from `0` to `priorityQueue.queueProductCount - 1`:
    - Calls `PQueue_get` to get item at global position `i`.
    - Calls `Queue_find` in `reportedProductsQueue` to see if this ID is currently reported.
    - On first match, sets `foundItem = TRUE`, retains `item`, `itemPositionRP`, `itemPositionP := i`, and exits.
  - At the end, `itemPositionP` is the index of the found item in the PQueue (line 57).


#### DeviceSlow

- **File**
  - `PLC_code/program_block/Device Data/DeviceSlow.scl`

- **Purpose**
  - **Simulates a slow device** using a **TON timer** and support-based waiting time.
  - Updates **device-running state**, **ActionStatus**, and `OutputPositionSr`.
  - This is where **timeout-based behavior** is implemented today.

- **Inputs/Outputs / InOut**

  ```4:18:PLC_code/program_block/Device Data/DeviceSlow.scl
  VAR_INPUT
    item : "typeProductInQueue";
    itemPositionP : DInt;
  VAR_OUTPUT
    deviceRunning : Bool;
  VAR_IN_OUT
    OutputPositionSr : WString;
    Valid : Bool;
    priorityQueue : "typePQueueData";
    actionStatus : WString;
  VAR
    waitTime : Int;
    IEC_Timer_0_Instance : TON_TIME;
    doAction : Bool;
    finishedAction : Bool;
  ```

- **Behavior**
  - Internal timer:

    ```32:35:PLC_code/program_block/Device Data/DeviceSlow.scl
    #IEC_Timer_0_Instance(IN := #doAction AND #OutputPositionSr = WSTRING#'0',
                          PT := INT_TO_TIME(#waitTime),
                          Q => #finishedAction);
    ```

    - It only runs if `doAction` is TRUE **and** output position is free (`OutputPositionSr = '0'`).
  - When `Valid = TRUE` (meaning PassiveAAS logic decided this item should run):
    - Initializes `waitTime` from `InitialWaitTime`.
    - Sets `deviceRunning := TRUE`.
    - If `actionStatus = 'idle'`, sets `doAction := TRUE`.
    - Recomputes `waitTime` based on material and supports (same pattern as `GetSupported`/`ReserveAction`) but using **delays** (`DelayedWaitTime`).
      - If result `waitTime = 0` → `tempFailed = TRUE`, `deviceRunning = FALSE`.
    - Updates `actionStatus`:
      - If `doAction` and `tempFailed = TRUE` → `actionStatus := 'failed'`.
      - Else if `doAction` and not failed → `actionStatus := 'inProgress'`.
  - When the timer finishes (`finishedAction = TRUE`):
    - Sets `actionStatus := 'idle'`.
    - `deviceRunning := FALSE`, `doAction := FALSE`, `Valid := FALSE`.
    - Sets `OutputPositionSr := <item.id as WString>` (without `+` sign):

      ```90:96:PLC_code/program_block/Device Data/DeviceSlow.scl
      IF #finishedAction THEN
          #actionStatus := WSTRING#'idle';
          #deviceRunning := FALSE;
          #doAction := FALSE;
          #Valid := FALSE;
          #OutputPositionSr := DELETE(IN := DINT_TO_WSTRING(#item.id), L := 1, P := 1);
      END_IF;
      ```

- **Important variables / DBs**
  - All these parameters are **InOut** fields of `PassiveAAS_DB` (see `Passive AAS_DB.pdf`):
    - `OutputPositionSr : WString` (SP output SR ID).
    - `Valid : Bool` – gating signal that says “execute now”.
    - `priorityQueue : typePQueueData`.
    - `actionStatus : WString` – mapped to OPC UA `ActionStatus`.


### 2.5.6 RemoveFinishedProduct & history

- **File**
  - `PLC_code/program_block/AAS/low level functions/RemoveFinishedProduct.scl`

- **Purpose**
  - When a product is finished, this FB:
    - Removes it from the **reportedProductsQueue**.
    - Removes it from the **priorityQueue** at given positions.
    - Appends its ID to the **history buffer** (`QueueHistoryStatus`).

- **Behavior**

  ```23:39:PLC_code/program_block/AAS/low level functions/RemoveFinishedProduct.scl
  "Queue_remove"(position:=#itemPositionRP,
                 initialItem:=#priorityQueue.emptyItem,
                 error=>#tempError,
                 status=>#tempStatus,
                 subFunctionStatus=>#tempSubStatus,
                 QueueData:=#reportedProductsQueue);

  "PQueue_removeByPosition"(position:=#itemPositionP,
                            error=>#tempError,
                            status=>#tempStatus,
                            subFunctionStatus=>#tempSubStatus,
                            PQueueData:=#priorityQueue);

  "AddItemIdToBuffer"(itemId:=#itemId,
                      historyBuffer:=#historyBuffer);
  ```

- **PassiveAAS_DB linkage**
  - In `PassiveAAS_DB [DB2]` (PDF) there is:
    - `RemoveFinishedProduct_Instance : "RemoveFinishedProduct"` with inputs: `itemPositionRP`, `itemPositionP`, `itemId`, and InOut: `priorityQueue`, `reportedProductsQueue`, `historyBuffer`.
    - `F_TRIG_Instance` triggered by `finishedAction` likely to call `RemoveFinishedProduct_Instance` once per rising/falling edge (single-shot).

- **History / status strings**
  - `QueueHistoryStatus` is mapped to UA variable `QueueHistoryBuffer` via `PLCSeviceProvider.xml`:
    - `si:VariableMapping"`PassiveAAS_DB"."QueueHistoryStatus"` (lines 609–619).


### 2.5.7 PassiveAAS_DB and Active AAS_DB

#### PassiveAAS_DB [DB2] – `Passive AAS_DB.pdf`

- **File**
  - `PLC_code/program_block/AAS/Passive AAS_DB.pdf`

- **Key fields (selection)**
  - Queues:
    - `priorityQueue : typePQueueData` – main SP priority queue (InOut).
    - `reportedProductsQueue : typeArrayOfProducts` – queue of reported products in SP sphere.
  - Action lookup / execution:
    - `CheckAction_Instance : CheckAction` – finds actionable items.
    - `DeviceSlow_Instance : DeviceSlow` – executes items using timeout-based simulation.
    - `RemoveFinishedProduct_Instance : RemoveFinishedProduct`.
  - Control / status:
    - `Valid : Bool` – “block can be executed, since item was found.”
    - `finishedAction : Bool` – “Product is finished.”
    - `waitTime : Int`, `IEC_Timer_0_Instance : TON_TIME` – device timer.
    - `deviceRunning : Bool` – from `DeviceSlow`.
    - `ActionStatus : WString` – initial `'idle'`, mapped to OPC UA `ActionStatus`.
    - `CurrentId : WString` – active ID.
    - `CurrentIdPosition : WString` – active ID position.
    - `ItemCountP`, `ItemCountRP`, `QueueStatus`, `QueueHistoryStatus`, `OutputPositionSr`, etc.
  - History:
    - `historyBuffer : WString` – last finished products, updated by `AddItemIdToBuffer`.
  - Triggering:
    - `F_TRIG_Instance` using `finishedAction` with comment “Falling trigger for finishedProduct”.


#### Active AAS_DB [DB1] – `Active AAS_DB.pdf`

- **File**
  - `PLC_code/program_block/AAS/Active AAS_DB.pdf`

- **Purpose**
  - Contains the **instances** of all OPC UA Methods as seen by UA:
    - `FreeFromPosition_Instance : FreeFromPosition`
    - `FreeFromQueue_Instance : FreeFromQueue`
    - `GetStatus_Instance : GetStatus`
    - `GetSupported_Instance : GetSupported`
    - `ReportProduct_Instance : ReportProduct`
    - `ReserveAction_Instance : ReserveAction`
  - For each, it embeds `OPC_UA_ServerMethodPre_Instance`, `OPC_UA_ServerMethodPost_Instance`, `statUAM_Status_Pre`, `statUAM_Status_Post`, `UAMethod_InParameters.InputMessage`, `UAMethod_OutParameters.OutputMessage`.
  - These fields are referenced in `PLCSeviceProvider.xml` via `<si:MethodMapping>`.


### 2.5.8 OPC UA model / mapping

- **File**
  - `PLC_code/PLCSeviceProvider.xml`

- **Key parts**
  - OPC UA methods mapped to instances in `Active AAS_DB`:

    ```471:536:PLC_code/PLCSeviceProvider.xml
    <UAMethod ... BrowseName="1:FreeFromPosition">
      <si:MethodMapping>"Active AAS_DB"."FreeFromPosition_Instance".Method</si:MethodMapping>
    </UAMethod>
    ...
    <UAMethod ... BrowseName="1:GetStatus">
      <si:MethodMapping>"Active AAS_DB"."GetStatus_Instance".Method</si:MethodMapping>
    </UAMethod>
    ...
    <UAMethod ... BrowseName="1:ReserveAction">
      <si:MethodMapping>"Active AAS_DB"."ReserveAction_Instance".Method</si:MethodMapping>
    </UAMethod>
    ```

  - OPC UA dynamic variables:

    ```537:631:PLC_code/PLCSeviceProvider.xml
    <UAVariable BrowseName="1:ActionStatus">
      <si:VariableMapping>"PassiveAAS_DB"."ActionStatus"</si:VariableMapping>
    </UAVariable>
    <UAVariable BrowseName="1:CurrentId">
      <si:VariableMapping>"PassiveAAS_DB"."CurrentId"</si:VariableMapping>
    </UAVariable>
    <UAVariable BrowseName="1:OutputPositionSr">
      <si:VariableMapping>"PassiveAAS_DB"."OutputPositionSr"</si:VariableMapping>
    </UAVariable>
    ...
    <UAVariable BrowseName="1:QueueStatus">
      <si:VariableMapping>"PassiveAAS_DB"."QueueStatus"</si:VariableMapping>
    </UAVariable>
    ```

- **Implication**
  - AAS/OPC UA client can directly **read/write these PassiveAAS_DB variables** (according to AccessLevel flags) to monitor `ActionStatus`, queue counts, etc., independent of the methods.


# 3) Current end-to-end AAS flow in PLC

- **Step 1 – ReportProduct**
  - AAS client sends `ReportProduct` with `InputMessage` containing at least product ID.
  - PLC parses ID and inserts it into `RPQueue`.ReportedProductsQueue if it is new.
  - Result: product is known to the SP as “reported” but **not yet planned/executing**.

- **Step 2 – GetSupported**
  - AAS client provides ID, desired priority, material, and support parameters.
  - PLC computes:
    - A support score (0–100).
    - An approximate queue position by checking `"PQueue".PQueue.subElementCount` up to the given priority.
  - The AAS client uses this to decide whether to reserve an action on this SP and what priority to use.

- **Step 3 – ReserveAction**
  - AAS client sends ID, priority, material, and positional parameters.
  - PLC:
    - Checks that the ID is not already in `"PQueue".PQueue`.
    - Reuses the `support` computation rules and fails if support = 0.
    - On success, pushes the item into `"PQueue".PQueue` with given priority.
  - Result: the product is now **enqueued** in the SP’s **priority queue**; still not necessarily executing.

- **Step 4 – Selection and execution (PassiveAAS_DB, CheckAction, DeviceSlow)**
  - In cyclic logic based on `PassiveAAS_DB`:
    - `CheckAction_Instance` scans `priorityQueue` and `reportedProductsQueue` to find an item that is both reported and queued.
    - When it finds such item, it sets `foundItem = TRUE`, outputs `item`, `itemPositionP`, and `itemPositionRP`.
    - `PassiveAAS_DB` logic then:
      - Sets `Valid := TRUE` for `DeviceSlow_Instance`.
      - Likely updates `CurrentId`, `CurrentIdPosition`, and maybe item counts.
    - `DeviceSlow_Instance` handles the execution:
      - Sets `ActionStatus` to `'inProgress'` (or `'failed'`) and uses a TON timer to simulate processing time.
      - Marks `OutputPositionSr` to the product ID when finished.
      - Sets `finishedAction := TRUE` when done.
    - `F_TRIG_Instance` on `finishedAction` triggers `RemoveFinishedProduct_Instance` to clean product from both queues and update history.
  - `GetStatus` OPC UA method reports:
    - `'position:n'` while the item is in queue and not active.
    - `'inProgress'` when its PQueue position equals `activeItemPosition`.

- **What the PLC considers “done” today**
  - From the **device/AAS perspective**:
    - The action for a product is “done” once `DeviceSlow`’s timer finishes:
      - `ActionStatus` goes back to `'idle'`.
      - `finishedAction` is TRUE (for one cycle).
      - `OutputPositionSr` is set to the product’s ID (finished piece at output).
      - `RemoveFinishedProduct` removes the product from both `priorityQueue` and `reportedProductsQueue`.
  - From the **GetStatus method’s perspective**:
    - A product is “no longer in PQueue” when `PQueue_find` returns not found; `GetStatus` then reports an error `'Error:...'`.
    - There is no explicit “Done” or “Completed” state in `GetStatus`; completion is **implied** by:
      - It was previously known, and later `GetStatus` returns item not found → probably finished and removed.
  - From the **output-position perspective**:
    - While the product is waiting at the output, `PassiveAAS_DB`.OutputPositionSr holds the product ID.
    - After an external agent (reader/operator) calls `FreeFromPosition` with that ID, the PLC resets `OutputPositionSr` to `'0'`.


# 4) Where timeout-based completion comes from

- **PLC-side timeout behavior**
  - `DeviceSlow` uses a **TON_TIME** timer to simulate machine runtime:

    ```32:35:PLC_code/program_block/Device Data/DeviceSlow.scl
    #IEC_Timer_0_Instance(IN := #doAction AND #OutputPositionSr = WSTRING#'0',
                          PT := INT_TO_TIME(#waitTime),
                          Q => #finishedAction);
    ```

  - `waitTime` is derived entirely from **material and support parameters** plus constants `InitialWaitTime` and `DelayedWaitTime`.
  - There are **no real machine I/O signals** in `DeviceSlow`:
    - No Ready/Busy/Done/Idle discrete signals.
    - Only internal booleans: `doAction`, `finishedAction`, `Valid`.
  - `ActionStatus` is set based on **logic and timer**, not on hardware signals:
    - `'inProgress'` while timer runs.
    - `'failed'` if configuration unsupported (waitTime = 0).
    - `'idle'` when timer finishes.

- **Reader-side timeout**
  - From OPC UA/AAS side, the PLC exposes:
    - `ActionStatus` (string) in `PassiveAAS_DB`.
    - `GetStatus` method returning `'inProgress'` or `'position:n'`.
    - But there is no explicit deterministic **Done event** or command handshake such as `Ready`, `Start`, `Busy`, `Done`, `ResetDone`, `Idle`.
  - Typical client behavior today (based on observable interfaces):
    - Call `ReserveAction`.
    - Then periodically call `GetStatus` or read `ActionStatus`/`OutputPositionSr` while waiting.
    - Because there is only a **timer-based simulation**, the reader often has to **wait for a fixed timeout** or poll until some condition is observed (e.g. `OutputPositionSr` set or `GetStatus` no longer finds the item).
  - **Missing PLC-side signals that force timeout usage**:
    - There is:
      - No `SetReady` command from AAS to machine.
      - No machine `Ready` feedback; `Valid` is only internal gating from AAS logic.
      - No `Start` pulse or machine `Busy`/`Done` bits.
      - No `ResetDone` handshake or separate `Idle` feedback bit – only `ActionStatus` text and queue/position data.


# 5) Which code parts we will probably touch later

- **Where a machine command adapter would likely be inserted**
  - `DeviceSlow.scl` (FB `DeviceSlow`):
    - This is the **core timeout-based “machine simulator”**.
    - Future changes:
      - Replace `TON_TIME`-based `finishedAction` with real machine handshake: `SetReady`, `Ready`, `Start`, `Busy`, `Done`, `ResetDone`, `Idle`.
      - Map `Valid`, `deviceRunning`, and `ActionStatus` to these handshake signals.
      - Potentially split `DeviceSlow` into a **machine command adapter FB** that writes/reads actual I/O and updates `ActionStatus` / `OutputPositionSr`.
  - `PassiveAAS_DB` networks:
    - Fields: `Valid`, `finishedAction`, `ActionStatus`, `OutputPositionSr`, `deviceRunning`, `CheckAction_Instance`, `DeviceSlow_Instance`, `RemoveFinishedProduct_Instance`, `F_TRIG_Instance`.
    - Future changes:
      - Where `Valid` is set and `DeviceSlow_Instance` is called is the natural place to insert handshake logic (e.g., set `SetReady`, generate `Start` when conditions are met).
      - Use `ActionStatus` to reflect machine states (Idle/Ready/Busy/Done/Failed) instead of a pure timer.

- **Which method would later trigger execution**
  - `ReserveAction` remains the most natural **“execution trigger”** from AAS perspective:
    - It enqueues items into `"PQueue".PQueue`.
    - After handshake integration, the “machine adapter” should pick items from PQueue (via `CheckAction`) and send commands to machine.
  - We will **not change** `ReportProduct` semantics; it should continue to be pure reporting.
  - `CheckAction` is the next step after `ReserveAction` and is responsible for deciding which item to execute; if we need to add more sophisticated logic (e.g., skip items if blocked by machine state), we’d extend or complement `CheckAction`.

- **Where GetStatus would likely need to read from**
  - Currently, `GetStatus` only looks at:
    - `PQueue.PQueue` via `PQueue_find`.
    - `PQueue.PQueue.activeItemPosition`.
  - For handshake integration, we likely want `GetStatus` to:
    - Incorporate `PassiveAAS_DB`.ActionStatus (Idle/Ready/Busy/Done/Failed).
    - Possibly show **machine-level states** per ID instead of just “inProgress / position / error”.
    - Optionally reflect **Done** vs **Removed** more explicitly, not just “not found”.

- **Candidate DB/FB/FC areas for extension**
  - AAS OPC UA Methods:
    - `ReportProduct.scl` – likely unchanged except maybe additional validation or consistent ID format.
    - `GetSupported.scl` – may be extended to account for machine capabilities (actual device state, not just static ranges).
    - `ReserveAction.scl` – central path where we might add logic to:
      - Reject reservations if machine not in correct state, or
      - Attach more detailed parameters required by the handshake.
    - `GetStatus.scl` – main candidate for returning richer status from handshake.
    - `FreeFromQueue.scl` – might need to honor machine handshake (e.g., disallow removing items if machine is already busy with them).
    - `FreeFromPosition.scl` – may remain largely unchanged; still a good place to confirm clearing of output.
  - Passive AAS:
    - `PassiveAAS_DB` structure – fields for actual machine I/O (Ready/Busy/Done/Idle, Start/ResetDone pulses) could be added here.
    - `CheckAction.scl` – would remain responsible for mapping queue items to machine executions, might need more context about machine capability/availability.
    - `RemoveFinishedProduct.scl` – could also tie into real machine Done signal instead of `finishedAction` from `DeviceSlow`.
  - Queue helpers:
    - `GetPQueueStatus.scl` – no logical change needed, but may be re-used to visualize queue plus machine states.


# 6) What the user should understand first

- **First read the AAS OPC UA methods**
  - `ReportProduct.scl`:
    - See how `InputMessage` is parsed with `GetMessage`, how **only the ID** is used, and how the product enters `RPQueue`.ReportedProductsQueue.
    - Focus on calls to `Queue_find` / `Queue_push` and error handling.
  - `GetSupported.scl`:
    - Understand the **support scoring** and **queue position** calculation using `"PQueue".PQueue.subElementCount`.
    - Note that this method does not affect state; it only **evaluates**.
  - `ReserveAction.scl`:
    - See how `InputArray` [0..4] is interpreted (ID, priority, material, positions).
    - Understand that it only **enqueues into `"PQueue".PQueue`** and enforces support > 0.
  - `GetStatus.scl`:
    - Understand that it only uses `PQueue_find` + `activeItemPosition` to distinguish “in queue” vs “in progress”, and that it does **not** read `ActionStatus`.

- **Then read the passive AAS DB and device logic**
  - `Passive AAS_DB.pdf`:
    - Skim the structure to see:
      - The relationships among `priorityQueue`, `reportedProductsQueue`, `CheckAction_Instance`, `DeviceSlow_Instance`, `RemoveFinishedProduct_Instance`, `finishedAction`, `ActionStatus`, `OutputPositionSr`, `QueueHistoryStatus`.
    - This gives you the **big picture of how queues and statuses live in one DB**.
  - `DeviceSlow.scl`:
    - Focus on `waitTime`, `IEC_Timer_0_Instance`, `Valid`, `finishedAction`, and how `ActionStatus` and `OutputPositionSr` are updated.
    - This is the **exact place where timeout-based execution is implemented**.
  - `CheckAction.scl`:
    - Understand how `priorityQueue` and `reportedProductsQueue` are cross-referenced to decide which item to execute.
    - This is where an adapter to real machine signals would decide **which ID is currently “active”**.

- **Next, examine completion/cleanup logic**
  - `RemoveFinishedProduct.scl`:
    - See how it removes items from both queues and appends IDs to `historyBuffer`.
    - Connect this with `F_TRIG_Instance` and `finishedAction` in `Passive AAS_DB.pdf`.
  - `FreeFromPosition.scl`:
    - Understand how `OutputPositionSr` is cleared via OPC UA, closing the loop between **finished product at output** and **reader acknowledgement**.

- **Finally, review queue infrastructure and UA mappings**
  - Queue functions (`Queue_find`, `Queue_remove`, `PQueue_push`, `PQueue_find`, `PQueue_get`):
    - Only necessary to understand detailed queue behavior (error codes, index calculations).
  - `GetPQueueStatus.scl`:
    - Shows how a string representation of queue fill is built.
  - `PLCSeviceProvider.xml`:
    - Read the `<si:MethodMapping>` and `<si:VariableMapping>` sections to see how UA methods/variables map to `Active AAS_DB` instances and `PassiveAAS_DB` fields.
    - Note especially the mapping of `ActionStatus`, `CurrentId`, `CurrentPosition`, `OutputPositionSr`, `QueueStatus`, and `QueueHistoryBuffer` to `PassiveAAS_DB`.

