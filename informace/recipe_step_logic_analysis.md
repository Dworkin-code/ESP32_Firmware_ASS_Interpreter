## Recipe step logic analysis (ESP32_Firmware_ASS_Interpreter)

### 1. Does recipe-step evaluation logic already exist?

**Yes, recipe-step evaluation logic exists, but it is split between two flows and does not yet implement the “only ToStorageGlass uses PLC AAS, others use transport” behavior.**

- The firmware reads recipe info and steps from the NFC tag into `TCardInfo.sRecipeInfo` and `TCardInfo.sRecipeStep` via the NFC handler/reader stack.
- The main runtime orchestration is implemented in `State_Machine` in `main/app.c`, which:
  - Uses `TRecipeInfo.ActualRecipeStep` as the current step index.
  - Indexes `TRecipeStep` arrays to interpret flags (`IsProcess`, `IsTransport`, `NeedForTransport`) and cell IDs (`ProcessCellID`, `TransportCellID`).
  - Drives both the **legacy OPC reservation/transport flow** and the newer **PLC AAS flow**.

However, **the PLC AAS branch currently does not distinguish individual process types (such as `ToStorageGlass`) when deciding whether to call AAS.** It calls PLC AAS based on the current step index and recipe-done status only.

---

### 2. Where is this logic implemented? (file + function)

- **Main orchestrator / state machine**
  - **File**: `main/app.c`
  - **Function**: `State_Machine`
  - Responsibilities:
    - React to card presence (`State_Mimo_Polozena` and others).
    - Load recipe data from NFC using `NFC_Handler_LoadData`.
    - Evaluate `ActualRecipeStep` vs `RecipeSteps` and `RecipeDone`.
    - Decide between:
      - PLC AAS-based step execution (`USE_PLC_AAS_FLOW` branch).
      - Legacy cell/transport reservation state flow.

- **Empty-recipe detection using `ActualRecipeStep` and steps**
  - **File**: `main/app.c`
  - **Function**: `NFC_IsRecipeEmpty`
  - Uses `TRecipeInfo` and the `TRecipeStep *steps` array to:
    - Detect invalid `ActualRecipeStep` (out of bounds when recipe not done).
    - Detect “all-zero” first step (uninitialized recipe).

- **Recipe and step generation / utilities**
  - **File**: `components/NFC_Recipes/NFC_recipes.c`
  - **Functions**:
    - `GetRecipeStepByNumber` – populates `TRecipeStep.TypeOfProcess` with values like `StorageAlcohol`, `StorageNonAlcohol`, `Shaker`, `Cleaner`, `ToStorageGlass`, `ToCustomer`, `Transport`.
    - `GetCardInfoByNumber` – generates a `TCardInfo` with `TRecipeInfo` and a `TRecipeStep` array and sets `ActualRecipeStep = 0`.
    - `GetCellInfoFromLDS`, `GetWinningCell`, `ExistType`, `AskForValidOffer`, `ReserveAllOfferedReservation`, `DoReservation`, `IsDoneReservation` – provide cell lookup, transport/process reservation and completion queries for the legacy flow (using OPC methods like `Inquire`, `Reserve`, `DoReservation_klient`, `IsFinished`).

- **NFC structures / `TRecipeInfo` and `TRecipeStep` definitions**
  - **File**: `components/NFC_Reader/NFC_reader.h`
  - **Structures**:
    - `TRecipeInfo` – contains `RecipeSteps`, `ActualRecipeStep`, `RecipeDone`, budget fields, etc.
    - `TRecipeStep` – contains `TypeOfProcess`, process and transport cell IDs, reservation IDs, prices, times, and flags (`NeedForTransport`, `IsTransport`, `IsProcess`, `IsStepDone`).

