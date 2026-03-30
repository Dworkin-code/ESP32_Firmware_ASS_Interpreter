## NFC tag write crash fix

### Root cause

The write flow could throw uncaught exceptions inside `WriteTagActivity.performWrite`, most notably while encoding the recipe (`encodeRecipe`) or during low-level writes, which caused the activity to crash when the user pressed **Write to Tag**. In addition, the validation and logging around the write path were not strong enough to clearly report invalid header/step combinations or capacity issues before calling the NFC tech APIs.

### Files changed

- `android-app/app/src/main/java/com/testbed/nfcrecipetag/ui/WriteTagActivity.kt`

### Fixes applied

- Wrapped the entire `performWrite(tag: Tag)` body in a `try/catch/finally` block so that **all exceptions are caught** and the app never crashes during write. The NFC reader mode is always disabled in `finally`.
- Added explicit **header and step validation** before encoding or writing:
  - `dump` and `decoded` must be non-null.
  - `header.recipeSteps` must equal `steps.size`.
  - `steps.size` must be at least 1.
  - `header.actualRecipeStep` must satisfy `0 ≤ actualRecipeStep < steps.size`.
  - Scanned tag type must match the stored metadata tag type.
- Added a guarded `encodeRecipe` call:
  - If `encodeRecipe` throws, the error is caught, logged, shown to the user, and the write is aborted without touching the tag.
- Kept the **firmware-compatible format** unchanged: header (12 bytes) + `RecipeSteps * 31` bytes for steps; checksum and `RightNumber` are still computed inside `encodeRecipe`.

### Capacity validation

Capacity is computed via `resolveTagCapacity(metadata)` in `TagCapacityInfo.kt`:

- **Ultralight/NTAG**:
  - `usableRecipeBytes = NTAG213_USER_MEMORY` (144 bytes; pages 8..39).
- **MIFARE Classic**:
  - `usableRecipeBytes = metadata.memorySizeBytes` from `RawTagDump.bytes` (data blocks only).

From this, the app computes:

```kotlin
val encodedRecipeSize = HEADER_SIZE + steps.size * STEP_SIZE
val maxStepsAllowed = capacity.maxRecipeSteps
```

where:

```kotlin
maxSteps = floor((usableBytes - HEADER_SIZE) / STEP_SIZE)
```

Before any write starts, the following checks are enforced:

- `encodedRecipeSize <= capacity.usableRecipeBytes`
- `steps.size <= maxStepsAllowed`

If either fails, the write is **aborted safely** with a clear, user-visible message explaining that the recipe is too large for the current tag and showing the tag's maximum supported steps and usable bytes.

### Logging and diagnostics

Additional logging was added to `WriteTagActivity.performWrite` to make debugging easier:

- Tag UID in hex (`uid`).
- Scanned tag type and stored metadata tag type.
- `encodedRecipeSize`, `capacity.usableRecipeBytes`, `capacity.maxRecipeSteps`.
- `header.recipeSteps` and `steps.size`.
- Markers for:
  - validation start and success,
  - backup start,
  - encode start and failures,
  - low-level write start for Ultralight and Classic,
  - write success / failure,
  - verification success / failure.

On any error, the user sees a readable message in the UI, while the full technical error (including exception message and stack trace for unexpected failures) is logged via `Log.e` / `Log.d` for later analysis.

