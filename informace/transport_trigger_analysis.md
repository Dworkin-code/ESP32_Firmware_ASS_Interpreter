# Transport Trigger Analysis

This document traces how the ESP32 firmware (`ESP32_Firmware_ASS_Interpreter/`) decides to enter the transport pipeline after a process step completes. Primary implementation: legacy state machine in `main/app.c`. PLC/AAS paths are partially integrated and interact with the same entry points.

---

## 1. Process completion path

### Legacy production completion (`State_Vyroba_SpravneProvedeni`)

**File:** `main/app.c`  
**Function:** `State_Machine`, case `State_Vyroba_SpravneProvedeni`

When `IsDoneReservation(..., ProcessCellID)` returns **1** (“process finished successfully”):

1. `NFC_Handler_GetRecipeStep` loads the **current** step (index `ActualRecipeStep`) into `tempStep`.
2. **Step fields updated:** `tempStep.IsProcess = 0`, `tempStep.IsStepDone = 1`, then written back with `NFC_Handler_WriteStep` at the **same** index.
3. **Recipe pointer updated (not in the same write as the step in one atomic block, but sequentially):**
   - If `tempStep.NextID != tempStep.ID`: `tempInfo.ActualRecipeStep = tempStep.NextID` (advance to next step).
   - Else: `tempInfo.RecipeDone = 1`, `++tempInfo.NumOfDrinks` (terminal step).
4. `NFC_Handler_WriteInfo` + `NFC_Handler_Sync` persist recipe info.
5. `RAF = State_Mimo_Polozena` — the next meaningful work happens when the tag is read again in the idle/on-reader state.

**Variables touched:** `TRecipeStep.IsProcess`, `IsStepDone` on the completed step; `TRecipeInfo.ActualRecipeStep` or `RecipeDone` / `NumOfDrinks`.

**`NextID`:** Read from `tempStep` after loading the **current** step — it is the linked “next step” field stored **on the step being completed**, not pre-loaded from a future index.

**`ActualRecipeStep`:** Updated **after** marking the current step done, only when moving to the next step (`NextID != ID`). It is **not** advanced inside the same `GetRecipeStep` that represents the “next” step; advancement uses `NextID` from the finished step.

**Cell / transport decisions at completion:** None. This state only finishes the process and advances `ActualRecipeStep` or sets `RecipeDone`. Transport is decided later in `State_Mimo_Polozena` based on the **new** `ActualRecipeStep` and tag fields (`TimeOfProcess`, reservations, `NeedForTransport`, etc.).

### PLC/AAS completion (`ToStorageGlass` only)

**File:** `main/app.c`, inside `State_Mimo_Polozena`, under `#if defined(USE_PLC_AAS_FLOW) && USE_PLC_AAS_FLOW`

After successful `OPC_AAS_WaitCompletionPoll`:

- `step->IsStepDone = 1`
- `iHandlerData.sWorkingCardInfo.sRecipeInfo.ActualRecipeStep = curStep + 1` (immediate linear advance)
- If `ActualRecipeStep >= numSteps`, `RecipeDone = true`
- Writes the **old** step at index `curStep` and info, then `RAF = State_WaitUntilRemoved`

**Other AAS types:** For `TypeOfProcess != ToStorageGlass`, the code sets `RAF = State_Inicializace_ZiskaniAdres` and `RAFnext = State_Poptavka_Vyroba` — it does **not** mark the step done or advance `ActualRecipeStep` in that branch; it jumps into the legacy “request production cell” pipeline for the **current** step.

---

## 2. Exact transitions into transport states

All paths below are in **`main/app.c`**, `State_Machine`, unless noted.

### `State_Poptavka_Transporty`

| Source | Condition / trigger | Variables |
|--------|---------------------|-----------|
| `State_Mimo_Polozena` | After `State_Inicializace_ZiskaniAdres`, `RAFnext` was set in the branch: `ProcessCellReservationID && NeedForTransport` | `sWorkingCardInfo.sRecipeStep[ActualRecipeStep].ProcessCellReservationID`, `NeedForTransport` |
| `State_Poptavka_Vyroba` | `TypeOfProcess == Transport` — skips manufacturing request | `TypeOfProcess` |
| `State_Poptavka_Vyroba` | Normal success path after writing process cell: `RAF = State_Poptavka_Transporty` | (always follows production poptavka when no early `continue`) |