- **OPC/PLC client and PLC AAS calls**
  - **File**: `components/OPC_Klient/OPC_klient.c`
  - **Relevant functions**:
    - `Inquire` – used by `GetWinningCell` to query candidate cells for a given `TypeOfProcess` and parameters.
    - `OPC_BuildSrIdFromUid`, `OPC_WriteCurrentId`, `OPC_ReportProductEx`, `OPC_GetSupported`, `OPC_ReserveAction`, `OPC_AAS_WaitCompletionPoll` – used in `app.c` to implement the PLC AAS workflow.

---

### 3. How does the firmware currently decide when to call PLC AAS?

**The PLC AAS flow is entered as soon as a non-empty recipe is detected and an sr_id can be built; it is not filtered by `TypeOfProcess` or by cell ownership.**

Detailed behavior in `State_Machine` (`main/app.c`, `State_Mimo_Polozena` case):

1. **Card appears** (`State_Mimo_Polozena`):
   - Takes `xNFCReader` semaphore.
   - Calls `NFC_Handler_LoadData(&iHandlerData)` → loads `TRecipeInfo` and `TRecipeStep` from the NFC tag into:
     - `iHandlerData.sWorkingCardInfo.sRecipeInfo`
     - `iHandlerData.sWorkingCardInfo.sRecipeStep`

2. **Empty-tag check**:
   - Calls `NFC_IsRecipeEmpty(&iHandlerData.sWorkingCardInfo.sRecipeInfo, iHandlerData.sWorkingCardInfo.sRecipeStep, RecipeSteps, reasonBuf, ...)`.
   - If empty:
     - Logs diagnostic.
     - Sets state to `State_WaitUntilRemoved`.
     - **Crucially: prints `Action=SKIP_PLC_CALLS` and does not invoke AAS.**

3. **Non-empty recipe → build `sr_id` and call `ReportProductEx`**:
   - Builds `sr_id` using `OPC_BuildSrIdFromUid`.
   - Writes `CurrentId` using `OPC_WriteCurrentId(MyCellInfo.IPAdress, uidStr)`.
   - Calls `OPC_ReportProductEx(MyCellInfo.IPAdress, sr_id, reportOutBuf, ...)` if `sr_id` is available.

4. **AAS branch gating (compile-time and runtime)**:
   - Under `#if defined(USE_PLC_AAS_FLOW) && USE_PLC_AAS_FLOW`, and if:
     - `have_sr_id == true`, and
     - `iHandlerData.sWorkingCardInfo.TRecipeInfoLoaded == true`,
     - and `OPC_ReportProductEx` succeeded with `OutputMessage` equal to `"Success"` or `"Error:8501"` (“already reported”) → treat as OK.
   - On other `"Error:XXXX"` codes from `ReportProductEx`, it:
     - Sets `RecipeDone = true`.
     - Writes back info via `NFC_Handler_WriteSafeInfo` + `NFC_Handler_Sync`.
     - Returns to `State_Mimo_Polozena` without going deeper into AAS.

5. **Step index validation before AAS ReserveAction**:
   - Reads:
     - `curStep = sRecipeInfo.ActualRecipeStep`
     - `numSteps = sRecipeInfo.RecipeSteps`
   - If `curStep >= numSteps`:
     - Treats recipe as finished, sets `RecipeDone = true`, writes back, and returns.
   - **This is the main place where `ActualRecipeStep` gates AAS, but it checks only bounds, not `TypeOfProcess`.**

6. **AAS call construction for the current step**:
   - Loads pointer to the current step:
     - `TRecipeStep *step = &iHandlerData.sWorkingCardInfo.sRecipeStep[curStep];`
   - Builds a 5-field message:
     - `msg5 = "sr_id/0/TypeOfProcess/ParameterProcess1/ParameterProcess2"`.
   - There is **no condition on `step->TypeOfProcess` here**; any `TypeOfProcess` value present in the step is directly encoded.

