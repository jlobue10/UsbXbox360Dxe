/** @file
  Xbox 360 Compatible Device Detection Header

  This module provides device detection and identification for Xbox 360
  compatible controllers, including built-in device list and custom device support.

  Copyright (c) 2024-2025. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _XBOX360_DEVICE_H_
#define _XBOX360_DEVICE_H_

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/UsbIo.h>

//
// Maximum number of custom devices that can be loaded from config
//
#define MAX_CUSTOM_DEVICES  16

//
// Xbox 360 compatible device structure
//
typedef struct {
  UINT16    VendorId;
  UINT16    ProductId;
  CHAR16    *Description;
} XBOX360_COMPATIBLE_DEVICE;

//
// Forward declaration for XBOX360_CONFIG
//
typedef struct _XBOX360_CONFIG XBOX360_CONFIG;

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
  );

/**
  Cleanup and free device list memory.
  Should be called on driver unload.

  @retval None
**/
VOID
CleanupDeviceList (
  VOID
  );

/**
  Check if the given USB device is an Xbox 360 compatible controller.
  
  This function checks the device's VID/PID against the device list
  (built-in devices + custom devices from config).

  @param  UsbIo    Pointer to USB I/O Protocol

  @retval TRUE     Device is Xbox 360 compatible
  @retval FALSE    Device is not compatible or error occurred
**/
BOOLEAN
IsUSBKeyboard (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

/**
  Check whether the bound USB interface is an XInput gamepad data interface
  (vendor class 0xFF, subclass 0x5D, protocol 0x01 wired / 0x81 wireless).

  @param  UsbIo    Pointer to USB I/O Protocol (one interface's instance)

  @retval TRUE     Interface carries Xbox 360 (XInput) gamepad data
  @retval FALSE    Any other interface
**/
BOOLEAN
IsXInputInterface (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

/**
  Check if the given USB device is an MSI Claw controller.

  @param  UsbIo    Pointer to USB I/O Protocol

  @retval TRUE     Device is MSI Claw
  @retval FALSE    Device is not MSI Claw
**/
BOOLEAN
IsMsiClaw (
  IN  EFI_USB_IO_PROTOCOL  *UsbIo
  );

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
  );

#endif // _XBOX360_DEVICE_H_

