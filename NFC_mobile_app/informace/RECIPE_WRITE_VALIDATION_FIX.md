## 1. Bug description

Writing a recipe to an NFC recipe tag (Ultralight / NTAG or Mifare Classic) was incorrectly blocked by PLC AAS cell profile validation.  
`WriteTagActivity` validated the step values (TypeOfProcess, ParameterProcess1, ParameterProcess2) against the active PLC AAS cell profile before writing the tag.  
If any step value was outside the configured AAS ranges, the app refused to write, even though recipe tags are only storage and are not PLC execution requests.

Example user-facing error:

- **Message**: `Refusing to write: step 0 values outside active profile 'Current Testbed Cell (PLC AAS)'`

## 2. PLC AAS validation vs. recipe tag validation

- **PLC AAS validation (execution domain)**
  - **Purpose**: Validate parameters before creating / sending PLC execution or reservation requests.
  - **Fields**:
    - `material` → NFC `TypeOfProcess`
    - `supportA` → NFC `ParameterProcess1`
    - `supportB` → NFC `ParameterProcess2`
  - **Logic**:
    - Check that `material` is inside the active cell profile `materialRange`.
    - Check that `supportA` is inside `supportARange`.
    - Check that `supportB` is inside `supportBRange`.
  - **Scope**:
    - Must be applied **only** when constructing PLC-level commands (e.g. AAS GetSupported/ReserveAction flows).
    - Not tied to physical NFC tag storage capacity.

- **Recipe tag validation (storage domain)**
  - **Purpose**: Ensure that the binary recipe data can be safely stored on the NFC tag and passes internal recipe consistency checks.
  - **Fields**:
    - `TypeOfProcess`
    - `ParameterProcess1`
    - `ParameterProcess2`
    - Header fields (ID, RecipeSteps, RightNumber, CheckSum, etc.)
  - **Logic (allowed for tag write)**:
    - Recipe fits into tag capacity:
      - `encodedRecipeSize = HEADER_SIZE + stepCount * STEP_SIZE`
      - `encodedRecipeSize <= usableRecipeBytes` for the specific tag.
    - Step count:
      - `stepCount <= capacity.maxRecipeSteps`.
    - Encoded recipe size:
      - `encodedRecipeSize <= usableRecipeBytes` (already enforced in `WriteTagActivity`).
    - Header fields valid for encoding:
      - `RightNumber = 255 - ID` (recomputed in `encodeRecipe()`).
      - `CheckSum` recomputed from step bytes via `computeStepChecksum()`.
    - Integrity / checksum on verification:
      - After write, `decodeRecipe()` is used and `checksumValid` / `integrityValid` are checked.
  - **Scope**:
    - Applies whenever writing recipe data to NFC tags, regardless of PLC cell profile configuration.

The fix ensures that **PLC AAS validation is not used in the recipe-tag write path**, but remains available for PLC execution logic.

## 3. Files modified

- `android-app/app/src/main/java/com/testbed/nfcrecipetag/ui/WriteTagActivity.kt`
- `informace/RECIPE_WRITE_VALIDATION_FIX.md` (this report)

## 4. Validation logic before the fix

**Recipe-tag write flow prior to the change (`WriteTagActivity.performWrite`)**:

1. Resolve tag capacity via `resolveTagCapacity(dump.metadata)`:
   - `usableRecipeBytes`
   - `maxRecipeSteps`
   - `tagType`
2. Compute encoded recipe size:
   - `encodedRecipeSize = HEADER_SIZE + steps.size * STEP_SIZE`.
3. Basic capacity checks:
   - If `encodedRecipeSize > usableRecipeBytes` **or**
   - `steps.size > maxRecipeSteps`
   - → Block write with "Recipe too large for this tag".
