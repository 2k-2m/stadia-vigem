# Stadia-ViGEm

Xbox 360 controller emulation for Stadia controller. Supports controllers connected via USB & bluetooth. Supports multiple devices and vibration (wired only). Forked from Mi-ViGEm (https://github.com/grayver/Mi-ViGEm) by grayver.
Xbox 360 controller emulation driver is provided by ViGEm (https://github.com/ViGEm/ViGEmBus), by Benjamin Höglinger.

## Requirements
- Windows 11 (should work on Windows 7-10 also)
- ViGEm bus installed (can be downloaded [here](https://github.com/ViGEm/ViGEmBus/releases))

## How it works
Stadia-ViGEm program at start scans for Stadia Controllers and then proxies found Stadia Controllers to virtual Xbox 360 gamepads (with help from ViGEmBus). Also Stadia-ViGEm subscribes to system device plug/unplug notifications and rescans for devices on each notification.
All found devices are displayed in the tray icon context menu. Manual device rescan can be initiated via the tray icon context menu.

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

## Nota sobre vibracion por Bluetooth (issue #16)

Este fork agrega diagnostico para el problema reportado en el issue #16, donde la vibracion no funciona al conectar el control por Bluetooth. Se confirmo que el control usa BLE (HID over GATT), y que el reporte HID que se envia es correcto: mismo ID, tamano y formato que en la conexion por USB, donde la vibracion si funciona sin problema.

El fallo esta del lado de Windows. Tanto `WriteFile` como `HidD_SetOutputReport` (en sus distintas variantes, con handle sincrono y asincrono) devuelven el error 87 (`ERROR_INVALID_PARAMETER`) al intentar enviar el reporte de salida por Bluetooth LE. Esto indica que el propio driver Bluetooth LE de Windows rechaza el envio, no el codigo de este proyecto. Coincide con lo que reporto el equipo de reWASD tras investigar el mismo problema. En Linux y macOS la vibracion funciona correctamente por Bluetooth, lo que refuerza que se trata de una limitacion especifica de Windows.

El logging de diagnostico usado para esta investigacion esta en `libstadia/src/hid.c`, controlado por la macro `STADIA_DEBUG_LOGGING` (desactivada por defecto). Para reproducir los logs basta con poner esa macro en `1` y recompilar.

Fork de [walkco/stadia-vigem](https://github.com/walkco/stadia-vigem), basado en [Mi-ViGEm](https://github.com/grayver/Mi-ViGEm) de grayver, con el driver [ViGEmBus](https://github.com/ViGEm/ViGEmBus) de Nefarius.
