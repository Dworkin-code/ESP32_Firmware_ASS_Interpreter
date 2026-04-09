# Target Reservation Endpoint Fix Report

Date: 2026-04-09  
Scope: ESP32 reader target-first orchestration (`TARGET_RESERVE`) endpoint resolution and usage path

## Objective

Ensure remote target reservation always uses strict production PLC endpoint mapping:

- `targetCellId -> opc.tcp://192.168.168.X:4840`
- never hostname fallback (for example `Lubin-Laptop.local:20002`)

## Problem Observed

Runtime log showed:

- `TARGET_RESERVE start ... targetCellId=3`
- endpoint effectively used as `Lubin-Laptop.local:20002`
- OPC connect failed
- `TARGET_RESERVE result=REJECTED`
- `TRANSPORT_GATE blocked`

This proved target cell resolution was correct, but endpoint selection was wrong.

## Root Cause

The wrong endpoint source is static LDS cell configuration in:

- `ESP32_Firmware_ASS_Interpreter/components/NFC_Recipes/NFC_recipes.c`

Inside `GetCellInfoFromLDS()`, `CellInfo Vsechny[]` includes:

- `opc.tcp://Lubin-Laptop.local:20000`
- `opc.tcp://Lubin-Laptop.local:20001`
- `opc.tcp://Lubin-Laptop.local:20002`

`TARGET_RESERVE` previously consumed `targetCell.IPAdress` from that structure, so remote reservation inherited stale hostname test endpoints.

## Changes Implemented

## 1) Strict production endpoint resolver added

File:

- `ESP32_Firmware_ASS_Interpreter/main/app.c`

Function added:

- `resolve_production_plc_endpoint_from_cell_id(uint8_t cellId)`

Mapping:

- `1 -> opc.tcp://192.168.168.66:4840`
- `2 -> opc.tcp://192.168.168.102:4840`
- `3 -> opc.tcp://192.168.168.150:4840`
- `4 -> opc.tcp://192.168.168.88:4840`
- `5 -> opc.tcp://192.168.168.63:4840`
- `6 -> opc.tcp://192.168.168.203:4840`
- default -> `NULL`

## 2) TARGET_RESERVE endpoint selection replaced

File:

- `ESP32_Firmware_ASS_Interpreter/main/app.c`

In `reserve_remote_target(...)`:

- old endpoint source: `targetCell.IPAdress` + normalization
- new endpoint source:

```c
const char *endpoint = resolve_production_plc_endpoint_from_cell_id((uint8_t)targetCell.IDofCell);
```

This endpoint is now used directly for:

- `OPC_GetSupported(endpoint, ...)`
- `OPC_ReserveAction(endpoint, ...)`
- `poll_remote_target_status(endpoint, ...)`

No dynamic replacement is applied in this flow.

## 3) Required debug logging added

Added logs:

- in `reserve_remote_target(...)`:
  - `TARGET_RESERVE resolved endpoint=%s`
- in `ClientStart(...)` (`OPC_klient.c`):
  - `OPC_CONNECT using endpoint=%s`
- in `OPC_CallAasMethod(...)` (`OPC_klient.c`):
  - `OPC_CallAasMethod endpoint=%s`

## 4) OPC client endpoint handling hardened

File:

- `ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`

`ClientStart(...)` now accepts both:

- full endpoint (`opc.tcp://...`)
- raw `host:port` (auto-prefixed with `opc.tcp://`)

This guarantees no malformed endpoint when strict resolver returns a full URL.

## Connection Path Verification

Updated path:

1. `TARGET_RESERVE` computes `targetCellId`
2. `resolve_production_plc_endpoint_from_cell_id(targetCellId)` returns fixed IP endpoint
3. `OPC_GetSupported` / `OPC_ReserveAction` receive same endpoint string
4. `OPC_CallAasMethod` logs and uses the same endpoint
5. `ClientStart` logs and connects using same endpoint

Expected for `targetCellId=3`:

- `TARGET_RESERVE resolved endpoint=opc.tcp://192.168.168.150:4840`
- `OPC_CONNECT using endpoint=opc.tcp://192.168.168.150:4840`
- `OPC_CallAasMethod endpoint=opc.tcp://192.168.168.150:4840`

## Discovery / Cache / Fallback Status

In project application code path (`main` + `components/OPC_Klient`):

- no `GetEndpoints` call used for reservation flow
- no `FindServers` call used for reservation flow
- no cached endpoint mechanism in this flow
- no hostname override in target reserve path

Note: open62541 library contains discovery APIs internally, but they are not invoked by this fixed application-level path.

## Scope Control Confirmation

Modified only target endpoint logic and related endpoint tracing:

- `main/app.c` (`TARGET_RESERVE` endpoint selection)
- `components/OPC_Klient/OPC_klient.c` (endpoint logging and URL acceptance)

Not modified:

- local PLC flow
- gate behavior
- transport PLC business logic
- NFC logic
- recipe parsing
- AAS method semantics
