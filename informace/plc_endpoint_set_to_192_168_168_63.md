# PLC OPC UA endpoint change

## Summary

Reader firmware was updated so OPC UA connects to the PLC at the endpoint confirmed in UaExpert.

| | Value |
|---|--------|
| **Old endpoint** | `192.168.0.63:4840` |
| **New endpoint** | `192.168.168.63:4840` |
| **UaExpert** | `opc.tcp://192.168.168.63:4840` |

## Files changed

- **`ESP32_Firmware_ASS_Interpreter/main/app.c`**  
  - Line 1090: `MyCellInfo.IPAdress = "192.168.168.63:4840";` (was `"192.168.0.63:4840"`).

- **`ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`**  
  - Comment only (lines 237–240): example in `ClientStart()` updated to `192.168.168.63:4840` and note that the single source of truth is `MyCellInfo.IPAdress` in `app.c`. No behaviour change.

## Single source of truth

All OPC UA operations use the same endpoint:

- **Source:** `MyCellInfo.IPAdress` set once in `app.c` (init).
- **Used by:**
  - `OPC_WriteCurrentId(MyCellInfo.IPAdress, ...)` (app.c)
  - `OPC_ReportProductEx(MyCellInfo.IPAdress, ...)` (app.c)
  - `OPC_GetSupported(MyCellInfo.IPAdress, ...)` (app.c)
  - `OPC_ReserveAction(MyCellInfo.IPAdress, ...)` (app.c)
  - `ClientStart(&client, MyCellInfo.IPAdress)` in OPC_TEST (app.c)

`ClientStart()` and all OPC_* functions in `OPC_klient.c` take the endpoint string and build `opc.tcp://%s` from it; they do not hardcode the PLC IP. No second endpoint definition was added; the only change is the value in `app.c` and the comment in `OPC_klient.c`.

## Startup log

The existing OPC_TEST log line prints the endpoint in use. On success you should see:

```text
OPC_TEST: Pripojeni na 192.168.168.63:4840 USPELO.
```

On failure:

```text
OPC_TEST: Pripojeni na 192.168.168.63:4840 SELHALO.
```

Runtime success depends on network and PLC availability; this change only makes the firmware use the correct PLC endpoint.
