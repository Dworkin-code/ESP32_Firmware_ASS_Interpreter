# Local cell IP mapping notes

## Assumptions

- `MyCellInfo.IDofCell` loaded from NVS key `ID_Interpretter` is the authoritative local cell identity.
- Existing OPC client calls (`ClientStart`, `OPC_*`) expect endpoint format as `ip:port` (without `opc.tcp://` prefix) in this code path.
- The second field in AAS InputMessage currently can carry local identity and replacing constant `0` with `MyCellInfo.IDofCell` is intended for this deployment.

## Uncertain places reviewed

- Cross-cell handover and local AAS recipe execution previously built InputMessage with hardcoded `0`.
- This has been changed to use the real local ID, but downstream PLC parser expectations for this field are assumed compatible with local ID values.

## Remaining limitations

- If NVS contains unexpected ID outside `1..6`, firmware now falls back to `192.168.168.102:4840` and logs a warning. This is safe but may not match physical deployment intent.
- Mapping table is static in firmware source; adding new cells still requires source update.
- No architectural redesign was introduced; only minimal localized updates were applied.
