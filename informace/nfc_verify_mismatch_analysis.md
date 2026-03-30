# NFC Verify Mismatch Analysis

## 1. Verification function location

- Main function: `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c` -> `NFC_CheckStructArrayIsSame(...)`.
- Signature: `uint8_t NFC_CheckStructArrayIsSame(pn532_t *aNFC, TCardInfo *aCardInfo, uint16_t NumOfStructureStart, uint16_t NumOfStructureEnd)`.
- Direct callers:
  - `NFC_WriteCheck(...)` in `NFC_reader.c` (verify phase after write).
  - `NFC_Handler_Sync(...)` in `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c` (fast pre-check for range `0-0` when nothing marked for write).

## 2. What data is compared

- The compare is **byte-by-byte**, not semantic/field-aware.
- For `i == 0` (info structure):
  - Expected: bytes of `aCardInfo->sRecipeInfo`.
  - Actual: bytes read from card into temporary `idataNFC1.sRecipeInfo` via `NFC_LoadTRecipeInfoStructure(...)`.
  - Loop: `for (j = 0; j < TRecipeInfo_Size; ++j)` with raw `uint8_t*` access.
- For `i > 0` (recipe step structures):
  - Expected: bytes from `aCardInfo->sRecipeStep[(i-1)]`.
  - Actual: bytes read from card into `idataNFC1.sRecipeStep[(i-1)]` via `NFC_LoadTRecipeStep(...)`.
  - Loop: `for (j = 0; j < TRecipeStep_Size; ++j)` with raw `uint8_t*` access.
- Range interpretation:
  - `0` means `TRecipeInfo`.
  - `1..RecipeSteps` means recipe step structs indexed `0..RecipeSteps-1`.
  - So `0-0` = only info.
  - `0-1` = info + first step.
  - `2-4` = step 1..3 (without info).

## 3. Meaning of verify result codes

From `NFC_CheckStructArrayIsSame(...)`:

- `0`: compared range is equal.
- `1`: **first mismatch found** in compared bytes (immediate return at first differing byte).
- `2`: invalid range index (start/end beyond `RecipeSteps`).
- `3`: read failure from card (after retry loop inside load function call).
- `4`: allocation failure for temporary step array.
- `5`: start > end.
- `6`: `aCardInfo->TRecipeInfoLoaded != true`.

What `res=1` means in your logs:

- In verify logs from `NFC_WriteCheck(...)`, `res=1` is a **generic data mismatch** (not timeout/read/auth failure).  
- It means write call itself returned success, but read-back bytes differ from expected buffer used for comparison.

## 4. Verification flow after WriteSafeInfo

Path:

1. `NFC_Handler_WriteSafeInfo(...)` updates `aHandlerData->sIntegrityCardInfo.sRecipeInfo = *aRecipeInfo`.
2. It calls `NFC_WriteCheck(&sNFC, &sIntegrityCardInfo, 0, 0)`.
3. `NFC_WriteCheck(...)`:
   - writes via `NFC_WriteStructRange(..., 0, 0)`,
   - then verifies via `NFC_CheckStructArrayIsSame(..., 0, 0)`.
4. For `0-0`, only `TRecipeInfo` bytes are compared.

Important internal behavior in write path:

- `NFC_WriteStructRange(...)` always recomputes checksum:
  - `CheckSumNew = NFC_GetCheckSum(*aCardInfo);`
  - if different, overwrites `aCardInfo->sRecipeInfo.CheckSum = CheckSumNew`.
- Therefore expected info bytes used in verify include potentially updated checksum.

Critical consistency risk here:

- In `NFC_Handler_WriteSafeInfo(...)`, `sRecipeInfo.RecipeSteps` may change **before** `sIntegrityCardInfo.sRecipeStep` buffer is resized (resize happens only later, after successful write).
- If `RecipeSteps > 0`, `NFC_GetCheckSum(...)` iterates `TRecipeStep_Size * RecipeSteps` bytes over `sRecipeStep`.
- If step array size/content no longer matches new `RecipeSteps`, checksum source bytes can be stale/out-of-range/inconsistent, producing expected info that does not match card read-back reliably.

## 5. Verification flow after Sync

Path in `NFC_Handler_Sync(...)` has two modes:

- No pending step writes (`zapis == false`):
  1. It first checks `NFC_CheckStructArrayIsSame(..., &sWorkingCardInfo, 0, 0)`.
  2. If mismatch (`1`), it runs `NFC_WriteCheck(..., &sWorkingCardInfo, 0, 0)`.
  3. If `NFC_WriteCheck` never reaches verify `0`, function returns `2`.

- Pending step writes (`zapis == true`):
  - It computes contiguous ranges and calls `NFC_WriteCheck(..., zacatek, konec)` per range.
  - Each call writes then verifies the exact structure range byte-by-byte.

Why your observed `NFC_Handler_Sync(...) -> 2` matches logs:

