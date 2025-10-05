# DNS Toggle Application - Build Instructions

## Prerequisites

- **MinGW-w64** (GCC for Windows) with C++17 support
- **windres** (Windows Resource Compiler - included with MinGW-w64)
- Windows 10 SDK headers (usually included with MinGW-w64)

## Files Required

Make sure you have all these files in the same directory:
- `main.cpp`
- `resource.h`
- `resource.rc`
- `dns_toggle.exe.manifest`

## Compilation Steps

### Step 1: Compile the Resource File

First, compile the resource script using `windres`:

```bash
windres resource.rc -O coff -o resource.o
```

This creates `resource.o` which contains the embedded manifest and resource definitions.

### Step 2: Compile and Link the Application

Compile the C++ code and link it with the resource object:

```bash
g++ -std=c++17 -municode -mwindows -O2 main.cpp resource.o -o dns_toggle.exe -liphlpapi -lws2_32 -ladvapi32 -static-libgcc -static-libstdc++
```

**Explanation of flags:**
- `-std=c++17` - Use C++17 standard
- `-municode` - Use Unicode entry point (`wWinMain`)
- `-mwindows` - Build as Windows GUI application (no console window)
- `-O2` - Optimization level 2
- `-liphlpapi` - Link against IP Helper API library
- `-lws2_32` - Link against Winsock 2 library
- `-ladvapi32` - Link against Advanced Windows 32 API (for registry access)
- `-static-libgcc -static-libstdc++` - Statically link runtime libraries (optional, for standalone executable)

### Step 3: Run the Application

The compiled executable `dns_toggle.exe` must be run with Administrator privileges:

```bash
# Right-click dns_toggle.exe and select "Run as administrator"
```

Or from an elevated command prompt:

```bash
dns_toggle.exe
```

## One-Line Build Command

For convenience, here's a single command that performs both steps:

```bash
windres resource.rc -O coff -o resource.o && g++ -std=c++17 -municode -mwindows -O2 main.cpp resource.o -o dns_toggle.exe -liphlpapi -lws2_32 -ladvapi32 -static-libgcc -static-libstdc++
```

## Troubleshooting

### "UNICODE redefined" warning

This warning is harmless - it occurs because `-municode` defines `UNICODE` automatically. You can safely ignore it, or remove the `#define UNICODE` line from the top of `main.cpp` if it bothers you.

### Visual Styles Not Applied

If the application doesn't show modern controls, verify that:
1. The manifest is properly embedded (check with Resource Hacker or similar tool)
2. You're running on Windows XP or later with Common Controls 6.0 available

### Application Crashes on Startup

Make sure you're running the application as Administrator. The DNS modification APIs require elevated privileges.

## Distribution

The final `dns_toggle.exe` is a standalone executable that can be distributed without additional dependencies (thanks to static linking). Users must run it as Administrator to modify DNS settings.

## Alternative: Using MSVC

If you prefer Microsoft Visual C++:

```bash
rc resource.rc
cl /std:c++17 /EHsc /O2 /Fe:dns_toggle.exe main.cpp resource.res iphlpapi.lib ws2_32.lib /link /SUBSYSTEM:WINDOWS
```