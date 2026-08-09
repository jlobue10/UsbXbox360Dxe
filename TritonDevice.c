/** @file
  Valve Steam Controller (Triton) support via the wireless puck dongle.

  See TritonDevice.h for the interface layout and report format overview.

  Copyright (c) 2026, jlobue10. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "TritonDevice.h"
#include "Xbox360Log.h"
#include "KeyBoard.h"

#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiUsbLib.h>
#include <IndustryStandard/Usb.h>
#include <Protocol/ComponentName2.h>

BOOLEAN
IsTritonController (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS                    Status;
  EFI_USB_DEVICE_DESCRIPTOR     DeviceDescriptor;
  EFI_USB_INTERFACE_DESCRIPTOR  InterfaceDescriptor;

  if (UsbIo == NULL) {
    return FALSE;
  }

  Status = UsbIo->UsbGetDeviceDescriptor (UsbIo, &DeviceDescriptor);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  if ((DeviceDescriptor.IdVendor != VALVE_VENDOR_ID) ||
      ((DeviceDescriptor.IdProduct != TRITON_PID_PROTEUS_DONGLE) &&
       (DeviceDescriptor.IdProduct != TRITON_PID_NEREID_DONGLE)))
  {
    return FALSE;
  }

  Status = UsbIo->UsbGetInterfaceDescriptor (UsbIo, &InterfaceDescriptor);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  //
  // Bind only the HID controller-slot interfaces. The CDC pair (0/1) and
  // the dongle-management HID interface (6) must stay unbound -- the CDC
  // comm interface even has an interrupt IN endpoint the generic binding
  // path would happily latch onto.
  //
  if (InterfaceDescriptor.InterfaceClass != TRITON_USB_CLASS_HID) {
    LOG_INFO ("Triton: puck iface %d class 0x%02X is not HID; skipping",
              InterfaceDescriptor.InterfaceNumber,
              InterfaceDescriptor.InterfaceClass);
    return FALSE;
  }

  //
  // Bind every HID interface of the puck, not just the slot range 2-5.
  // The platform's own USB HID driver can hold the controller slots
  // BY_DRIVER before this driver ever probes them (observed with the AMI
  // USB HID driver), which makes Supported() fail silently there -- the
  // dongle-management interface (6) is then the only foothold, and its
  // Start() runs TritonReclaimPuckInterfaces to pull the slots over.
  // Management-interface traffic never parses as controller state, so
  // binding it as a slot is harmless.
  //
  LOG_INFO ("Triton: binding puck HID iface %d (sub 0x%02X proto 0x%02X, %d EPs)",
            InterfaceDescriptor.InterfaceNumber,
            InterfaceDescriptor.InterfaceSubClass,
            InterfaceDescriptor.InterfaceProtocol,
            InterfaceDescriptor.NumEndpoints);
  return TRUE;
}

EFI_STATUS
SendTritonLizardDisable (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS                    Status;
  EFI_USB_INTERFACE_DESCRIPTOR  InterfaceDescriptor;
  UINT8                         Payload[TRITON_FEATURE_REPORT_BYTES];

  if (UsbIo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = UsbIo->UsbGetInterfaceDescriptor (UsbIo, &InterfaceDescriptor);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Numbered report: the report ID leads the data stage, then the
  // FeatureReportMsg -- header {type, length} followed by one packed
  // ControllerSetting {settingNum (u8), settingValue (u16 LE)}.
  //
  ZeroMem (Payload, sizeof (Payload));
  Payload[0] = TRITON_FEATURE_REPORT_ID;
  Payload[1] = TRITON_ID_SET_SETTINGS;
  Payload[2] = 3;  // one ControllerSetting
  Payload[3] = TRITON_SETTING_LIZARD_MODE;
  Payload[4] = TRITON_LIZARD_MODE_OFF;       // u16 LE, low byte
  Payload[5] = 0;                            // u16 LE, high byte

  return UsbSetReportRequest (
           UsbIo,
           InterfaceDescriptor.InterfaceNumber,
           TRITON_FEATURE_REPORT_ID,
           HID_FEATURE_REPORT,
           TRITON_FEATURE_REPORT_BYTES,
           Payload
           );
}

VOID
EFIAPI
TritonLizardTimerHandler (
  IN  EFI_EVENT  Event,
  IN  VOID       *Context
  )
{
  USB_KB_DEV  *UsbKeyboardDevice;

  UsbKeyboardDevice = (USB_KB_DEV *)Context;
  if ((UsbKeyboardDevice == NULL) || (UsbKeyboardDevice->UsbIo == NULL)) {
    return;
  }

  //
  // Failures are expected while no controller is linked to this slot;
  // the command is only meaningful once one connects, and re-sending
  // every period covers that case with no connect bookkeeping.
  //
  SendTritonLizardDisable (UsbKeyboardDevice->UsbIo);
}

#if XBOX360_LOG_ENABLED

/**
  DEBUG builds: log the driver holding a puck interface, by ComponentName2
  name when it offers one.

  @param  AgentHandle  BY_DRIVER agent from OpenProtocolInformation
  @param  IsUs         Agent is this driver's own binding handle
**/
STATIC
VOID
TritonLogHoldingDriver (
  IN  EFI_HANDLE  AgentHandle,
  IN  BOOLEAN     IsUs
  )
{
  EFI_STATUS                    Status;
  EFI_COMPONENT_NAME2_PROTOCOL  *Name2;
  CHAR16                        *DriverName;

  if (IsUs) {
    LOG_INFO ("Triton:   held BY_DRIVER by this driver");
    return;
  }

  DriverName = NULL;
  Status     = gBS->HandleProtocol (
                      AgentHandle,
                      &gEfiComponentName2ProtocolGuid,
                      (VOID **)&Name2
                      );
  if (!EFI_ERROR (Status)) {
    Status = Name2->GetDriverName (Name2, "en", &DriverName);
  }

  if (!EFI_ERROR (Status) && (DriverName != NULL)) {
    LOG_INFO ("Triton:   held BY_DRIVER by %p (%s)", AgentHandle, DriverName);
  } else {
    LOG_INFO ("Triton:   held BY_DRIVER by %p (no ComponentName2)", AgentHandle);
  }
}

