#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <wlanapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wlanapi.lib")

// Default configuration (can override interval via argv[1])
static const char* PING_TARGET    = "8.8.8.8";
static const int   PING_COUNT     = 4;
static const DWORD PING_TIMEOUT_MS = 2000;
static const int   DEFAULT_INTERVAL_S = 10;

static volatile bool g_running = true;

BOOL WINAPI ctrlHandler(DWORD) {
    g_running = false;
    return TRUE;
}

// Wall-clock time as 100-nanosecond intervals since 1601-01-01
static ULONGLONG wallNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

// Local time as "YYYY-MM-DD HH:MM:SS"
static std::string timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// Wrap a CSV field in quotes if it contains commas or quotes
static std::string csvEscape(const std::string& s) {
    if (s.find_first_of(",\"") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

struct PingStats {
    int sent;
    int received;
    int rtt_min_ms;
    int rtt_avg_ms;
    int rtt_max_ms;
    int jitter_ms;   // mean absolute deviation from average RTT
};

static PingStats runPings(HANDLE icmp, IPAddr target) {
    PingStats s{};
    s.sent = PING_COUNT;
    s.rtt_min_ms = s.rtt_avg_ms = s.rtt_max_ms = s.jitter_ms = -1;

    char payload[32];
    memset(payload, 0x41, sizeof(payload));  // 'A' * 32

    DWORD replyBufSize = sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 8;
    std::vector<BYTE> replyBuf(replyBufSize);

    std::vector<int> rtts;
    rtts.reserve(PING_COUNT);

    for (int i = 0; i < PING_COUNT; i++) {
        DWORD ret = IcmpSendEcho(
            icmp, target,
            payload, sizeof(payload),
            nullptr,
            replyBuf.data(), replyBufSize,
            PING_TIMEOUT_MS);

        if (ret > 0) {
            auto* reply = reinterpret_cast<ICMP_ECHO_REPLY*>(replyBuf.data());
            if (reply->Status == IP_SUCCESS)
                rtts.push_back((int)reply->RoundTripTime);
        }

        if (i < PING_COUNT - 1)
            Sleep(200);
    }

    s.received = (int)rtts.size();
    if (rtts.empty())
        return s;

    int sum = 0, mn = rtts[0], mx = rtts[0];
    for (int v : rtts) {
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    int avg = sum / s.received;

    int jsum = 0;
    for (int v : rtts)
        jsum += abs(v - avg);

    s.rtt_min_ms = mn;
    s.rtt_avg_ms = avg;
    s.rtt_max_ms = mx;
    s.jitter_ms  = jsum / s.received;
    return s;
}

struct WifiInfo {
    bool connected = false;
    std::string ssid;
    std::string bssid;
    int rssi_dbm    = 0;   // derived: (quality/2) - 100
    int quality_pct = 0;   // 0..100 from Windows
    int rx_mbps     = 0;
    int tx_mbps     = 0;
};

static WifiInfo queryWifi() {
    WifiInfo info;

    HANDLE wlan = nullptr;
    DWORD negVer = 0;
    if (WlanOpenHandle(2, nullptr, &negVer, &wlan) != ERROR_SUCCESS)
        return info;

    PWLAN_INTERFACE_INFO_LIST ifList = nullptr;
    if (WlanEnumInterfaces(wlan, nullptr, &ifList) != ERROR_SUCCESS) {
        WlanCloseHandle(wlan, nullptr);
        return info;
    }

    for (DWORD i = 0; i < ifList->dwNumberOfItems; i++) {
        auto* iface = &ifList->InterfaceInfo[i];
        if (iface->isState != wlan_interface_state_connected)
            continue;

        PWLAN_CONNECTION_ATTRIBUTES attr = nullptr;
        DWORD dataSize = 0;
        WLAN_OPCODE_VALUE_TYPE opcode;

        if (WlanQueryInterface(wlan, &iface->InterfaceGuid,
                               wlan_intf_opcode_current_connection,
                               nullptr, &dataSize,
                               reinterpret_cast<PVOID*>(&attr),
                               &opcode) == ERROR_SUCCESS) {
            info.connected = true;

            auto& assoc = attr->wlanAssociationAttributes;

            // SSID
            info.ssid = std::string(
                reinterpret_cast<char*>(assoc.dot11Ssid.ucSSID),
                assoc.dot11Ssid.uSSIDLength);

            // BSSID
            char bssid[20];
            snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                     assoc.dot11Bssid[0], assoc.dot11Bssid[1],
                     assoc.dot11Bssid[2], assoc.dot11Bssid[3],
                     assoc.dot11Bssid[4], assoc.dot11Bssid[5]);
            info.bssid = bssid;

            // Signal: quality 0..100 -> RSSI = (quality/2) - 100 dBm
            info.quality_pct = (int)assoc.wlanSignalQuality;
            info.rssi_dbm    = (info.quality_pct / 2) - 100;

            // Link speeds (Kbps -> Mbps)
            info.rx_mbps = (int)(assoc.ulRxRate / 1000);
            info.tx_mbps = (int)(assoc.ulTxRate / 1000);

            WlanFreeMemory(attr);
        }
        break;  // use first connected adapter
    }

    WlanFreeMemory(ifList);
    WlanCloseHandle(wlan, nullptr);
    return info;
}

static void writeRow(FILE* log,
                     const std::string& ts,
                     const PingStats& p,
                     const WifiInfo& w,
                     const std::string& notes) {
    auto intOrEmpty = [](int v) -> std::string {
        return v < 0 ? "" : std::to_string(v);
    };

    double lossPct = p.sent > 0
        ? 100.0 * (p.sent - p.received) / p.sent
        : 0.0;
    char lossBuf[16];
    snprintf(lossBuf, sizeof(lossBuf), "%.1f", lossPct);

    std::string row =
        csvEscape(ts)                    + "," +
        std::to_string(p.sent)           + "," +
        std::to_string(p.received)       + "," +
        intOrEmpty(p.rtt_min_ms)         + "," +
        intOrEmpty(p.rtt_avg_ms)         + "," +
        intOrEmpty(p.rtt_max_ms)         + "," +
        intOrEmpty(p.jitter_ms)          + "," +
        lossBuf                          + "," +
        (w.connected ? csvEscape(w.ssid) : "") + "," +
        (w.connected ? w.bssid          : "") + "," +
        (w.connected ? std::to_string(w.rssi_dbm)    : "") + "," +
        (w.connected ? std::to_string(w.quality_pct) : "") + "," +
        (w.connected ? std::to_string(w.rx_mbps)     : "") + "," +
        (w.connected ? std::to_string(w.tx_mbps)     : "") + "," +
        csvEscape(notes);

    printf("%s\n", row.c_str());
    if (log) {
        fprintf(log, "%s\n", row.c_str());
        fflush(log);
    }
}

int main(int argc, char* argv[]) {
    int intervalS = DEFAULT_INTERVAL_S;
    if (argc >= 2) {
        int v = atoi(argv[1]);
        if (v >= 1) intervalS = v;
    }

    SetConsoleCtrlHandler(ctrlHandler, TRUE);

    // Need Winsock for inet_pton
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    struct in_addr addr4;
    if (inet_pton(AF_INET, PING_TARGET, &addr4) != 1) {
        fprintf(stderr, "Invalid PING_TARGET address\n");
        return 1;
    }
    IPAddr target = addr4.s_addr;

    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "IcmpCreateFile failed: %lu\n", GetLastError());
        return 1;
    }

    // Log file named after start time
    SYSTEMTIME st;
    GetLocalTime(&st);
    char logName[64];
    snprintf(logName, sizeof(logName), "netmoni_%04d%02d%02d_%02d%02d%02d.csv",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    FILE* log = fopen(logName, "w");
    if (!log)
        fprintf(stderr, "Warning: cannot open log file %s\n", logName);

    static const char* HEADER =
        "timestamp,pings_sent,pings_received,"
        "rtt_min_ms,rtt_avg_ms,rtt_max_ms,jitter_ms,loss_pct,"
        "wifi_ssid,wifi_bssid,wifi_rssi_dbm,wifi_quality_pct,"
        "wifi_rx_mbps,wifi_tx_mbps,notes";

    printf("%s\n", HEADER);
    if (log) {
        fprintf(log, "%s\n", HEADER);
        fflush(log);
    }

    fprintf(stderr, "netmoni: pinging %s every %ds, log: %s\n",
            PING_TARGET, intervalS, log ? logName : "(none)");
    fprintf(stderr, "Press Ctrl+C to stop.\n");

    // Track previous probe wall-clock time to detect sleep/hibernate
    ULONGLONG prevWall = wallNow();

    while (g_running) {
        ULONGLONG nowWall = wallNow();
        // elapsed in seconds (100ns units / 10,000,000)
        ULONGLONG elapsedS = (nowWall - prevWall) / 10000000ULL;
        prevWall = nowWall;

        std::string notes;
        if (elapsedS > (ULONGLONG)(intervalS * 2) + 10) {
            char buf[48];
            snprintf(buf, sizeof(buf), "wake_after_%llds", (long long)elapsedS);
            notes = buf;
        }

        PingStats ping = runPings(icmp, target);
        WifiInfo  wifi = queryWifi();

        writeRow(log, timestamp(), ping, wifi, notes);

        // Sleep until next probe, waking every 100ms to check g_running
        int remainMs = intervalS * 1000;
        while (g_running && remainMs > 0) {
            int chunk = remainMs < 100 ? remainMs : 100;
            Sleep(chunk);
            remainMs -= chunk;
        }
    }

    IcmpCloseHandle(icmp);
    if (log) fclose(log);
    WSACleanup();

    fprintf(stderr, "netmoni: stopped.\n");
    return 0;
}