7. **AAS capability and execution**:
   - Calls `OPC_GetSupported(MyCellInfo.IPAdress, msg5, outBuf, ...)`.
     - If `outBuf` starts with `"Error:"`: consider it failure, mark `RecipeDone = true`, write back, and abort.
   - Calls `OPC_ReserveAction(MyCellInfo.IPAdress, msg5, outBuf, ...)`.
     - On failure or `"Error:"` again: mark `RecipeDone = true`, write back, and abort.
   - On success:
     - Stores `sr_id` and timestamp into `s_lastSeenSrId` and `s_lastActionTimestampMs`.
     - Calls `OPC_AAS_WaitCompletionPoll(MyCellInfo.IPAdress, sr_id, AAS_COMPLETION_TIMEOUT_MS, 500)` to wait for completion.
     - On completion:
       - Marks `step->IsStepDone = 1`.
       - Increments `ActualRecipeStep` by 1; if now >= `numSteps`, sets `RecipeDone = true`.
       - Writes back the updated step and info via `NFC_Handler_WriteStep`, `NFC_Handler_WriteSafeInfo`, and `NFC_Handler_Sync`.
       - Transitions to `State_WaitUntilRemoved`.

8. **Re-scan guard**:
   - If the same `sr_id` appears again within `AAS_RESCAN_GUARD_MS`, it skips calling `ReserveAction` again and moves to `State_WaitUntilRemoved`.

**Conclusion for this question:**  
Once a non-empty recipe is found, **PLC AAS calls are made for the current `ActualRecipeStep` unconditionally with respect to `TypeOfProcess` and without checking whether the step belongs to this specific cell.** AAS is therefore not currently restricted to `ToStorageGlass` or to steps where `ProcessCellID == MyCellInfo.IDofCell`.

---

### 4. Does the firmware currently call PLC AAS for every recipe step?

**Effectively yes, for all “active” steps that pass the recipe-empty and AAS error checks; there is no explicit per-type or per-cell filter before AAS.**

More precisely:

- For each card placement where:
  - The recipe is not considered empty by `NFC_IsRecipeEmpty`,
  - `TRecipeInfoLoaded` is true,
  - `ReportProductEx` succeeds with `"Success"` or `"Error:8501"`,
  - `ActualRecipeStep < RecipeSteps`,
  - And not blocked by the re-scan guard,
- The firmware:
  - Reads the `TRecipeStep` at `ActualRecipeStep`.
  - Sends its `TypeOfProcess`, `ParameterProcess1`, and `ParameterProcess2` to PLC AAS via `GetSupported` and `ReserveAction`.
  - On success, marks that step done and increments `ActualRecipeStep`.

There is **no branch like**:

- “If `TypeOfProcess == ToStorageGlass` then call AAS; otherwise use transport only.”
- “If `ProcessCellID != MyCellInfo.IDofCell` then skip AAS and only do transport.”

So, in practice:

- **Yes, AAS is invoked for whatever the current recipe step is, independent of `TypeOfProcess`.**
- The only steps skipped by AAS are:
  - When the recipe is considered empty or invalid.
  - When `ReportProductEx`, `GetSupported`, or `ReserveAction` fail or return errors (in which case the recipe is usually marked done).

---

### 5. Does transport logic already exist?

**Yes, transport logic exists and is quite detailed in the legacy (non-AAS) flow.**

Key places:

- **State selection based on flags and timings** (in `State_Machine`, `State_Mimo_Polozena` case after the AAS section):
  - Uses `ActualRecipeStep` to index the current step and then evaluates:
    - `IsProcess` / `IsTransport`.
    - `TimeOfProcess`, `TimeOfTransport`.
    - `ProcessCellID`, `TransportCellID`.
    - `TransportCellReservationID`, `ProcessCellReservationID`.
    - `NeedForTransport`.
  - Based on these, it transitions to:
    - `State_Vyroba_SpravneProvedeni`, `State_Vyroba_Objeveni`, `State_Vyroba_OznameniOProvedeni` (manufacturing-related).
    - `State_Transport`, `State_Poptavka_Transporty`, `State_Rezervace` (transport-related).