#endif // XBOX360_LOG_ENABLED

VOID
TritonReclaimPuckInterfaces (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_USB_IO_PROTOCOL          *UsbIo
  )
{
  STATIC BOOLEAN                        ReclaimRan = FALSE;
  EFI_STATUS                            Status;
#if XBOX360_LOG_ENABLED
  UINT32                                UsbStatus;
  UINT8                                 Header[9];   // config descriptor is 9 bytes
  UINT8                                 *Full;
  UINT16                                TotalLen;
  UINTN                                 Off;
#endif
  UINTN                                 HandleCount;
  EFI_HANDLE                            *Handles;
  UINTN                                 Index;
  EFI_USB_IO_PROTOCOL                   *Io;
  EFI_USB_DEVICE_DESCRIPTOR             DevDesc;
  EFI_USB_INTERFACE_DESCRIPTOR          IfDesc;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY   *Entries;
  UINTN                                 EntryCount;
  UINTN                                 Entry;
  BOOLEAN                               HeldByUs;
  BOOLEAN                               HeldByOther;
  EFI_HANDLE                            DriverList[2];

  //
  // The reconnects below re-enter DriverBindingStart (and so this
  // function) for each interface pulled over; one sweep is enough.
  //
  if (ReclaimRan) {
    return;
  }

  ReclaimRan = TRUE;

#if XBOX360_LOG_ENABLED
  //
  // DEBUG builds: dump the full configuration descriptor via control
  // transfer -- does the active UEFI configuration even contain slot
  // interfaces 2-5, or did the puck present a reduced layout to this host?
  //
  Status = UsbGetDescriptor (
             UsbIo,
             (UINT16)(USB_DESC_TYPE_CONFIG << 8),
             0,
             sizeof (Header),
             Header,
             &UsbStatus
             );
  if (EFI_ERROR (Status)) {
    LOG_WARN ("Triton: config descriptor header read failed: %r (usb 0x%X)", Status, UsbStatus);
  } else {
    TotalLen = (UINT16)(Header[2] | (Header[3] << 8));
    LOG_INFO ("Triton: active config has bNumInterfaces %d, wTotalLength %d",
              Header[4], TotalLen);

    if ((TotalLen > sizeof (Header)) && (TotalLen <= 1024)) {
      Full = AllocateZeroPool (TotalLen);
      if (Full != NULL) {
        Status = UsbGetDescriptor (
                   UsbIo,
                   (UINT16)(USB_DESC_TYPE_CONFIG << 8),
                   0,
                   TotalLen,
                   Full,
                   &UsbStatus
                   );
        if (EFI_ERROR (Status)) {
          LOG_WARN ("Triton: full config descriptor read failed: %r (usb 0x%X)", Status, UsbStatus);
        } else {
          for (Off = 0; (Off + 2 <= TotalLen) && (Full[Off] >= 2); Off += Full[Off]) {
            if ((Full[Off + 1] == USB_DESC_TYPE_INTERFACE) &&
                (Off + 9 <= TotalLen))  // interface descriptor is 9 bytes
            {
              LOG_INFO ("Triton:   iface %d alt %d class 0x%02X sub 0x%02X proto 0x%02X, %d EPs",
                        Full[Off + 2], Full[Off + 3], Full[Off + 5],
                        Full[Off + 6], Full[Off + 7], Full[Off + 4]);
            } else if ((Full[Off + 1] == USB_DESC_TYPE_ENDPOINT) &&
                       (Off + 7 <= TotalLen))
            {
              LOG_INFO ("Triton:     EP 0x%02X attr 0x%02X maxpkt %d interval %d",
                        Full[Off + 2], Full[Off + 3],
                        (UINT16)(Full[Off + 4] | (Full[Off + 5] << 8)), Full[Off + 6]);
            }
          }
        }

        FreePool (Full);
      }
    }
  }
#endif // XBOX360_LOG_ENABLED

  //
  // Walk every USB I/O handle UEFI created for the puck. A slot interface
  // missing here but present in the DEBUG config dump means the USB bus
  // driver never created its child.
  //
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiUsbIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    LOG_WARN ("Triton: LocateHandleBuffer for USB I/O failed: %r", Status);
    return;
  }

  LOG_INFO ("Triton: sweeping %d USB I/O handles for puck interfaces", (UINT32)HandleCount);

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiUsbIoProtocolGuid, (VOID **)&Io);
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (EFI_ERROR (Io->UsbGetDeviceDescriptor (Io, &DevDesc)) ||
        (DevDesc.IdVendor != VALVE_VENDOR_ID) ||
        ((DevDesc.IdProduct != TRITON_PID_PROTEUS_DONGLE) &&
         (DevDesc.IdProduct != TRITON_PID_NEREID_DONGLE)) ||
        EFI_ERROR (Io->UsbGetInterfaceDescriptor (Io, &IfDesc)))
    {
      continue;
    }

    LOG_INFO ("Triton: handle %p = puck iface %d (class 0x%02X sub 0x%02X proto 0x%02X, %d EPs)",
              Handles[Index], IfDesc.InterfaceNumber, IfDesc.InterfaceClass,
              IfDesc.InterfaceSubClass, IfDesc.InterfaceProtocol, IfDesc.NumEndpoints);

    HeldByUs    = FALSE;
    HeldByOther = FALSE;
    Status      = gBS->OpenProtocolInformation (
                         Handles[Index],
                         &gEfiUsbIoProtocolGuid,
                         &Entries,
                         &EntryCount
                         );
    if (!EFI_ERROR (Status)) {
      for (Entry = 0; Entry < EntryCount; Entry++) {
        if ((Entries[Entry].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) != 0) {
          if (Entries[Entry].AgentHandle == This->DriverBindingHandle) {
            HeldByUs = TRUE;
          } else {
            HeldByOther = TRUE;
          }

#if XBOX360_LOG_ENABLED
          TritonLogHoldingDriver (
            Entries[Entry].AgentHandle,
            Entries[Entry].AgentHandle == This->DriverBindingHandle
            );
#endif
        }
      }

      FreePool (Entries);
    }

    //
    // Pull foreign-held or unconnected HID interfaces over to this
    // driver: their Start() sends the lizard disable and begins native
    // state decoding for whichever controller links to that slot.
    //
    if ((IfDesc.InterfaceClass != TRITON_USB_CLASS_HID) || HeldByUs) {
      continue;
    }

    if (HeldByOther) {
      Status = gBS->DisconnectController (Handles[Index], NULL, NULL);
      LOG_INFO ("Triton: disconnect of iface %d holder: %r", IfDesc.InterfaceNumber, Status);
      if (EFI_ERROR (Status)) {
        continue;
      }
    }

    DriverList[0] = This->DriverBindingHandle;
    DriverList[1] = NULL;
    Status        = gBS->ConnectController (Handles[Index], DriverList, NULL, FALSE);
    LOG_INFO ("Triton: reconnect of iface %d to this driver: %r", IfDesc.InterfaceNumber, Status);
  }

  FreePool (Handles);
}

