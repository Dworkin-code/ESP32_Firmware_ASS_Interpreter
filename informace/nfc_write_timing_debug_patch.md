# NFC Write Timing Debug Patch

## 1. Modified files
- `ESP32_Firmware_ASS_Interpreter/main/app.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Handler/NFC_handler.c`
- `ESP32_Firmware_ASS_Interpreter/components/NFC_Reader/NFC_reader.c`

## 2. Added timestamp logs
- Added millisecond timestamps via `esp_timer_get_time()/1000`.
- Added `start/end` logs with elapsed duration (`dt`) and result codes.
- Kept log format concise and focused on write/verify timing bottlenecks.

## 3. Instrumented functions
- In post-AAS write-back path (`main/app.c`):
  - before/after `NFC_Handler_WriteStep(...)`
  - before/after `NFC_Handler_WriteSafeInfo(...)`
  - before/after `NFC_Handler_Sync(...)`
- In `NFC_Handler_WriteSafeInfo(...)`:
  - per `NFC_WriteCheck(...)` attempt: start/end + retry index + elapsed time
- In `NFC_Handler_Sync(...)`:
  - per `NFC_WriteCheck(...)` call: start/end + range (`start/end`) + retry index + elapsed time
- In `NFC_WriteCheck(...)`:
  - function entry/exit
  - before/after each `NFC_WriteStructRange(...)` call with outer/inner retry counters
  - before/after each `NFC_CheckStructArrayIsSame(...)` verification call with outer/inner retry counters
- In `NFC_WriteStructRange(...)`:
  - before/after `pn532_readPassiveTargetID(...)` with timeout, return value, and elapsed time

## 4. Expected new runtime visibility
- Exact split of post-AAS write-back delay between:
  - `WriteStep` (RAM/dirty flag path),
  - `WriteSafeInfo` (physical write+verify),
  - `Sync` (physical sync write path).
- Per-retry timing inside NFC write/verify loops to show whether delay is from:
  - low-level write (`NFC_WriteStructRange`),
  - compare/verification (`NFC_CheckStructArrayIsSame`),
  - card detect wait (`pn532_readPassiveTargetID` with `MAXTIMEOUT=5000`).

## 5. Notes
- Changes are debug-only instrumentation; no business/transport/recipe logic was changed.
- Return values and control flow were preserved.
- Logs are intentionally compact to identify blocking points quickly during runtime traces.
