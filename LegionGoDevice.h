/** @file
  Lenovo Legion Go 2 controller support (vendor raw HID interfaces).

  The Legion Go 2 exposes TWO Lenovo vendor HID input streams on different
  interfaces in EVERY controller mode -- including XInput mode (PID 0x61EB),
  where they sit alongside the XInput data interface. This driver binds the
  vendor interfaces and decodes gamepad state from them, so the converter
  must recognize both streams by their framing:

  1. The "xinput data" stream (usage page 0xFFA0, usage 0x0003, interface 2;
     framing and layout verified from ShadowBlip/InputPlumber's annotated
     Legion Go 2 hid-recorder captures, drivers/lego/hid_report.rs):
       [0]  report ID 0x04
       [1]  report size (0x3C = 60)
       [2]  command ID 0x74 (only reports with this command are gamepad
            state; other commands are config replies)
       [9]  gamepad mode (0 xinput / 1 dinput / 2 fps)
     This stream carries the full gamepad state -- INCLUDING all buttons --
     in every controller mode, at ~40 Hz. It is the only place the buttons
     reliably appear: in the DInput-family modes the legacy stream's button
     bytes stay zero, and in XInput mode the XInput data interface itself
     never reports the face buttons either (a 40-event field capture on a
     real Legion Go 2, rEFInd_GUI issue #23, showed button presses changing
     only the D-pad bits of the standard frame) -- which is why every mode
     reported working sticks/triggers but dead buttons.

  2. The legacy raw stream (usage page 0xFFA0, usage 0x0001; layout from
     hhd's LGO_RAW_INTERFACE_BTN_MAP/AXIS_MAP, which is only verified in
     XInput mode):
       [0]  report ID 0x74
     In the DInput-family modes this stream still carries live sticks and
     triggers but its button bytes stay zero (observed in the field,
     rEFInd_GUI issue #23).

  Both streams use the SAME absolute offsets for the shared payload:
    [14..17] left X / left Y / right X / right Y (unsigned 8-bit, centered
             128; X positive right, Y positive DOWN -- inverted vs. the
             Xbox 360 wire format)
    [18]     masks: 0x80 Legion/mode, 0x40 share/quick-access, 0x20 LS
             click, 0x10 RS click, 0x08/0x04/0x02/0x01 dpad U/D/L/R
    [19]     masks: 0x80 A, 0x40 B, 0x20 X, 0x10 Y, 0x08 LB, 0x04 LT-press,
             0x02 RB, 0x01 RT-press
    [20]     masks: 0x80/0x40/0x20 Y1/Y2/Y3, 0x10/0x08/0x04 M1/M2/M3,
             0x02 Select/View, 0x01 Start/Menu
    [22..23] analog triggers (0-255). Order differs per stream: the xinput
             data stream is LT@22/RT@23 (InputPlumber captures), the legacy
             stream is RT@22/LT@23 (hhd map, and field behavior: R2 acting
             as MouseLeft through the legacy path)
    [26..29] touchpad X/Y (unsigned 16-bit BIG-endian, ~0..1000 range,
             0/0 when not touched) -- xinput data stream only

  Copyright (c) 2026, jlobue10. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _LEGION_GO_DEVICE_H_
#define _LEGION_GO_DEVICE_H_

#include <Uefi.h>
#include <Protocol/UsbIo.h>

#define LEGION_GO_VID              0x17EF
#define LEGION_GO2_PID_XINPUT      0x61EB
#define LEGION_GO2_PID_DINPUT      0x61EC
#define LEGION_GO2_PID_DUAL_DINPUT 0x61ED
#define LEGION_GO2_PID_FPS         0x61EE

#define LEGION_GO_RAW_REPORT_ID     0x74  // legacy stream (usage 0xFFA0/0x0001)
#define LEGION_GO_XDATA_REPORT_ID   0x04  // xinput data stream (usage 0xFFA0/0x0003)
#define LEGION_GO_XDATA_CMD         0x74  // gamepad-state command, at byte 2
#define LEGION_GO_RAW_REPORT_MIN    24
#define LEGION_GO_TOUCH_REPORT_MIN  30    // through touchpad bytes 26..29

///
/// Touchpad sample extracted from an xinput-data-stream report.
/// X/Y are absolute pad coordinates; 0/0 means "not touched".
///
typedef struct {
  BOOLEAN  Valid;
  UINT16   X;
  UINT16   Y;
} LEGION_GO_TOUCH;

/**
  Check if the given USB device is a Legion Go 2 controller in a mode handled
  via the vendor raw HID interfaces (DInput / dual DInput / FPS).

  @param  UsbIo    Pointer to USB I/O Protocol

  @retval TRUE     Device is a Legion Go 2 in a raw-HID-handled mode
  @retval FALSE    Device is not, or an error occurred
**/
BOOLEAN
IsLegionGoRaw (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

/**
  Convert a Legion Go vendor HID report (either stream) to Xbox 360 format.

  @param  RawReport   Raw interrupt report as received (report ID at byte 0)
  @param  ReportLen   Length of the raw report
  @param  XboxReport  Receives the 20-byte Xbox 360 format report
  @param  Touch       Optional; receives the report's touchpad sample.
                      Touch->Valid stays FALSE when the report carries no
                      touch data (legacy stream, or report too short)

  @retval EFI_SUCCESS            Converted successfully
  @retval EFI_INVALID_PARAMETER  Not a gamepad state report (wrong ID/length);
                                 the caller must ignore the report
**/
EFI_STATUS
ConvertLegionGoToXbox360 (
  IN  VOID             *RawReport,
  IN  UINTN            ReportLen,
  OUT UINT8            *XboxReport,
  OUT LEGION_GO_TOUCH  *Touch  OPTIONAL
  );

#endif