Full condition for **entering** the transport-request state from idle routing:

```c
else if (ProcessCellReservationID && NeedForTransport)
  RAFnext = State_Poptavka_Transporty;
```

(Indices use `ActualRecipeStep` throughout.)

### `State_Rezervace`

| Source | Condition / trigger | Variables |
|--------|---------------------|-----------|
| `State_Mimo_Polozena` | After init: `RAFnext = State_Rezervace` when `TransportCellReservationID \|\| (ProcessCellReservationID && !NeedForTransport)` | `TransportCellReservationID`, `ProcessCellReservationID`, `NeedForTransport` |
| `State_Poptavka_Transporty` | `!NeedForTransport && !(TypeOfProcess == Transport)` — “no transport needed”, skip to reservation | Same fields on current step |
| `State_Poptavka_Transporty` | After successfully selecting transport cell and writing tag: `RAF = State_Rezervace` | After `TransportCellID` / `TransportCellReservationID` written |

### `State_Transport`

| Source | Condition / trigger | Variables |
|--------|---------------------|-----------|
| `State_Mimo_Polozena` | `TimeOfTransport > 0 && MyCellInfo.IDofCell != ProcessCellID` (glass appeared with reserved transport, not at process cell) | `TimeOfTransport`, `MyCellInfo.IDofCell`, `ProcessCellID` |
| `State_Rezervace` | After `ReserveAllOfferedReservation` succeeds (`Error == 0`): `RAF = State_Transport` | reservation flow outcome |

**Note:** If `State_Rezervace` hits `Error != 0` after `AskForValidOffer`, `RAF = State_Poptavka_Vyroba` (retry). If error after `ReserveAllOfferedReservation`, `RAF = State_Mimo_Polozena`.

---

## 3. `NeedForTransport` lifecycle

### Where it is set

1. **`State_Poptavka_Vyroba`** (after `GetWinningCell` chooses process cell):
   - Starts from tag: `NFC_Handler_GetRecipeStep` → `tempStep`
   - `tempStep.NeedForTransport = false`
   - **If** `MyCellInfo.IDofCell != Process.IDofCell`: `tempStep.NeedForTransport = true`
   - Meaning: the **winning process cell** is not this reader’s cell → physical transport will be required to reach that cell.

2. **`State_Poptavka_Transporty`** (after `GetWinningCell` for transport):
   - `tempStep.NeedForTransport = true` is **always** set when writing the transport cell (even if one could argue redundancy when skipping the branch above).

### Where it is cleared / false

- Explicit **false** only in `State_Poptavka_Vyroba` when `MyCellInfo.IDofCell == Process.IDofCell` (local process, no move).
- There is **no** dedicated “clear `NeedForTransport` after transport completes” in the snippets traced; the flag is a property of the **current recipe step** on the tag. Completing a transport step in `State_Vyroba_Objeveni` (`TypeOfProcess == Transport`) advances `ActualRecipeStep` via `NextID` and clears `IsProcess`/`IsTransport` on that step — the **next** step’s `NeedForTransport` is whatever was stored for that step (often fresh from a new `State_Poptavka_Vyroba` cycle).

### Practical meaning

**`NeedForTransport`** on the **current** `ActualRecipeStep` means: “the assigned **process** cell (`ProcessCellID`) is not the current cell; therefore a **transport** leg must be requested (`State_Poptavka_Transporty`) before reservations can cover movement.”

It is tied to the **current** step’s process assignment (whether the glass must leave this cell to process). It is **not** a separate flag for “next step needs transport”; the next step’s needs are evaluated only after `ActualRecipeStep` advances.

---

## 4. Next-step evaluation

### Where `NextID` is read

1. **`State_Vyroba_SpravneProvedeni`** (case `1`): `GetRecipeStep` for **current** index → `tempStep.NextID` drives `ActualRecipeStep` or end-of-recipe.
2. **`State_Vyroba_Objeveni`** (when `TypeOfProcess == Transport`): `tempStep.NextID` → `tempInfo.ActualRecipeStep` to skip the transport step after it is considered done at appearance.

### Order relative to transport

