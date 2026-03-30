# NFC Low-Level Write Fail Fix

## 1. Modified files
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`
- `informace/nfc_low_level_write_fail_fix.md`

## 2. Exact change
- In `NFC_WriteStructRange(...)`, immediate return-value checks were added right after:
  - `pn532_mifareclassic_WriteDataBlock(...)`
  - `pn532_mifareultralight_WritePage(...)`
- If write return value indicates failure (`0`), function now returns immediately with error code `2`.
- Successful write flow and existing logic order were left unchanged.

## 3. How low-level write failures are now handled
- `Classic` path:
  - after block write call, failure is treated as hard error
  - function logs failure context and exits immediately
- `Ultralight` path:
  - after page write call, failure is treated as hard error
  - function logs failure context and exits immediately
- No continuation to next chunk occurs after low-level write failure.

## 4. Added logs
- On failure:
  - `LOWLEVEL WRITE FAIL medium=Classic block=<index> chunk_i=<i>`
  - `LOWLEVEL WRITE FAIL medium=Ultralight page=<index> chunk_i=<i>`
- On success (concise, one line per written chunk):
  - `LOWLEVEL WRITE OK medium=Classic block=<index> chunk_i=<i>`
  - `LOWLEVEL WRITE OK medium=Ultralight page=<index> chunk_i=<i>`

## 5. Expected runtime behavior after patch
- Any low-level page/block write failure now causes immediate hard-fail from `NFC_WriteStructRange(...)`.
- Caller receives error `2` without writing remaining chunks.
- Existing recipe/transport/verify behavior is unchanged; only low-level write failure handling is hardened.
