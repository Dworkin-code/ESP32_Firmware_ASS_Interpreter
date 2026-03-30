# Shaker Step Support — Implementation Note

## Summary

The mobile NFC app was extended to create, edit, and write recipe steps that the reader firmware interprets as **Shaker with duration** (TypeOfProcess = 3, ParameterProcess1 = duration in seconds). No redesign; minimal practical changes only.

---

## Files changed

| File | Change |
|------|--------|
| `android-app/.../core/tagmodel/ProcessTypes.kt` | **New.** Process type constants (Shaker = 3), display names, and `createStep()` helper. |
| `android-app/.../core/tagmodel/DecodedRecipe.kt` | Added `createTestRecipeShaker5(unknownTail)` for the built-in test recipe. |
| `android-app/.../res/layout/item_step_editor.xml` | **New.** One step row: process type Spinner, Duration [s] (visible for Shaker). |
| `android-app/.../res/layout/activity_edit_recipe.xml` | Added “Steps” section and `step_editors_container` for dynamic step rows. |
| `android-app/.../res/layout/activity_tag_detail.xml` | Added “Load test recipe (Shaker 5 s)” button. |
| `android-app/.../res/values/strings.xml` | Added `load_test_recipe_shaker`. |
| `android-app/.../ui/EditRecipeActivity.kt` | Step editor: build step rows from count, process type spinner, duration for Shaker; build `newSteps` and sync `header.recipeSteps` on Done. |
| `android-app/.../ui/TagDetailActivity.kt` | “Load test recipe” sets `decoded = createTestRecipeShaker5(...)`, `refreshDecodedUi()`. |
| `android-app/.../ui/WriteTagActivity.kt` | After write: re-read tag, compare written bytes with expected, show clear success/failure (bytes match + checksum + integrity). |
| `android-app/.../ui/StepsAdapter.kt` | Display process type name and, for Shaker, “Shaker N s”. |
| `NFC_mobile_app/tag_format_spec.md` | New subsection 6.1: Shaker = TypeOfProcess 3, ParameterProcess1 = duration; “case 6” is not the tag value. |

---

## What was implemented

1. **UI / form**
   - Process type dropdown (all ProcessTypes from firmware).
   - When process type = Shaker, “Duration [s]” field (0–255).
   - Step editors created from current step count; changing step count rebuilds rows.

2. **Data model**
   - Uses existing `RecipeStep`: `typeOfProcess = 3` for Shaker, `parameterProcess1 = duration`. No “case 6” or extra field.

3. **Encoding**
   - Existing `encodeRecipe()` / `encodeStep()`: byte 2 = TypeOfProcess, byte 3 = ParameterProcess1; header.recipeSteps = steps.size; RightNumber and checksum recomputed.

4. **Write flow**
   - Read tag → save backup → encode → write allowed Ultralight pages → re-read → compare written bytes with expected → show success/failure (bytes match, checksum, integrity).

5. **Test template**
   - “Load test recipe (Shaker 5 s)” on Tag detail: one-step recipe, Shaker, 5 s. User can then Edit or Write to tag.

6. **Documentation**
   - `tag_format_spec.md` §6.1: Shaker = 3, ParameterProcess1 = duration; “case 6” in firmware is not the value on the tag.

---

## How to create a Shaker 5 s tag in the app

1. **Enable writing:** Settings → Write enabled → ON.
2. **Scan a tag** (NTAG/Ultralight) → Tag detail opens.
3. **Load test recipe:** Tap “Load test recipe (Shaker 5 s)”. Summary shows one step: Shaker 5 s.
4. **Optional:** Tap “Edit recipe” to change duration or other header/step fields.
5. **Write:** Tap “Write to tag” → Confirm → hold tag to phone. App backs up current tag, writes recipe, re-reads and verifies (bytes + checksum + integrity).
6. **Success:** “Write OK. Verification passed (bytes match, checksum and integrity OK).”

To create a custom Shaker duration (e.g. 10 s): after scanning (or after loading test recipe), tap “Edit recipe”, set Step 1 process type to “Shaker”, set “Duration [s]” to 10, Done → Write to tag.

---

## What still needs real hardware testing

- **Reader firmware:** Place written tag on ESP32 reader; confirm log shows Shaker step with correct duration (e.g. “Protrepani o dobe trvani 5 s”) and that the machine runs the step.
- **NTAG213 vs STEP_SIZE:** App uses STEP_SIZE = 32; if firmware `sizeof(TRecipeStep)` differs, step layout may need adjustment (see ENCODING_SHAKER_STEP_REPORT.md).
- **Multiple steps:** Editing and writing recipes with 2+ steps (e.g. Shaker then another type) exercised only in app; verify on reader if needed.

---

## Fix: Step type not preserved after encode/decode (Shaker showing as ToStorageGlass)

### Root cause

1. **Process type display mapping was wrong:** The app used 0=ToStorageGlass, 1=StorageAlcohol, 2=StorageNonAlcohol, 3=Shaker. The required mapping is 0=Unknown, 1=ToStorageGlass, 2=FromStorageGlass, 3=Shaker. If the decoded value was ever 0 (e.g. from reading the wrong byte or an uninitialized tag), the UI showed "ToStorageGlass" instead of "Unknown" or the intended type.
2. **Encoder could overwrite step bytes:** In `encodeStep`, the first four bytes (ID, NextID, TypeOfProcess, ParameterProcess1) were switched to absolute `put(index, byte)`. The following sequential `putShort`/`put` calls then wrote from buffer position 0, overwriting those bytes. So TypeOfProcess (and ID, NextID, param1) could be corrupted in the encoded stream. This was fixed by setting `buf.position(4)` after the four absolute puts.

### Files changed in this fix

| File | Change |
|------|--------|
| `ProcessTypes.kt` | Mapping updated: 0=Unknown, 1=ToStorageGlass, 2=FromStorageGlass, 3=Shaker; added UNKNOWN, FROM_STORAGE_GLASS. |
| `RecipeCodec.kt` | Added STEP_OFFSET_* constants; decode/encode use STEP_OFFSET_TYPE_OF_PROCESS (2) and STEP_OFFSET_PARAMETER_PROCESS1 (3); encodeStep sets position(4) after first four bytes; debug logging (tag RecipeCodec). |
| `StepsAdapter.kt` | Debug logging: Decoded TypeOfProcess and displayed step type (tag StepsAdapter). |

### Confirmation

- **Encoder:** Writes `step.typeOfProcess` at step byte offset 2 via `buf.put(STEP_OFFSET_TYPE_OF_PROCESS, step.typeOfProcess.toByte())`. For Shaker, that is 3.
- **Decoder:** Reads TypeOfProcess from step byte offset 2 via `buf.get(STEP_OFFSET_TYPE_OF_PROCESS)`. Value 3 is then passed to `ProcessTypes.name(3)` → "Shaker".
- **Display:** StepsAdapter shows `ProcessTypes.name(s.typeOfProcess)`, so 3 → "Shaker".
