/** @file
  Xbox 360 Driver Logging System Header

  This module provides logging functionality for the Xbox 360 driver.
  Logs are written to daily rotating files on the ESP partition.

  Copyright (c) 2024-2025. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _XBOX360_LOG_H_
#define _XBOX360_LOG_H_

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>

//
// Logging configuration
//
// File logging writes to the ESP on every message. That is useful when
// debugging, but in production it is a flash-wear / FAT-corruption hazard on
// a boot-critical volume, so it is compiled out of RELEASE builds
// (MDEPKG_NDEBUG is defined for RELEASE targets in UsbXbox360Dxe.inf
// [BuildOptions]).
//
#ifndef XBOX360_LOG_ENABLED
  #ifdef MDEPKG_NDEBUG
    #define XBOX360_LOG_ENABLED   0
  #else
    #define XBOX360_LOG_ENABLED   1
  #endif
#endif
#define XBOX360_LOG_MAX_SIZE        (1024 * 1024)  // 1 MB per log file
#define XBOX360_LOG_MAX_FILES       5              // Keep only last 5 log files

//
// Stamped into the log session header so a field log unambiguously
// identifies the build that produced it (a debug-log round-trip on
// rEFInd_GUI issue #23 was burned on exactly this ambiguity).
// Keep in sync with the release tag.
//
#define XBOX360_DRIVER_VERSION      "1.7.2"

//
// Log levels
//
#define LOG_LEVEL_INFO    0
#define LOG_LEVEL_WARN    1
#define LOG_LEVEL_ERROR   2

//
// Logging macros for convenience
//
#define LOG_INFO(Format, ...)   Xbox360Log(LOG_LEVEL_INFO, Format, ##__VA_ARGS__)
#define LOG_WARN(Format, ...)   Xbox360Log(LOG_LEVEL_WARN, Format, ##__VA_ARGS__)
#define LOG_ERROR(Format, ...)  Xbox360Log(LOG_LEVEL_ERROR, Format, ##__VA_ARGS__)

/**
  Write a formatted log entry to the daily log file.
  Log files are named "driver_YYYYMMDD.log" and are automatically rotated.

  @param  Level    Log level (LOG_LEVEL_INFO/WARN/ERROR)
  @param  Format   Printf-style format string (ASCII)
  @param  ...      Variable arguments

  @retval None
**/
VOID
EFIAPI
Xbox360Log (
  IN UINT8        Level,
  IN CONST CHAR8  *Format,
  ...
  );

/**
  Cleanup old log files (can be called on driver unload).
  Removes log files older than the retention policy.

  @retval None
**/
VOID
EFIAPI
Xbox360LogCleanup (
  VOID
  );

/**
  Set the driver's image handle for the logging system.
  This allows the logging system to locate the correct ESP partition
  from which the driver was loaded.

  @param  ImageHandle  Driver's image handle from entry point

  @retval None
**/
VOID
EFIAPI
Xbox360LogSetImageHandle (
  IN EFI_HANDLE  ImageHandle
  );

#endif // _XBOX360_LOG_H_