- `NFC_Handler_Sync` returns `2` for generic write/verify failure paths (`default` in post-`NFC_WriteCheck` switch).
- If `NFC_WriteCheck` keeps ending with verify mismatch (`res=1`) after retries, Sync exits with `2`.

## 6. Most likely mismatch causes

Ordered by likelihood from current code:

1. **Checksum computed from inconsistent step buffer during info-only write (`0-0`)**
   - `NFC_WriteStructRange` recomputes checksum from full `sRecipeStep` data even when writing only info.
   - `WriteSafeInfo` can change `RecipeSteps` before resizing/filling `sIntegrityCardInfo.sRecipeStep`.
   - This creates unstable/incorrect `CheckSum` byte in expected `TRecipeInfo`.

2. **Expected buffer source differs between phases (`sIntegrityCardInfo` vs `sWorkingCardInfo`)**
   - `WriteSafeInfo` uses `sIntegrityCardInfo` as expected source.
   - `Sync` may use `sWorkingCardInfo`.
   - If one buffer is not fully synchronized (especially steps/checksum coupling), compare can fail repeatedly.

3. **Range `0-0` still depends on step bytes indirectly**
   - Although verify compares only info bytes, `CheckSum` field inside info depends on all step bytes.
   - So an info-only verify can fail due to step-buffer inconsistency.

4. **Potential stale data in newly resized arrays**
   - `NFC_ChangeRecipeStepsSize` zero-fills new entries, but timing/order of resize relative to checksum recalculation is the issue, not packing.

Lower-likelihood causes:

- Struct packing/alignment mismatch is less likely (`TRecipeInfo` and `TRecipeStep` are `__attribute__((packed))`, compare/write/read all use raw bytes consistently).
- Endianness mismatch is unlikely here because both write/read use identical raw in-memory byte layout on same MCU.
- Wrong page range mapping appears symmetric in write vs read paths (same offsets and index mapping function used).

## 7. Most likely first mismatch location

Most likely first differing byte is inside `TRecipeInfo`, specifically:

- `sRecipeInfo.CheckSum` (last field of `TRecipeInfo`, declared as `uint16_t CheckSum; // musi byt vzdy posledni`).

Reason:

- This field is automatically recomputed from step buffer in write path.
- If `RecipeSteps` was changed but step buffer state is not yet consistent at checksum computation time, `CheckSum` is first high-risk byte pair to diverge.
- Since compare exits at first mismatch, this can repeatedly surface as `res=1` with long retry loops.

## 8. Best minimal debug points

Recommended minimal additions (no refactor, just targeted logs):

1. In `NFC_WriteStructRange(...)` just before checksum update:
   - log `NumOfStructureStart`, `NumOfStructureEnd`
   - log `aCardInfo->sRecipeInfo.RecipeSteps`
   - log `aCardInfo->TRecipeStepArrayCreated`
   - log pointer `aCardInfo->sRecipeStep`
   - log old/new checksum (`old CheckSum`, `CheckSumNew`)

2. In `NFC_CheckStructArrayIsSame(...)` at mismatch points:
   - for info mismatch (`i==0`):
     - log `j`, expected byte, actual byte
     - if `j` is within `CheckSum` offset, print both full `expected CheckSum` and `actual CheckSum`
   - for step mismatch (`i>0`):
     - log `i`, `j`, expected byte, actual byte

3. In `NFC_Handler_WriteSafeInfo(...)` before `NFC_WriteCheck(...,0,0)`:
   - log `tempData.RecipeSteps`, `new aRecipeInfo->RecipeSteps`
   - log `sIntegrityCardInfo.TRecipeStepArrayCreated`
   - log `sIntegrityCardInfo.sRecipeStep` pointer

4. In `NFC_WriteCheck(...)` around verify call:
   - when `Error==1`, print one short line with range and retry indices (already mostly present), plus explicit text `"verify mismatch (data differ), not read/auth failure"`.

Minimal byte dump to prove source:

- Dump only `TRecipeInfo` bytes for expected (`aCardInfo->sRecipeInfo`) and actual (`idataNFC1.sRecipeInfo`) when `range=0-0`.
- If too verbose, dump:
  - `RecipeSteps`,
  - `CheckSum` (2 bytes),
  - and `sizeof(TRecipeInfo)`.

## 9. Final conclusion

- The verification failure (`res=1`) is a **true byte mismatch after successful write**, not a low-level write/read timeout/auth problem.
- The compare logic is deterministic byte-by-byte and returns on first difference.
- The strongest code-level root cause is **checksum/data coupling during info-only writes**:
  - `NFC_WriteStructRange(...,0,0)` recomputes `CheckSum` from step array,
  - while `WriteSafeInfo` can temporarily have changed `RecipeSteps` before step buffer is resized/consistent.
- This can make expected `TRecipeInfo` (especially `CheckSum`) inconsistent, causing repeated verify mismatch loops and observed long runtimes in `WriteSafeInfo` and `Sync`.
