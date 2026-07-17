# UEFI Driver for Xbox 360 Controller

> **Note:** This is a fork of **[SkorionOS/UsbXbox360Dxe](https://github.com/SkorionOS/UsbXbox360Dxe)** — all credit for the driver goes to the original project and its contributors. This fork adds Lenovo Legion Go 2 controller support (submitted upstream as [PR #6](https://github.com/SkorionOS/UsbXbox360Dxe/pull/6)) and stability fixes for intermittent load failures and lockups in rEFInd: ESP file logging is compiled out of release builds (it crashed on an uninitialized-variable path during driver load and could run at TPL_NOTIFY from USB callbacks), `config.ini.example` is written only once instead of on every boot, and the ASUS Ally polling path no longer submits a conflicting async transfer on the same endpoint. Prefer the upstream releases once these changes are merged there.

This driver is modified from [edk2](https://github.com/tianocore/edk2) USB keyboard driver, with AI-assistance. It provides full Xbox 360 controller support in UEFI environments with mouse emulation, enabling controller use in BIOS, bootloaders, and other UEFI applications.

## Features

- **40+ Built-in Device Support**: Xbox 360 controllers, handheld gaming devices (GPD, OneXPlayer, Legion Go, MSI Claw, etc.), 8BitDo, Logitech, HyperX, and more
- **DirectInput Support**: Native support for ASUS ROG Ally/Ally X and other DirectInput devices
- **Mouse Emulation**: Analog sticks can control mouse cursor and scroll wheel (via EFI_SIMPLE_POINTER_PROTOCOL)
- **Trigger Button Support**: Left and Right triggers can be mapped to keyboard keys or mouse buttons
- **Flexible Stick Modes**: Each stick can independently operate in Mouse, Keys, Scroll, or Disabled mode
- **Configuration File Support**: Customize settings via INI file on ESP partition with semantic key names
- **Semantic Key Names**: Use readable names like `KeyEnter`, `MouseLeft` instead of hex codes
- **Custom Device Support**: Add your own Xbox 360 protocol compatible devices
- **Auto-configuration**: Driver creates default config on first boot
- **Debug Logging**: Automatic logging to ESP partition with rotation and cleanup

## Default Key Mappings

### D-Pad & Face Buttons
- **D-Pad Up/Down/Left/Right**: Arrow Keys
- **A Button**: Enter
- **B Button**: Escape
- **X Button**: Backspace
- **Y Button**: Tab

### Shoulder Buttons & Triggers
- **Left/Right Shoulder (LB/RB)**: Page Up/Down
- **Left/Right Thumb Click (L3/R3)**: Left Control/Alt
- **Left Trigger (LT)**: Mouse Right Button (default threshold: 128/255)
- **Right Trigger (RT)**: Mouse Left Button (default threshold: 128/255)

### Special Buttons
- **Start**: Space
- **Back**: Tab
- **Guide**: Left Shift

### Analog Sticks
- **Left Stick**: Mouse cursor control (default mode)
  - Move stick to control mouse pointer
  - Sensitivity: 50/100
  - Max speed: 20 pixels/poll
  - Deadzone: 8000/32767
- **Right Stick**: Scroll wheel control (default mode)
  - Move stick up/down to scroll
  - Scroll sensitivity: 30/100
  - Deadzone: 8689/32767

> **Note**: Mouse functionality requires UEFI firmware with mouse support. Most modern UEFI implementations support this. Each stick can be independently configured for Mouse, Keys, Scroll, or Disabled mode via config file.

## Configuration

The driver supports configuration via an INI file on your ESP partition. On first boot, the driver will automatically create a default configuration file at `\EFI\Xbox360\config.ini`.

### Configuration File Location

- **Linux**: `/boot/efi/EFI/Xbox360/config.ini`
- **Windows**: `X:\EFI\Xbox360\config.ini` (where X: is your ESP mount point)

### Editing Configuration

1. Mount your ESP partition
2. Edit `\EFI\Xbox360\config.ini` (see `config.ini.example` for all options)
3. Reboot to apply changes

### Configuration Options

#### Basic Settings
- **Deadzone**: Analog stick deadzone (0-32767, default: 8000)
- **TriggerThreshold**: Trigger activation threshold (0-255, default: 128)
- **LeftTrigger/RightTrigger**: Trigger mappings (semantic names or hex codes)
  - Semantic names (recommended): `MouseLeft`, `MouseRight`, `KeyEnter`, `KeyEscape`, etc.
  - Mouse functions: `MouseLeft`, `MouseRight`, `MouseMiddle`, `ScrollUp`, `ScrollDown`
  - Hex codes (legacy): `0xF0-0xF4` for mouse, `0x00-0xE7` for keyboard keys

#### Stick Mode Configuration
Each stick can be configured independently:
- **LeftStickMode / RightStickMode**: `Mouse`, `Keys`, `Scroll`, or `Disabled`
- **Mouse Mode Settings**:
  - `MouseSensitivity`: 1-100 (default: 50)
  - `MouseMaxSpeed`: Max pixels per poll (default: 20)
  - `MouseCurve`: Response curve (1=Linear, 2=Square/recommended, 3=S-curve)
- **Scroll Mode Settings**:
  - `ScrollSensitivity`: 1-100 (default: 30)
  - `ScrollDeadzone`: Additional deadzone (default: 0, uses stick deadzone)
- **Keys Mode Settings**:
  - Direction mappings (e.g., `LeftStickUp`, `RightStickDown`)
  - `DirectionMode`: 4 or 8-way (default: 4)

#### Button Mappings
- **Semantic Key Names**: Use readable names for all button mappings
  - Common keys: `KeyEnter`, `KeyEscape`, `KeySpace`, `KeyTab`, `KeyBackspace`
  - Arrow keys: `KeyUp`, `KeyDown`, `KeyLeft`, `KeyRight`
  - Navigation: `KeyPageUp`, `KeyPageDown`, `KeyHome`, `KeyEnd`
  - Modifiers: `KeyLeftCtrl`, `KeyLeftAlt`, `KeyLeftShift`
  - Mouse: `MouseLeft`, `MouseRight`, `MouseMiddle`
  - Disable: `Disabled` or `0xFF`
- **Hex Codes**: Still supported for backward compatibility (e.g., `0x28` for Enter)
- See `config.ini.example` for complete key name reference

#### Other Options
- **Custom Devices**: Add your own Xbox 360 compatible devices

Example configuration:
```ini
Version=1.0
Deadzone=8000
TriggerThreshold=128

# Triggers as mouse buttons (default) - using semantic names
RightTrigger=MouseLeft
LeftTrigger=MouseRight

# Button customization with semantic names
ButtonA=KeyEnter
ButtonB=KeyEscape
ButtonStart=KeySpace

# Left stick: Mouse mode (default)
LeftStickMode=Mouse
LeftStickMouseSensitivity=50
LeftStickMouseMaxSpeed=20

# Right stick: Mouse mode (default) — both sticks drive the cursor
RightStickMode=Mouse
RightStickMouseSensitivity=50

# Alternative: Scroll mode (note: rEFInd ignores scroll input)
# RightStickMode=Scroll
# RightStickScrollSensitivity=30

# Alternative: Use Keys mode with semantic names
# LeftStickMode=Keys
# LeftStickUpMapping=KeyW
# LeftStickDownMapping=KeyS
# LeftStickLeftMapping=KeyA
# LeftStickRightMapping=KeyD

# Add custom devices
# [CustomDevices]
# Device1=0x1234:0x5678:My Custom Controller
```

## Adding Custom Devices

If your Xbox 360 compatible controller isn't recognized:

1. Find your device's VID and PID:
   - **Linux**: `lsusb`
   - **Windows**: Device Manager → Properties → Hardware Ids
   - **macOS**: System Information → USB

2. Add to config file:
   ```ini
   [CustomDevices]
   Device1=0x1234:0x5678:My Custom Xbox Controller
   ```

3. Reboot

The driver will log detected devices to debug output.

## Supported Devices

### Built-in Support (40+ devices)

- **Microsoft**: Xbox 360 Wired/Wireless controllers
- **Handheld Gaming Devices (XInput)**:
  - GPD Win 2
  - OneXPlayer
  - Lenovo Legion Go / Legion Go 2 / Legion Go S
  - MSI Claw
  - TECNO Pocket Go
  - ZOTAC Gaming Zone
- **Handheld Gaming Devices (DirectInput)**:
  - ASUS ROG Ally X
- **8BitDo**: Ultimate, Pro 2, SN30 Pro, Ultimate 2/2C
- **Logitech**: F310, F510, F710, Chillstream
- **HyperX**: Clutch (wired/wireless)
- **SteelSeries**: Stratus Duo
- **Razer**: Onza Tournament/Classic, Sabertooth
- And many more...

See `KeyBoard.c` for the complete list of supported devices.

### Scope: gamepads only, not built-in touchscreens

This driver binds `EFI_USB_IO_PROTOCOL`, so it only sees devices on the USB
bus. Handheld built-in touchscreens are **not** USB devices: on the ROG Xbox
Ally / Ally X the panel is a Goodix **GT7868Q** on the SoC's **I2C** bus,
serviced by `i2c-hid`/`hid-multitouch` under Linux. It never enumerates over
USB, so this driver structurally cannot see it, and no amount of report parsing
here can add touch support. Driving it in the pre-boot menu would require a
separate I2C controller + I2C-HID UEFI stack producing
`EFI_ABSOLUTE_POINTER_PROTOCOL` (which rEFInd already consumes natively) — a
distinct project, and one dependent on the firmware leaving the I2C controller
usable before OS handoff. External **USB** touchscreens are a different case and
could in principle be supported, but the built-in handheld panels cannot.

## Debug Logging

The driver automatically logs diagnostic information to your ESP partition for troubleshooting.

### Log File Location
- **Path**: `\EFI\Xbox360\driver_YYYYMMDD.log` (e.g., `driver_20251021.log`)
- **Linux**: `/boot/efi/EFI/Xbox360/driver_YYYYMMDD.log`
- **Windows**: `X:\EFI\Xbox360\driver_YYYYMMDD.log`

### Log Features
- **Daily rotation**: New log file created each day
- **Auto-cleanup**: Keeps last 5 log files
- **Max size**: 1MB per file (rotates when exceeded)
- **Timestamped entries**: Each log entry includes date and time

### What's Logged
- Driver initialization and device detection
- Configuration file loading/parsing
- USB device VID/PID for detected controllers
- Protocol installation status
- Error conditions and warnings

### Viewing Logs
1. Mount your ESP partition
2. Navigate to `\EFI\Xbox360\`
3. Open the most recent log file with a text editor

The log is useful for diagnosing why a controller isn't being detected or if configuration isn't loading properly.

## Troubleshooting

**Q: My controller isn't recognized**
A: Add it as a custom device in the config file (see above).

**Q: Mouse cursor doesn't appear**
A: Not all UEFI firmwares support mouse functionality. If your firmware doesn't show a mouse cursor, you can:
1. Switch sticks to `Keys` mode for keyboard-based navigation
2. Use triggers and buttons which still work as keyboard keys

**Q: Triggers aren't working**
A: Check that `TriggerThreshold` is appropriate for your controller (try lowering it).

**Q: Stick movement is too fast/slow**
A: Adjust sensitivity settings in config:
- For mouse mode: `LeftStickMouseSensitivity` (1-100)
- For scroll mode: `RightStickScrollSensitivity` (1-100)

**Q: Changes don't take effect**
A: Ensure:
1. The config file is in the correct location (`\EFI\Xbox360\config.ini`)
2. The file is saved in UTF-8 or ASCII format
3. You've rebooted after making changes
4. Check the log file (`\EFI\Xbox360\driver_YYYYMMDD.log`) for errors

**Q: How do I know if the driver is working?**
A: Try using the controller in your UEFI BIOS menu or boot manager. The controller should work for navigation. Check the log file for device detection info.

## License

This project inherits the license of original driver, BSD-2-Clause-Patent.

Copyright (c) 2025, Chenx Dust. All rights reserved.

Copyright (c) 2004 - 2018, Intel Corporation. All rights reserved.
