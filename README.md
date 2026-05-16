# netmoni

Monitors the reliability and performance of a Windows Internet connection. Designed to evaluate suitability for video calls (Zoom etc.) while consuming minimal bandwidth.

## What it measures

Each probe (default: every 10 seconds) records:

| Column | Description |
|---|---|
| `timestamp` | Local time of the probe |
| `pings_sent` / `pings_received` | 4 ICMP pings to 8.8.8.8 |
| `rtt_min_ms` / `rtt_avg_ms` / `rtt_max_ms` | Round-trip time |
| `jitter_ms` | Mean absolute deviation of RTT — key for video quality |
| `loss_pct` | Packet loss percentage |
| `wifi_ssid` / `wifi_bssid` | Connected network and access point |
| `wifi_rssi_dbm` | Signal strength in dBm (from Windows WLAN API) |
| `wifi_quality_pct` | Windows signal quality 0–100% |
| `wifi_rx_mbps` / `wifi_tx_mbps` | Current Wi-Fi link speeds |
| `notes` | `wake_after_Ns` when a sleep/hibernate gap is detected |

Output goes to the terminal and to a CSV log file named `netmoni_YYYYMMDD_HHMMSS.csv` in the working directory.

## Zoom suitability thresholds

| Metric | Good | Acceptable | Poor |
|---|---|---|---|
| RTT | < 100ms | < 300ms | > 300ms |
| Jitter | < 10ms | < 30ms | > 30ms |
| Packet loss | < 1% | < 5% | > 5% |
| Wi-Fi RSSI | > -67 dBm | > -70 dBm | < -80 dBm |

## Building

Requires Visual Studio 2022. Open an **x64 Native Tools Command Prompt for VS 2022** and run:

```
cl /O2 /W4 /EHsc /std:c++17 netmoni.cpp /link ws2_32.lib iphlpapi.lib wlanapi.lib /out:netmoni.exe
```

Or open `netmoni.sln` in Visual Studio and build Release|x64.

## Running

```
netmoni.exe           # probe every 10 seconds (default)
netmoni.exe 30        # probe every 30 seconds
```

Press **Ctrl+C** to stop. The CSV log is flushed after every row so it is safe to kill the process at any time.

## Notes

- Runs on Windows; uses the WLAN API for Wi-Fi signal data, so Wi-Fi columns are empty on wired Ethernet connections.
- Handles sleep/hibernate: if the wall clock gap between probes exceeds twice the interval, the probe is marked with a `wake_after_Ns` note.
- Bandwidth cost is minimal: 4 × 32-byte ICMP echo requests per probe interval.
