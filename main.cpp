// ToggleDNSApp: Win32 C++ application to toggle DNS settings between custom and automatic.
// Requires running as Administrator (add a manifest file alongside the executable).

#include <windows.h>
#include <vector>
#include <string>
#include <cstdio>
#include <sstream>

const char* DNS1 = "185.51.200.2";
const char* DNS2 = "178.22.122.100";
HWND hToggleBtn, hStatusLabel;

// Execute a command and capture its output
std::string ExecCommand(const std::string& cmd) {
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return {};
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    _pclose(pipe);
    return result;
}

// Find first connected interface name
std::string GetActiveInterface() {
    auto output = ExecCommand("netsh interface show interface");
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("Connected") != std::string::npos &&
            line.find("Loopback") == std::string::npos) {
            std::istringstream ls(line);
            std::vector<std::string> tokens;
            std::string tok;
            while (ls >> tok) tokens.push_back(tok);
            if (tokens.size() >= 3) return tokens.back();
        }
    }
    return {};
}

bool IsCustomDNSSet(const std::string& iface) {
    auto out = ExecCommand("netsh interface ipv4 show dnsservers name=\"" + iface + "\"");
    return out.find(DNS1) != std::string::npos;
}

void SetCustomDNS(const std::string& iface) {
    ExecCommand("netsh interface ip set dns name=\"" + iface + "\" static " + DNS1 + " primary");
    ExecCommand("netsh interface ip add dns name=\"" + iface + "\" " + DNS2 + " index=2");
}

void ResetDNS(const std::string& iface) {
    ExecCommand("netsh interface ip set dns name=\"" + iface + "\" source=dhcp");
}

void UpdateStatusLabel(bool custom) {
    std::string status = custom ? "Status: Custom DNS Active" : "Status: Automatic DNS";
    SetWindowTextA(hStatusLabel, status.c_str());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static std::string iface = GetActiveInterface();
    switch (msg) {
    case WM_CREATE:
        hToggleBtn = CreateWindowA("BUTTON", "Use Custom DNS",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            50, 20, 200, 30, hwnd, (HMENU)1, NULL, NULL);
        hStatusLabel = CreateWindowA("STATIC", "Status: Unknown",
            WS_VISIBLE | WS_CHILD,
            20, 70, 250, 20, hwnd, NULL, NULL, NULL);
        {
            bool custom = !iface.empty() && IsCustomDNSSet(iface);
            SendMessage(hToggleBtn, BM_SETCHECK, custom ? BST_CHECKED : BST_UNCHECKED, 0);
            UpdateStatusLabel(custom);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            bool checked = (SendMessage(hToggleBtn, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (checked) SetCustomDNS(iface);
            else ResetDNS(iface);
            UpdateStatusLabel(checked);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "ToggleDNSClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(wc.lpszClassName, "DNS Toggle",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 150,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, nCmdShow);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

/*
Build & Run with g++ in VS Code:

1. Install MinGW-w64 and add its `bin/` directory to your `PATH`.
2. Save the above code in `main.cpp`.
3. Create a manifest `dns_toggle.exe.manifest` alongside your executable:
   ```xml
   <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
   <assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
     <trustInfo>
       <security>
         <requestedPrivileges>
           <requestedExecutionLevel level="requireAdministrator" uiAccess="false" />
         </requestedPrivileges>
       </security>
     </trustInfo>
   </assembly>
   ```
4. Compile and link as a GUI app (no Unicode stub) to avoid `wWinMain` errors:
   ```batch
   g++ main.cpp -std=c++17 -static -mwindows -o dns_toggle.exe
   ```
   - `-mwindows` tells the linker to use the Windows GUI subsystem and look for `WinMain`.
   - Remove `-municode` so the ANSI `WinMain` entry point is used.
5. Ensure `dns_toggle.exe.manifest` is in the same folder as `dns_toggle.exe`.
6. Right-click and **Run as Administrator** to toggle DNS.
*/
