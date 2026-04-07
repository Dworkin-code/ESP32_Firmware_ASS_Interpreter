# Process owner logic fix

## What was changed

- Updated decision logic in `ESP32_Firmware_ASS_Interpreter/main/app.c` inside the AAS branch in `State_Mimo_Polozena`.
- Added helper:
  - `resolve_owner_cell_id_from_process_type(uint8_t typeOfProcess)`
- Replaced old local check with generic ownership check:
  - old behavior: local only when `TypeOfProcess == ToStorageGlass`
  - new behavior: local when `resolve_owner_cell_id_from_process_type(TypeOfProcess) == MyCellInfo.IDofCell`

## Why old logic was wrong

- The old condition effectively treated one process type as local and routed all other process types into transport path.
- This failed for valid local steps on other cells.
- Example failure:
  - local cell from NVS: `MyCellInfo.IDofCell = 3`
  - current step: `TypeOfProcess = 3` (Shaker)
  - old logic still produced `REQUEST_TRANSPORT`

## Added helper / mapping

The new helper maps process type to owning production cell ID:

- `1 -> 1` (Skladkapalin)
- `2 -> 2` (SodaMaker)
- `3 -> 3` (Shaker)
- `4 -> 4` (SkladAlkoholu)
- `5 -> 5` (SkladSklenicek)
- `6 -> 6` (DrticLedu)
- other values -> `0` (unknown / unmapped)

## New local-vs-transport decision

1. Read current step `TypeOfProcess`
2. Resolve owner cell ID via `resolve_owner_cell_id_from_process_type(...)`
3. Compare against local identity loaded from NVS (`MyCellInfo.IDofCell`)
4. Decision:
   - `owner == local` and owner is known (`owner != 0`) -> `LOCAL_PROCESS`
   - otherwise -> `REQUEST_TRANSPORT`

This keeps one shared firmware binary for all readers and uses persistent local identity from NVS.