- **Process completion:** Step is marked done; **then** `ActualRecipeStep` is set to `NextID` **before** returning to `State_Mimo_Polozena`. The **next** reader cycle therefore runs the **idle routing chain** for the **next** step index.
- **Transport request** (`State_Poptavka_Transporty`, `State_Rezervace`, `State_Transport`) always uses **`ActualRecipeStep` as it already is on the tag** — i.e. the step that owns `ProcessCellID` / `TransportCellID` / reservation IDs for that leg. Transport is **not** chosen by reading `NextID` first in those states; `NextID` matters when **finishing** a step and when **skipping** a pure transport step at `State_Vyroba_Objeveni`.

### Current vs next step for transport

- **Idle routing** (`State_Mimo_Polozena`): decisions use **`sRecipeStep[ActualRecipeStep]`** — always the **current** step.
- After legacy process completion, **`ActualRecipeStep` already points at the next step**, so “transport for the next operation” is really “transport requirements of the step that is now current on the tag.”

---

## 5. Cell comparison logic

### `main/app.c` — `State_Mimo_Polozena` (legacy chain)

| Condition | Meaning |
|-----------|---------|
| `TimeOfProcess > 0 && MyCellInfo.IDofCell == ProcessCellID` | Glass is back at the **assigned process cell** with a recorded process time → path to **production appearance** (`State_Vyroba_Objeveni`), interpreted as “appeared without reserved transport” in the log message. |
| `TimeOfTransport > 0 && MyCellInfo.IDofCell != ProcessCellID` | Transport timestamp set and reader is **not** at process cell → go to **`State_Transport`** (execute movement leg). |
| `State_Vyroba_Objeveni`: `sIntegrityCardInfo...ProcessCellID != MyCellInfo.IDofCell` | Tag appeared for **production** but integrity says process belongs to **another** cell → **inject** transport step via `AddRecipe` (wrong-cell recovery). |

### `State_Poptavka_Vyroba`

- `MyCellInfo.IDofCell != Process.IDofCell` (winning cell from LDS vs. this cell) → sets **`NeedForTransport`**.

### `State_Poptavka_Transporty` — `GetWinningCell` for `Transport`

- If `ProcessCellID == Transport` (enum value **7** — note this overloads the field name with type `Transport`): use `ParameterProcess1` as transport parameter.
- Else: use `ProcessCellID` as the numeric parameter to pick the transport route/cell.

**Important naming collision:** `ProcessCellID` is a **uint8_t** cell identifier on the step; **`Transport` is also an enum value** (`ProcessTypes`). The code compares `ProcessCellID == Transport` to select a different `GetWinningCell` code path — not a cell ID equality check to the word “transport” in the English sense.

### `NFC_recipes.c` (reservation / time stamping)

Loops over steps from `ActualRecipeStep` compare `ProcessCellID` / `TransportCellID` to entries in `aCellInfo[]` for writing `TimeOfProcess` / `TimeOfTransport` and for `DoReservation`-related helpers — supporting the same “which cell owns this leg” model.

**Local execution vs transport:**  
- **Local process:** `NeedForTransport == false` after poptavka (this cell won the process).  
- **Transport leg:** `NeedForTransport == true` → `State_Poptavka_Transporty` → transport cell written → `State_Rezervace` → `State_Transport`.  
- **Direct to transport execution:** `TimeOfTransport > 0` and not at `ProcessCellID` from idle state.

---

## 6. Potential logical mismatch / bug

### AAS vs legacy step advancement

- **AAS `ToStorageGlass`:** Advances `ActualRecipeStep` immediately and may set `RecipeDone` without going through `State_Poptavka_Vyroba` / transport states. Any step that is **not** `ToStorageGlass` only **routes** to `State_Poptavka_Vyroba` and does not mark the step complete on the tag in that pass — consistent with “still on same step until legacy pipeline updates the tag.”

### `ActualRecipeStep` advanced “too early”?

- In **legacy** completion, advancement happens **after** writing `IsStepDone` on the completed step. Transport for the **following** step is evaluated on the **next** `State_Mimo_Polozena` visit with the **new** index — generally consistent.
- **Risk:** If `NextID` is wrong, points to self, or is out of range, routing uses the wrong step or hits “broken recipe” (`ActualRecipeStep >= RecipeSteps`).

