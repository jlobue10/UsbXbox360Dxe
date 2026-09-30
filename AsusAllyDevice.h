/** @file
  ASUS ROG Ally Device Detection and Handling
  
  This module implements DirectInput support for ASUS ROG Ally devices that
  do not provide XInput mode.
  
  HID protocol specification reference:
  - https://github.com/flukejones/linux (wip/ally-6.14-refactor branch)
    drivers/hid/asus-ally-hid/ by Luke Jones <luke@ljones.dev>
  
  Copyright (c) 2024-2025. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _ASUS_ALLY_DEVICE_H_
#define _ASUS_ALLY_DEVICE_H_

#include <Uefi.h>
#include <Protocol/UsbIo.h>

//
// ASUS ROG Ally Device IDs
//
#define ASUS_VENDOR_ID  0x0B05

// ASUS ROG Ally X (DirectInput only, no XInput)
// Original Ally (0x1ABE) has XInput and is not handled here
#define ASUS_ALLY_X_PID 0x1B4C

//
// ASUS ROG Ally USB Endpoint Addresses
// Ally X has multiple interfaces, we need the gamepad one (0x87)
//
#define HID_ALLY_X_INTF_IN         0x87  // Gamepad interface (Ally X)

//
// ASUS ROG Ally X HID Report Structure
//
// Report ID: 0x0B
// Report Length: 16 bytes (after report ID byte)
//
#pragma pack(1)
typedef struct {
  UINT8   ReportId;        // 0x0B
  
  // Analog Sticks (8 bytes, 4x UINT16)
  // Raw values are 0-65535, subtract 32768 to get -32768 ~ 32767
  UINT16  LeftStickX;      // Left stick X-axis
  UINT16  LeftStickY;      // Left stick Y-axis
  UINT16  RightStickX;     // Right stick X-axis
  UINT16  RightStickY;     // Right stick Y-axis
  
  // Triggers (4 bytes, 2x UINT16)
  // Raw values are 0-1023 (10-bit precision)
  UINT16  LeftTrigger;     // Left trigger (0-1023)
  UINT16  RightTrigger;    // Right trigger (0-1023)
  
  // Buttons (4 bytes)
  UINT8   Buttons[4];      // Button states
} ASUS_ALLY_HID_REPORT;
#pragma pack()

//
// ASUS ROG Ally Button Bit Definitions
//
// Buttons[0] - byte offset 13 in report
#define ALLY_BTN_A        BIT0  // A button
#define ALLY_BTN_B        BIT1  // B button
#define ALLY_BTN_X        BIT2  // X button
#define ALLY_BTN_Y        BIT3  // Y button
#define ALLY_BTN_LB       BIT4  // Left bumper
#define ALLY_BTN_RB       BIT5  // Right bumper
#define ALLY_BTN_VIEW     BIT6  // View/Select button
#define ALLY_BTN_MENU     BIT7  // Menu/Start button

// Buttons[1] - byte offset 14 in report
#define ALLY_BTN_L3       BIT0  // Left stick press
#define ALLY_BTN_R3       BIT1  // Right stick press
#define ALLY_BTN_MODE     BIT2  // Mode/Guide button

// Buttons[2] - byte offset 15 in report
// Contains D-Pad as hatswitch value (0-8)

// D-Pad hatswitch values (in Buttons[2])
#define ALLY_DPAD_NEUTRAL     0
#define ALLY_DPAD_UP          1
#define ALLY_DPAD_UP_RIGHT    2
#define ALLY_DPAD_RIGHT       3
#define ALLY_DPAD_DOWN_RIGHT  4
#define ALLY_DPAD_DOWN        5
#define ALLY_DPAD_DOWN_LEFT   6
#define ALLY_DPAD_LEFT        7
#define ALLY_DPAD_UP_LEFT     8

/**
  Check if the given USB device is an ASUS ROG Ally X device.
  
  Only Ally X is supported. Original Ally has XInput and doesn't need this.
  
  @param  UsbIo    Pointer to USB I/O Protocol
  
  @retval TRUE     Device is ASUS ROG Ally X gamepad interface
  @retval FALSE    Device is not Ally X or wrong interface
**/
BOOLEAN
IsAsusAlly (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

/**
  Initialize ASUS ROG Ally X device for input.
  
  @param  UsbIo    Pointer to USB I/O Protocol
  
  @retval EFI_SUCCESS     Device initialized successfully
  @retval Other           Initialization failed
**/
EFI_STATUS
InitializeAsusAlly (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

//
// Consecutive non-timeout poll errors after which polling stops
//
#define ALLY_MAX_POLL_ERRORS  5

/**
  Minimal ASUS ROG Ally X initialization (standard HID requests only, no
  vendor commands, so the MCU does not re-enumerate).

  @param  UsbIo    Pointer to USB I/O Protocol

  @retval EFI_SUCCESS     Always
**/
EFI_STATUS
InitializeAsusAllyMinimal (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

/**
  Parse ASUS ROG Ally X HID report and convert to Xbox 360 format.
  
  @param  AllyReport   Pointer to Ally X HID report data (Report ID 0x0B)
  @param  ReportLen    Length of the report (17 bytes)
  @param  XboxReport   Output buffer for Xbox 360 format report (20 bytes)
  
  @retval EFI_SUCCESS     Report converted successfully
  @retval Other           Conversion failed
**/
EFI_STATUS
ConvertAsusAllyToXbox360 (
  IN  VOID    *AllyReport,
  IN  UINTN   ReportLen,
  OUT UINT8   *XboxReport
  );

/**
  Polling timer callback for ASUS ROG Ally devices that don't support
  async interrupt transfers.
  
  @param  Event     The timer event
  @param  Context   Pointer to USB_KB_DEV instance
**/
VOID
EFIAPI
AsusAllyPollingHandler (
  IN  EFI_EVENT  Event,
  IN  VOID       *Context
  );

#endif // _ASUS_ALLY_DEVICE_H_

