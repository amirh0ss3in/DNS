// Modern DNS Toggle Application
// Uses registry-based approach for maximum compatibility
// Requires Windows Vista or later and Administrator privileges

#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <string>
#include <vector>
#include <memory>
#include "resource.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

// DNS servers to use for custom configuration
const wchar_t* DNS1 = L"185.51.200.2";
const wchar_t* DNS2 = L"178.22.122.100";

// Application state
struct AppState {
    HWND hWnd;
    HWND hToggleBtn;
    HWND hStatusLabel;
    HWND hInterfaceLabel;
    HFONT hFont;
    std::wstring activeInterfaceGuid;
    std::wstring activeInterfaceName;
    bool hasActiveInterface;
};

// Convert IP address from SOCKADDR to string
std::wstring SockAddrToString(const SOCKADDR* addr) {
    wchar_t buffer[INET6_ADDRSTRLEN] = {0};
    DWORD bufferLen = INET6_ADDRSTRLEN;
    
    if (addr->sa_family == AF_INET) {
        auto* addr4 = reinterpret_cast<const SOCKADDR_IN*>(addr);
        InetNtopW(AF_INET, &addr4->sin_addr, buffer, bufferLen);
    } else if (addr->sa_family == AF_INET6) {
        auto* addr6 = reinterpret_cast<const SOCKADDR_IN6*>(addr);
        InetNtopW(AF_INET6, &addr6->sin6_addr, buffer, bufferLen);
    }
    
    return std::wstring(buffer);
}

// Find the first active, connected, non-loopback IPv4-enabled adapter
bool FindActiveAdapter(std::wstring* outGuid, std::wstring* outName) {
    ULONG bufferSize = 15000;
    auto buffer = std::make_unique<BYTE[]>(bufferSize);
    auto addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.get());
    
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST;
    ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &bufferSize);
    
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer = std::make_unique<BYTE[]>(bufferSize);
        addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.get());
        result = GetAdaptersAddresses(AF_INET, flags, nullptr, addresses, &bufferSize);
    }
    
    if (result != NO_ERROR) {
        return false;
    }
    
    for (auto adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        // Skip loopback and disconnected adapters
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            adapter->OperStatus != IfOperStatusUp) {
            continue;
        }
        
        // Check if it has an IPv4 address
        bool hasIPv4 = false;
        for (auto addr = adapter->FirstUnicastAddress; addr != nullptr; addr = addr->Next) {
            if (addr->Address.lpSockaddr->sa_family == AF_INET) {
                hasIPv4 = true;
                break;
            }
        }
        
        if (hasIPv4) {
            // Convert narrow string to wide string
            int sizeNeeded = MultiByteToWideChar(CP_ACP, 0, adapter->AdapterName, -1, nullptr, 0);
            if (sizeNeeded > 0) {
                std::vector<wchar_t> wideGuid(sizeNeeded);
                MultiByteToWideChar(CP_ACP, 0, adapter->AdapterName, -1, wideGuid.data(), sizeNeeded);
                *outGuid = wideGuid.data();
            }
            *outName = adapter->FriendlyName;
            return true;
        }
    }
    
    return false;
}

