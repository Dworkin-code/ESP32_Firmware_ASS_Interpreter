# Local cell IP mapping test plan

## Objective

Verify that firmware uses persistent NVS cell ID (`ID_Interpretter`) as source of truth, auto-assigns `MyCellInfo.IPAdress`, and uses real local cell ID in recipe/cross-cell processing (not `0`).

## Test 1: Boot mapping from NVS ID to endpoint

1. Ensure `ID_Interpretter` in NVS is set to a known value (`1..6`).
2. Reboot reader.
3. Observe boot logs.

Expected logs:

- `[BOOT] NVS loaded ID_Interpretter=<ID>`
- `[BOOT] Local endpoint assigned from cell ID: ID=<ID> endpoint=<mapped_ip:4840>`

Expected mapping:

- `1 -> 192.168.168.66:4840`
- `2 -> 192.168.168.102:4840`
- `3 -> 192.168.168.150:4840`
- `4 -> 192.168.168.88:4840`
- `5 -> 192.168.168.63:4840`
- `6 -> 192.168.168.203:4840`

## Test 2: Persistence across restart

1. Boot once and confirm mapped endpoint log.
2. Power cycle / restart reader without writing new compile-time ID.
3. Check logs again.

Expected:

- Same `ID_Interpretter` and same assigned endpoint as previous boot.

## Test 3: Unknown ID fallback safety

1. Put an unmapped `ID_Interpretter` value in NVS (for example `7`).
2. Reboot reader.

Expected logs:

- `[BOOT] NVS loaded ID_Interpretter=7`
- `[BOOT] WARN unknown cell ID=7, fallback endpoint=192.168.168.102:4840`
- `[BOOT] Local endpoint assigned from cell ID: ID=7 endpoint=192.168.168.102:4840`

## Test 4: Recipe processing uses real local ID in InputMessage

1. Place NFC tag with recipe that runs local AAS path (`ToStorageGlass` branch).
2. Observe runtime logs.

Expected log:

- `AAS: local cell ID used in InputMessage=<ID> msg=<sr_id>/<ID>/...`

Pass criteria:

- Middle field in message is actual local cell ID from NVS, not `0`.

## Test 5: Cross-cell handover uses real local ID

1. Execute recipe where current cell finishes and reserves next remote target.
2. Observe cross-cell logs.

Expected:

- Cross-cell resolver log includes local ID:
  - `[CROSS_CELL] local cell finished, localCell=<ID> nextCell=<target> endpoint=<...>`
- Outgoing InputMessage for remote calls (`GetSupported` / `ReserveAction`) uses `<sr_id>/<ID>/...` format.

## Test 6: OPC endpoint compatibility sanity

1. Let `OPC_Permanent_Test` run after boot.
2. Confirm it attempts connection using assigned endpoint.

Expected:

- `OPC_TEST: Pripojeni na <mapped_ip:4840> ...`
- No regression from endpoint format (`ip:port` remains compatible).
