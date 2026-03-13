# Ethernet static IP fix (ESP32 reader – PLC subnet)

## Summary

The ESP32 reader Ethernet interface was configured to use a static IP on the **192.168.0.x** subnet. The PLC OPC UA endpoint is **192.168.168.63:4840**. Because reader and PLC were on different subnets, the OPC UA connection failed. The firmware was updated so the reader uses a **static IP on the PLC subnet** (192.168.168.x).

## Previous behavior

- Ethernet used a **static** IP (DHCP client was already stopped in code).
- Configured static values were **wrong subnet**: IP **192.168.0.10**, netmask **255.255.255.0**, gateway **192.168.0.1**.
- Runtime log showed something like: `ETHIP:192.168.0.10`, `ETHMASK:255.255.255.0`, `ETHGW:192.168.0.1`.
- PLC endpoint: **192.168.168.63:4840**.
- **Why OPC UA was failing:** Reader (192.168.0.10) and PLC (192.168.168.63) are in different subnets; without routing between 192.168.0.0/24 and 192.168.168.0/24, the connection attempt to 192.168.168.63:4840 could not succeed.

## New static IP settings

| Parameter | Value        |
|-----------|--------------|
| Reader IP | 192.168.168.10 |
| Netmask   | 255.255.255.0  |
| Gateway   | 192.168.168.1  |

- DHCP client remains **disabled** for Ethernet (`esp_netif_dhcpc_stop`).
- Static IP is applied with `esp_netif_set_ip_info()`.
- A one-line log was added to print the configured static Ethernet IP/netmask/gateway after configuration.

## Files changed

- **`ESP32_Firmware_ASS_Interpreter/components/OPC_Klient/OPC_klient.c`**
  - In `connection_scan()`:
    - `IP4_ADDR` for reader IP: **192.168.0.10** → **192.168.168.10**
    - `IP4_ADDR` for gateway: **192.168.0.1** → **192.168.168.1**
    - Netmask left as **255.255.255.0**
    - Added `ESP_LOGI(TAG, "Ethernet static IP configured: " IPSTR " / " IPSTR " gw " IPSTR, ...)` for clear runtime logging
    - Updated Czech debug string to "192.168.168.10"

No other files were modified. PLC code and NFC/AAS logic were not changed.

## Expected success condition

- Reader Ethernet interface has IP **192.168.168.10** in the same subnet as the PLC.
- OPC UA connection is attempted to **192.168.168.63:4840** (same subnet).
- With correct cabling and no firewall blocking, the OPC UA client should be able to connect to the PLC endpoint.

## Verification

After flashing and boot:

1. Log should show: **Ethernet static IP configured: 192.168.168.10 / 255.255.255.0 gw 192.168.168.1**
2. `got_ip_event_handler` (if it runs for the static assignment) will log ETHIP/ETHMASK/ETHGW with the same values.
3. OPC UA connection to **opc.tcp://192.168.168.63:4840** should succeed if the network path is correct.
