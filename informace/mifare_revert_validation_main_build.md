# MIFARE Classic 1K validation focus in main build

## What should now work again

- `NFC_WriteCheck(...)` no longer reduces outer retries for 7-byte UID tags; fixed retry behavior is restored.
- `NFC_Handler_Sync(...)` and `NFC_Handler_WriteSafeInfo(...)` use fixed retry count again, matching classic minimal-safe behavior.
- `NFC_WriteStructRange(...)` no longer enforces NTAG213-specific page limit guard, so write flow no longer aborts on that condition.
- Classic auth/read/write flow remains intact and unchanged (4-byte UID path with MIFARE Classic block auth/write/readback logic).

## Hardware tests to run on this main build

1. MIFARE Classic 1K baseline write/read
   - Load known recipe into working data.
   - Execute `NFC_Handler_WriteSafeInfo(...)`, `NFC_Handler_WriteStep(...)`, `NFC_Handler_Sync(...)`.
   - Verify full readback with `NFC_LoadAllData(...)` and checksum consistency.

2. Repeated write stability test
   - Perform multiple consecutive writes on same Classic 1K tag.
   - Confirm no premature failure due to reduced retry profile.

3. Step progression write-back test
   - Run one process step end-to-end in runtime state machine.
   - Confirm `ActualRecipeStep`, `IsStepDone`, and `RecipeDone` persist correctly on tag.

4. AAS/cross-cell regression smoke test
   - Confirm ReportProduct/GetSupported/ReserveAction flow still works.
   - Confirm process-owner routing and cross-cell handover still behaves as before.

5. Optional NTAG smoke test
   - Verify whether current NTAG cards still behave acceptably in your deployment after guard removal.

## `main/app.c` change status

- `main/app.c` was intentionally left untouched.
- No direct proof was identified in this exact build version that `MAX_RECIPE_STEPS 64` or `NFC_IsRecipeEmpty(...)` blocks valid legacy MIFARE Classic recipes.
