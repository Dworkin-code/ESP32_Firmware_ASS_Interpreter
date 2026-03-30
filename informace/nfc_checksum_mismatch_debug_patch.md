# NFC Checksum Mismatch Debug Patch

## 1. Modified files
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`

## 2. Added logs
- In `NFC_CheckStructArrayIsSame(...)` for `i == 0` mismatch path:
  - byte mismatch index `j`
  - `expectedByte` (from in-memory `aCardInfo->sRecipeInfo`)
  - `actualByte` (from NFC-loaded `idataNFC1.sRecipeInfo`)
  - expected vs actual `RecipeSteps`
  - expected vs actual `CheckSum`
  - `mismatchInCheckSum` flag (whether `j` is inside `CheckSum` field byte range)
- In `NFC_Handler_WriteSafeInfo(...)`, immediately before `NFC_WriteCheck(..., 0, 0)`:
  - old `sIntegrityCardInfo.sRecipeInfo.RecipeSteps`
  - new `aRecipeInfo->RecipeSteps`
  - `sIntegrityCardInfo.TRecipeStepArrayCreated`
  - `sIntegrityCardInfo.sRecipeStep` pointer
- In `NFC_WriteStructRange(...)`, around checksum recomputation:
  - `NumOfStructureStart`
  - `NumOfStructureEnd`
  - `aCardInfo->sRecipeInfo.RecipeSteps`
  - old stored checksum (`oldCheckSum`)
  - newly computed checksum (`newCheckSum`)

## 3. What the logs should prove
- Whether info-only write/verify mismatch is primarily at the `CheckSum` bytes.
- Whether computed checksum changes during `NFC_WriteStructRange(...)` for `range 0-0`.
- Whether write-safe info path is invoking `NFC_WriteCheck(...)` while step-array state (`RecipeSteps`, array-created flag, pointer) indicates possible inconsistency.

## 4. Expected runtime indicators
- `DBG checksum precompute ... oldCheckSum=...` and `DBG checksum computed newCheckSum=...` differ in `range=0-0` runs.
- Verify mismatch line `DBG info mismatch ... mismatchInCheckSum=1` appears, with expected/actual checksum values differing.
- `DBG pre-write-check ...` shows transition in `RecipeSteps` and step buffer state that correlates with the checksum mismatch timing.