- **Transport cell selection and reservation**:
  - `State_Inicializace_ZiskaniAdres`:
    - Calls `GetCellInfoFromLDS(TypeOfProcess, &BunkyVelikost)` to get candidate cells for this `TypeOfProcess`.
    - Checks if at least one transport cell exists (`ExistType(Bunky, BunkyVelikost, Transport)`).
  - `State_Poptavka_Transporty`:
    - Checks `NeedForTransport` and whether the current step’s `TypeOfProcess` is `Transport`.
    - Uses `GetWinningCell` with `Transport` as process type if transport is needed, to choose a specific transport cell.
    - Writes `TransportCellID`, `TransportCellReservationID`, and `NeedForTransport` into the current `TRecipeStep`.
  - `State_Transport`:
    - Uses `AskForValidReservation` to validate transport reservations.
    - Uses `DoReservation` with `process=false` to start the transport.
    - Marks `IsTransport = 1`, updates budget (`ActualBudget -= PriceForTransport`), and writes back via NFC handler.

- **Cell mismatch handling and injected transport steps**:
  - `State_Vyroba_Objeveni`:
    - If the integrity recipe’s `ProcessCellID` for the current step does not match `MyCellInfo.IDofCell`, it:
      - Calls `GetMinule` and `AddRecipe` with a `Transport` step (`GetRecipeStepByNumber(10, ProcessCellID)`).
      - Adjusts `NextID` and `ActualRecipeStep` so that a transport step is inserted before continuing the original process.

So, **transport reservation and execution are already implemented**, both for planned transport steps and for additional transport inserted when the glass appears at the wrong cell.

---

### 6. Current control flow: NFC read → PLC call

Below is the **current control flow**, including both AAS and legacy transport/OPC logic.

1. **Tag placement detection**:
   - Task `Is_Card_On_Reader` (`app.c`) periodically calls `NFC_isCardReady` to update `TaskParams.CardOnReader`.
   - `State_Machine` watches `CardOnReader` and transitions between `State_Mimo_Polozena`, `State_WaitUntilRemoved`, etc.

2. **On tag appearance (`State_Mimo_Polozena`)**:
   - Logs “Sklenice se objevila”.
   - Takes `xNFCReader` and calls `NFC_Handler_LoadData(&iHandlerData)`:
     - This, via `NFC_handler.c` and `NFC_reader.c`, reads:
       - `TRecipeInfo` into `iHandlerData.sWorkingCardInfo.sRecipeInfo`.
       - All `TRecipeStep` structs into `iHandlerData.sWorkingCardInfo.sRecipeStep`.

3. **Recipe validation and empty check**:
   - On load error `Error == 4`, sets `RAF = State_Mimo_NastaveniNaPresunDoSkladu` (load a “return to storage” recipe) and continues.
   - Otherwise, calls `NFC_IsRecipeEmpty(...)`:
     - If empty: log once and go to `State_WaitUntilRemoved` with **no PLC calls**.

4. **Non-empty recipe → initial PLC AAS interaction**:
   - Prints the recipe via `NFC_Print`.
   - Builds `sr_id` from the tag UID using `OPC_BuildSrIdFromUid` and `uidStr`.
   - With `xEthernet` semaphore taken:
     - Calls `OPC_WriteCurrentId(MyCellInfo.IPAdress, uidStr)` to write the textual UID into PLC variable `CurrentId`.
     - Calls `OPC_ReportProductEx(MyCellInfo.IPAdress, sr_id, reportOutBuf, ...)`.
   - If `ReportProductEx` fails or returns a non-allowed error (other than `"Error:8501"`), the recipe is marked done and written back, and the state machine restarts from `State_Mimo_Polozena`.

