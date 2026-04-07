# Local cell IP mapping implementation

## What code was changed

- File changed: `ESP32_Firmware_ASS_Interpreter/main/app.c`
- Added helper function:
  - `assign_local_endpoint_from_cell_id(uint16_t cellId)`
  - Maps persistent local cell ID to endpoint:
    - `1 -> 192.168.168.66:4840`
    - `2 -> 192.168.168.102:4840`
    - `3 -> 192.168.168.150:4840`
    - `4 -> 192.168.168.88:4840`
    - `5 -> 192.168.168.63:4840`
    - `6 -> 192.168.168.203:4840`

## Where the mapping was inserted

- Inserted in `app.c` near other static helper functions (after `parse_supported_positive`), as a local static mapping utility.

## Where `MyCellInfo.IPAdress` is assigned

- In `app_main()`, right after reading `ID_Interpretter` from NVS into `MyCellInfo.IDofCell`.
- Replaced hardcoded:
  - `MyCellInfo.IPAdress = "192.168.168.102:4840";`
- With automatic assignment from:
  - `const char *localEndpoint = assign_local_endpoint_from_cell_id(MyCellInfo.IDofCell);`
  - fallback to `192.168.168.102:4840` only if ID is outside mapping table.

## Logs added (boot identity + endpoint)

- Added boot logs:
  - `[BOOT] NVS loaded ID_Interpretter=%u`
  - `[BOOT] Local endpoint assigned from cell ID: ID=%u endpoint=%s`
  - `[BOOT] WARN unknown cell ID=%u, fallback endpoint=%s` (only for unmapped IDs)

## Recipe processing / local ID fixes made

- Fixed recipe/cross-cell InputMessage builder to use real local cell ID instead of `0`.
- Updated:
  - `build_target_action_message(...)`
    - old message format body used `.../0/...`
    - now uses `.../<localCellId>/...`
  - cross-cell caller now passes current local ID (`myCellId`) into message builder.
- Updated local AAS message creation in recipe processing (ToStorageGlass branch):
  - old: `"%s/0/%u/%u/%u"`
  - new: `"%s/%u/%u/%u/%u"` with `MyCellInfo.IDofCell`.
- Added runtime debug log confirming recipe path uses the actual local cell ID:
  - `AAS: local cell ID used in InputMessage=%u msg=%s`

## Endpoint formatting compatibility

- Endpoint string format stays unchanged (`"ip:port"` without `opc.tcp://`), compatible with existing `normalize_cell_endpoint(...)` and current `ClientStart(...)` usage.
