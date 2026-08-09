/** @file
  Valve Steam Controller (2026, codename "Triton") support via the 2.4GHz
  wireless "puck" dongle.

  The puck (VID 0x28DE, PID 0x1304; the Steam Frame's "Nereid" dongle is
  0x1305 and shares the protocol) enumerates with seven interfaces:

    0-1  CDC ACM pair (firmware/debug serial, ignored)
    2-5  HID controller slots, one per paired controller, 64-byte
         interrupt IN/OUT endpoints
    6    HID dongle management (pairing), ignored

  Each controller-slot interface carries three top-level HID collections:
  a lizard-mode mouse (report ID 0x40), a lizard-mode keyboard (0x41), and
  a vendor collection with the native controller state. None of them are
  HID *boot* protocol (subclass 0, protocol 0), so firmware USB keyboard
  drivers ignore these interfaces entirely -- lizard mode does not work in
  UEFI, which is why this native handler exists.

  Native state arrives unsolicited (verified against live hardware: no
  activation command is needed) as report ID 0x42, ~260 Hz, with 0x45
  (BLE-framed state) and 0x47 (newer "Ibex" framing with trackpad
  timestamps) sharing the same layout through the stick fields. All other
  report IDs on the endpoint (lizard mouse/keyboard echoes, 0x43 battery,
  0x79 wireless connect/disconnect, 0x7B) are ignored; state reports simply
  stop when the controller drops off the radio, so no connect bookkeeping
  is needed.

  Protocol reference: SDL's SDL_hidapi_steam_triton.c and
  src/joystick/hidapi/steam/controller_structs.h (TritonMTUNoQuat_t),
  cross-checked against a live descriptor dump and idle/deflected state
  captures from puck firmware of 2026-08.

  Copyright (c) 2026, jlobue10. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _TRITON_DEVICE_H_
#define _TRITON_DEVICE_H_

#include <Uefi.h>
#include <Protocol/UsbIo.h>
#include <Protocol/DriverBinding.h>

//
// Valve Steam Controller puck dongle IDs
//
#define VALVE_VENDOR_ID           0x28DE
#define TRITON_PID_PROTEUS_DONGLE 0x1304  // Steam Controller puck
#define TRITON_PID_NEREID_DONGLE  0x1305  // Steam Frame dongle (same protocol)

//
// Controller-slot interface range on the dongle (SDL binds the same range)
//
#define TRITON_SLOT_INTERFACE_FIRST  2
#define TRITON_SLOT_INTERFACE_LAST   5

//
// USB HID interface class (same value as CLASS_HID in EfiKey.h, which this
// header cannot include without a cycle)
//
#define TRITON_USB_CLASS_HID  3

//
// Native state report IDs (ETritonReportIDTypes in SDL). The three state
// variants are identical through the stick fields parsed here.
//
#define TRITON_REPORT_STATE            0x42
#define TRITON_REPORT_STATE_BLE        0x45
#define TRITON_REPORT_STATE_TIMESTAMP  0x47

//
// Lizard-mode disable command, sent as a 64-byte HID feature report.
// Because the report is numbered (ID 0x01), the report ID is the first
// byte of the Set_Report data stage (HID 1.11 sec. 7.2.2) -- hidapi/SDL
// put it in buffer[0] and send all 64 bytes, and the FeatureReportMsg
// starts at offset 1. Field values from SDL's controller_constants.h;
// the controller re-arms lizard mode a few seconds after the last
// command, so SDL re-sends it every 3 seconds and this driver does the
// same from a periodic timer.
//
#define TRITON_FEATURE_REPORT_ID     0x01
#define TRITON_FEATURE_REPORT_BYTES  64
#define TRITON_ID_SET_SETTINGS       0x87  // ID_SET_SETTINGS_VALUES
#define TRITON_SETTING_LIZARD_MODE   9     // SETTING_LIZARD_MODE
#define TRITON_LIZARD_MODE_OFF       0     // LIZARD_MODE_OFF

//
// Keepalive period: 2 seconds in 100ns units (SDL uses 3s; the re-arm
// window is not documented, so stay comfortably inside it)
//
#define TRITON_LIZARD_TIMER_PERIOD   20000000

//
// Leading fields of a Triton state report as it arrives on the interrupt
// endpoint (report ID included; packed, little-endian). The full 0x42
// report is 54 bytes -- trackpad and IMU fields follow these -- but only
// this prefix is consumed here.
//
#pragma pack(1)
typedef struct {
  UINT8   ReportId;      // 0x42 / 0x45 / 0x47
  UINT8   SeqNum;
  UINT32  Buttons;       // TRITON_BTN_* bits
  INT16   TriggerLeft;   // 0 - 32767
  INT16   TriggerRight;  // 0 - 32767
  INT16   LeftStickX;    // -32768 - 32767, Y positive up
  INT16   LeftStickY;
  INT16   RightStickX;
  INT16   RightStickY;
} TRITON_STATE_PREFIX;
#pragma pack()

#define TRITON_STATE_PREFIX_LEN  sizeof (TRITON_STATE_PREFIX)  // 18 bytes

//
// Button bits (TritonButtons in SDL). The MENU bit is the View/Back
// position and the VIEW bit the Start/Menu position -- SDL maps them to
// BACK and START respectively; that mapping is kept here.
//
#define TRITON_BTN_A               0x00000001
#define TRITON_BTN_B               0x00000002
#define TRITON_BTN_X               0x00000004
#define TRITON_BTN_Y               0x00000008
#define TRITON_BTN_QAM             0x00000010
#define TRITON_BTN_R3              0x00000020
#define TRITON_BTN_VIEW            0x00000040
#define TRITON_BTN_R4              0x00000080
#define TRITON_BTN_R5              0x00000100
#define TRITON_BTN_RB              0x00000200
#define TRITON_BTN_DPAD_DOWN       0x00000400
#define TRITON_BTN_DPAD_RIGHT      0x00000800
#define TRITON_BTN_DPAD_LEFT       0x00001000
#define TRITON_BTN_DPAD_UP         0x00002000
#define TRITON_BTN_MENU            0x00004000
#define TRITON_BTN_L3              0x00008000
#define TRITON_BTN_STEAM           0x00010000
#define TRITON_BTN_L4              0x00020000
#define TRITON_BTN_L5              0x00040000
#define TRITON_BTN_LB              0x00080000
#define TRITON_BTN_RPAD_CLICK      0x00400000
#define TRITON_BTN_LPAD_CLICK      0x04000000

/**
  Check if the given USB interface is a controller slot of a Steam
  Controller puck (or Steam Frame) dongle.

  @param  UsbIo    Pointer to USB I/O Protocol (one interface's instance)

  @retval TRUE     Interface is a puck controller slot
  @retval FALSE    Different device, or a non-slot interface of the puck
**/
BOOLEAN
IsTritonController (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

/**
  Send the lizard-mode disable command to a puck controller slot.

  Harmless when no controller is linked to the slot (the dongle just
  drops or stalls the request).

  @param  UsbIo    Pointer to USB I/O Protocol of the slot interface

  @retval EFI_SUCCESS  Command sent
  @retval Other        USB transfer failed
**/
EFI_STATUS
SendTritonLizardDisable (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

/**
  Periodic timer callback re-sending the lizard-mode disable command so
  native state reports keep flowing (the controller re-arms lizard mode
  without a refresh).

  @param  Event     The timer event
  @param  Context   Pointer to USB_KB_DEV instance
**/
VOID
EFIAPI
TritonLizardTimerHandler (
  IN  EFI_EVENT  Event,
  IN  VOID       *Context
  );

/**
  Parse a Triton native state report and convert to Xbox 360 format.

  @param  RawReport    Raw interrupt transfer data (report ID first)
  @param  ReportLen    Length of the raw data
  @param  XboxReport   Output buffer for Xbox 360 format report (20 bytes)

  @retval EFI_SUCCESS            Report converted successfully
  @retval EFI_INVALID_PARAMETER  Not a state report (other IDs share the
                                 endpoint and must be ignored by the caller)
**/
EFI_STATUS
ConvertTritonToXbox360 (
  IN  VOID    *RawReport,
  IN  UINTN   ReportLen,
  OUT UINT8   *XboxReport
  );

/**
  One-shot sweep of the puck's USB I/O handles that reconnects
  foreign-held or unconnected HID interfaces to this driver.

  The platform's own USB HID driver can hold the controller-slot
  interfaces BY_DRIVER before this driver probes them (observed with the
  AMI USB Hid Driver on the 2026-08-08 field log), which makes
  Supported() fail silently on the slots; the dongle-management interface
  is then the only foothold, and its Start() calls this to pull the slots
  over. DEBUG builds additionally dump the active configuration
  descriptor and name the drivers found holding each interface.

  Must be called at TPL_APPLICATION (after RestoreTPL): it calls
  DisconnectController/ConnectController.

  @param  This     This driver's driver binding protocol
  @param  UsbIo    USB I/O Protocol of the already-bound puck interface
**/
VOID
TritonReclaimPuckInterfaces (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_USB_IO_PROTOCOL          *UsbIo
  );

#endif // _TRITON_DEVICE_H_
