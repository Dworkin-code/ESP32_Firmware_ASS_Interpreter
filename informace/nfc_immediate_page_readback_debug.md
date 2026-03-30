# NFC Immediate Page Readback Debug

## 1. Modified files
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`
- `informace/nfc_immediate_page_readback_debug.md`

## 2. Added immediate read-back checks
- In `NFC_WriteStructRange(...)`, right after successful `pn532_mifareclassic_WriteDataBlock(...)`, an immediate `pn532_mifareclassic_ReadDataBlock(...)` is executed on the same block.
- In `NFC_WriteStructRange(...)`, right after successful `pn532_mifareultralight_WritePage(...)`, an immediate `pn532_mifareultralight_ReadPage(...)` is executed starting from the same page.
- On read-back failure, a concise debug log is emitted:
  - `IMMEDIATE READBACK FAIL medium=... block/page=... chunk_i=...`
- Existing low-level success/fail logs were left intact.

## 3. What is compared
- **MIFARE Classic**
  - Compares all 16 bytes of the written block (`iData[0..15]`) against immediate read-back data from the same physical block.
  - For each mismatch, logs:
    - medium type (`Classic`)
    - block index
    - byte offset in block
    - expected byte
    - actual byte
- **Ultralight / NTAG**
  - Reads from the same page that was written.
  - Compares the first 4 bytes (the just-written page bytes) of read-back data against written `iData[0..3]`.
  - For each mismatch, logs:
    - medium type (`Ultralight`)
    - page index
    - byte offset in page
    - expected byte
    - actual byte

## 4. Expected runtime output
- Normal case:
  - Existing `LOWLEVEL WRITE OK ...` logs continue to appear per block/page.
  - No additional mismatch lines.
- If physical persistence fails despite write success:
  - `IMMEDIATE READBACK MISMATCH medium=... block/page=... byte_offset=... expected=0x.. actual=0x..`
- If immediate read-back call itself fails:
  - `IMMEDIATE READBACK FAIL medium=... block/page=... chunk_i=...`

## 5. Notes
- This patch is intentionally minimal and debug-oriented.
- No architectural refactor was made.
- No recipe logic was changed.
- No transport logic was changed.
- No retry counts were changed.
- No high-level verify behavior was changed.
