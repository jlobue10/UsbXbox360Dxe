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
  XboxButtons = 0;

  // Byte 18: stick clicks and dpad
  if (Raw[18] & BIT2) XboxButtons |= BIT6;   // Left Stick click
  if (Raw[18] & BIT3) XboxButtons |= BIT7;   // Right Stick click
  if (Raw[18] & BIT4) XboxButtons |= BIT0;   // D-pad Up
  if (Raw[18] & BIT5) XboxButtons |= BIT1;   // D-pad Down
  if (Raw[18] & BIT6) XboxButtons |= BIT2;   // D-pad Left
  if (Raw[18] & BIT7) XboxButtons |= BIT3;   // D-pad Right

  // Byte 19: face buttons and bumpers
  if (Raw[19] & BIT0) XboxButtons |= BIT12;  // A
  if (Raw[19] & BIT1) XboxButtons |= BIT13;  // B
  if (Raw[19] & BIT2) XboxButtons |= BIT14;  // X
  if (Raw[19] & BIT3) XboxButtons |= BIT15;  // Y
  if (Raw[19] & BIT4) XboxButtons |= BIT8;   // Left Bumper
  if (Raw[19] & BIT6) XboxButtons |= BIT9;   // Right Bumper

  // Byte 20: menu buttons (bits 0-5 are the back paddles, left unmapped)
  if (Raw[20] & BIT6) XboxButtons |= BIT5;   // Select/View -> Back
  if (Raw[20] & BIT7) XboxButtons |= BIT4;   // Start/Menu  -> Start

  XboxReport[2] = (UINT8)(XboxButtons & 0xFF);
  XboxReport[3] = (UINT8)((XboxButtons >> 8) & 0xFF);

  //
  // Triggers: already 0-255. Note the raw report orders RT before LT.
  //
  XboxReport[4] = Raw[23];  // Left trigger
  XboxReport[5] = Raw[22];  // Right trigger

  //
  // Sticks: signed 8-bit centered on 0 -> signed 16-bit (scale by 256).
  //
  StickValue = (INT16)((INT8)Raw[14] * 256);  // Left X
  CopyMem (&XboxReport[6], &StickValue, sizeof (INT16));
  StickValue = (INT16)((INT8)Raw[15] * 256);  // Left Y
  CopyMem (&XboxReport[8], &StickValue, sizeof (INT16));
  StickValue = (INT16)((INT8)Raw[16] * 256);  // Right X
  CopyMem (&XboxReport[10], &StickValue, sizeof (INT16));
  StickValue = (INT16)((INT8)Raw[17] * 256);  // Right Y
  CopyMem (&XboxReport[12], &StickValue, sizeof (INT16));

  return EFI_SUCCESS;
}
