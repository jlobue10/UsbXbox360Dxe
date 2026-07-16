/** @file
  Lenovo Legion Go 2 controller support (vendor raw HID interface).

  See LegionGoDevice.h for the report layout and mode/PID overview.

  Copyright (c) 2026, jlobue10. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "LegionGoDevice.h"
#include "Xbox360Log.h"

#include <Library/BaseMemoryLib.h>

BOOLEAN
IsLegionGoRaw (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS                 Status;
  EFI_USB_DEVICE_DESCRIPTOR  DeviceDescriptor;

  if (UsbIo == NULL) {
    return FALSE;
  }

  Status = UsbIo->UsbGetDeviceDescriptor (UsbIo, &DeviceDescriptor);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  if (DeviceDescriptor.IdVendor != LEGION_GO_VID) {
    return FALSE;
  }

  //
  // Only the DInput-family modes, which carry no XInput data interface. The
  // XInput mode (0x61EB) goes through the standard Xbox 360 path instead.
  // Every interface of a matching device is accepted here; the converter
  // only acts on report ID 0x74, so sibling interfaces (config, touchpad)
  // bind but stay inert.
  //
  return (DeviceDescriptor.IdProduct == LEGION_GO2_PID_DINPUT) ||
         (DeviceDescriptor.IdProduct == LEGION_GO2_PID_DUAL_DINPUT) ||
         (DeviceDescriptor.IdProduct == LEGION_GO2_PID_FPS);
}

/**
  Scale a raw 128-centered unsigned stick byte (hhd type "m8") to the signed
  16-bit Xbox 360 axis range, optionally inverting the direction.

  @param  RawValue  Raw axis byte, 0-255 resting at 0x80
  @param  Invert    TRUE to flip the direction (Y axes: the raw report is
                    positive-down per the Linux input convention hhd uses,
                    the Xbox 360 wire format is positive-up)

  @return Signed 16-bit Xbox 360 axis value
**/
STATIC
INT16
ScaleStickAxis (
  IN  UINT8    RawValue,
  IN  BOOLEAN  Invert
  )
{
  INT32  Value;

  Value = ((INT32)RawValue - 128) * 256;
  if (Invert) {
    Value = -Value;   // 32768 when RawValue == 0, hence the clamp below
  }

  if (Value > MAX_INT16) {
    Value = MAX_INT16;
  }

  return (INT16)Value;
}

EFI_STATUS
ConvertLegionGoToXbox360 (
  IN  VOID    *RawReport,
  IN  UINTN   ReportLen,
  OUT UINT8   *XboxReport
  )
{
  UINT8   *Raw;
  UINT16  XboxButtons;
  INT16   StickValue;

  if ((RawReport == NULL) || (XboxReport == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Raw = (UINT8 *)RawReport;

  //
  // Accept only the gamepad state report; anything else on this or a
  // sibling interface (config replies, touchpad packets) is ignored.
  //
  if ((ReportLen < LEGION_GO_RAW_REPORT_MIN) || (Raw[0] != LEGION_GO_RAW_REPORT_ID)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (XboxReport, 20);
  XboxReport[0] = 0x00;  // Message type
  XboxReport[1] = 0x14;  // Packet size

  //
  // Buttons -> Xbox 360 button word (see AsusAllyDevice.c for the layout).
  //
  // The raw report's button bits are MSB-first: hhd's LGO_RAW_INTERFACE_BTN_MAP
  // bit index n means mask (1 << (7 - n)) within the byte (get_button() in
  // hhd's controller/lib/common.py). v1.5.0/1.5.1 read them LSB-first, which
  // mirrored every button within its byte (A read the digital-RT bit, D-pad
  // read the stick clicks, ...).
  //
  XboxButtons = 0;

  // Byte 18: mode/share, stick clicks, dpad
  if (Raw[18] & BIT7) XboxButtons |= BIT10;  // Legion/Mode -> Guide
  if (Raw[18] & BIT5) XboxButtons |= BIT6;   // Left Stick click
  if (Raw[18] & BIT4) XboxButtons |= BIT7;   // Right Stick click
  if (Raw[18] & BIT3) XboxButtons |= BIT0;   // D-pad Up
  if (Raw[18] & BIT2) XboxButtons |= BIT1;   // D-pad Down
  if (Raw[18] & BIT1) XboxButtons |= BIT2;   // D-pad Left
  if (Raw[18] & BIT0) XboxButtons |= BIT3;   // D-pad Right

  // Byte 19: face buttons and bumpers (BIT2/BIT0 are digital trigger presses)
  if (Raw[19] & BIT7) XboxButtons |= BIT12;  // A
  if (Raw[19] & BIT6) XboxButtons |= BIT13;  // B
  if (Raw[19] & BIT5) XboxButtons |= BIT14;  // X
  if (Raw[19] & BIT4) XboxButtons |= BIT15;  // Y
  if (Raw[19] & BIT3) XboxButtons |= BIT8;   // Left Bumper
  if (Raw[19] & BIT1) XboxButtons |= BIT9;   // Right Bumper

  // Byte 20: menu buttons (the other bits are the back paddles, left unmapped)
  if (Raw[20] & BIT1) XboxButtons |= BIT5;   // Select/View -> Back
  if (Raw[20] & BIT0) XboxButtons |= BIT4;   // Start/Menu  -> Start

  XboxReport[2] = (UINT8)(XboxButtons & 0xFF);
  XboxReport[3] = (UINT8)((XboxButtons >> 8) & 0xFF);

  //
  // Triggers: already 0-255. Note the raw report orders RT before LT.
  //
  XboxReport[4] = Raw[23];  // Left trigger
  XboxReport[5] = Raw[22];  // Right trigger

  //
  // Sticks: raw bytes are unsigned, resting at 0x80 (hhd axis type "m8" =
  // value - 128). v1.5.0/1.5.1 cast them to INT8, turning the resting 0x80
  // into -128 -> -32768: permanent full deflection on all four axes, i.e.
  // constantly-held arrow keys that scrolled rEFInd's menu by themselves
  // and let the timeout boot a random entry (rEFInd_GUI issue #23).
  // Y axes are additionally inverted: raw is positive-down, Xbox 360 wire
  // format is positive-up (the Linux xpad driver negates Y for the same
  // reason).
  //
  StickValue = ScaleStickAxis (Raw[14], FALSE);  // Left X
  CopyMem (&XboxReport[6], &StickValue, sizeof (INT16));
  StickValue = ScaleStickAxis (Raw[15], TRUE);   // Left Y
  CopyMem (&XboxReport[8], &StickValue, sizeof (INT16));
  StickValue = ScaleStickAxis (Raw[16], FALSE);  // Right X
  CopyMem (&XboxReport[10], &StickValue, sizeof (INT16));
  StickValue = ScaleStickAxis (Raw[17], TRUE);   // Right Y
  CopyMem (&XboxReport[12], &StickValue, sizeof (INT16));

  return EFI_SUCCESS;
}
