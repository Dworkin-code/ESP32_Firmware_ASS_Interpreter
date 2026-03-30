# NFC High-Level Verify Read Trace

## 1. Verify call chain

High-level verify after write goes through this chain:

1. `NFC_Handler_WriteSafeInfo(...)` in `components/NFC_Handler/NFC_handler.c`
   - sets `aHandlerData->sIntegrityCardInfo.sRecipeInfo = *aRecipeInfo`
   - calls:
2. `NFC_WriteCheck(&aHandlerData->sNFC, &aHandlerData->sIntegrityCardInfo, 0, 0)` in `components/NFC_Reader/NFC_reader.c`
   - write phase:
3. `NFC_WriteStructRange(aNFC, aCardInfo, 0, 0)`
   - low-level write done by:
     - `pn532_mifareultralight_WritePage(...)` or
     - `pn532_mifareclassic_WriteDataBlock(...)`
   - immediate low-level read-back debug runs here (already confirmed OK)
   - verify phase:
4. `NFC_CheckStructArrayIsSame(aNFC, aCardInfo, 0, 0)`
   - for `i == 0`:
5. `NFC_LoadTRecipeInfoStructure(aNFC, &idataNFC1)`
6. byte compare:
   - expected: `aCardInfo->sRecipeInfo` (from `sIntegrityCardInfo`)
   - actual: `idataNFC1.sRecipeInfo` (freshly loaded buffer)
   - mismatch is reported here (`firstMismatchIndex=4`, `expected=1`, `actual=0`)

## 2. Function that reads actual data for verify

The actual data used by verify is read by:

- **Function:** `NFC_LoadTRecipeInfoStructure(...)`
- **File:** `components/NFC_Reader/NFC_reader.c`
- **Call site (verify):** `NFC_CheckStructArrayIsSame(...)` when `i == 0`
- **Arguments at call:** `(aNFC, &idataNFC1)`
- **Target buffer:** `idataNFC1.sRecipeInfo` (local `TCardInfo idataNFC1` inside compare function)

So verify does not compare against `sIntegrityCardInfo` as "actual"; it compares against local `idataNFC1` filled by `NFC_LoadTRecipeInfoStructure`.

## 3. Actual TRecipeInfo data path

For `range=0-0`, this is the exact "actual" path:

1. `NFC_CheckStructArrayIsSame(...)` creates local `TCardInfo idataNFC1;`
2. `NFC_InitTCardInfo(&idataNFC1);`
3. `NFC_LoadTRecipeInfoStructure(aNFC, &idataNFC1);`
   - low-level reads from tag:
     - Classic: `pn532_mifareclassic_ReadDataBlock(...)`
     - Ultralight: `pn532_mifareultralight_ReadPage(...)`
   - bytes copied into `idataNFC1.sRecipeInfo` via byte-wise assignment
   - sets `idataNFC1.TRecipeInfoLoaded = true` and returns `0` on success paths
4. Compare loop in `NFC_CheckStructArrayIsSame(...)`:
   - expected byte source: `((uint8_t *)&aCardInfo->sRecipeInfo)[j]`
   - actual byte source: `((uint8_t *)&idataNFC1.sRecipeInfo)[j]`

This confirms the "actual" byte 4 is whatever `NFC_LoadTRecipeInfoStructure` produced in `idataNFC1.sRecipeInfo`.

## 4. Buffer/structure instances involved

- **Expected side in verify:**
  - `aCardInfo->sRecipeInfo`
  - for your path this is `aHandlerData->sIntegrityCardInfo.sRecipeInfo` (passed into `NFC_WriteCheck`)
- **Actual side in verify:**
  - `idataNFC1.sRecipeInfo` (local temporary inside compare)
- **Other handler buffers (not actual compare target):**
  - `aHandlerData->sWorkingCardInfo.sRecipeInfo`
  - `aHandlerData->sIntegrityCardInfo.sRecipeInfo`

Important: `NFC_CheckStructArrayIsSame` does **not** read actual bytes from `sIntegrityCardInfo` or `sWorkingCardInfo`; it reads into local `idataNFC1`.

## 5. Possible stale-data points

Primary stale/invalid-data risks in current verify read path:

1. `NFC_LoadTRecipeInfoStructure(...)` has a branch where UID length is neither 4 nor 7:
   - it only logs (`"Na kartu z karty precist hodnoty"`), but does **not** return error
   - function then still marks loaded and returns `0`
   - this allows compare to use whatever remained in `aCardInfo->sRecipeInfo` (for `idataNFC1`, usually zeros from init)
2. `NFC_CheckStructArrayIsSame(...)` retry loop does not re-init `idataNFC1` between retries
   - if a read path is partial/invalid-but-returned-0, stale bytes can survive in local buffer state
3. verify read does a fresh `pn532_readPassiveTargetID(...)` again and does not enforce UID equality with just-written context
   - no explicit guard that verify read is from the same tag identity

Lower-probability stale risks (present but less aligned with your symptom):

- partial copy in read function if a low-level API behaved unexpectedly while still reporting success
- concurrent caller reusing PN532/tag session (no explicit locking in these paths)

## 6. Most likely direct cause

Given your evidence:

- low-level write succeeded
- immediate low-level read-back after write showed no mismatch
- later high-level verify still reports byte 4 old value

the most likely direct cause in the higher-level path is:

`NFC_LoadTRecipeInfoStructure(...)` can return success (`0`) without guaranteeing a valid/fresh `TRecipeInfo` payload for compare in all branches (notably unsupported/unexpected UID-length branch and weak success validation).  

That leads `NFC_CheckStructArrayIsSame(...)` to compare against `idataNFC1.sRecipeInfo` that can still contain old/zero byte values, producing exactly the observed "first mismatch at byte 4".

## 7. Best minimal fix point

Best minimal fix point is **inside `NFC_LoadTRecipeInfoStructure(...)`** (single choke point for verify actual info reads):

- enforce strict success criteria:
  - if UID length is not expected/supported for your tags, return read error (not success)
  - return success only after all required bytes of `TRecipeInfo` are filled
- optionally clear destination info structure at function start for deterministic behavior
- optionally validate UID consistency in verify path

This is safer and smaller than changing higher-level compare logic.

## 8. Final conclusion

The verify "actual" `TRecipeInfo` is obtained from local `idataNFC1` inside `NFC_CheckStructArrayIsSame(...)`, filled by `NFC_LoadTRecipeInfoStructure(...)`.  

So the mismatch (`byte 4 expected=1, actual=0`) is not coming from the low-level write itself; it is most consistent with the high-level read/validation path accepting an invalid/insufficient read as success and comparing against stale/default bytes in `idataNFC1.sRecipeInfo`.