4. **PLC AAS cell profile validation (buggy for tag writes)**:
   - Call `getActiveCellProfile()`.
   - Determine the step index:
     - `stepIndex = header.actualRecipeStep.coerceIn(0, steps.lastIndex.coerceAtLeast(0))`.
   - Map NFC fields to AAS parameters:
     - `material = step.typeOfProcess`
     - `supportA = step.parameterProcess1`
     - `supportB = step.parameterProcess2`
   - Log active cell profile ranges and AAS-mapped values.
   - Call `profile.validateStepValues(material, supportA, supportB)`:
     - If **false**, block write:
       - UI text: **"Refusing to write: step X values outside active profile 'Current Testbed Cell (PLC AAS)'"**
       - Toast: **"Invalid AAS parameters for current cell profile."**
       - NFC reader mode disabled, write aborted.
5. If PLC AAS validation passed:
   - Encode recipe bytes via `encodeRecipe(header, steps, ByteArray(0))`.
   - Write to tag (`writeUltralightRecipeBytes` / `writeClassicRecipeBytes`).
   - Re-read and verify via `decodeRecipe()`, `checksumValid`, `integrityValid`.

Because of step 4, recipes whose parameters were outside the PLC AAS ranges were refused **even though they were valid as stored recipe data** and fit the tag capacity.

## 5. Validation logic after the fix

**Updated recipe-tag write flow (`WriteTagActivity.performWrite`)**:

1. Resolve tag capacity:
   - `capacity = resolveTagCapacity(dump.metadata)`.
   - `usableRecipeBytes = capacity.usableRecipeBytes`.
   - `maxRecipeSteps = capacity.maxRecipeSteps`.
2. Compute encoded recipe size:
   - `encodedRecipeSize = HEADER_SIZE + steps.size * STEP_SIZE`.
3. Log recipe write context:
   - Tag type (`scannedTagType` and `storedMetadataType`).
   - `usableRecipeBytes`.
   - `maxRecipeSteps`.
   - `encodedRecipeSize`.
   - `stepCount`.
4. Tag type consistency check:
   - If `writeTagType != dump.metadata.tagType`:
     - Block write with user-visible message about tag type mismatch.
     - Debug log:
       - `writeAllowed=false reason=TAG_TYPE_MISMATCH` including tag type, `usableRecipeBytes`, `encodedRecipeSize`, `stepCount`.
5. Capacity checks (recipe-tag validation only):
   - If `encodedRecipeSize > usableRecipeBytes` **or**
   - `steps.size > maxRecipeSteps`
   - → Block write:
     - UI message about max steps / bytes.
     - Toast "Recipe too large for this tag".
     - Debug log:
       - `writeAllowed=false reason=RECIPE_TOO_LARGE` including tag type, `usableRecipeBytes`, `encodedRecipeSize`, `maxRecipeSteps`, `stepCount`.
6. **No PLC AAS cell profile validation**:
   - The block that:
     - Called `getActiveCellProfile()`.
     - Derived `material`, `supportA`, `supportB` from step fields.
     - Checked `profile.validateStepValues(...)`.
   - **Has been removed from the write path.**
   - No AAS range checks are performed when writing to tags.
7. Recipe-level validation before write:
   - Log that validation passed using **recipe-level checks only**:
     - `writeAllowed=true` with tag type, `usableRecipeBytes`, `encodedRecipeSize`, `stepCount`.
8. Encoding and write:
   - Backup existing tag (`BackupManager`) if readable.
   - Encode bytes with `encodeRecipe(decoded.header, decoded.steps, ByteArray(0))`.
   - Call `writeUltralightRecipeBytes` or `writeClassicRecipeBytes` depending on `tagType`.
   - For unsupported tag types:
     - Block write with user-visible message.
     - Debug log:
       - `writeAllowed=false reason=UNSUPPORTED_TAG_TYPE` with tag type and capacity details.
