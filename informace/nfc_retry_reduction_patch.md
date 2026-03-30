# NFC Retry Reduction Patch

## 1. Modified files
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`

## 2. Exact retry changes
- `NFC_Handler_WriteSafeInfo(...)`:
  - effective retry count is now `1` when `UID len == 7`
  - remains unchanged (`MAXERRORREADING`) for other UID lengths (including `UID len == 4`)
- `NFC_Handler_Sync(...)`:
  - effective retry count is now `1` when `UID len == 7`
  - remains unchanged (`MAXERRORREADING`) for other UID lengths (including `UID len == 4`)
- `NFC_WriteCheck(...)`:
  - outer retry loop count is now `1` when `aCardInfo->sUidLength == 7`
  - outer retry loop remains unchanged (`MAXERRORREADING`) for other UID lengths (including `UID len == 4`)
  - inner write/verify loops remain unchanged

## 3. Scope of the change (UID len 7 only)
- Retry reduction is gated by existing tag detection data (`sUidLength`).
- Only `UID len == 7` (NTAG/Ultralight path) uses reduced retries.
- `UID len == 4` (MIFARE Classic path) keeps existing behavior.
- Write + verify flow and return codes are unchanged.

## 4. Added logs
- Added concise retry-profile log in handler path:
  - format: `[NFC_RETRY][<function>] uid_len=<n> write_safe_retries=<n> sync_retries=<n> writecheck_outer_retries=<n>`
  - emitted from:
    - `NFC_Handler_WriteSafeInfo(...)`
    - `NFC_Handler_Sync(...)`

## 5. Expected runtime improvement
- Significant reduction expected on NTAG/Ultralight tests because high-level retries and `NFC_WriteCheck` outer retries are reduced to single-attempt behavior.
- Typical improvement should be close to removing repeated full write+verify cycles, while preserving the same write/verify mechanism and error handling.
