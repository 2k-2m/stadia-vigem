# Stadia-ViGEm

Xbox 360 controller emulation for Stadia controller. Supports controllers connected via USB & bluetooth. Supports multiple devices and vibration (wired only). Forked from Mi-ViGEm (https://github.com/grayver/Mi-ViGEm) by grayver.
Xbox 360 controller emulation driver is provided by ViGEm (https://github.com/ViGEm/ViGEmBus), by Benjamin Höglinger.

## Requirements
- Windows 11 (should work on Windows 7-10 also)
- ViGEm bus installed (can be downloaded [here](https://github.com/ViGEm/ViGEmBus/releases))

## How it works
Stadia-ViGEm program at start scans for Stadia Controllers and then proxies found Stadia Controllers to virtual Xbox 360 gamepads (with help from ViGEmBus). Also Stadia-ViGEm subscribes to system device plug/unplug notifications and rescans for devices on each notification.
All found devices are shown in a flyout panel when you click the tray icon, with their connection type (USB or Bluetooth). The panel updates live as controllers connect or disconnect, and has Refresh (manual rescan), Quit and Hide buttons.

## Tray icon and panel
The tray icon changes each time a controller connects (white, black or wasabi, like the Stadia controller colors) and turns blue when no controller is connected or ViGEmBus has an error. Clicking it opens a small panel that stays open until you press Hide or Esc, and can be dragged anywhere.

## Double input
Stadia-ViGEm creates a virtual Xbox 360 controller which results in double input issues when some applications will read input from both the virtual and the real Stadia controller. To avoid this, install [HidHide](https://github.com/ViGEm/HidHide) and configure it as follows:
 - Open HidHide Configuration Client
 - On Applications tab:
   - Click "+" button
   - Browse to the Stadia-ViGEm executable you normally use (Stadia-ViGEm-x86.exe or Stadia-ViGEm-x64.exe)
 - On Devices tab:
   - Tick box next to the Stadia controller entry (wired controllers are named "Google LLC Stadia Controller rev. A" & bluetooth controllers are named "HID-compliant game controller")
   - Tick "Enable device hiding" at the bottom of the window
 - Reboot your PC

After this, only Stadia-ViGEm will be able to see the real controller. Note: This means that whenever Stadia-ViGEm isn't running, the controller will not be able to control anything on your PC.

## Thanks to

grayver, the developer of Mi-ViGEm that makes up 95% of this program.

This project is inspired by following projects written on C#:
- https://github.com/irungentoo/Xiaomi_gamepad
- https://github.com/dancol90/mi-360

Thanks to following libraries and resources:
- https://github.com/libusb/hidapi for HID implementation
- https://github.com/zserge/tray for lightweight tray app implementation
- https://www.flaticon.com/authors/freepik for application icon

## Note on Bluetooth vibration (issue #16)

This fork adds diagnostics for the problem reported in issue #16, where vibration does not work when the controller is connected over Bluetooth. It was confirmed that the controller uses BLE (HID over GATT), and that the HID report being sent is correct: same ID, size and format as over USB, where vibration works without any problem.

The failure is on the Windows side. Both `WriteFile` and `HidD_SetOutputReport` (in their different variants, with synchronous and asynchronous handles) return error 87 (`ERROR_INVALID_PARAMETER`) when trying to send the output report over Bluetooth LE. This indicates that the Windows Bluetooth LE driver itself rejects the write, not this project's code. It matches what the reWASD team reported after investigating the same problem. On Linux and macOS vibration works correctly over Bluetooth, which reinforces that this is a Windows-specific limitation.

The diagnostic logging used for this investigation is in `libstadia/src/hid.c`, controlled by the `STADIA_DEBUG_LOGGING` macro (disabled by default). To reproduce the logs, set that macro to `1` and rebuild.

Fork of [walkco/stadia-vigem](https://github.com/walkco/stadia-vigem), based on [Mi-ViGEm](https://github.com/grayver/Mi-ViGEm) by grayver, with the [ViGEmBus](https://github.com/ViGEm/ViGEmBus) driver by Nefarius.
