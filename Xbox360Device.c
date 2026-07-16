/** @file
  Xbox 360 Compatible Device Detection Implementation

  This module implements device detection and identification for Xbox 360
  compatible controllers. It maintains a list of known devices and provides
  functions to check if a USB device matches.

  Copyright (c) 2024-2025. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "Xbox360Device.h"
#include "Xbox360Log.h"
#include "KeyBoard.h"
#include "AsusAllyDevice.h"
#include "LegionGoDevice.h"

//
// Known Xbox 360 compatible devices
// VID/PID reference: Linux kernel xpad driver
//
STATIC CONST XBOX360_COMPATIBLE_DEVICE  mXbox360BuiltinDevices[] = {
  //
  // Microsoft Official Controllers
  //
  { 0x045E, 0x028E, L"Xbox 360 Wired Controller" },
  { 0x045E, 0x028F, L"Xbox 360 Wired Controller v2" },
  { 0x045E, 0x0719, L"Xbox 360 Wireless Receiver" },
  
  //
  // Handheld Gaming Devices (High Priority)
  //
  { 0x0079, 0x18D4, L"GPD Win 2 Controller" },
  { 0x2563, 0x058D, L"OneXPlayer Gamepad" },
  { 0x17EF, 0x6182, L"Lenovo Legion Go" },
  { 0x17EF, 0x61EB, L"Lenovo Legion Go 2" },
  { 0x17EF, 0x61EC, L"Lenovo Legion Go 2 (DInput)" },
  { 0x17EF, 0x61ED, L"Lenovo Legion Go 2 (detached)" },
  { 0x17EF, 0x61EE, L"Lenovo Legion Go 2 (FPS mode)" },
  { 0x1A86, 0xE310, L"Legion Go S" },
  { 0x0DB0, 0x1901, L"MSI Claw" },
  { 0x2993, 0x2001, L"TECNO Pocket Go" },
  { 0x1EE9, 0x1590, L"ZOTAC Gaming Zone" },
  
  //
  // 8BitDo Controllers
  //
  { 0x2DC8, 0x3106, L"8BitDo Ultimate / Pro 2 Wired" },
  { 0x2DC8, 0x3109, L"8BitDo Ultimate Wireless" },
  { 0x2DC8, 0x310A, L"8BitDo Ultimate 2C Wireless" },
  { 0x2DC8, 0x310B, L"8BitDo Ultimate 2 Wireless" },
  { 0x2DC8, 0x6001, L"8BitDo SN30 Pro" },
  
  //
  // Logitech
  //
  { 0x046D, 0xC21D, L"Logitech F310" },
  { 0x046D, 0xC21E, L"Logitech F510" },
  { 0x046D, 0xC21F, L"Logitech F710" },
  { 0x046D, 0xC242, L"Logitech Chillstream" },
  
  //
  // HyperX
  //
  { 0x03F0, 0x038D, L"HyperX Clutch (wired)" },
  { 0x03F0, 0x048D, L"HyperX Clutch (wireless)" },
  
  //
  // Other Popular Brands
  //
  { 0x1038, 0x1430, L"SteelSeries Stratus Duo" },
  { 0x1038, 0x1431, L"SteelSeries Stratus Duo (alt)" },
  { 0x2345, 0xE00B, L"Machenike G5 Pro" },
  { 0x3537, 0x1004, L"GameSir T4 Kaleid" },
  { 0x37D7, 0x2501, L"Flydigi Apex 5" },
  { 0x413D, 0x2104, L"Black Shark Green Ghost" },
  { 0x1949, 0x041A, L"Amazon Game Controller" },
  
  //
  // Razer
  //
  { 0x1689, 0xFD00, L"Razer Onza Tournament" },
  { 0x1689, 0xFD01, L"Razer Onza Classic" },
  { 0x1689, 0xFE00, L"Razer Sabertooth" },
  
  //
  // Add more devices here as needed
  // Format: { VID, PID, L"Description" },
  //
};

#define XBOX360_BUILTIN_DEVICE_COUNT \
  (sizeof(mXbox360BuiltinDevices) / sizeof(XBOX360_COMPATIBLE_DEVICE))

//
// Dynamic device list (built-in + custom from config)
//
STATIC XBOX360_COMPATIBLE_DEVICE  *mXbox360DeviceList = NULL;
STATIC UINTN                       mXbox360DeviceCount = 0;
STATIC BOOLEAN                     mDeviceListInitialized = FALSE;

/**
  Initialize the device list by combining built-in and custom devices.
  Must be called after loading configuration.

  @param  Config   Pointer to configuration containing custom devices

  @retval EFI_SUCCESS            Device list initialized successfully
  @retval EFI_INVALID_PARAMETER  Config is NULL
  @retval EFI_OUT_OF_RESOURCES   Failed to allocate memory for device list

**/
EFI_STATUS
InitializeDeviceList (
  IN XBOX360_CONFIG  *Config
  )
{
  UINTN  TotalDevices;
  UINTN  Index;

  if (mDeviceListInitialized) {
    return EFI_SUCCESS;
  }

  if (Config == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Calculate total device count
  TotalDevices = XBOX360_BUILTIN_DEVICE_COUNT + Config->CustomDeviceCount;

  // Allocate memory for combined list
  mXbox360DeviceList = AllocateZeroPool(sizeof(XBOX360_COMPATIBLE_DEVICE) * TotalDevices);
  
  if (mXbox360DeviceList == NULL) {
    // Fallback to built-in only
    mXbox360DeviceList = (XBOX360_COMPATIBLE_DEVICE*)mXbox360BuiltinDevices;
    mXbox360DeviceCount = XBOX360_BUILTIN_DEVICE_COUNT;
    mDeviceListInitialized = TRUE;
    DEBUG((DEBUG_WARN, "Xbox360: Failed to allocate device list, using built-in only\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  // Copy built-in devices
  CopyMem(
    mXbox360DeviceList,
    mXbox360BuiltinDevices,
    sizeof(XBOX360_COMPATIBLE_DEVICE) * XBOX360_BUILTIN_DEVICE_COUNT
  );

  // Append custom devices
  for (Index = 0; Index < Config->CustomDeviceCount; Index++) {
    CopyMem(
      &mXbox360DeviceList[XBOX360_BUILTIN_DEVICE_COUNT + Index],
      &Config->CustomDevices[Index],
      sizeof(XBOX360_COMPATIBLE_DEVICE)
    );

    DEBUG((DEBUG_INFO, 
      "Xbox360: Added custom device: %s (VID:0x%04X PID:0x%04X)\n",
      Config->CustomDevices[Index].Description,
      Config->CustomDevices[Index].VendorId,
      Config->CustomDevices[Index].ProductId
    ));
  }

  mXbox360DeviceCount = TotalDevices;
  mDeviceListInitialized = TRUE;

  LOG_INFO("Device list initialized with %d devices (%d built-in + %d custom)",
           (UINT32)TotalDevices,
           (UINT32)XBOX360_BUILTIN_DEVICE_COUNT,
           (UINT32)Config->CustomDeviceCount);

  return EFI_SUCCESS;
}

/**
  Cleanup and free device list memory.
  Should be called on driver unload.

  @retval None
**/
VOID
CleanupDeviceList (
  VOID
  )
{
  UINTN  i;

  if (!mDeviceListInitialized) {
    return;
  }

  // Free custom device descriptions
  if (mXbox360DeviceList != NULL && 
      mXbox360DeviceList != (XBOX360_COMPATIBLE_DEVICE*)mXbox360BuiltinDevices) {
    // Free allocated descriptions for custom devices
    for (i = XBOX360_BUILTIN_DEVICE_COUNT; i < mXbox360DeviceCount; i++) {
      if (mXbox360DeviceList[i].Description != NULL) {
        FreePool(mXbox360DeviceList[i].Description);
      }
    }
    FreePool(mXbox360DeviceList);
  }

  mXbox360DeviceList = NULL;
  mXbox360DeviceCount = 0;
  mDeviceListInitialized = FALSE;
}

/**
  Check if the given USB device is an Xbox 360 compatible controller.
  
  This function checks the device's VID/PID against the device list
  (built-in devices + custom devices from config).
  
  Now also supports ASUS ROG Ally DirectInput devices.

  @param  UsbIo    Pointer to USB I/O Protocol

  @retval TRUE     Device is Xbox 360 compatible or ASUS ROG Ally
  @retval FALSE    Device is not compatible or error occurred
**/
BOOLEAN
IsUSBKeyboard (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS                  Status;
  EFI_USB_DEVICE_DESCRIPTOR   DeviceDescriptor;
  UINTN                       Index;

  Status = UsbIo->UsbGetDeviceDescriptor (UsbIo, &DeviceDescriptor);
  if (EFI_ERROR (Status)) {
    LOG_WARN ("Failed to get device descriptor: %r", Status);
    return FALSE;
  }

  // Log the device being checked (important for debugging)
  LOG_INFO ("Checking USB device: VID:0x%04X PID:0x%04X", 
            DeviceDescriptor.IdVendor, 
            DeviceDescriptor.IdProduct);
  
  //
  // Priority 1: Check for ASUS ROG Ally devices (DirectInput)
  // These devices do not support XInput mode and require special handling
  //
  if (IsAsusAlly (UsbIo)) {
    return TRUE;
  }

  //
  // Priority 2: Legion Go 2 in a DInput-family mode (no XInput data
  // interface exists) -- handled via Lenovo's vendor raw HID reports.
  //
  if (IsLegionGoRaw (UsbIo)) {
    LOG_INFO ("Lenovo Legion Go 2 (DInput-family mode) detected");
    return TRUE;
  }

  // Initialize device list if not already done
  if (!mDeviceListInitialized) {
    // Use built-in devices only as fallback
    mXbox360DeviceList = (XBOX360_COMPATIBLE_DEVICE*)mXbox360BuiltinDevices;
    mXbox360DeviceCount = XBOX360_BUILTIN_DEVICE_COUNT;
    LOG_INFO ("Device list initialized with %d built-in devices", (UINT32)mXbox360DeviceCount);
  }

  //
  // Check against combined device list (built-in + custom)
  //
  for (Index = 0; Index < mXbox360DeviceCount; Index++) {
    if ((DeviceDescriptor.IdVendor == mXbox360DeviceList[Index].VendorId) &&
        (DeviceDescriptor.IdProduct == mXbox360DeviceList[Index].ProductId))
    {
      //
      // VID/PID matched -- but driver binding is per USB *interface*, and
      // multi-interface pads (Legion Go 2 & co.) also expose HID config and
      // touchpad interfaces whose reports must never be parsed as Xbox 360
      // state. Bind only the XInput data interface. The MSI Claw is exempt:
      // it enumerates in DirectInput mode and needs a binding to receive the
      // XInput mode-switch command (it re-enumerates afterwards).
      //
      if (!IsXInputInterface (UsbIo) && !IsMsiClaw (UsbIo)) {
        LOG_INFO ("Device VID:0x%04X PID:0x%04X matched, but this interface is not an XInput data interface; skipping it",
                  DeviceDescriptor.IdVendor,
                  DeviceDescriptor.IdProduct);
        return FALSE;
      }

      // Found a match! Log the details
      LOG_INFO ("MATCH FOUND! Device: %s (VID:0x%04X PID:0x%04X)%s",
                mXbox360DeviceList[Index].Description,
                DeviceDescriptor.IdVendor,
                DeviceDescriptor.IdProduct,
                (Index >= XBOX360_BUILTIN_DEVICE_COUNT) ? L" [CUSTOM]" : L"");
      DEBUG ((
        DEBUG_INFO,
        "Xbox360Dxe: Found compatible device: %s (VID:0x%04X PID:0x%04X)%s\n",
        mXbox360DeviceList[Index].Description,
        DeviceDescriptor.IdVendor,
        DeviceDescriptor.IdProduct,
        (Index >= XBOX360_BUILTIN_DEVICE_COUNT) ? L" [CUSTOM]" : L""
        ));
      return TRUE;
    }
  }

  // Log when device doesn't match (important for debugging)
  LOG_INFO ("Device VID:0x%04X PID:0x%04X does not match any known Xbox 360 controller",
            DeviceDescriptor.IdVendor,
            DeviceDescriptor.IdProduct);
  return FALSE;
}

/**
  Check whether the bound USB interface is an XInput gamepad data interface.

  UEFI driver binding runs once per USB *interface*, and multi-interface
  gamepads (e.g. the Lenovo Legion Go 2: XInput data + HID config + HID
  touchpad) previously matched on VID/PID alone, so this driver also bound
  their HID interfaces and parsed those reports as Xbox 360 state -- the
  source of the phantom-input "menu clicks itself" bug (rEFInd_GUI issue
  #23). An Xbox 360 protocol data interface is vendor-specific class 0xFF,
  subclass 0x5D, protocol 0x01 (wired) or 0x81 (wireless receiver) -- the
  same match the Linux xpad driver uses.

  @param  UsbIo    Pointer to USB I/O Protocol (one interface's instance)

  @retval TRUE     Interface carries Xbox 360 (XInput) gamepad data
  @retval FALSE    Any other interface (HID config/touchpad/audio/...)
**/
BOOLEAN
IsXInputInterface (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS                    Status;
  EFI_USB_INTERFACE_DESCRIPTOR  InterfaceDescriptor;

  if (UsbIo == NULL) {
    return FALSE;
  }

  Status = UsbIo->UsbGetInterfaceDescriptor (UsbIo, &InterfaceDescriptor);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  return (InterfaceDescriptor.InterfaceClass == 0xFF) &&
         (InterfaceDescriptor.InterfaceSubClass == 0x5D) &&
         ((InterfaceDescriptor.InterfaceProtocol == 0x01) ||
          (InterfaceDescriptor.InterfaceProtocol == 0x81));
}

/**
  Check if the given USB device is an MSI Claw controller.

  @param  UsbIo    Pointer to USB I/O Protocol

  @retval TRUE     Device is MSI Claw
  @retval FALSE    Device is not MSI Claw
**/
BOOLEAN
IsMsiClaw (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS                  Status;
  EFI_USB_DEVICE_DESCRIPTOR   DeviceDescriptor;

  if (UsbIo == NULL) {
    return FALSE;
  }

  Status = UsbIo->UsbGetDeviceDescriptor (UsbIo, &DeviceDescriptor);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  // MSI Claw: VID=0x0DB0, PID=0x1901
  return (DeviceDescriptor.IdVendor == 0x0DB0 && 
          DeviceDescriptor.IdProduct == 0x1901);
}

/**
  Switch MSI Claw controller to XInput mode.
  
  MSI Claw controllers default to DirectInput mode in UEFI. This function
  sends USB commands to switch the controller to XInput mode for better
  compatibility.

  @param  UsbIo    Pointer to USB I/O Protocol

  @retval EFI_SUCCESS     Mode switch commands sent successfully
  @retval Other           Failed to switch mode
**/
EFI_STATUS
SwitchMsiClawToXInputMode (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_STATUS              Status;
  UINT8                   CommandBuffer[64];
  UINT32                  UsbStatus;
  EFI_USB_DEVICE_REQUEST  Request;

  if (UsbIo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  LOG_INFO ("MSI Claw detected, switching to XInput mode...");

  //
  // Command 1: Switch to XInput mode
  // Format: [0x0F, 0x00, 0x00, 0x3C, 0x24, 0x01, 0x00, ... (rest zeros)]
  //
  ZeroMem (CommandBuffer, sizeof(CommandBuffer));
  CommandBuffer[0] = 0x0F;  // Report ID
  CommandBuffer[1] = 0x00;
  CommandBuffer[2] = 0x00;
  CommandBuffer[3] = 0x3C;
  CommandBuffer[4] = 0x24;  // Command: SWITCH_MODE
  CommandBuffer[5] = 0x01;  // Mode: XInput
  CommandBuffer[6] = 0x00;  // Macro function: disabled

  // Setup USB HID Set_Report request
  Request.RequestType = 0x21;  // Host to Device, Class, Interface
  Request.Request     = 0x09;  // SET_REPORT
  Request.Value       = 0x020F;  // Report Type (Output=0x02) | Report ID (0x0F)
  Request.Index       = 0;     // Interface 0
  Request.Length      = sizeof(CommandBuffer);
  
  Status = UsbIo->UsbControlTransfer (
                    UsbIo,
                    &Request,
                    EfiUsbDataOut,
                    100,  // 100ms timeout
                    CommandBuffer,
                    sizeof(CommandBuffer),
                    &UsbStatus
                    );

  if (EFI_ERROR (Status)) {
    LOG_ERROR ("Failed to send SWITCH_MODE command: %r", Status);
    return Status;
  }

  LOG_INFO ("SWITCH_MODE command sent successfully");

  // Delay between commands
  gBS->Stall (50000);  // 50ms

  //
  // Command 2: Sync to ROM (save settings)
  // Format: [0x0F, 0x00, 0x00, 0x3C, 0x22, ... (rest zeros)]
  //
  ZeroMem (CommandBuffer, sizeof(CommandBuffer));
  CommandBuffer[0] = 0x0F;  // Report ID
  CommandBuffer[1] = 0x00;
  CommandBuffer[2] = 0x00;
  CommandBuffer[3] = 0x3C;
  CommandBuffer[4] = 0x22;  // Command: SYNC_TO_ROM

  // Setup USB HID Set_Report request
  Request.RequestType = 0x21;  // Host to Device, Class, Interface
  Request.Request     = 0x09;  // SET_REPORT
  Request.Value       = 0x020F;  // Report Type (Output=0x02) | Report ID (0x0F)
  Request.Index       = 0;     // Interface 0
  Request.Length      = sizeof(CommandBuffer);
  
  Status = UsbIo->UsbControlTransfer (
                    UsbIo,
                    &Request,
                    EfiUsbDataOut,
                    100,  // 100ms timeout
                    CommandBuffer,
                    sizeof(CommandBuffer),
                    &UsbStatus
                    );

  if (EFI_ERROR (Status)) {
    LOG_WARN ("Failed to send SYNC_TO_ROM command: %r (non-critical)", Status);
    // Don't return error, SYNC_TO_ROM is optional
  } else {
    LOG_INFO ("SYNC_TO_ROM command sent successfully");
  }

  // Final delay to let device process
  gBS->Stall (100000);  // 100ms

  LOG_INFO ("MSI Claw mode switch completed");
  
  return EFI_SUCCESS;
}