// Get current DNS servers from registry
std::vector<std::wstring> GetCurrentDNSServers(const std::wstring& guid) {
    std::vector<std::wstring> servers;
    
    std::wstring keyPath = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + guid;
    HKEY hKey;
    
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return servers;
    }
    
    // Try to read NameServer value (static DNS)
    wchar_t buffer[512] = {0};
    DWORD bufferSize = sizeof(buffer);
    DWORD type;
    
    if (RegQueryValueExW(hKey, L"NameServer", nullptr, &type, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
        if (type == REG_SZ && wcslen(buffer) > 0) {
            // Parse comma or space-separated DNS servers
            std::wstring str(buffer);
            size_t pos = 0;
            while (pos < str.length()) {
                size_t commaPos = str.find_first_of(L", ", pos);
                if (commaPos == std::wstring::npos) {
                    commaPos = str.length();
                }
                std::wstring server = str.substr(pos, commaPos - pos);
                if (!server.empty()) {
                    servers.push_back(server);
                }
                pos = commaPos + 1;
            }
            RegCloseKey(hKey);
            return servers;
        }
    }
    
    // If no static DNS, try DHCP DNS
    bufferSize = sizeof(buffer);
    if (RegQueryValueExW(hKey, L"DhcpNameServer", nullptr, &type, (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
        if (type == REG_SZ && wcslen(buffer) > 0) {
            std::wstring str(buffer);
            size_t pos = 0;
            while (pos < str.length()) {
                size_t spacePos = str.find(L' ', pos);
                if (spacePos == std::wstring::npos) {
                    spacePos = str.length();
                }
                std::wstring server = str.substr(pos, spacePos - pos);
                if (!server.empty()) {
                    servers.push_back(server);
                }
                pos = spacePos + 1;
            }
        }
    }
    
    RegCloseKey(hKey);
    return servers;
}

// Check if custom DNS is currently set
bool IsCustomDNSSet(const std::wstring& guid) {
    auto servers = GetCurrentDNSServers(guid);
    
    if (servers.empty()) {
        return false;
    }
    
    // Check if first DNS matches our custom DNS1
    return servers[0] == DNS1;
}

// Notify system of IP address changes
void NotifyIPAddressChange() {
    // Send notification to trigger DNS cache flush and network refresh
    DWORD_PTR dwRet;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, 0, SMTO_ABORTIFHUNG, 5000, &dwRet);
}

// Set custom DNS servers via registry
bool SetCustomDNS(const std::wstring& guid) {
    std::wstring keyPath = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + guid;
    HKEY hKey;
    
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    // Set NameServer value (comma-separated)
    std::wstring dnsServers = std::wstring(DNS1) + L"," + DNS2;
    LONG result = RegSetValueExW(hKey, L"NameServer", 0, REG_SZ,
                                  (const BYTE*)dnsServers.c_str(),
                                  (DWORD)((dnsServers.length() + 1) * sizeof(wchar_t)));
    
    RegCloseKey(hKey);
    
    if (result == ERROR_SUCCESS) {
        NotifyIPAddressChange();
        // Flush DNS cache
        system("ipconfig /flushdns >nul 2>&1");
    }
    
    return result == ERROR_SUCCESS;
}

// Reset DNS to automatic (DHCP) via registry
bool ResetDNS(const std::wstring& guid) {
    std::wstring keyPath = L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces\\" + guid;
    HKEY hKey;
    
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    // Clear NameServer value to use DHCP
    LONG result = RegSetValueExW(hKey, L"NameServer", 0, REG_SZ, (const BYTE*)L"", sizeof(wchar_t));
    
    RegCloseKey(hKey);
    
    if (result == ERROR_SUCCESS) {
        NotifyIPAddressChange();
        // Flush DNS cache
        system("ipconfig /flushdns >nul 2>&1");
    }
    
    return result == ERROR_SUCCESS;
}

// Update UI to reflect current state
void UpdateUI(AppState* state) {
    if (!state->hasActiveInterface) {
        SetWindowTextW(state->hStatusLabel, L"Status: No active network found");
        SetWindowTextW(state->hInterfaceLabel, L"Interface: None");
        EnableWindow(state->hToggleBtn, FALSE);
        return;
    }
    
    bool customSet = IsCustomDNSSet(state->activeInterfaceGuid);
    auto servers = GetCurrentDNSServers(state->activeInterfaceGuid);
    
    // Update button text
    if (customSet) {
        SetWindowTextW(state->hToggleBtn, L"Set Automatic DNS");
    } else {
        SetWindowTextW(state->hToggleBtn, L"Set Custom DNS");
    }
    
    // Update status label
    std::wstring status = customSet ? L"Status: Custom DNS Active" : L"Status: Automatic DNS";
    if (!servers.empty()) {
        status += L" (";
        for (size_t i = 0; i < servers.size() && i < 2; ++i) {
            if (i > 0) status += L", ";
            status += servers[i];
        }
        status += L")";
    }
    SetWindowTextW(state->hStatusLabel, status.c_str());
    
    // Update interface label
    std::wstring ifaceText = L"Interface: " + state->activeInterfaceName;
    SetWindowTextW(state->hInterfaceLabel, ifaceText.c_str());
}

// Create the standard Windows UI font (Segoe UI)
HFONT CreateUIFont() {
    NONCLIENTMETRICSW ncm = {0};
    ncm.cbSize = sizeof(ncm);
    
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        return CreateFontIndirectW(&ncm.lfMessageFont);
    }
    
    // Fallback
    return CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    
    switch (msg) {
    case WM_CREATE: {
        // Allocate and initialize state
        state = new AppState();
        state->hWnd = hwnd;
        state->hasActiveInterface = FindActiveAdapter(&state->activeInterfaceGuid, &state->activeInterfaceName);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        
        // Create UI font
        state->hFont = CreateUIFont();
        
        // Create controls (centered layout)
        state->hInterfaceLabel = CreateWindowExW(0, L"STATIC", L"Interface: ",
            WS_VISIBLE | WS_CHILD,
            30, 20, 440, 25, hwnd, (HMENU)IDC_INTERFACE_LABEL, nullptr, nullptr);
        
        state->hStatusLabel = CreateWindowExW(0, L"STATIC", L"Status: ",
            WS_VISIBLE | WS_CHILD,
            30, 50, 440, 25, hwnd, (HMENU)IDC_STATUS_LABEL, nullptr, nullptr);
        
        state->hToggleBtn = CreateWindowExW(0, L"BUTTON", L"Toggle DNS",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            150, 90, 200, 35, hwnd, (HMENU)IDC_TOGGLE_BTN, nullptr, nullptr);
        
        // Apply font to all controls
        SendMessageW(state->hInterfaceLabel, WM_SETFONT, (WPARAM)state->hFont, TRUE);
        SendMessageW(state->hStatusLabel, WM_SETFONT, (WPARAM)state->hFont, TRUE);
        SendMessageW(state->hToggleBtn, WM_SETFONT, (WPARAM)state->hFont, TRUE);
        
        UpdateUI(state);
        break;
    }
    
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_TOGGLE_BTN && state && state->hasActiveInterface) {
            bool currentlyCustom = IsCustomDNSSet(state->activeInterfaceGuid);
            bool success;
            
            if (currentlyCustom) {
                success = ResetDNS(state->activeInterfaceGuid);
            } else {
                success = SetCustomDNS(state->activeInterfaceGuid);
            }
            
            if (!success) {
                MessageBoxW(hwnd, L"Failed to update DNS settings. Make sure the application is running as Administrator.",
                           L"Error", MB_OK | MB_ICONERROR);
            } else {
                // Wait a moment for changes to take effect
                Sleep(500);
            }
            
            UpdateUI(state);
        }
        break;
    
    case WM_DESTROY:
        if (state) {
            if (state->hFont) {
                DeleteObject(state->hFont);
            }
            delete state;
        }
        PostQuitMessage(0);
        break;
    
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    
    return 0;
}

// Application entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Register window class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ModernDNSToggleClass";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Window registration failed!", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // Create window
    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"DNS Toggle",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        500, 180,
        nullptr, nullptr, hInstance, nullptr);
    
    if (!hwnd) {
        MessageBoxW(nullptr, L"Window creation failed!", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    return static_cast<int>(msg.wParam);
}