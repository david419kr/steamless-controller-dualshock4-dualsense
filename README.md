# SteamlessController VIIPER Backend Fork

[한국어 설명](README-fork-KR.md)

This fork turns a Steam Controller into a VIIPER-backed virtual Xbox 360 or DualShock 4 controller. The main branch goal is parity with the original Xbox 360 behavior plus native DualShock 4 features for games that support them.

## Fork Notes

- Uses a patched [VIIPER](https://github.com/Alia5/VIIPER) sidecar as the virtual controller backend.
- Release builds should ship `SteamlessController.exe` and the patched `viiper.exe` side by side.
- Windows requires the generic [usbip-win2](https://github.com/vadimgrn/usbip-win2) driver so VIIPER can attach virtual USB devices to the OS.
- For Steam usage, install [HidHide](https://github.com/nefarius/HidHide/releases/latest) so SteamlessController can hide the physical Steam Controller/Puck while Steamless Mode is active.
- When using HidHide with Steam, enable Steamless Mode first, then restart Steam. If Steam is already running, Steam may see both the physical controller and the virtual DualShock 4 at the same time.
- Might also need [Microsoft Visual C++ Redistributable 2015-2022 x64](https://aka.ms/vc14/vc_redist.x64.exe), if the app does not open.
- Download the app from the [latest release](https://github.com/david419kr/steamless-controller-dualshock4/releases/latest), keep `viiper.exe` next to it, and run it directly.
- To use native gyro in Steam games with DualShock 4 support, you may need to right-click the game in Steam, open Properties, and disable Steam Input for that game.
- DualShock 4 mode supports buttons, sticks, triggers, native gyro/accelerometer output, touchpad output, rumble, Steam Controller trackpad haptics, trackpad D-pad, and L4/L5/R4/R5 back-button mapping.
- HidHide + DualShock 4 mode has been tested in Steam with native gyro controls working in **Pragmata**.
- Native gyro input has also been tested outside Steam in **Cemu**.

<img width="413" height="252" alt="image" src="https://github.com/user-attachments/assets/770f88d3-5fc4-459d-a453-0941e204fa30" />
<img width="512" height="239" alt="image" src="https://github.com/user-attachments/assets/a7ffceb8-4ac2-4f34-88f3-1833251141af" />
<img width="342" height="217" alt="image" src="https://github.com/user-attachments/assets/6a95ed72-a9cd-46ff-becc-6bc9d8e5e9b6" />
<img width="425" height="361" alt="image" src="https://github.com/user-attachments/assets/90992791-e4dd-4156-a173-022f305af0ec" />

---

# SteamlessController

A lightweight Windows system tray app that lets you use a **Steam Controller** as a standard gamepad without Steam running.

<img width="261" height="194" alt="image" src="https://github.com/user-attachments/assets/8e4a1355-d854-4b67-a486-590d225700f5" />

When **Steamless Mode** is active, the app disables the controller's built-in keyboard/mouse emulation, reads raw HID input from the Steam Controller, and exposes it as a virtual Xbox 360 or DualShock 4 controller through VIIPER.

## Features

- System tray icon shows connection and mode status
- **Steamless Mode** exposes the controller as Xbox 360 or DualShock 4
- **Output Mode** switches between Xbox 360 and DualShock 4
- **Trackpad Mouse** uses the right or left trackpad as a mouse cursor
- **Trackpad D-pad** maps one trackpad to D-pad directions
- **Back Button Mappings** maps L4/L5/R4/R5 to gamepad buttons
- **Use Left Trackpad Instead** mirrors trackpad/back-button mouse functionality to the left side
- **Hide Original Controller** integrates with HidHide when installed
- **Restart Steam** tray action restarts Steam after hide/show changes
- **Start with Windows** launches automatically at login
- Settings persist across restarts
- Single-instance guard is safe to leave running

<img width="482" height="302" alt="image" src="https://github.com/user-attachments/assets/62e274a5-9d23-4af2-aaca-0f3ecdca3feb" />

## Requirements

### To run

- Windows 10 or later, 64-bit
- `SteamlessController.exe`
- Patched `viiper.exe` built from this branch, next to `SteamlessController.exe`
- [usbip-win2](https://github.com/vadimgrn/usbip-win2) installed
- Steam Controller VID `0x28DE` / PID `0x1302` or `0x1304`
- Optional: [HidHide](https://github.com/nefarius/HidHide/releases/latest) for hiding the physical controller from Steam

### To build

- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- [CMake](https://cmake.org/download/) 3.20 or later
- Windows SDK 10.0.22000 or later
- [Git](https://git-scm.com/download/win)
- [Go](https://go.dev/dl/) 1.26 or later, for building the patched VIIPER sidecar

## Building

Build the app plus patched sidecar with the preset:

```bat
git clone https://github.com/david419kr/steamless-controller-dualshock4.git
cd steamless-controller-dualshock4
cmake --preset release
cmake --build --preset release
```

The release output is placed under `build\release\Release` when using Visual Studio generators. It should contain both `SteamlessController.exe` and `viiper.exe`.

You can also build the patched VIIPER sidecar directly:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-viiper.ps1 -OutputDir build\release\Release
```

The script clones upstream VIIPER `v0.6.1`, applies `third_party\viiper-patches\viiper-v0.6.1-ds4-compat.patch`, builds `viiper.exe`, and marks it as `v0.6.1-steamless3`. Stock `v0.6.1` is intentionally rejected in DualShock 4 mode because it does not expose the DS4 reports required by native gyro/touch clients.

## Bundled Installer

Build a single offline installer that contains the app, patched `viiper.exe`, Microsoft Visual C++ Redistributable 2015-2022 x64, usbip-win2, and the optional HidHide installer:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-installer.ps1
```

The installer output is `build\installer\SteamlessController-Setup.exe`. It installs VC++ and usbip-win2 automatically when missing. HidHide is presented as an optional task and is checked by default because it is recommended for Steam duplicate-input prevention.

The build script downloads the prerequisite installers into `build\prereqs`, writes `build\prereqs\prereqs.json` with source URLs and SHA-256 hashes, then compiles `resources\InnoInstallerScript.iss` with Inno Setup 6. If `ISCC.exe` is not on `PATH`, install Inno Setup 6 first.

You can also use the CMake preset after configuring:

```powershell
cmake --preset release
cmake --build --preset installer
```

When SteamlessController starts the bundled VIIPER sidecar, VIIPER logs to `%LOCALAPPDATA%\SteamlessController\viiper.log`. This keeps installed builds from trying to write logs under `Program Files`.

## CMake Targets

| Target | Description |
|---|---|
| `SteamlessController` | Main system tray application |
| `ViiperSidecar` | Builds `SteamlessController`, then builds/copies the patched `viiper.exe` next to it |
| `PackageInstaller` | Builds a bundled Inno Setup installer with app, VIIPER sidecar, VC++ runtime, usbip-win2, and optional HidHide |
| `SteamlessControllerTests` | Assert-based conversion and VIIPER protocol helper tests |
| `SteamProbe` | Console diagnostic tool that dumps raw HID report bytes |
| `RawControllerProbe` | Checks whether `Windows.Gaming.Input.RawGameController` can enumerate the Steam Controller |

## How it works

The Steam Controller exposes a vendor HID collection, usage page `0xFF00`, that carries all game input in a 54-byte report, ID `0x42`, at about 60 Hz. By default the firmware runs in keyboard/mouse emulation mode so the controller works without drivers.

SteamlessController sends HID feature reports to disable that firmware mode, reads raw input reports, and translates them into VIIPER Xbox 360 or DualShock 4 input states. A background heartbeat re-sends the disable command every 800 ms to keep the controller in gamepad mode.

The full input report layout is documented in [`src/steam/SteamController.h`](src/steam/SteamController.h).

## Third-party

- [VIIPER](https://github.com/Alia5/VIIPER) - GPLv3, sidecar virtual USB/controller backend. Release bundles that include `viiper.exe` must include its license and corresponding source/patch notice.
- [usbip-win2](https://github.com/vadimgrn/usbip-win2) - required OS driver for virtual USB attachment on Windows.
