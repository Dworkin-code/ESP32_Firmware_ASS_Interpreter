# NFC Load TRecipeInfo Fix

## 1. Modified files
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`
- `informace/nfc_load_trecipeinfo_fix.md`

## 2. Exact fix
- In `NFC_LoadTRecipeInfoStructure(...)`, `aCardInfo->sRecipeInfo` is cleared at function start using `memset(...)`, and `aCardInfo->TRecipeInfoLoaded` is reset to `false`.
- Added strict validation with `bytesLoaded` counter:
  - bytes are counted only when copied into `aCardInfo->sRecipeInfo`
  - function now returns success only if `bytesLoaded == TRecipeInfo_Size`
- Unsupported UID length branch now returns explicit read error (`2`) instead of falling through.

## 3. New failure conditions
- UID length is neither 4 (Classic) nor 7 (Ultralight) -> returns `2`.
- Any insufficient/partial read where loaded bytes are not exactly `TRecipeInfo_Size` -> returns `2`.
- Existing read failures (no card/read fail) continue to return `2`, now with explicit read-failure log.

## 4. Added logs
- Unsupported UID length:
  - `Unsupported UID length: %d`
- Read failure:
  - `Read failure while loading TRecipeInfo.`
- Successful full info load:
  - `TRecipeInfo fully loaded (%d bytes).`

## 5. Expected runtime behavior after patch
- `NFC_LoadTRecipeInfoStructure(...)` now fails hard unless full `TRecipeInfo` payload is freshly read.
- Higher-level verify/compare logic is unchanged, but no longer receives stale/default `sRecipeInfo` data as a false-success read result from this function.
- Behavior for supported tags with complete reads remains unchanged (still returns `0`).
