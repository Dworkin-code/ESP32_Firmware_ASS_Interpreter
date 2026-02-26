# Empty-tag / NO RECIPE handling (ESP32 reader)

## What is considered an "empty tag"

The reader treats an NFC tag as **empty (NO_RECIPE)** when **any** of the following is true, using the already loaded `TRecipeInfo` / `TRecipeStep` data:

- **A) Read failure**  
  Reading recipe info or steps from the tag fails (read error, timeout, or invalid length). In that case `NFC_Handler_LoadData` returns non-zero and the empty-tag helper is not called; the state machine does not perform PLC calls.

- **B) RecipeSteps out of bounds**  
  `RecipeSteps == 0` or `RecipeSteps > MAX_RECIPE_STEPS` (64). Tags with no steps or an invalid step count are treated as uninitialized.

- **C) ActualRecipeStep out of bounds and not done**  
  `ActualRecipeStep >= RecipeSteps` and the tag does not show a valid “done” state (`RecipeDone` not set), i.e. it looks uninitialized.

- **D) First step and recipe info look uninitialized**  
  The first recipe step has all of: `TypeOfProcess == 0`, `ParameterProcess1 == 0`, `ParameterProcess2 == 0`, `ProcessCellID == 0`, `TransportCellID == 0`, and the recipe info looks like default values (`ID`, `NumOfDrinks`, `ActualBudget`, `Parameters`, `RightNumber` all zero).

The heuristic is conservative so that valid recipes are not misclassified as empty.

## What is printed

When an empty tag is detected, a single diagnostic block is printed to the serial monitor (once per tag-present event), for example:

```
[NFC] EMPTY TAG / NO RECIPE DETECTED
[NFC] UID=<hex bytes>  sr_id=<computed>
[NFC] Reason=<A/B/C/D + details>
[NFC] Action=SKIP_PLC_CALLS
```

- **UID**: Hex bytes of the tag UID, or `(none)` if not available.
- **sr_id**: Computed product/session ID from UID, or `(none)` / `(build failed)`.
- **Reason**: Short string from the empty-tag helper, e.g. `RecipeSteps=0`, `RecipeSteps>64`, `ActualStepOutOfBounds`, `Step0 and info zeros`, `StepsNull`, `InfoNull`.

## PLC calls are skipped

When the tag is classified as empty (NO_RECIPE):

- **ReportProduct** is not called.
- **GetSupported** is not called.
- **ReserveAction** is not called.
- **FreeFromPosition** is not called.

The reader goes to the **State_WaitUntilRemoved** state and waits for the tag to be removed; no OPC UA / PLC methods are invoked for that tag.
