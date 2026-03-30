# NFC Verify First Mismatch Debug Patch

## 1. Why previous logs were missing

The previously added mismatch diagnostics were emitted through `NFC_READER_ALL_DEBUG(...)` inside `NFC_CheckStructArrayIsSame(...)`. In this firmware build, `NFC_READER_ALL_DEBUG_EN` is not enabled, so those lines are compiled out and never printed at runtime.

As a result, active verify retries in `NFC_WriteCheck(...)` could still return `res=1` repeatedly, but the byte-level mismatch details remained invisible.

## 2. Modified files

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`

## 3. Added guaranteed mismatch logs

Minimal debug-only additions were made in the active verify path:

- Added a small one-shot mismatch capture state (`s_verifyMismatchInfo`) in `NFC_reader.c`.
- In `NFC_CheckStructArrayIsSame(...)`, on the first detected mismatch, capture:
  - compare range (`NumOfStructureStart`-`NumOfStructureEnd`)
  - structure index being compared
  - first mismatch byte index
  - expected byte
  - actual byte
- For `TRecipeInfo` mismatch, also capture:
  - expected/actual `RecipeSteps`
  - expected/actual `CheckSum`
  - `mismatchInCheckSum` flag
- For step mismatch, also capture:
  - `stepIndex`
  - `stepByteIndex`
  - expected byte
  - actual byte
- In `NFC_WriteCheck(...)`, reset capture at function entry and print captured mismatch exactly once only when the whole verify fails (`return 1` after retries).

This guarantees visibility without changing write/verify business logic and avoids flooding during inner retry loops.

## 4. Expected new runtime output

On final verify failure (`NFC_WriteCheck` exit with `res=1`), output will include one of:

- Info mismatch:
  - `[NFC_VERIFY_MISMATCH] compareFn=NFC_CheckStructArrayIsSame range=... struct=... firstMismatchIndex=... expectedByte=... actualByte=...`
  - `[NFC_VERIFY_MISMATCH] expectedRecipeSteps=... actualRecipeSteps=... expectedCheckSum=... actualCheckSum=... mismatchInCheckSum=...`

- Step mismatch:
  - `[NFC_VERIFY_MISMATCH] compareFn=NFC_CheckStructArrayIsSame range=... struct=... stepIndex=... stepByteIndex=... expectedByte=... actualByte=...`

## 5. Notes

- Patch is debug-focused and minimal.
- No architectural refactor.
- No business logic changes.
- No write/verify algorithm changes.
- Existing timing logs remain unchanged.
