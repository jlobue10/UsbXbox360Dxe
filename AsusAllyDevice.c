/** @file
  ASUS ROG Ally Device Implementation
  
  This module provides DirectInput support for ASUS ROG Ally devices.
  
  HID protocol specification reference:
  - https://github.com/flukejones/linux (wip/ally-6.14-refactor branch)
    drivers/hid/asus-ally-hid/ by Luke Jones <luke@ljones.dev>
  
  Copyright (c) 2024-2025. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "AsusAllyDevice.h"
#include "Xbox360Log.h"
#include "KeyBoard.h"

/**
  Check if the given USB device is an ASUS ROG Ally device.
  
  For Ally X, we need to bind to the gamepad interface (endpoint 0x87).
  The device has multiple interfaces (keyboard/mouse/config/gamepad),
  we only want the gamepad one.
  
  @param  UsbIo    Pointer to USB I/O Protocol
  
  @retval TRUE     Device is ASUS ROG Ally gamepad interface
  @retval FALSE    Device is not an ASUS ROG Ally or wrong interface
**/
BOOLEAN
IsAsusAlly (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS                     Status;
  EFI_USB_DEVICE_DESCRIPTOR      DeviceDescriptor;
  EFI_USB_INTERFACE_DESCRIPTOR   InterfaceDescriptor;
  EFI_USB_ENDPOINT_DESCRIPTOR    EndpointDescriptor;
  UINT8                          Index;
  UINT8                          EndpointAddr;
  BOOLEAN                        FoundGamepadEndpoint;

  if (UsbIo == NULL) {
    return FALSE;
  }

  Status = UsbIo->UsbGetDeviceDescriptor (UsbIo, &DeviceDescriptor);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  // Check for ASUS vendor ID and known Ally PIDs
  if (DeviceDescriptor.IdVendor != ASUS_VENDOR_ID) {
    return FALSE;
  }

  // Only support Ally X (0x1B4C)
  // Original Ally (0x1ABE) has XInput mode and doesn't need DirectInput conversion
  if (DeviceDescriptor.IdProduct != ASUS_ALLY_X_PID) {
    return FALSE;
  }

  LOG_INFO ("ASUS ROG Ally X detected: VID:0x%04X PID:0x%04X",
            DeviceDescriptor.IdVendor,
            DeviceDescriptor.IdProduct);

  //
  // Get interface descriptor
  //
  Status = UsbIo->UsbGetInterfaceDescriptor (UsbIo, &InterfaceDescriptor);
  if (EFI_ERROR (Status)) {
    LOG_WARN ("Failed to get interface descriptor: %r", Status);
    return FALSE;
  }
  
  //
  // Ally X gamepad uses endpoint 0x87
  // Check all endpoints to find it
  //
  FoundGamepadEndpoint = FALSE;
  for (Index = 0; Index < InterfaceDescriptor.NumEndpoints; Index++) {
    Status = UsbIo->UsbGetEndpointDescriptor (UsbIo, Index, &EndpointDescriptor);
    if (EFI_ERROR (Status)) {
      continue;
    }

    EndpointAddr = EndpointDescriptor.EndpointAddress;
    
    // Check if this is endpoint 0x87 (Ally X gamepad)
    if (EndpointAddr == HID_ALLY_X_INTF_IN) {
      FoundGamepadEndpoint = TRUE;
      break;
    }
  }

  if (!FoundGamepadEndpoint) {
    return FALSE;
  }
  
  LOG_INFO ("ASUS ROG Ally X gamepad detected");
  return TRUE;
}

