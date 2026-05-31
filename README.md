# SteamlessController DualShock 4 Fork

[한국어 설명](README-fork-KR.md)

This fork adds DualShock 4 output support to SteamlessController, mainly for using a Steam Controller as a native DualShock 4-compatible controller in Steam games.

## Fork Notes

- Uses [VIIPER](https://github.com/Alia5/VIIPER) as the virtual controller backend; place `viiper.exe` next to `SteamlessController.exe`.
- VIIPER does not use ViGEmBus, but Windows still requires the generic [usbip-win2](https://github.com/vadimgrn/usbip-win2) driver so virtual USB devices can attach to the OS.
- For Steam usage, install [HidHide](https://github.com/nefarius/HidHide/releases/latest) so SteamlessController can hide the physical Steam Controller/Puck while Steamless Mode is active.
- When using HidHide with Steam, enable Steamless Mode first, then restart Steam. If Steam is already running, Steam may see both the Steam Controller and the virtual DualShock 4 at the same time, causing duplicate inputs.
- Might also need [Microsoft Visual C++ Redistributable 2015-2022 x64](https://aka.ms/vc14/vc_redist.x64.exe), if the app does not open.
- Download `SteamlessController.exe` from the [latest release](https://github.com/david419kr/steamless-controller-dualshock4/releases/latest), place `viiper.exe` next to it, and run it directly.
- With HidHide enabled, Steam can see the virtual DualShock 4 instead of the original Steam Controller, allowing native DualShock 4 input paths in supported games.
- To use native gyro in Steam games with DualShock 4 support, you may need to right-click the game in Steam, open Properties, and disable Steam Input for that game.
- DualShock 4 mode supports buttons, sticks, triggers, native gyro/accelerometer output, touchpad output, rumble, and some of enhanced Steam Controller built-in trackpad haptics.
- HidHide + DualShock 4 mode has been tested in Steam with native gyro controls working in **Pragmata**.
- Native gyro input has also been tested outside Steam in **Cemu**.
- L4/L5/R4/R5 back-button mapping is supported.

<img width="413" height="252" alt="image" src="https://github.com/user-attachments/assets/770f88d3-5fc4-459d-a453-0941e204fa30" />
<img width="512" height="239" alt="image" src="https://github.com/user-attachments/assets/a7ffceb8-4ac2-4f34-88f3-1833251141af" />
<img width="342" height="217" alt="image" src="https://github.com/user-attachments/assets/6a95ed72-a9cd-46ff-becc-6bc9d8e5e9b6" />
<img width="425" height="361" alt="image" src="https://github.com/user-attachments/assets/90992791-e4dd-4156-a173-022f305af0ec" />


---

# SteamlessController

A lightweight Windows system tray app that lets you use a **Steam Controller** as a standard gamepad — without Steam running.

<img width="261" height="194" alt="image" src="https://github.com/user-attachments/assets/8e4a1355-d854-4b67-a486-590d225700f5" />

When **Steamless Mode** is active, the app disables the controller's built-in keyboard/mouse emulation (lizard mode) and exposes it as a virtual Xbox 360 or DualShock 4 controller via VIIPER, making it compatible with games that support XInput, Xbox controllers, or native DualShock 4 input.

## Features

- System tray icon shows connection and mode status
- **Steamless Mode** — disables lizard mode and exposes controller as Xbox 360 gamepad
- **Trackpad Mouse** — use the right (or left) trackpad as a mouse cursor
- **Back Buttons for Clicking** — map R4/R5 (or L4/L5) to left/right mouse click
- **Use Left Trackpad Instead** — mirror all trackpad/back-button functionality to the left side for left-handed users
- **Start with Windows** — launch automatically at login
- Settings persist across restarts
- Single-instance guard — safe to leave running

<img width="482" height="302" alt="image" src="https://github.com/user-attachments/assets/62e274a5-9d23-4af2-aaca-0f3ecdca3feb" />


## Requirements

### To run
- Windows 10 or later (64-bit)
- `viiper.exe` v0.6.1 or newer next to `SteamlessController.exe`
- [usbip-win2](https://github.com/vadimgrn/usbip-win2) installed
- Steam Controller (VID `0x28DE` / PID `0x1302`)
- Steam **closed** (Steam claims the controller when running)

### To build
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- [CMake](https://cmake.org/download/) 3.20 or later (included with Visual Studio, or install separately)
- Windows SDK 10.0.22000 or later (installed via Visual Studio Installer)

## Building

```bat
git clone https://github.com/your-username/SteamlessController.git
cd SteamlessController
cmake -B build
cmake --build build --target SteamlessController
```

The executable will be at `build\Debug\SteamlessController.exe`.

For a release build:

```bat
cmake -B build/release -G "Visual Studio 18 2025"
cmake --build build/release --config Release --target SteamlessController
```

> If you have Visual Studio 2022, replace `"Visual Studio 18 2025"` with `"Visual Studio 17 2022"`.

## CMake Targets

| Target | Description |
|---|---|
| `SteamlessController` | Main system tray application |
| `SteamlessControllerTests` | Assert-based conversion and VIIPER protocol helper tests. |
| `SteamProbe` | Console diagnostic tool — dumps raw HID report bytes as you interact with the controller. Useful for protocol research. |
| `RawControllerProbe` | Checks whether `Windows.Gaming.Input.RawGameController` can enumerate the Steam Controller (requires WinRT). |

## How it works

The Steam Controller exposes a vendor HID collection (usage page `0xFF00`) that carries all game input in a 54-byte report (ID `0x42`) at ~60 Hz. By default the firmware runs in **lizard mode**, emulating a keyboard and mouse so the controller works without drivers.

SteamlessController sends HID feature reports to disable lizard mode, then reads the raw input reports and translates them into VIIPER Xbox 360 or DualShock 4 input states. A background heartbeat re-sends the disable command every 800 ms to keep lizard mode off.

The full input report layout is documented in [`src/steam/SteamController.h`](src/steam/SteamController.h).

## Third-party

- [VIIPER](https://github.com/Alia5/VIIPER) — GPLv3, used as a sidecar virtual USB/controller backend