/**
  Scale a Triton trigger value (0-32767) to the Xbox 360 8-bit range.

  @param  RawValue  Raw trigger value

  @return Trigger value scaled to 0-255
**/
STATIC
UINT8
ScaleTriggerValue (
  IN  INT16  RawValue
  )
{
  if (RawValue <= 0) {
    return 0;
  }

  return (UINT8)(RawValue >> 7);
}

EFI_STATUS
ConvertTritonToXbox360 (
  IN  VOID    *RawReport,
  IN  UINTN   ReportLen,
  OUT UINT8   *XboxReport
  )
{
  TRITON_STATE_PREFIX  State;
  UINT8                *Raw;
  UINT32               Buttons;
  UINT16               XboxButtons;

  if ((RawReport == NULL) || (XboxReport == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Raw = (UINT8 *)RawReport;

  //
  // Only the three state framings are decoded; lizard mouse/keyboard
  // echoes (0x40/0x41), battery (0x43), wireless status (0x79) and pad
  // debug (0x7B) reports share the endpoint and are ignored.
  //
  if ((Raw[0] != TRITON_REPORT_STATE) &&
      (Raw[0] != TRITON_REPORT_STATE_BLE) &&
      (Raw[0] != TRITON_REPORT_STATE_TIMESTAMP))
  {
    return EFI_INVALID_PARAMETER;
  }

  if (ReportLen < TRITON_STATE_PREFIX_LEN) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // The prefix is packed little-endian and byte-aligned in the transfer
  // buffer; copy it out rather than casting, the buffer has no alignment
  // guarantee.
  //
  CopyMem (&State, Raw, sizeof (State));
  Buttons = State.Buttons;

  ZeroMem (XboxReport, 20);
  XboxReport[0] = 0x00;  // Message type
  XboxReport[1] = 0x14;  // Packet size

  //
  // Buttons -> Xbox 360 button word (bit layout: see Xbox360Input.h).
  // MENU-bit -> Back and VIEW-bit -> Start follows SDL's mapping of the
  // same bits. QAM has no Xbox 360 equivalent and joins Steam on Guide;
  // the grip paddles (L4/L5/R4/R5) and pad clicks are left unmapped.
  //
  XboxButtons = 0;

  if (Buttons & TRITON_BTN_DPAD_UP)    XboxButtons |= BIT0;   // D-pad Up
  if (Buttons & TRITON_BTN_DPAD_DOWN)  XboxButtons |= BIT1;   // D-pad Down
  if (Buttons & TRITON_BTN_DPAD_LEFT)  XboxButtons |= BIT2;   // D-pad Left
  if (Buttons & TRITON_BTN_DPAD_RIGHT) XboxButtons |= BIT3;   // D-pad Right
  if (Buttons & TRITON_BTN_VIEW)       XboxButtons |= BIT4;   // Start
  if (Buttons & TRITON_BTN_MENU)       XboxButtons |= BIT5;   // Back
  if (Buttons & TRITON_BTN_L3)         XboxButtons |= BIT6;   // Left Stick click
  if (Buttons & TRITON_BTN_R3)         XboxButtons |= BIT7;   // Right Stick click
  if (Buttons & TRITON_BTN_LB)         XboxButtons |= BIT8;   // Left Bumper
  if (Buttons & TRITON_BTN_RB)         XboxButtons |= BIT9;   // Right Bumper
  if (Buttons & TRITON_BTN_STEAM)      XboxButtons |= BIT10;  // Guide
  if (Buttons & TRITON_BTN_QAM)        XboxButtons |= BIT10;  // Guide
  if (Buttons & TRITON_BTN_A)          XboxButtons |= BIT12;  // A
  if (Buttons & TRITON_BTN_B)          XboxButtons |= BIT13;  // B
  if (Buttons & TRITON_BTN_X)          XboxButtons |= BIT14;  // X
  if (Buttons & TRITON_BTN_Y)          XboxButtons |= BIT15;  // Y

  XboxReport[2] = (UINT8)(XboxButtons & 0xFF);
  XboxReport[3] = (UINT8)((XboxButtons >> 8) & 0xFF);

  //
  // Triggers: 0-32767 -> 0-255
  //
  XboxReport[4] = ScaleTriggerValue (State.TriggerLeft);
  XboxReport[5] = ScaleTriggerValue (State.TriggerRight);

  //
  // Sticks: already signed 16-bit with Y positive up, matching the Xbox
  // 360 wire format directly (SDL negates Y only for its own positive-down
  // convention).
  //
  CopyMem (&XboxReport[6],  &State.LeftStickX,  sizeof (INT16));
  CopyMem (&XboxReport[8],  &State.LeftStickY,  sizeof (INT16));
  CopyMem (&XboxReport[10], &State.RightStickX, sizeof (INT16));
  CopyMem (&XboxReport[12], &State.RightStickY, sizeof (INT16));

  return EFI_SUCCESS;
}