/**
  Initialize ASUS ROG Ally device for input.
  
  Sends initialization sequence to configure the device for gamepad input.
  
  @param  UsbIo    Pointer to USB I/O Protocol
  
  @retval EFI_SUCCESS     Device initialized successfully
  @retval Other           Initialization failed
**/
EFI_STATUS
InitializeAsusAlly (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS                    Status;
  EFI_USB_DEVICE_REQUEST        Request;
  EFI_USB_INTERFACE_DESCRIPTOR  InterfaceDescriptor;
  UINT32                        UsbStatus;
  UINT8                         Buffer[64];
  UINT8                         InterfaceNumber;
  UINTN                         Retry;
  
  // EC initialization string
  STATIC CONST UINT8 EcInitString[] = { 
    0x5A, 'A', 'S', 'U', 'S', ' ', 'T', 'e', 'c', 'h', '.', 'I', 'n', 'c', '.', '\0' 
  };

  if (UsbIo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Get interface number
  //
  Status = UsbIo->UsbGetInterfaceDescriptor (UsbIo, &InterfaceDescriptor);
  if (EFI_ERROR (Status)) {
    LOG_WARN ("Failed to get interface descriptor: %r", Status);
    InterfaceNumber = 0;  // Fallback to 0
  } else {
    InterfaceNumber = InterfaceDescriptor.InterfaceNumber;
  }


  //
  // Send EC initialization string (HID Feature Report)
  // Report ID: 0x5A
  //
  ZeroMem (Buffer, sizeof(Buffer));
  CopyMem (Buffer, EcInitString, sizeof(EcInitString));
  
  Request.RequestType = 0x21;  // Host to Device, Class, Interface
  Request.Request     = 0x09;  // SET_REPORT
  Request.Value       = 0x035A; // Report Type (Feature=0x03) | Report ID (0x5A)
  Request.Index       = InterfaceNumber;  // Interface number
  Request.Length      = sizeof(Buffer);

  Status = UsbIo->UsbControlTransfer (
                    UsbIo,
                    &Request,
                    EfiUsbDataOut,
                    200,  // 200ms timeout
                    Buffer,
                    sizeof(Buffer),
                    &UsbStatus
                    );

  if (EFI_ERROR (Status)) {
    LOG_ERROR ("Failed to send EC init string: %r", Status);
    return Status;
  }
  
  // Small delay after init
  gBS->Stall (50000);  // 50ms

  //
  // Check device ready status (CMD_CHECK_READY)
  //
  for (Retry = 0; Retry < 3; Retry++) {
    ZeroMem (Buffer, sizeof(Buffer));
    Buffer[0] = 0x5A;  // Report ID
    Buffer[1] = 0xD1;  // Feature code page
    Buffer[2] = 0x0A;  // CMD_CHECK_READY
    Buffer[3] = 0x01;  // Length
    
    Request.RequestType = 0x21;
    Request.Request     = 0x09;
    Request.Value       = 0x035A;
    Request.Index       = InterfaceNumber;
    Request.Length      = sizeof(Buffer);
    
    Status = UsbIo->UsbControlTransfer (
                      UsbIo,
                      &Request,
                      EfiUsbDataOut,
                      100,
                      Buffer,
                      sizeof(Buffer),
                      &UsbStatus
                      );
    
    if (!EFI_ERROR (Status)) {
      // Try to read response
      Request.RequestType = 0xA1;  // Device to Host
      Request.Request     = 0x01;  // GET_REPORT
      Request.Value       = 0x030D; // Report ID 0x0D
      
      Status = UsbIo->UsbControlTransfer (
                        UsbIo,
                        &Request,
                        EfiUsbDataIn,
                        100,
                        Buffer,
                        sizeof(Buffer),
                        &UsbStatus
                        );
      
      if (!EFI_ERROR (Status) && Buffer[2] == 0x0A) {
        break;  // Exit retry loop, continue with initialization
      }
    }
    
    gBS->Stall (2000);  // 2ms delay between retries
  }

  if (Retry >= 3) {
    LOG_WARN ("ASUS ROG Ally ready check failed after %d retries, continuing anyway", (UINT32)Retry);
  }
  
  //
  // Critical: Set HID Protocol to Report Protocol (not Boot Protocol)
  // This is required for devices to send full HID reports
  //
  
  ZeroMem (&Request, sizeof(Request));
  Request.RequestType = 0x21;  // Host to Device, Class, Interface
  Request.Request     = 0x0B;  // HID SET_PROTOCOL
  Request.Value       = 0x0001; // 1 = Report Protocol, 0 = Boot Protocol
  Request.Index       = InterfaceNumber;
  Request.Length      = 0;
  
  Status = UsbIo->UsbControlTransfer (
                    UsbIo,
                    &Request,
                    EfiUsbNoData,
                    100,
                    NULL,
                    0,
                    &UsbStatus
                    );
  
  if (EFI_ERROR (Status)) {
    LOG_WARN ("SET_PROTOCOL failed: %r (continuing anyway)", Status);
  }
  
  gBS->Stall (20000);  // 20ms delay
  
  //
  // Try to enable HID input reports (SET_IDLE with duration = 0 means infinite)
  // This is a standard HID command that might help start data flow
  //
  
  ZeroMem (&Request, sizeof(Request));
  Request.RequestType = 0x21;  // Host to Device, Class, Interface
  Request.Request     = 0x0A;  // HID SET_IDLE
  Request.Value       = 0x0000; // Duration = 0 (infinite), Report ID = 0 (all)
  Request.Index       = InterfaceNumber;  // Interface number
  Request.Length      = 0;
  
  Status = UsbIo->UsbControlTransfer (
                    UsbIo,
                    &Request,
                    EfiUsbNoData,
                    100,
                    NULL,
                    0,
                    &UsbStatus
                    );
  
  if (EFI_ERROR (Status)) {
    LOG_WARN ("SET_IDLE failed: %r (continuing anyway)", Status);
  }
  
  //
  // Set gamepad mode to enable interrupt reports
  //
  
  ZeroMem (Buffer, sizeof(Buffer));
  Buffer[0] = 0x5A;  // HID_ALLY_SET_REPORT_ID
  Buffer[1] = 0xD1;  // HID_ALLY_FEATURE_CODE_PAGE  
  Buffer[2] = 0x01;  // CMD_SET_GAMEPAD_MODE
  Buffer[3] = 0x01;  // Length
  Buffer[4] = 0x01;  // ALLY_GAMEPAD_MODE_GAMEPAD
  
  Request.RequestType = 0x21;
  Request.Request     = 0x09;  // SET_REPORT
  Request.Value       = 0x035A;
  Request.Index       = InterfaceNumber;
  Request.Length      = sizeof(Buffer);
  
  Status = UsbIo->UsbControlTransfer (
                    UsbIo,
                    &Request,
                    EfiUsbDataOut,
                    200,
                    Buffer,
                    sizeof(Buffer),
                    &UsbStatus
                    );
  
  if (EFI_ERROR (Status)) {
    LOG_ERROR ("Failed to set gamepad mode: %r", Status);
    LOG_WARN ("Device may not send interrupt data without gamepad mode");
  }
  
  // Small delay after mode change
  gBS->Stall (50000);  // 50ms
  
  //
  // Disable force feedback
  //
  
  ZeroMem (Buffer, sizeof(Buffer));
  Buffer[0] = 0x0D;
  Buffer[1] = 0x0F;
  Buffer[2] = 0x00;
  Buffer[3] = 0x00;
  Buffer[4] = 0x00;
  Buffer[5] = 0x00;
  Buffer[6] = 0xFF;
  Buffer[7] = 0x00;
  Buffer[8] = 0xEB;
  
  Request.RequestType = 0x21;
  Request.Request     = 0x09;  // SET_REPORT
  Request.Value       = 0x030D; // Report Type (Feature) | Report ID (0x0D)
  Request.Index       = InterfaceNumber;
  Request.Length      = 9;
  
  Status = UsbIo->UsbControlTransfer (
                    UsbIo,
                    &Request,
                    EfiUsbDataOut,
                    200,
                    Buffer,
                    9,
                    &UsbStatus
                    );
  
  if (EFI_ERROR (Status)) {
    LOG_WARN ("Failed to disable force feedback: %r (continuing anyway)", Status);
  }
  
  gBS->Stall (50000);  // 50ms
  
  LOG_INFO ("ASUS ROG Ally X initialization completed");
  return EFI_SUCCESS;
}

/**
  Polling timer callback for ASUS ROG Ally devices.
  
  ASUS Ally X does not send async interrupt data, so we need to poll
  the endpoint using synchronous interrupt transfer.
  
  @param  Event     The timer event
  @param  Context   Pointer to USB_KB_DEV instance
**/
VOID
EFIAPI
AsusAllyPollingHandler (
  IN  EFI_EVENT  Event,
  IN  VOID       *Context
  )
{
  USB_KB_DEV           *UsbKeyboardDevice;
  EFI_USB_IO_PROTOCOL  *UsbIo;
  EFI_STATUS           Status;
  UINTN                DataLength;
  UINT32               UsbStatus;
  UINT8                *Buffer;
  
  UsbKeyboardDevice = (USB_KB_DEV *)Context;
  UsbIo = UsbKeyboardDevice->UsbIo;
  Buffer = UsbKeyboardDevice->PollingBuffer;
  
  //
  // Poll the interrupt endpoint using synchronous transfer
  //
  DataLength = 64;
  Status = UsbIo->UsbSyncInterruptTransfer (
                    UsbIo,
                    UsbKeyboardDevice->IntEndpointDescriptor.EndpointAddress,
                    Buffer,
                    &DataLength,
                    10,  // 10ms timeout
                    &UsbStatus
                    );
  
  //
  // If we got data, call the keyboard handler to process it
  //
  if (!EFI_ERROR (Status) && DataLength > 0) {
    // Call the existing handler with the polled data
    KeyboardHandler (Buffer, DataLength, Context, EFI_USB_NOERROR);
  }
  // Ignore timeout errors - just means no data this cycle
}

/**
  Parse ASUS ROG Ally HID report and convert to Xbox 360 format.
  
  Converts Ally DirectInput report to internal Xbox 360 format.
  
  @param  AllyReport   Pointer to ASUS Ally HID report data
  @param  ReportLen    Length of the report
  @param  XboxReport   Output buffer for Xbox 360 format report (20 bytes min)
  
  @retval EFI_SUCCESS     Report converted successfully
  @retval Other           Conversion failed
**/
EFI_STATUS
ConvertAsusAllyToXbox360 (
  IN  VOID    *AllyReport,
  IN  UINTN   ReportLen,
  OUT UINT8   *XboxReport
  )
{
  ASUS_ALLY_HID_REPORT  *Ally;
  UINT16                XboxButtons;
  UINT8                 DPadBits;
  INT16                 StickValue;

  if ((AllyReport == NULL) || (XboxReport == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  // Validate report length
  // USB stack may strip Report ID, so we accept either 16 or 17 bytes
  if (ReportLen < 16) {
    LOG_WARN ("ASUS Ally report too short: %d bytes", (UINT32)ReportLen);
    return EFI_INVALID_PARAMETER;
  }

  Ally = (ASUS_ALLY_HID_REPORT *)AllyReport;

  // If Report ID is present (17 bytes), skip it
  if (ReportLen == 17 && Ally->ReportId == 0x0B) {
    // Report ID present, skip to data
    Ally = (ASUS_ALLY_HID_REPORT *)((UINT8*)AllyReport + 1);
  }
  // If 16 bytes, assume Report ID already stripped by USB stack

  //
  // Convert to Xbox 360 report format:
  // Byte 0:    Message type (0x00)
  // Byte 1:    Packet size (0x14 = 20 bytes)
  // Byte 2-3:  Button state (16 bits)
  // Byte 4:    Left trigger (0-255)
  // Byte 5:    Right trigger (0-255)
  // Byte 6-7:  Left stick X (INT16)
  // Byte 8-9:  Left stick Y (INT16)
  // Byte 10-11: Right stick X (INT16)
  // Byte 12-13: Right stick Y (INT16)
  //
  ZeroMem (XboxReport, 20);
  XboxReport[0] = 0x00;  // Message type
  XboxReport[1] = 0x14;  // Packet size

  //
  // Convert buttons from ASUS format to Xbox 360 format
  // Xbox 360 button layout (16 bits):
  // Bit 0:  D-pad Up
  // Bit 1:  D-pad Down
  // Bit 2:  D-pad Left
  // Bit 3:  D-pad Right
  // Bit 4:  Start
  // Bit 5:  Back
  // Bit 6:  Left Stick
  // Bit 7:  Right Stick
  // Bit 8:  Left Bumper
  // Bit 9:  Right Bumper
  // Bit 10: Guide
  // Bit 11: (unused)
  // Bit 12: A
  // Bit 13: B
  // Bit 14: X
  // Bit 15: Y
  //
  XboxButtons = 0;

  // Map D-Pad (hatswitch in Buttons[2])
  DPadBits = 0;
  switch (Ally->Buttons[2]) {
    case ALLY_DPAD_UP:
      DPadBits = BIT0;  // Up
      break;
    case ALLY_DPAD_UP_RIGHT:
      DPadBits = BIT0 | BIT3;  // Up + Right
      break;
    case ALLY_DPAD_RIGHT:
      DPadBits = BIT3;  // Right
      break;
    case ALLY_DPAD_DOWN_RIGHT:
      DPadBits = BIT1 | BIT3;  // Down + Right
      break;
    case ALLY_DPAD_DOWN:
      DPadBits = BIT1;  // Down
      break;
    case ALLY_DPAD_DOWN_LEFT:
      DPadBits = BIT1 | BIT2;  // Down + Left
      break;
    case ALLY_DPAD_LEFT:
      DPadBits = BIT2;  // Left
      break;
    case ALLY_DPAD_UP_LEFT:
      DPadBits = BIT0 | BIT2;  // Up + Left
      break;
    case ALLY_DPAD_NEUTRAL:
    default:
      DPadBits = 0;
      break;
  }
  XboxButtons |= DPadBits;

  // Map face buttons (Buttons[0])
  if (Ally->Buttons[0] & ALLY_BTN_A)    XboxButtons |= BIT12;  // A
  if (Ally->Buttons[0] & ALLY_BTN_B)    XboxButtons |= BIT13;  // B
  if (Ally->Buttons[0] & ALLY_BTN_X)    XboxButtons |= BIT14;  // X
  if (Ally->Buttons[0] & ALLY_BTN_Y)    XboxButtons |= BIT15;  // Y

  // Map shoulder buttons (Buttons[0])
  if (Ally->Buttons[0] & ALLY_BTN_LB)   XboxButtons |= BIT8;   // Left Bumper
  if (Ally->Buttons[0] & ALLY_BTN_RB)   XboxButtons |= BIT9;   // Right Bumper

  // Map menu buttons (Buttons[0])
  if (Ally->Buttons[0] & ALLY_BTN_VIEW) XboxButtons |= BIT5;   // Back/View
  if (Ally->Buttons[0] & ALLY_BTN_MENU) XboxButtons |= BIT4;   // Start/Menu

  // Map stick buttons (Buttons[1])
  if (Ally->Buttons[1] & ALLY_BTN_L3)   XboxButtons |= BIT6;   // Left Stick
  if (Ally->Buttons[1] & ALLY_BTN_R3)   XboxButtons |= BIT7;   // Right Stick

  // Map Guide button (Buttons[1])
  if (Ally->Buttons[1] & ALLY_BTN_MODE) XboxButtons |= BIT10;  // Guide

  // Write button state
  XboxReport[2] = (UINT8)(XboxButtons & 0xFF);
  XboxReport[3] = (UINT8)((XboxButtons >> 8) & 0xFF);

  //
  // Convert triggers: Ally uses 0-1023 (10-bit), Xbox 360 uses 0-255 (8-bit)
  // Divide by 4 to convert
  //
  XboxReport[4] = (UINT8)(Ally->LeftTrigger >> 2);   // 1023/4 = 255
  XboxReport[5] = (UINT8)(Ally->RightTrigger >> 2);

  //
  // Convert analog sticks: Ally uses 0-65535, Xbox 360 uses -32768 to 32767
  // Subtract 32768 from Ally values to convert
  //
  StickValue = (INT16)(Ally->LeftStickX - 32768);
  CopyMem (&XboxReport[6], &StickValue, sizeof(INT16));
  
  StickValue = (INT16)(Ally->LeftStickY - 32768);
  CopyMem (&XboxReport[8], &StickValue, sizeof(INT16));
  
  StickValue = (INT16)(Ally->RightStickX - 32768);
  CopyMem (&XboxReport[10], &StickValue, sizeof(INT16));
  
  StickValue = (INT16)(Ally->RightStickY - 32768);
  CopyMem (&XboxReport[12], &StickValue, sizeof(INT16));

  return EFI_SUCCESS;
}

