/** @file
  Lenovo Legion Go 2 controller support (vendor raw HID interface).

  The Legion Go 2's controllers expose Lenovo's vendor HID interface (usage
  page 0xFFA0) in every controller mode; it delivers 64-byte reports with
  report ID 0x74 carrying the full combined gamepad state. Converting those
  reports gives boot-menu input in the DInput-family modes (PIDs
  0x61EC/0x61ED/0x61EE), which have no XInput data interface. The XInput mode
  (PID 0x61EB) is intentionally NOT handled here -- it exposes a genuine
  Xbox 360 data interface and uses the standard path.

  Report layout (byte offsets include the leading report ID, matching the
  hhd-dev/hhd Legion Go driver's LGO_RAW_INTERFACE_BTN_MAP/AXIS_MAP):
    [0]      report ID 0x74
    [14..17] left X / left Y / right X / right Y (signed 8-bit, centered 0)
    [18]     bit2 LS click, bit3 RS click, bits4-7 dpad up/down/left/right
    [19]     bit0 A, bit1 B, bit2 X, bit3 Y, bit4 LB, bit6 RB
    [20]     bit6 Select/View, bit7 Start/Menu (bits 0-5: back paddles)
    [22]     right trigger (0-255)
    [23]     left trigger (0-255)

  Copyright (c) 2026, jlobue10. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _LEGION_GO_DEVICE_H_
#define _LEGION_GO_DEVICE_H_

#include <Uefi.h>
#include <Protocol/UsbIo.h>

#define LEGION_GO_VID              0x17EF
#define LEGION_GO2_PID_DINPUT      0x61EC
#define LEGION_GO2_PID_DUAL_DINPUT 0x61ED
#define LEGION_GO2_PID_FPS         0x61EE

#define LEGION_GO_RAW_REPORT_ID    0x74
#define LEGION_GO_RAW_REPORT_MIN   24

/**
  Check if the given USB device is a Legion Go 2 controller in a mode handled
  via the vendor raw HID interface (DInput / dual DInput / FPS).

  @param  UsbIo    Pointer to USB I/O Protocol

  @retval TRUE     Device is a Legion Go 2 in a raw-HID-handled mode
  @retval FALSE    Device is not, or an error occurred
**/
BOOLEAN
IsLegionGoRaw (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

/**
  Convert a Legion Go vendor raw HID report (ID 0x74) to Xbox 360 format.

  @param  RawReport   Raw interrupt report as received (report ID at byte 0)
  @param  ReportLen   Length of the raw report
  @param  XboxReport  Receives the 20-byte Xbox 360 format report

  @retval EFI_SUCCESS            Converted successfully
  @retval EFI_INVALID_PARAMETER  Not a gamepad state report (wrong ID/length);
                                 the caller must ignore the report
**/
EFI_STATUS
ConvertLegionGoToXbox360 (
  IN  VOID    *RawReport,
  IN  UINTN   ReportLen,
  OUT UINT8   *XboxReport
  );

#endif
