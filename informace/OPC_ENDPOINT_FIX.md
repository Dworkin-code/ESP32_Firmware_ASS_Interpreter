# OPC UA endpoint fix – reader/PLC same subnet

## Summary

- **Previous endpoint:** `192.168.168.63:4840`
- **New endpoint:** `192.168.0.63:4840`
- **Exact file(s) changed:**
  - `main/app.c` (line 1090) – runtime assignment of `MyCellInfo.IPAdress`
  - `components/OPC_Klient/OPC_klient.c` (lines 238–239) – comment-only update for consistency

## Why the connection was failing (subnet mismatch)

- Reader (ESP32) is on **192.168.0.10** with mask **255.255.255.0** (subnet 192.168.0.0/24).
- PLC was configured as **192.168.168.63** (subnet 192.168.168.0/24).
- 192.168.0.x and 192.168.168.x are different subnets; without routing the reader cannot reach the PLC, so the OPC UA connection failed.

## What was changed

1. **Single source of truth:** The PLC endpoint is set in `app.c` as `MyCellInfo.IPAdress`. All OPC UA usage (OPC_TEST, `OPC_WriteCurrentId`, `OPC_ReportProductEx`, `OPC_GetSupported`, `OPC_ReserveAction`, and `ClientStart` in OPC_klient) uses this value or the same cell-info struct. No second hardcoded endpoint was added.
2. **Comment in OPC_klient.c:** The comment that documented the endpoint format was updated to show the new IP so the codebase stays consistent.

## Log line after the fix

On successful connection you should see:

```text
OPC_TEST: Pripojeni na 192.168.0.63:4840 USPELO.
```

If it still fails (e.g. PLC not on 192.168.0.63 or firewall/port issue):

```text
OPC_TEST: Pripojeni na 192.168.0.63:4840 SELHALO.
```

The startup log will clearly show the endpoint in use because the same `MyCellInfo.IPAdress` is printed in the existing OPC_TEST success/failure messages.
