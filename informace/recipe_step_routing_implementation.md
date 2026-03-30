## Recipe-step based routing implementation

- **Changed files**:
  - `main/app.c`

- **Modified function/state**:
  - `State_Machine` – specifically the `State_Mimo_Polozena` branch where the PLC AAS flow is executed.

- **Recipe finished condition**:
  - A recipe is considered finished for the AAS path if:
    - `sRecipeInfo.RecipeDone == true`, **or**
    - `sRecipeInfo.ActualRecipeStep >= sRecipeInfo.RecipeSteps`.
  - This is implemented via:
    - `TRecipeInfo *info = &iHandlerData.sWorkingCardInfo.sRecipeInfo;`
    - `uint8_t curStep = info->ActualRecipeStep;`
    - `uint8_t numSteps = info->RecipeSteps;`
    - `bool recipeFinished = (info->RecipeDone == true) || (curStep >= numSteps);`
  - If `recipeFinished` is true:
    - A debug log is emitted:
      - **`"AAS_DECISION: step %u / %u -> RECIPE_FINISHED"`**
    - If `RecipeDone` is not yet set, it is set to `true` and persisted to the tag using `NFC_Handler_WriteSafeInfo` and `NFC_Handler_Sync`.
    - Routing:
      - `RAF = State_KonecReceptu;`
      - The state machine `continue`s, going into the existing finished / final handling branch.

- **Local-step detection condition**:
  - The current step is read as:
    - `TRecipeStep *step = &iHandlerData.sWorkingCardInfo.sRecipeStep[curStep];`
  - Logs include:
    - **`"AAS_DECISION: ActualRecipeStep=%u RecipeSteps=%u TypeOfProcess=%u P1=%u P2=%u"`**
  - A step is considered **local to this cell** (for this reader) if:
    - `step->TypeOfProcess == ToStorageGlass`
  - For this case:
    - A decision log is emitted:
      - **`"AAS_DECISION: LOCAL_PLC_AAS (TypeOfProcess=ToStorageGlass)"`**
    - The existing PLC AAS flow is executed unchanged:
      - Build 5-field message: `sr_id/0/TypeOfProcess/ParameterProcess1/ParameterProcess2`
      - `OPC_GetSupported`
      - `OPC_ReserveAction`
      - `OPC_AAS_WaitCompletionPoll`
      - On success: mark step done, increment `ActualRecipeStep`, update `RecipeDone` if last step, and write back via `NFC_Handler_WriteStep` and `NFC_Handler_WriteSafeInfo`.
      - Route to `State_WaitUntilRemoved` as before.

- **Non-local step routing into transport logic**:
  - If the recipe is not finished and the step is **not** local:
    - Condition:
      - `step->TypeOfProcess != ToStorageGlass`
    - A decision log is emitted:
      - **`"AAS_DECISION: REQUEST_TRANSPORT (TypeOfProcess=%u)"`**
    - Instead of calling PLC AAS for this cell, the code routes into the existing transport / production routing logic by:
      - `RAF = State_Inicializace_ZiskaniAdres;`
      - `RAFnext = State_Poptavka_Vyroba;`
      - `continue;`
  - This reuses the already implemented states:
    - `State_Inicializace_ZiskaniAdres` (address discovery via `GetCellInfoFromLDS`, `ExistType`)
    - `State_Poptavka_Vyroba` / `State_Poptavka_Transporty` / `State_Rezervace` / `State_Transport`
  - No new transport architecture is introduced; we only redirect the non-local step into the existing path.

- **Re-scan guard**:
  - The existing re-scan guard is preserved but now runs after the recipe-finished check and before the local/non-local decision:
    - If the same `sr_id` is seen within `AAS_RESCAN_GUARD_MS`, the reader logs and routes to:
      - `RAF = State_WaitUntilRemoved;`
    - This prevents duplicate `ReserveAction` calls even with the new routing.

- **New logs added**:
  - When recipe is finished:
    - **`"AAS_DECISION: step %u / %u -> RECIPE_FINISHED"`**
  - For each AAS decision on a non-finished recipe:
    - **`"AAS_DECISION: ActualRecipeStep=%u RecipeSteps=%u TypeOfProcess=%u P1=%u P2=%u"`**
  - For local step (belongs to this cell):
    - **`"AAS_DECISION: LOCAL_PLC_AAS (TypeOfProcess=ToStorageGlass)"`**
  - For non-local step (belongs to another cell, request transport):
    - **`"AAS_DECISION: REQUEST_TRANSPORT (TypeOfProcess=%u)"`**

- **Before / after control-flow summary**:
  - **Before**:
    - After NFC tag read and `TRecipeInfo` / `TRecipeStep[]` load, the AAS flow:
      - Called `ReportProductEx` for a valid sr_id.
      - If successful, it:
        - Checked only whether `ActualRecipeStep >= RecipeSteps` to mark `RecipeDone` and exit.
        - Unconditionally built the AAS message from the current step and called:
          - `OPC_GetSupported`
          - `OPC_ReserveAction`
          - `OPC_AAS_WaitCompletionPoll`
      - There was **no filtering by `TypeOfProcess`**, so PLC AAS was triggered for any step type.
      - Transport and production routing lived in separate legacy states, used mainly when the AAS flow was not active or for other branches.
  - **After**:
    - After NFC tag read and recipe loading, in `State_Mimo_Polozena` and with a valid sr_id:
      1. **Recipe finished gate**:
         - If `RecipeDone` is true or `ActualRecipeStep >= RecipeSteps`:
           - Log **`RECIPE_FINISHED`**, persist `RecipeDone` if needed, and route to `State_KonecReceptu` (finished handling).
      2. **Re-scan guard**:
         - Same-sr_id within `AAS_RESCAN_GUARD_MS` skips AAS execution and waits for removal.
      3. **Step-based routing** (non-finished recipe):
         - Load `currentStep = sRecipeStep[ActualRecipeStep]` and log `ActualRecipeStep`, `RecipeSteps`, `TypeOfProcess`, `ParameterProcess1`, `ParameterProcess2`.
         - If `currentStep.TypeOfProcess == ToStorageGlass`:
           - Log **`LOCAL_PLC_AAS`** and execute the **existing PLC AAS flow unchanged**.
         - Else:
           - Log **`REQUEST_TRANSPORT`** and redirect control to:
             - `State_Inicializace_ZiskaniAdres` → `State_Poptavka_Vyroba` and the rest of the existing transport / production path.

This implementation keeps the PLC AAS handshake and message format identical for local steps, while ensuring that:
- Finished recipes go directly to the final handling branch.
- Only `ToStorageGlass` steps trigger PLC AAS in this cell.
- All other process types are delegated to the existing transport-oriented state machine.