### `NeedForTransport` “too late”?

- It is only set in **`State_Poptavka_Vyroba`**. Idle routing requires **`ProcessCellReservationID && NeedForTransport`** to go to **`State_Poptavka_Transporty`**. If the tag has a process reservation but **`NeedForTransport` was never true** (e.g. written false while cells mismatch), the code takes the **`State_Rezervace`** branch instead (`ProcessCellReservationID && !NeedForTransport`), which **skips** explicit transport poptavka — plausible inconsistency if data on tag was edited or partially written.

### `ProcessCellID` checked on the “wrong” step?

- After completion, **`ActualRecipeStep` points to the next step**. All comparisons in idle routing use that index — so **`ProcessCellID` is always that of the “current” recipe step**, which is the **next** work item after a successful advance. That matches “plan transport for the step we are about to execute,” not “the step we just finished,” except when the tag was not yet advanced (AAS non-storage branch).

### Expectation of an explicit `Transport` recipe step

- The nominal flow can use **`TypeOfProcess == Transport`** steps (template via `GetRecipeStepByNumber(10, ...)`).
- **`State_Poptavka_Vyroba`** short-circuits to **`State_Poptavka_Transporty`** when the current step **is** already a transport step — so a **dedicated transport step** is a **supported** shape of the recipe.
- Transport can also be driven by **flags/times/reservations** on a **non-transport** step (`NeedForTransport`, `TimeOfTransport`, reservation IDs). So a **separate** transport step is **not strictly required** for all paths, but **`TypeOfProcess == Transport`** is integrated (e.g. skip manufacturing poptavka, special handling in `State_Vyroba_Objeveni`).

### `State_Transport` early exit

- If `NeedForTransport == 0`, firmware jumps to **`State_Vyroba_OznameniOProvedeni`** — i.e. “no transport needed, treat as ready to announce process.” Combined with poptavka always setting `NeedForTransport = true` when writing transport cell data, this is a guard against inconsistent tag state.

### Integrity vs working in `State_Vyroba_Objeveni`

- Wrong-cell detection uses **`sIntegrityCardInfo`** while many writes use **`sWorkingCardInfo`**. After load/sync these should match; if they diverge, the wrong-cell branch could misfire. Worth awareness for debugging, not proven as a runtime bug here.

### `GetMinule` guard vs step `ID` (`NFC_recipes.c`)

```c
if (aHandlerData->sWorkingCardInfo.sRecipeInfo.RecipeSteps >= ActualID)
  return 1; // mimo rozsah
```

`ActualID` is the current step’s **`ID`** field (often in `0 .. RecipeSteps-1`). For many valid IDs, **`RecipeSteps >= ActualID`** is true, so the function can return **1** before the search loop runs. In **`State_Vyroba_Objeveni`**, **`Last` is declared `0`** and **`GetMinule`’s return value is not checked** before **`AddRecipe(..., Last, ...)`**. If **`GetMinule` fails**, **`Last` stays 0**, so **`AddRecipe` may splice relative to step index 0** instead of the real predecessor — likely incorrect graph surgery. Worth validating whether the guard was meant to compare a **step index** rather than **`ID`**, or use **`>`** instead of **`>=`**.

---

## 7. Transport step injection behavior

### When `AddRecipe(...)` is called

**File:** `main/app.c`, **`State_Vyroba_Objeveni`**

- Condition: current step is **not** pure transport (`TypeOfProcess == Transport` handled above), and  
  `iHandlerData.sIntegrityCardInfo.sRecipeStep[ActualRecipeStep].ProcessCellID != MyCellInfo.IDofCell`

So: tag arrived at production flow but the **process cell ID on the tag** does not match **this** cell.

### What is inserted

**File:** `components/NFC_Recipes/NFC_recipes.c`

- `GetRecipeStepByNumber(10, ProcessCellID)` → **`TypeOfProcess = Transport`**, **`ParameterProcess1 = aParam`** (target process cell id).
- `AddRecipe(&iHandlerData, ..., Last, ...)` where **`Last`** comes from **`GetMinule(..., step.ID, &Last)`** — the index of the step whose `NextID` links to the current step (previous step in the linked list).