5. **AAS step execution for the current recipe step** (only if `USE_PLC_AAS_FLOW` and ReportProduct is acceptable):
   - Validates `ActualRecipeStep < RecipeSteps`; otherwise marks recipe done and returns.
   - Enforces a re-scan guard to avoid double-calling `ReserveAction`.
   - Loads `TRecipeStep *step` at `curStep`.
   - Builds the AAS 5-field message (`sr_id/priority/material/parameterA/parameterB`) using:
     - `TypeOfProcess`, `ParameterProcess1`, `ParameterProcess2`.
   - Takes `xEthernet`, then:
     - Calls `OPC_GetSupported` with this message; on `Error:` from PLC → mark recipe done, sync to NFC, and finish.
     - Calls `OPC_ReserveAction` with this message; on `Error:` → mark recipe done, sync to NFC, and finish.
     - On success: stores the sr_id in `s_lastSeenSrId` and calls `OPC_AAS_WaitCompletionPoll` to wait until PLC reports the step as finished.
   - On successful completion:
     - Sets `step->IsStepDone = 1`.
     - Increments `ActualRecipeStep`, possibly sets `RecipeDone = true` if it was the last step.
     - Writes updated `TRecipeStep` and `TRecipeInfo` back using NFC handler and marks the step done.
     - Goes to `State_WaitUntilRemoved`.

6. **Fallback to legacy OPC/LDS reservation and transport flow**:
   - If the build is compiled without `USE_PLC_AAS_FLOW`, or if any AAS preconditions fail (e.g. no `sr_id`, ReportProductEx not called or not accepted), execution continues into the existing legacy logic:
     - Validates `ActualRecipeStep` and `RecipeDone`.
     - Based on the current `TRecipeStep` flags and cell IDs, moves through states:
       - `State_Inicializace_ZiskaniAdres` → obtains candidate cells from `GetCellInfoFromLDS(TypeOfProcess, ...)`.
       - `State_Poptavka_Vyroba` / `State_Vyroba_*` → uses `GetWinningCell`, `DoReservation`, `IsDoneReservation` to run process steps at the right cell.
       - `State_Poptavka_Transporty` / `State_Transport` → uses `GetWinningCell` and `DoReservation` with `process=false` to execute transport steps.
       - `State_Rezervace` → validates and reserves all offers (`AskForValidOffer`, `ReserveAllOfferedReservation`).
     - These legacy states **do consider whether `ProcessCellID` matches `MyCellInfo.IDofCell`** (for example in `State_Vyroba_Objeveni`), and inject extra `Transport` steps when needed.

---

### Summary relative to the desired behavior

- **Reading recipe and step selection**:
  - Implemented: recipe is read from NFC; `ActualRecipeStep` is used as the active step index; the corresponding `TRecipeStep` is loaded.
- **Evaluating `TypeOfProcess` and cell ownership for AAS**:
  - Partially implemented:
    - `TypeOfProcess` is used to look up candidate cells and to build AAS messages.
    - Cell ownership (`ProcessCellID` vs `MyCellInfo.IDofCell`) is used in the **legacy** flow (for inserting transport when at the wrong cell).
  - **Not implemented** for AAS:
    - There is no branch that restricts PLC AAS calls only to `ToStorageGlass` steps.
    - There is no AAS-side check like “only call AAS when this step belongs to this cell; otherwise request transport.”
- **Transport logic**:
  - Already implemented in legacy flow (states `State_Poptavka_Transporty`, `State_Transport`, `State_Rezervace`, `State_Vyroba_Objeveni`, and helpers in `NFC_recipes.c` and `OPC_klient.c`).

Therefore, **the current firmware behaves as a product orchestrator that always calls PLC AAS for the active recipe step (when AAS is enabled and not in an error condition), regardless of `TypeOfProcess` or cell ownership, and uses the existing transport/OPC logic primarily in the legacy path.** The specific policy “only `ToStorageGlass` uses PLC AAS, all other types only trigger transport for this reader” is **not yet present** in the code.

