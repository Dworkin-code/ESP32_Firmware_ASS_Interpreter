# Verification and safety – code reference and logic

This document records the exact logic for id/nextId, actualRecipeStep, and the verification flow.

---

## 1. Exact code: UnifiedRecipeCodec.kt

Full file content (no changes in this pass; shown for reference):

```kotlin
package com.testbed.nfcrecipetag.core.codec

import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.tagmodel.TagType
import com.testbed.nfcrecipetag.core.tagmodel.UnifiedRecipe

/**
 * Adapters between physical tag dumps and the unified recipe model.
 *
 * The underlying logical layout is always the same (TRecipeInfo + TRecipeStep[]),
 * only the mapping from NFC memory to the linear byte stream differs per tag type.
 * NfcTagReader already exposes that stream as RawTagDump.bytes, so these
 * helpers simply validate the tag type and decode using the existing codec.
 */

fun decodeUltralightToUnifiedRecipe(dump: RawTagDump): UnifiedRecipe? {
    if (dump.metadata.tagType != TagType.ULTRALIGHT_NTAG) return null
    val decoded: DecodedRecipe = decodeRecipe(dump.bytes) ?: return null
    return UnifiedRecipe(decoded)
}

fun decodeClassicToUnifiedRecipe(dump: RawTagDump): UnifiedRecipe? {
    if (dump.metadata.tagType != TagType.CLASSIC) return null
    val decoded: DecodedRecipe = decodeRecipe(dump.bytes) ?: return null
    return UnifiedRecipe(decoded)
}

/**
 * Encode a unified recipe back to the canonical firmware byte stream
 * (TRecipeInfo followed by TRecipeStep[]), including recomputed RightNumber
 * and CheckSum. Unknown tail bytes from the original dump are preserved.
 */
fun encodeUnifiedRecipeToCanonicalBytes(recipe: UnifiedRecipe): ByteArray {
    return encodeRecipe(recipe.header, recipe.steps, recipe.unknownTail)
}
```

---

## 2. id and nextId logic (recipe steps)

**Firmware behaviour (from app.c / NFC_recipes.c):**

- `TRecipeStep.ID` = step’s own identity (often equals position in array).
- `TRecipeStep.NextID` = index of the **next step to execute** after this one. Used when advancing: `tempInfo.ActualRecipeStep = tempStep.NextID`. For linear recipes the firmware sets `NextID = i+1` for non-last steps and `NextID = i` for the last step.

**Required app behaviour:**

- **Do not** overwrite `nextId` with the row index for every step (that would set every step’s next to itself and break flow).
- **Preserve** `id` and `nextId` from the decoded step when we are editing an **existing** step at that index.
- **New steps** (row index beyond original step count): set `id = index`, and for linear chain set `nextId = index + 1` if there is a next step, else `nextId = index` (last step points to self).

**Exact logic in `buildStepsFromEditors`:**

- For each row index `index` in `0 until maxCount`:
  - `existing = steps.getOrNull(index)` (original decoded steps).
  - If `existing != null`: use `id = existing.id`, `nextId = existing.nextId` (preserve from tag).
  - If `existing == null`: use `id = index`, `nextId = if (index < maxCount - 1) index + 1 else index`.

So: **id** and **nextId** are only taken from the decoded recipe for existing steps; for newly added steps we use a linear chain. They are never silently forced to the row index for all steps.

---

## 3. actualRecipeStep interpretation

**Firmware (app.c):**

- `ActualRecipeStep` is used as the **0-based index of the current step to execute** (or the step currently in progress).
- After a step completes: `ActualRecipeStep = curStep + 1`; when `ActualRecipeStep >= RecipeSteps`, the recipe is marked done.
- So it is **not** “last completed step” (that would be `actualRecipeStep - 1`). It is the **index of the step that is next to run** (or currently running).

**App interpretation:**

- **actualRecipeStep** = 0-based index of the **next step to execute** (or current step in progress).
- When `actualRecipeStep >= recipeSteps`, the recipe is considered finished (and `recipeDone` should be true).
- In the editor we clamp `actualRecipeStep` to `[0, steps.size - 1]` so it always refers to a valid step index when the user edits; the firmware will increment it as steps complete.

---

## 4. Safe preview before write

- **WriteTagActivity** receives:
  - `EXTRA_ORIGINAL_DECODED`: recipe as read from the tag before editing (optional).
  - `EXTRA_DECODED`: recipe to write (after edit).
- Before the user taps “Write”, the screen shows:
  - **Old recipe**: header summary, step count, stored checksum, integrity (ID+RightNumber=255).
  - **New recipe**: same for the edited recipe.
  - **Recalculated for write**: `rightNumber = 255 - id`, and checksum over the step bytes (from `encodeRecipe` / `computeStepChecksum`).
- User can verify and then confirm write. **Classic write remains disabled**; only Ultralight write is used.

---

## 5. Classic write

- **Disabled.** Only `writeUltralightRecipeBytes` is used. No code path writes to MifareClassic. Explicit comment in `WriteTagActivity` and no Classic write implementation.

---

## 6. Debug / test output

- **DebugRecipeActivity**: screen that shows for a given decoded recipe:
  - All header fields (id, numOfDrinks, recipeSteps, actualRecipeStep, actualBudget, parameters, rightNumber, recipeDone, checksum).
  - For each step: id, nextId, typeOfProcess, parameterProcess1/2, transport/process IDs, flags, raw values.
  - checksumValid, integrityValid.
  - Optional short hex of raw bytes.
- **Logcat**: same information logged with tag `RecipeDebug` when opening TagDetail (on decode) and when opening Debug recipe screen.

---

## 7. Exact code reference (key files)

### UnifiedRecipeCodec.kt (full file)

Location: `android-app/app/src/main/java/com/testbed/nfcrecipetag/core/codec/UnifiedRecipeCodec.kt`

- See the file: `decodeUltralightToUnifiedRecipe`, `decodeClassicToUnifiedRecipe`, `encodeUnifiedRecipeToCanonicalBytes` (no changes in this verification pass).

### EditRecipeActivity.kt (id/nextId and write flow)

- **buildStepsFromEditors**: id and nextId are taken from `existing` (decoded step) when present; for new steps: `id = index`, `nextId = if (index < maxCount - 1) index + 1 else index`.
- **Done button**: `WriteTagActivity.start(this, dump!!, decoded!!, edited)` so the write screen receives original (decoded!!) and to-write (edited).

### item_step_editor.xml

- Contains: step label, Process type Spinner, Parameter 1/2 rows, human summary TextView, transport/process IDs and prices EditTexts, four CheckBoxes (NeedForTransport, IsTransport, IsProcess, IsStepDone). id/nextId are not edited in the UI; they are preserved or set in code.