`AddRecipe` splices the new step: adjusts `RecipeSteps`, rewrites **`NextID` links** between the new step and the step at `aRecipeBefore`, appends the new step at the end of the array (see `NFC_recipes.c` ~677–757).

### Post-injection pointer fix

After `AddRecipe`, the code loads the step at **`Last`**, sets `tempStep.NextID = RecipeSteps` (new last index), writes info with **`ActualRecipeStep = RecipeSteps - 1`** so the active step becomes the **newly appended transport step**, then `RAF = State_Mimo_Polozena`.

### Is a separate transport step required for transport to work?

- **No for the main pipeline:** `NeedForTransport` + reservations + `TimeOfTransport` can drive **`State_Poptavka_Transporty` → `State_Rezervace` → `State_Transport`** without `AddRecipe`.
- **Yes for the wrong-cell path:** injection **creates** that explicit **`Transport`** step so the state machine can run the normal transport/recipe machinery when the glass shows up at an unexpected cell.

---

## 8. Best patch points for minimal change

1. **`State_Mimo_Polozena` — legacy `if / else if` chain** (approximately lines 470–526)  
   Single place where **every** path into `RAFnext` for `State_Poptavka_Vyroba`, `State_Poptavka_Transporty`, `State_Rezervace`, or `State_Transport` is decided from tag + `MyCellInfo`. Adjusting **order** or **conditions** here changes global transport triggering with no change to lower states.

2. **`State_Poptavka_Vyroba` — `NeedForTransport` assignment** (lines ~612–620)  
   Only location that derives **`NeedForTransport`** from “winning process cell vs my cell.” Patching here directly changes when the machine **requires** `State_Poptavka_Transporty` vs going straight to **`State_Rezervace`**.

3. **AAS branch — `REQUEST_TRANSPORT` handoff** (lines ~457–464)  
   Where non-`ToStorageGlass` types jump to **`State_Inicializace_ZiskaniAdres` / `State_Poptavka_Vyroba`**. Any fix for “AAS completed logically but tag still on old step” or “force reservation refresh” would likely start here **or** immediately after PLC success in the `ToStorageGlass` block (lines ~442–455) if the issue is step indexing vs PLC.

---

## 9. Final conclusion

- **Legacy process completion** happens in **`State_Vyroba_SpravneProvedeni`**: the current step gets **`IsStepDone`**, then **`ActualRecipeStep`** moves to **`NextID`** (or recipe ends). **No transport decision** occurs there.
- **Transport is triggered** on a **later** **`State_Mimo_Polozena`** evaluation using **`sRecipeStep[ActualRecipeStep]`**: combinations of **`TimeOfTransport`**, **`TransportCellReservationID`**, **`ProcessCellReservationID`**, **`NeedForTransport`**, **`IsProcess` / `IsTransport`**, and **cell ID vs `MyCellInfo.IDofCell`** select **`State_Poptavka_Transporty`**, **`State_Rezervace`**, or **`State_Transport`** (via **`State_Inicializace_ZiskaniAdres`** + **`RAFnext`**).
- **`NeedForTransport`** means “assigned **process** cell is not here”; it is set in **`State_Poptavka_Vyroba`** and consumed in idle routing and **`State_Poptavka_Transporty`** / **`State_Transport`**.
- **`NextID`** is read from the **step being finished** (or from the transport step in **`State_Vyroba_Objeveni`**), and **`ActualRecipeStep`** is updated **before** the next transport routing pass in the legacy path.
- **`AddRecipe` + `GetRecipeStepByNumber(10, …)`** is a **recovery / injection** path when the tag appears at the **wrong** cell relative to **`ProcessCellID`**, inserting an explicit **`Transport`**-typed step; it is **not** the only way transport runs.
- **Highest-risk mismatches** for “transport never fires”: wrong **`NextID`** after completion, **`NeedForTransport` false** while **`ProcessCellReservationID`** is set (skips **`State_Poptavka_Transporty`**), missing **`TimeOfTransport`/reservations** when the state machine expects them, or **AAS** paths that advance or do not advance **`ActualRecipeStep`** inconsistently with what the legacy chain expects on the next read.

---

*Analysis only; no firmware code was modified for this report.*