9. Post-write verification:
   - Re-read into a new dump.
   - Compare written bytes vs. re-read bytes.
   - Decode via `decodeRecipe()` and evaluate:
     - `checksumValid`
     - `integrityValid`
   - Log outcomes:
     - `writeCompleted=true verifyRead=false` if re-read failed.
     - Or `writeCompleted=true verifyRead=true success=... bytesMatch=... checksumOk=... integrityOk=...` with tag type and size context.
   - On low-level write failure (`ok == false`):
     - UI: "Write failed."
     - Debug log:
       - `writeCompleted=false reason=LOW_LEVEL_WRITE_FAILURE` including tag type and capacity details.

With this change, **only recipe storage constraints** (capacity, step count, encoding, checksum / integrity on verification) govern whether a tag write is allowed.  
PLC AAS cell profile ranges are no longer consulted for recipe-tag writes.

## 6. Example debug output

Example log sequence for a successful Classic recipe write with 2 steps:

```text
D/WriteTagActivity: performWrite: scannedTagType=CLASSIC, storedMetadataType=CLASSIC, usableRecipeBytes=736, maxRecipeSteps=23, encodedRecipeSize=74 bytes, stepCount=2
D/WriteTagActivity: performWrite: validation passed (recipe-level only); writeAllowed=true tagType=CLASSIC usableRecipeBytes=736 encodedRecipeSize=74 stepCount=2
D/WriteTagActivity: performWrite: encodedRecipeSize=74, unknownTailSize=0, finalStreamSize=74, tagType=CLASSIC
D/WriteTagActivity: performWrite: starting Classic write, bytes=74
D/WriteTagActivity: performWrite: writeCompleted=true verifyRead=true success=true bytesMatch=true checksumOk=true integrityOk=true tagType=CLASSIC usableRecipeBytes=736 encodedRecipeSize=74 stepCount=2
```

Example log sequence for a blocked write due to tag type mismatch:

```text
D/WriteTagActivity: performWrite: scannedTagType=CLASSIC, storedMetadataType=ULTRALIGHT_NTAG, usableRecipeBytes=144, maxRecipeSteps=4, encodedRecipeSize=74 bytes, stepCount=2
D/WriteTagActivity: performWrite: writeAllowed=false reason=TAG_TYPE_MISMATCH tagType=CLASSIC storedMetadataType=ULTRALIGHT_NTAG usableRecipeBytes=144 encodedRecipeSize=74 stepCount=2
```

Example log sequence for a blocked write due to recipe too large:

```text
D/WriteTagActivity: performWrite: scannedTagType=ULTRALIGHT_NTAG, storedMetadataType=ULTRALIGHT_NTAG, usableRecipeBytes=144, maxRecipeSteps=4, encodedRecipeSize=200 bytes, stepCount=6
D/WriteTagActivity: performWrite: writeAllowed=false reason=RECIPE_TOO_LARGE tagType=ULTRALIGHT_NTAG usableRecipeBytes=144 encodedRecipeSize=200 maxRecipeSteps=4 stepCount=6
```

## 7. Example successful write scenario

**Scenario**:

- **Recipe**:
  - `ID = 5`
  - `Steps = 2`
- **Layout constants**:
  - `HEADER_SIZE = 12`
  - `STEP_SIZE = 31`
- **Encoded size**:
  - `encodedRecipeSize = HEADER_SIZE + 2 × STEP_SIZE = 12 + 2 × 31 = 74 bytes`
- **Classic tag capacity**:
  - `usableRecipeBytes = 736 bytes`

**Result after fix**:

- Capacity checks:
  - `74 <= 736` → OK.
  - `stepCount = 2 <= maxRecipeSteps` (e.g. 23) → OK.
- No PLC AAS cell profile validation is applied in the write path.
- `encodeRecipe()` computes `RightNumber` and `CheckSum` based on the recipe header and steps.
- Write proceeds via `writeClassicRecipeBytes`, followed by re-read and successful verification (`bytesMatch`, `checksumValid`, `integrityValid`).
- User sees:
  - "Write OK. Verification passed (bytes match, checksum and integrity OK)."

Thus, a valid 2-step recipe that fits within the Classic tag capacity is now written successfully, independent of the PLC AAS cell profile ranges.

