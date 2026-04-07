# MIFARE Classic 1K revert applied in main build

## Exact files changed

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`

## Exact functions changed

- In `NFC_reader.c`:
  - `NFC_WriteStructRange(...)`
  - `NFC_WriteCheck(...)`
  - removed helper: `NFC_Reader_GetWriteCheckOuterRetries(...)`
- In `NFC_handler.c`:
  - `NFC_Handler_Sync(...)`
  - `NFC_Handler_WriteSafeInfo(...)`
  - removed helpers:
    - `NFC_Handler_GetRetryCountForUidLen(...)`
    - `NFC_Handler_LogRetryProfile(...)`

## Exact logic reverted

1. NTAG213 page guard removed from `NFC_WriteStructRange(...)`
   - Removed `NTAG213_MAX_USER_PAGE` specific upper-page abort check.
   - Ultralight/7-byte UID write path no longer exits early based on NTAG213 max page.

2. Fixed outer retry count restored in `NFC_WriteCheck(...)`
   - Replaced UID-length-dependent outer retry selection with fixed loop count:
     - `for (...; k < MAXERRORREADING; ...)`
   - Removed UID-length retry helper usage.

3. Fixed retry count restored in handler write/sync paths
   - `NFC_Handler_Sync(...)`: all retry loops now use fixed `MAXERRORREADING`.
   - `NFC_Handler_WriteSafeInfo(...)`: retry loop now uses fixed `MAXERRORREADING`.
   - Removed UID-length-based retry profile logic and logging helper.

## Intentionally left untouched

- `main/app.c` was intentionally not modified (no direct proof in this build that `MAX_RECIPE_STEPS 64` or `NFC_IsRecipeEmpty(...)` blocks valid legacy MIFARE Classic recipes).
- AAS/OPC UA communication logic unchanged.
- Cross-cell handover logic unchanged.
- Local cell ID from NVS (`ID_Interpretter`) unchanged.
- Local IP assignment from cell ID unchanged.
- Runtime logging/debug architecture unchanged except removal of UID-length retry profile logs tied to reverted helper.
- Process-owner / local-vs-transport decision logic unchanged.
- Endpoint mapping and production-cell communication logic unchanged.
- PN532 layer unchanged.
- Card/recipe struct layout unchanged.
- Checksum logic unchanged.
