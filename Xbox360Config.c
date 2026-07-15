/** @file
  Xbox 360 Driver Configuration System Implementation

  This module implements configuration file parsing, validation, and management
  for the Xbox 360 controller driver. It supports INI-format configuration files
  with version migration and automatic template generation.

  Copyright (c) 2024-2025. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "Xbox360Config.h"
#include "Xbox360Log.h"
#include "KeyBoard.h"
#include "Xbox360KeyNames.h"

//
// Global configuration
//
STATIC XBOX360_CONFIG  mGlobalConfig;

//
// Helper function prototypes
//
STATIC
VOID
TrimString (
  IN OUT CHAR8  *Str
  );

STATIC
BOOLEAN
ParseDeviceString (
  IN  CHAR8                      *DeviceStr,
  OUT XBOX360_COMPATIBLE_DEVICE  *Device
  );

STATIC
VOID
TrimString (
  IN OUT CHAR8  *Str
  )
{
  CHAR8  *Start;
  CHAR8  *End;
  UINTN  Len;

  if (Str == NULL || *Str == '\0') {
    return;
  }

  // Find first non-whitespace character
  Start = Str;
  while (*Start == ' ' || *Start == '\t' || *Start == '\r' || *Start == '\n') {
    Start++;
  }

  // If string is all whitespace, set to empty string
  if (*Start == '\0') {
    *Str = '\0';
    return;
  }

  // Move content to beginning if there was leading whitespace
  if (Start != Str) {
    Len = AsciiStrLen(Start);
    CopyMem(Str, Start, Len + 1);  // +1 to include null terminator
  }

  // Trim trailing whitespace
  End = Str + AsciiStrLen(Str) - 1;
  while (End > Str && (*End == ' ' || *End == '\t' || *End == '\r' || *End == '\n')) {
    *End = '\0';
    End--;
  }
}

/**
  Parse device string in format: VID:PID:Description
  VID and PID can be hex (0x1234 or 1234).

  @param  DeviceStr  String to parse (e.g., "0x1234:0x5678:My Controller").
  @param  Device     Output device structure.

  @retval TRUE   Successfully parsed.
  @retval FALSE  Parse error.
**/
STATIC
BOOLEAN
ParseDeviceString (
  IN  CHAR8                      *DeviceStr,
  OUT XBOX360_COMPATIBLE_DEVICE  *Device
  )
{
  CHAR8  *VidStr;
  CHAR8  *PidStr;
  CHAR8  *DescStr;
  CHAR8  *Colon1;
  CHAR8  *Colon2;
  UINTN  DescLen;
  UINTN  i;

  if (DeviceStr == NULL || Device == NULL) {
    return FALSE;
  }

  // Find first colon
  Colon1 = AsciiStrStr(DeviceStr, ":");
  if (Colon1 == NULL) {
    return FALSE;
  }
  *Colon1 = '\0';

  // Find second colon
  Colon2 = AsciiStrStr(Colon1 + 1, ":");
  if (Colon2 == NULL) {
    return FALSE;
  }
  *Colon2 = '\0';

  VidStr = DeviceStr;
  PidStr = Colon1 + 1;
  DescStr = Colon2 + 1;

  // Trim strings
  TrimString(VidStr);
  TrimString(PidStr);
  TrimString(DescStr);

  // Parse VID (support both 0x1234 and 1234 format)
  if ((AsciiStrnCmp(VidStr, "0x", 2) == 0) || (AsciiStrnCmp(VidStr, "0X", 2) == 0)) {
    Device->VendorId = (UINT16)AsciiStrHexToUintn(VidStr + 2);
  } else {
    Device->VendorId = (UINT16)AsciiStrHexToUintn(VidStr);
  }

  // Parse PID
  if ((AsciiStrnCmp(PidStr, "0x", 2) == 0) || (AsciiStrnCmp(PidStr, "0X", 2) == 0)) {
    Device->ProductId = (UINT16)AsciiStrHexToUintn(PidStr + 2);
  } else {
    Device->ProductId = (UINT16)AsciiStrHexToUintn(PidStr);
  }

  // Convert description from ASCII to Unicode
  DescLen = AsciiStrLen(DescStr);
  if (DescLen > 63) {
    DescLen = 63;  // Limit length
  }

  // Allocate memory for description
  Device->Description = AllocateZeroPool((DescLen + 1) * sizeof(CHAR16));
  if (Device->Description == NULL) {
    return FALSE;
  }

  for (i = 0; i < DescLen; i++) {
    Device->Description[i] = (CHAR16)DescStr[i];
  }
  Device->Description[DescLen] = L'\0';

  // Validate VID/PID (must not be 0x0000)
  if ((Device->VendorId == 0) || (Device->ProductId == 0)) {
    if (Device->Description != NULL) {
      FreePool(Device->Description);
      Device->Description = NULL;
    }
    return FALSE;
  }

  return TRUE;
}

/**
  Set default configuration values.

  @param  Config  Pointer to configuration structure to initialize.
**/
STATIC
VOID
SetDefaultConfig (
  OUT XBOX360_CONFIG  *Config
  )
{
  if (Config == NULL) {
    return;
  }

  ZeroMem(Config, sizeof(XBOX360_CONFIG));

  Config->Version = XBOX360_CONFIG_VERSION_CURRENT;
  Config->StickDeadzone = 8000;
  Config->TriggerThreshold = 128;
  // Trigger key mappings - Default to mouse buttons for better mouse mode experience
  Config->LeftTriggerKey = FUNCTION_CODE_MOUSE_RIGHT;   // 0xF1 - Mouse Right Button
  Config->RightTriggerKey = FUNCTION_CODE_MOUSE_LEFT;   // 0xF0 - Mouse Left Button

  // Default button mappings (from existing mXbox360ButtonMap)
  Config->ButtonMap[0] = 0x52;   // DPAD_UP -> Up Arrow
  Config->ButtonMap[1] = 0x51;   // DPAD_DOWN -> Down Arrow
  Config->ButtonMap[2] = 0x50;   // DPAD_LEFT -> Left Arrow
  Config->ButtonMap[3] = 0x4F;   // DPAD_RIGHT -> Right Arrow
  Config->ButtonMap[4] = 0x2C;   // START -> Space
  Config->ButtonMap[5] = 0x2B;   // BACK -> Tab
  Config->ButtonMap[6] = 0xE0;   // LEFT_THUMB -> Left Control
  Config->ButtonMap[7] = 0xE2;   // RIGHT_THUMB -> Left Alt
  Config->ButtonMap[8] = 0x4B;   // LEFT_SHOULDER -> Page Up
  Config->ButtonMap[9] = 0x4E;   // RIGHT_SHOULDER -> Page Down
  Config->ButtonMap[10] = 0xE1;  // GUIDE -> Left Shift
  Config->ButtonMap[11] = 0xFF;  // Reserved
  Config->ButtonMap[12] = 0x28;  // A -> Enter
  Config->ButtonMap[13] = 0x29;  // B -> Escape
  Config->ButtonMap[14] = 0x2A;  // X -> Backspace
  Config->ButtonMap[15] = 0x2B;  // Y -> Tab

  Config->CustomDeviceCount = 0;
  
  // Left stick defaults: Mouse mode for cursor control
  Config->LeftStick.Mode = STICK_MODE_MOUSE;
  Config->LeftStick.Deadzone = 8000;
  Config->LeftStick.Saturation = 32000;
  Config->LeftStick.MouseSensitivity = 50;
  Config->LeftStick.MouseMaxSpeed = 20;
  Config->LeftStick.MouseCurve = 2;  // Square curve (recommended)
  Config->LeftStick.DirectionMode = 4;  // 4-way
  Config->LeftStick.UpMapping = 0x52;    // Up Arrow
  Config->LeftStick.DownMapping = 0x51;  // Down Arrow
  Config->LeftStick.LeftMapping = 0x50;  // Left Arrow
  Config->LeftStick.RightMapping = 0x4F; // Right Arrow
  
  // Right stick defaults: Scroll mode (vertical only)
  Config->RightStick.Mode = STICK_MODE_SCROLL;
  Config->RightStick.Deadzone = 8689;  // Xbox standard for right stick
  Config->RightStick.Saturation = 32000;
  Config->RightStick.MouseSensitivity = 50;
  Config->RightStick.MouseMaxSpeed = 20;
  Config->RightStick.MouseCurve = 2;
  Config->RightStick.DirectionMode = 4;
  Config->RightStick.UpMapping = 0x1A;    // W
  Config->RightStick.DownMapping = 0x16;  // S
  Config->RightStick.LeftMapping = 0x04;  // A
  Config->RightStick.RightMapping = 0x07; // D
  Config->RightStick.ScrollSensitivity = 30;  // Medium sensitivity
  Config->RightStick.ScrollDeadzone = 0;      // Use standard deadzone
  
  // Left stick scroll settings (if user switches to scroll mode)
  Config->LeftStick.ScrollSensitivity = 30;
  Config->LeftStick.ScrollDeadzone = 0;
}

/**
  Parse version from config file.
  Format: Version=1.0 or Version=0x0100

  @param  ConfigData  Configuration file content.

  @retval Version number, or 0 if not found.
**/
STATIC
UINT16
ParseConfigVersion (
  IN CHAR8  *ConfigData
  )
{
  CHAR8   *VersionLine;
  UINT16  Major;
  UINT16  Minor;
  CHAR8   *Dot;

  if (ConfigData == NULL) {
    return 0;
  }

  // Look for "Version=" line
  VersionLine = AsciiStrStr(ConfigData, "Version=");
  if (VersionLine == NULL) {
    return 0;
  }

  VersionLine += 8; // Skip "Version="

  // Trim leading whitespace
  while (*VersionLine == ' ' || *VersionLine == '\t') {
    VersionLine++;
  }

  // Support hex format (0x0100)
  if ((VersionLine[0] == '0') && ((VersionLine[1] == 'x') || (VersionLine[1] == 'X'))) {
    return (UINT16)AsciiStrHexToUintn(VersionLine);
  }

  // Parse decimal "major.minor" format
  Major = (UINT16)AsciiStrDecimalToUintn(VersionLine);
  Dot = AsciiStrStr(VersionLine, ".");
  Minor = 0;
  if (Dot != NULL) {
    Minor = (UINT16)AsciiStrDecimalToUintn(Dot + 1);
  }

  return (Major << 8) | Minor;
}

/**
  Parse INI configuration file.

  @param  IniData  Configuration file content (will be modified).
  @param  Config   Configuration structure to populate.
**/
STATIC
VOID
ParseIniConfig (
  IN  CHAR8           *IniData,
  OUT XBOX360_CONFIG  *Config
  )
{
  CHAR8  *Line;
  CHAR8  *NextLine;
  CHAR8  *Key;
  CHAR8  *Value;
  CHAR8  *Equals;
  UINTN  DeviceIndex;

  if ((IniData == NULL) || (Config == NULL)) {
    return;
  }

  Line = IniData;
  DeviceIndex = 0;

  while ((Line != NULL) && (*Line != '\0')) {
    // Find next line
    NextLine = AsciiStrStr(Line, "\n");
    if (NextLine != NULL) {
      *NextLine = '\0';
      NextLine++;
    }

    // Trim line
    TrimString(Line);

    // Skip empty lines, comments, and section headers
    if ((*Line == '\0') || (*Line == '#') || (*Line == ';') || (*Line == '[')) {
      Line = NextLine;
      continue;
    }

    // Find '=' separator
    Equals = AsciiStrStr(Line, "=");
    if (Equals == NULL) {
      Line = NextLine;
      continue;
    }

    *Equals = '\0';
    Key = Line;
    Value = Equals + 1;

    // Trim key and value
    TrimString(Key);
    TrimString(Value);

    // Skip empty values
    if (*Value == '\0') {
      Line = NextLine;
      continue;
    }

    // Parse known configuration keys
    if (AsciiStrCmp(Key, "Version") == 0) {
      // Already parsed separately
    }
    else if (AsciiStrCmp(Key, "Deadzone") == 0) {
      Config->StickDeadzone = (UINT16)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "TriggerThreshold") == 0) {
      Config->TriggerThreshold = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "LeftTrigger") == 0) {
      Config->LeftTriggerKey = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "RightTrigger") == 0) {
      Config->RightTriggerKey = ParseKeyValue(Value);
    }
    // Parse button mappings
    else if (AsciiStrCmp(Key, "ButtonDpadUp") == 0) {
      Config->ButtonMap[0] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonDpadDown") == 0) {
      Config->ButtonMap[1] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonDpadLeft") == 0) {
      Config->ButtonMap[2] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonDpadRight") == 0) {
      Config->ButtonMap[3] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonStart") == 0) {
      Config->ButtonMap[4] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonBack") == 0) {
      Config->ButtonMap[5] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonLeftThumb") == 0) {
      Config->ButtonMap[6] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonRightThumb") == 0) {
      Config->ButtonMap[7] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonLeftShoulder") == 0) {
      Config->ButtonMap[8] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonRightShoulder") == 0) {
      Config->ButtonMap[9] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonGuide") == 0) {
      Config->ButtonMap[10] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonA") == 0) {
      Config->ButtonMap[12] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonB") == 0) {
      Config->ButtonMap[13] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonX") == 0) {
      Config->ButtonMap[14] = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "ButtonY") == 0) {
      Config->ButtonMap[15] = ParseKeyValue(Value);
    }
    // Parse custom devices (Device1=, Device2=, etc.)
    else if ((AsciiStrnCmp(Key, "Device", 6) == 0) && (DeviceIndex < MAX_CUSTOM_DEVICES)) {
      if (ParseDeviceString(Value, &Config->CustomDevices[DeviceIndex])) {
        DeviceIndex++;
      }
    }
    // Parse left stick configuration
    else if (AsciiStrCmp(Key, "LeftStickMode") == 0) {
      if (AsciiStrCmp(Value, "Mouse") == 0 || AsciiStrCmp(Value, "mouse") == 0) {
        Config->LeftStick.Mode = STICK_MODE_MOUSE;
      } else if (AsciiStrCmp(Value, "Keys") == 0 || AsciiStrCmp(Value, "keys") == 0) {
        Config->LeftStick.Mode = STICK_MODE_KEYS;
      } else if (AsciiStrCmp(Value, "Scroll") == 0 || AsciiStrCmp(Value, "scroll") == 0) {
        Config->LeftStick.Mode = STICK_MODE_SCROLL;
      } else if (AsciiStrCmp(Value, "Disabled") == 0 || AsciiStrCmp(Value, "disabled") == 0) {
        Config->LeftStick.Mode = STICK_MODE_DISABLED;
      }
    }
    else if (AsciiStrCmp(Key, "LeftStickDeadzone") == 0) {
      Config->LeftStick.Deadzone = (UINT16)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "LeftStickSaturation") == 0) {
      Config->LeftStick.Saturation = (UINT16)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "LeftStickMouseSensitivity") == 0) {
      Config->LeftStick.MouseSensitivity = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "LeftStickMouseMaxSpeed") == 0) {
      Config->LeftStick.MouseMaxSpeed = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "LeftStickMouseCurve") == 0) {
      Config->LeftStick.MouseCurve = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "LeftStickDirectionMode") == 0) {
      Config->LeftStick.DirectionMode = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "LeftStickUpMapping") == 0) {
      Config->LeftStick.UpMapping = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "LeftStickDownMapping") == 0) {
      Config->LeftStick.DownMapping = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "LeftStickLeftMapping") == 0) {
      Config->LeftStick.LeftMapping = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "LeftStickRightMapping") == 0) {
      Config->LeftStick.RightMapping = ParseKeyValue(Value);
    }
    // Parse right stick configuration
    else if (AsciiStrCmp(Key, "RightStickMode") == 0) {
      if (AsciiStrCmp(Value, "Mouse") == 0 || AsciiStrCmp(Value, "mouse") == 0) {
        Config->RightStick.Mode = STICK_MODE_MOUSE;
      } else if (AsciiStrCmp(Value, "Keys") == 0 || AsciiStrCmp(Value, "keys") == 0) {
        Config->RightStick.Mode = STICK_MODE_KEYS;
      } else if (AsciiStrCmp(Value, "Scroll") == 0 || AsciiStrCmp(Value, "scroll") == 0) {
        Config->RightStick.Mode = STICK_MODE_SCROLL;
      } else if (AsciiStrCmp(Value, "Disabled") == 0 || AsciiStrCmp(Value, "disabled") == 0) {
        Config->RightStick.Mode = STICK_MODE_DISABLED;
      }
    }
    else if (AsciiStrCmp(Key, "RightStickDeadzone") == 0) {
      Config->RightStick.Deadzone = (UINT16)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickSaturation") == 0) {
      Config->RightStick.Saturation = (UINT16)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickMouseSensitivity") == 0) {
      Config->RightStick.MouseSensitivity = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickMouseMaxSpeed") == 0) {
      Config->RightStick.MouseMaxSpeed = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickMouseCurve") == 0) {
      Config->RightStick.MouseCurve = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickDirectionMode") == 0) {
      Config->RightStick.DirectionMode = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickUpMapping") == 0) {
      Config->RightStick.UpMapping = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickDownMapping") == 0) {
      Config->RightStick.DownMapping = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickLeftMapping") == 0) {
      Config->RightStick.LeftMapping = ParseKeyValue(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickRightMapping") == 0) {
      Config->RightStick.RightMapping = ParseKeyValue(Value);
    }
    // Parse scroll sensitivity for both sticks
    else if (AsciiStrCmp(Key, "LeftStickScrollSensitivity") == 0) {
      Config->LeftStick.ScrollSensitivity = (UINT8)AsciiStrDecimalToUintn(Value);
    }
    else if (AsciiStrCmp(Key, "RightStickScrollSensitivity") == 0) {
      Config->RightStick.ScrollSensitivity = (UINT8)AsciiStrDecimalToUintn(Value);
    }

    Line = NextLine;
  }

  Config->CustomDeviceCount = DeviceIndex;
}

/**
  Validate and sanitize configuration values.

  @param  Config  Configuration structure to validate (modified in place).
**/
STATIC
VOID
ValidateAndSanitizeConfig (
  IN OUT XBOX360_CONFIG  *Config
  )
{
  UINTN  i;

  if (Config == NULL) {
    return;
  }

  // Clamp deadzone to valid range
  if (Config->StickDeadzone > 32767) {
    DEBUG((DEBUG_WARN, "Xbox360: Deadzone %d out of range, clamping to 32767\n", Config->StickDeadzone));
    Config->StickDeadzone = 32767;
  }

  // Validate trigger keys (USB HID scan codes <= 0xE7, function codes 0xF0-0xF4, or 0xFF for disabled)
  if ((Config->LeftTriggerKey > 0xE7) && (Config->LeftTriggerKey < 0xF0) && (Config->LeftTriggerKey != 0xFF)) {
    DEBUG((DEBUG_WARN, "Xbox360: Invalid LeftTriggerKey 0x%02X, using default\n", Config->LeftTriggerKey));
    Config->LeftTriggerKey = FUNCTION_CODE_MOUSE_RIGHT;
  }
  if ((Config->LeftTriggerKey > 0xF4) && (Config->LeftTriggerKey != 0xFF)) {
    DEBUG((DEBUG_WARN, "Xbox360: Invalid LeftTriggerKey 0x%02X, using default\n", Config->LeftTriggerKey));
    Config->LeftTriggerKey = FUNCTION_CODE_MOUSE_RIGHT;
  }

  if ((Config->RightTriggerKey > 0xE7) && (Config->RightTriggerKey < 0xF0) && (Config->RightTriggerKey != 0xFF)) {
    DEBUG((DEBUG_WARN, "Xbox360: Invalid RightTriggerKey 0x%02X, using default\n", Config->RightTriggerKey));
    Config->RightTriggerKey = FUNCTION_CODE_MOUSE_LEFT;
  }
  if ((Config->RightTriggerKey > 0xF4) && (Config->RightTriggerKey != 0xFF)) {
    DEBUG((DEBUG_WARN, "Xbox360: Invalid RightTriggerKey 0x%02X, using default\n", Config->RightTriggerKey));
    Config->RightTriggerKey = FUNCTION_CODE_MOUSE_LEFT;
  }

  // Validate button mappings (USB HID codes <= 0xE7, function codes 0xF0-0xF4, or 0xFF for disabled)
  for (i = 0; i < 16; i++) {
    if ((Config->ButtonMap[i] > 0xE7) && (Config->ButtonMap[i] < 0xF0) && (Config->ButtonMap[i] != 0xFF)) {
      DEBUG((DEBUG_WARN, "Xbox360: Invalid scan code 0x%02X for button %d, disabling\n", Config->ButtonMap[i], i));
      Config->ButtonMap[i] = 0xFF;
    }
    if ((Config->ButtonMap[i] > 0xF4) && (Config->ButtonMap[i] != 0xFF)) {
      DEBUG((DEBUG_WARN, "Xbox360: Invalid scan code 0x%02X for button %d, disabling\n", Config->ButtonMap[i], i));
      Config->ButtonMap[i] = 0xFF;
    }
  }

  // Clamp custom device count
  if (Config->CustomDeviceCount > MAX_CUSTOM_DEVICES) {
    DEBUG((DEBUG_WARN, "Xbox360: Custom device count %d exceeds maximum, clamping to %d\n", 
      Config->CustomDeviceCount, MAX_CUSTOM_DEVICES));
    Config->CustomDeviceCount = MAX_CUSTOM_DEVICES;
  }

  // Validate left stick configuration
  if (Config->LeftStick.Mode > STICK_MODE_SCROLL) {
    DEBUG((DEBUG_WARN, "Xbox360: Invalid LeftStick mode %d, defaulting to Keys\n", Config->LeftStick.Mode));
    Config->LeftStick.Mode = STICK_MODE_KEYS;
  }
  if (Config->LeftStick.Deadzone > 32767) {
    DEBUG((DEBUG_WARN, "Xbox360: LeftStick deadzone %d out of range, clamping to 32767\n", Config->LeftStick.Deadzone));
    Config->LeftStick.Deadzone = 32767;
  }
  if (Config->LeftStick.MouseSensitivity < 1 || Config->LeftStick.MouseSensitivity > 100) {
    Config->LeftStick.MouseSensitivity = 50;
  }
  if (Config->LeftStick.MouseCurve < 1 || Config->LeftStick.MouseCurve > 3) {
    Config->LeftStick.MouseCurve = 2;  // Default to square
  }
  if (Config->LeftStick.DirectionMode != 4 && Config->LeftStick.DirectionMode != 8) {
    Config->LeftStick.DirectionMode = 4;  // Default to 4-way
  }
  
  // Validate right stick configuration
  if (Config->RightStick.Mode > STICK_MODE_SCROLL) {
    DEBUG((DEBUG_WARN, "Xbox360: Invalid RightStick mode %d, defaulting to Scroll\n", Config->RightStick.Mode));
    Config->RightStick.Mode = STICK_MODE_SCROLL;
  }
  if (Config->RightStick.Deadzone > 32767) {
    DEBUG((DEBUG_WARN, "Xbox360: RightStick deadzone %d out of range, clamping to 32767\n", Config->RightStick.Deadzone));
    Config->RightStick.Deadzone = 32767;
  }
  if (Config->RightStick.MouseSensitivity < 1 || Config->RightStick.MouseSensitivity > 100) {
    Config->RightStick.MouseSensitivity = 50;
  }
  if (Config->RightStick.MouseCurve < 1 || Config->RightStick.MouseCurve > 3) {
    Config->RightStick.MouseCurve = 2;
  }
  if (Config->RightStick.DirectionMode != 4 && Config->RightStick.DirectionMode != 8) {
    Config->RightStick.DirectionMode = 4;
  }

  // Validate scroll sensitivity
  if (Config->LeftStick.ScrollSensitivity < 1 || Config->LeftStick.ScrollSensitivity > 100) {
    Config->LeftStick.ScrollSensitivity = 30;
  }
  if (Config->RightStick.ScrollSensitivity < 1 || Config->RightStick.ScrollSensitivity > 100) {
    Config->RightStick.ScrollSensitivity = 30;
  }

  // Validate Saturation > Deadzone to prevent division-by-zero
  if (Config->LeftStick.Saturation <= Config->LeftStick.Deadzone) {
    DEBUG((DEBUG_WARN, "Xbox360: LeftStick Saturation (%d) must be greater than Deadzone (%d), adjusting\n", 
      Config->LeftStick.Saturation, Config->LeftStick.Deadzone));
    Config->LeftStick.Saturation = Config->LeftStick.Deadzone + 1000;
    if (Config->LeftStick.Saturation > 32767) {
      Config->LeftStick.Saturation = 32767;
      Config->LeftStick.Deadzone = 31767;
    }
  }
  if (Config->RightStick.Saturation <= Config->RightStick.Deadzone) {
    DEBUG((DEBUG_WARN, "Xbox360: RightStick Saturation (%d) must be greater than Deadzone (%d), adjusting\n", 
      Config->RightStick.Saturation, Config->RightStick.Deadzone));
    Config->RightStick.Saturation = Config->RightStick.Deadzone + 1000;
    if (Config->RightStick.Saturation > 32767) {
      Config->RightStick.Saturation = 32767;
      Config->RightStick.Deadzone = 31767;
    }
  }

  // Update version to current
  Config->Version = XBOX360_CONFIG_VERSION_CURRENT;
}

/**
  Generate default configuration file template.

  @retval Pointer to configuration template string (static storage).
**/
STATIC
CHAR8 *
GenerateConfigTemplate (
  VOID
  )
{
  STATIC CHAR8 Template[] = 
    "# Xbox 360 Controller Driver Configuration\r\n"
    "# =========================================\r\n"
    "# Edit this file and reboot to apply changes\r\n"
    "# This file was auto-generated on first boot\r\n"
    "\r\n"
    "Version=1.0\r\n"
    "\r\n"
    "# Analog Stick Settings\r\n"
    "# Deadzone: 0-32767 (default: 8000)\r\n"
    "Deadzone=8000\r\n"
    "\r\n"
    "# Trigger Settings\r\n"
    "# TriggerThreshold: 0-255 (default: 128)\r\n"
    "TriggerThreshold=128\r\n"
    "\r\n"
    "# Trigger key mappings (semantic names or hex codes)\r\n"
    "# Mouse functions: MouseLeft, MouseRight, MouseMiddle, ScrollUp, ScrollDown\r\n"
    "# Keyboard keys: KeyEnter, KeyEscape, KeySpace, KeyA-KeyZ, Key0-Key9,\r\n"
    "#                KeyUp/Down/Left/Right, KeyPageUp/PageDown, KeyHome/End, etc.\r\n"
    "# Hex codes: 0x00-0xE7 (USB HID spec) or 0xF0-0xF4 (mouse functions)\r\n"
    "# Set to 'Disabled' or 0xFF to disable\r\n"
    "\r\n"
    "# Default: Triggers as mouse buttons (recommended for mouse mode)\r\n"
    "RightTrigger=MouseLeft\r\n"
    "LeftTrigger=MouseRight\r\n"
    "\r\n"
    "# Alternative: Use as keyboard keys\r\n"
    "# RightTrigger=KeyEnd\r\n"
    "# LeftTrigger=KeyDelete\r\n"
    "\r\n"
    "# Button Mappings (Optional)\r\n"
    "# Uncomment and modify to customize button mappings\r\n"
    "# If not specified, defaults shown in comments are used\r\n"
    "# Set to 'Disabled' to disable a button\r\n"
    "#\r\n"
    "# Default mappings:\r\n"
    "# ButtonDpadUp=KeyUp\r\n"
    "# ButtonDpadDown=KeyDown\r\n"
    "# ButtonDpadLeft=KeyLeft\r\n"
    "# ButtonDpadRight=KeyRight\r\n"
    "# ButtonStart=KeySpace\r\n"
    "# ButtonBack=KeyTab\r\n"
    "# ButtonLeftThumb=KeyLeftCtrl\r\n"
    "# ButtonRightThumb=KeyLeftAlt\r\n"
    "# ButtonLeftShoulder=KeyPageUp\r\n"
    "# ButtonRightShoulder=KeyPageDown\r\n"
    "# ButtonGuide=KeyLeftShift\r\n"
    "# ButtonA=KeyEnter\r\n"
    "# ButtonB=KeyEscape\r\n"
    "# ButtonX=KeyBackspace\r\n"
    "# ButtonY=KeyTab\r\n"
    "#\r\n"
    "# Example: Swap A and B buttons\r\n"
    "# ButtonA=KeyEscape\r\n"
    "# ButtonB=KeyEnter\r\n"
    "\r\n"
    "# ==================\r\n"
    "# Analog Stick Configuration\r\n"
    "# ==================\r\n"
    "# Each stick can be configured independently\r\n"
    "# Mode: Mouse / Keys / Disabled (each stick ONE mode only)\r\n"
    "\r\n"
    "# Left Stick (default: Mouse mode for cursor control)\r\n"
    "LeftStickMode=Mouse\r\n"
    "LeftStickDeadzone=8000           # Dead zone (0-32767, recommended: 8000)\r\n"
    "LeftStickMouseSensitivity=50     # Sensitivity (1-100, default: 50)\r\n"
    "LeftStickMouseMaxSpeed=20        # Max speed (pixels/poll, default: 20)\r\n"
    "LeftStickMouseCurve=2            # 1=Linear, 2=Square(recommended), 3=S-curve\r\n"
    "\r\n"
    "# Keys mode settings (only when LeftStickMode=Keys)\r\n"
    "# LeftStickDirectionMode=4       # 4=4-way, 8=8-way diagonal support\r\n"
    "# LeftStickUpMapping=KeyUp\r\n"
    "# LeftStickDownMapping=KeyDown\r\n"
    "# LeftStickLeftMapping=KeyLeft\r\n"
    "# LeftStickRightMapping=KeyRight\r\n"
    "\r\n"
    "# Right Stick (default: Scroll mode)\r\n"
    "RightStickMode=Scroll\r\n"
    "RightStickScrollSensitivity=30   # 1-100, higher = faster scroll\r\n"
    "# RightStickDeadzone=8689         # Xbox standard for right stick\r\n"
    "\r\n"
    "# Alternative: Use as direction keys\r\n"
    "# RightStickMode=Keys\r\n"
    "# RightStickDirectionMode=4       # 4=4-way, 8=8-way\r\n"
    "# RightStickUpMapping=KeyW\r\n"
    "# RightStickDownMapping=KeyS\r\n"
    "# RightStickLeftMapping=KeyA\r\n"
    "# RightStickRightMapping=KeyD\r\n"
    "\r\n"
    "# Alternative: Disable right stick\r\n"
    "# RightStickMode=Disabled\r\n"
    "\r\n"
    "# Common scenarios:\r\n"
    "# - Complete mouse control (default):\r\n"
    "#     LeftStickMode=Mouse, RightStickMode=Scroll\r\n"
    "#     RightTrigger=MouseLeft, LeftTrigger=MouseRight\r\n"
    "# - BIOS/GRUB navigation:\r\n"
    "#     LeftStickMode=Keys, RightStickMode=Disabled\r\n"
    "# - Dual stick control:\r\n"
    "#     LeftStickMode=Keys (arrows), RightStickMode=Keys (WASD)\r\n"
    "\r\n"
    "# Custom Device Support\r\n"
    "# Add your own Xbox 360 compatible devices here\r\n"
    "# Format: DeviceN=VID:PID:Description\r\n"
    "# Example: Device1=0x1234:0x5678:My Custom Controller\r\n"
    "#\r\n"
    "# [CustomDevices]\r\n"
    "# Device1=\r\n"
    "# Device2=\r\n"
    "\r\n"
    "# End of configuration\r\n";
  
  return Template;
}

/**
  Try to read configuration file from a specific volume.

  @param  FileSystem  File system protocol instance.
  @param  ConfigData  Output pointer to allocated config data.
  @param  ConfigSize  Output size of config data.

  @retval EFI_SUCCESS      Config file read successfully.
  @retval EFI_NOT_FOUND    Config file not found on this volume.
  @retval Other            Error reading file.
**/
STATIC
EFI_STATUS
TryReadConfigFromVolume (
  IN  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem,
  OUT CHAR8                            **ConfigData,
  OUT UINTN                            *ConfigSize
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *ConfigFile;
  EFI_FILE_INFO      *FileInfo;
  UINTN              InfoSize;
  UINTN              BufferSize;
  CHAR8              *Buffer;
  CHAR16             *ConfigPaths[] = {
    L"EFI\\Xbox360\\config.ini",
    L"EFI\\BOOT\\xbox360.ini",
    L"xbox360.ini",
    NULL
  };
  UINTN              PathIndex;

  if ((FileSystem == NULL) || (ConfigData == NULL) || (ConfigSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  // Try multiple possible paths
  for (PathIndex = 0; ConfigPaths[PathIndex] != NULL; PathIndex++) {
    Status = Root->Open(
      Root,
      &ConfigFile,
      ConfigPaths[PathIndex],
      EFI_FILE_MODE_READ,
      0
    );

    if (!EFI_ERROR(Status)) {
      // Found config file, read it
      InfoSize = SIZE_OF_EFI_FILE_INFO + 256;
      FileInfo = AllocatePool(InfoSize);
      if (FileInfo == NULL) {
        ConfigFile->Close(ConfigFile);
        Root->Close(Root);
        return EFI_OUT_OF_RESOURCES;
      }

      Status = ConfigFile->GetInfo(
        ConfigFile,
        &gEfiFileInfoGuid,
        &InfoSize,
        FileInfo
      );

      if (EFI_ERROR(Status)) {
        FreePool(FileInfo);
        ConfigFile->Close(ConfigFile);
        Root->Close(Root);
        return Status;
      }

      BufferSize = (UINTN)FileInfo->FileSize;
      Buffer = AllocateZeroPool(BufferSize + 1);
      if (Buffer == NULL) {
        FreePool(FileInfo);
        ConfigFile->Close(ConfigFile);
        Root->Close(Root);
        return EFI_OUT_OF_RESOURCES;
      }

      Status = ConfigFile->Read(ConfigFile, &BufferSize, Buffer);
      Buffer[BufferSize] = '\0'; // Null terminate

      FreePool(FileInfo);
      ConfigFile->Close(ConfigFile);
      Root->Close(Root);

      if (!EFI_ERROR(Status)) {
        *ConfigData = Buffer;
        *ConfigSize = BufferSize;
        return EFI_SUCCESS;
      } else {
        FreePool(Buffer);
        return Status;
      }
    }
  }

  Root->Close(Root);
  return EFI_NOT_FOUND;
}

/**
  Find and read configuration file from any available volume.

  @param  ConfigData  Output pointer to allocated config data.
  @param  ConfigSize  Output size of config data.

  @retval EFI_SUCCESS      Config file found and read.
  @retval EFI_NOT_FOUND    Config file not found on any volume.
  @retval Other            Error.
**/
STATIC
EFI_STATUS
FindAndReadConfig (
  OUT CHAR8  **ConfigData,
  OUT UINTN  *ConfigSize
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *HandleBuffer;
  UINTN                            HandleCount;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;

  if ((ConfigData == NULL) || (ConfigSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  // Locate all file system handles
  Status = gBS->LocateHandleBuffer(
    ByProtocol,
    &gEfiSimpleFileSystemProtocolGuid,
    NULL,
    &HandleCount,
    &HandleBuffer
  );

  if (EFI_ERROR(Status)) {
    return Status;
  }

  // Try each file system
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol(
      HandleBuffer[Index],
      &gEfiSimpleFileSystemProtocolGuid,
      (VOID**)&FileSystem
    );

    if (!EFI_ERROR(Status)) {
      Status = TryReadConfigFromVolume(FileSystem, ConfigData, ConfigSize);
      if (!EFI_ERROR(Status)) {
        // Found and loaded successfully
        FreePool(HandleBuffer);
        return EFI_SUCCESS;
      }
    }
  }

  FreePool(HandleBuffer);
  return EFI_NOT_FOUND;
}

/**
  Try to write configuration file to a specific volume.

  @param  FileSystem  File system protocol instance.

  @retval EFI_SUCCESS  Config file written successfully.
  @retval Other        Error writing file.
**/
STATIC
EFI_STATUS
TryWriteConfigToVolume (
  IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *Dir;
  EFI_FILE_PROTOCOL  *ConfigFile;
  CHAR8              *ConfigTemplate;
  UINTN              ConfigSize;

  if (FileSystem == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  // Try to open EFI directory (must exist for this to be valid ESP)
  Status = Root->Open(
    Root,
    &Dir,
    L"EFI",
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
    EFI_FILE_DIRECTORY
  );

  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    return Status;
  }
  Dir->Close(Dir);

  // Create Xbox360 directory
  Status = Root->Open(
    Root,
    &Dir,
    L"EFI\\Xbox360",
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
    EFI_FILE_DIRECTORY
  );

  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    return Status;
  }

  // Create config file
  Status = Dir->Open(
    Dir,
    &ConfigFile,
    L"config.ini",
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
    0
  );

  Dir->Close(Dir);

  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    return Status;
  }

  // Write config template
  ConfigTemplate = GenerateConfigTemplate();
  ConfigSize = AsciiStrLen(ConfigTemplate);

  Status = ConfigFile->Write(ConfigFile, &ConfigSize, ConfigTemplate);

  ConfigFile->Close(ConfigFile);
  Root->Close(Root);

  return Status;
}

/**
  Try to write example config file to a specific volume.

  @param  FileSystem  File system protocol instance.

  @retval EFI_SUCCESS  Example file written successfully.
  @retval Other        Error writing file.
**/
STATIC
EFI_STATUS
TryWriteExampleToVolume (
  IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  EFI_FILE_PROTOCOL  *Dir;
  EFI_FILE_PROTOCOL  *ExampleFile;
  CHAR8              *ConfigTemplate;
  UINTN              ConfigSize;

  if (FileSystem == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  // Try to open Xbox360 directory (assume it exists)
  Status = Root->Open(
    Root,
    &Dir,
    L"EFI\\Xbox360",
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
    EFI_FILE_DIRECTORY
  );

  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    return Status;
  }

  // Only create the example once. Rewriting it on every driver start is a
  // needless flash write on a boot-critical volume; delete the file to have
  // it regenerated (e.g. after a driver update).
  Status = Dir->Open(
    Dir,
    &ExampleFile,
    L"config.ini.example",
    EFI_FILE_MODE_READ,
    0
  );

  if (!EFI_ERROR(Status)) {
    ExampleFile->Close(ExampleFile);
    Dir->Close(Dir);
    Root->Close(Root);
    return EFI_SUCCESS;
  }

  // Create example file
  Status = Dir->Open(
    Dir,
    &ExampleFile,
    L"config.ini.example",
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
    0
  );

  Dir->Close(Dir);

  if (EFI_ERROR(Status)) {
    Root->Close(Root);
    return Status;
  }

  // Write config template
  ConfigTemplate = GenerateConfigTemplate();
  ConfigSize = AsciiStrLen(ConfigTemplate);

  Status = ExampleFile->Write(ExampleFile, &ConfigSize, ConfigTemplate);

  ExampleFile->Close(ExampleFile);
  Root->Close(Root);

  return Status;
}

/**
  Generate default configuration file on first run.

  @retval EFI_SUCCESS  Config file created successfully.
  @retval Other        Error creating file (not critical).
**/
STATIC
EFI_STATUS
GenerateDefaultConfigFile (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *HandleBuffer;
  UINTN                            HandleCount;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;

  // Locate all file system handles
  Status = gBS->LocateHandleBuffer(
    ByProtocol,
    &gEfiSimpleFileSystemProtocolGuid,
    NULL,
    &HandleCount,
    &HandleBuffer
  );

  if (EFI_ERROR(Status)) {
    return Status;
  }

  // Try each file system until successful
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol(
      HandleBuffer[Index],
      &gEfiSimpleFileSystemProtocolGuid,
      (VOID**)&FileSystem
    );

    if (!EFI_ERROR(Status)) {
      Status = TryWriteConfigToVolume(FileSystem);
      if (!EFI_ERROR(Status)) {
        // Successfully created config
        FreePool(HandleBuffer);
        return EFI_SUCCESS;
      }
    }
  }

  FreePool(HandleBuffer);
  return EFI_NOT_FOUND;
}

/**
  Generate example configuration file.
  Tries to write config.ini.example to all available ESP partitions.
  Non-critical operation - failure does not affect driver functionality.

  @retval EFI_SUCCESS  Example file created successfully.
  @retval Other        Error creating file (non-critical).
**/
STATIC
EFI_STATUS
GenerateExampleFile (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *HandleBuffer;
  UINTN                            HandleCount;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;

  // Locate all file system handles
  Status = gBS->LocateHandleBuffer(
    ByProtocol,
    &gEfiSimpleFileSystemProtocolGuid,
    NULL,
    &HandleCount,
    &HandleBuffer
  );

  if (EFI_ERROR(Status)) {
    return Status;
  }

  // Try each file system until successful
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol(
      HandleBuffer[Index],
      &gEfiSimpleFileSystemProtocolGuid,
      (VOID**)&FileSystem
    );

    if (!EFI_ERROR(Status)) {
      Status = TryWriteExampleToVolume(FileSystem);
      if (!EFI_ERROR(Status)) {
        // Successfully created example file
        FreePool(HandleBuffer);
        return EFI_SUCCESS;
      }
    }
  }

  FreePool(HandleBuffer);
  // Return success even if we failed - this is non-critical
  return EFI_SUCCESS;
}

/**
  Load configuration with version migration support.

  @param  Config  Pointer to configuration structure to populate.

  @retval EFI_SUCCESS  Configuration loaded (or defaults used).
**/
EFI_STATUS
LoadConfigWithMigration (
  OUT XBOX360_CONFIG  *Config
  )
{
  EFI_STATUS  Status;
  CHAR8       *ConfigData;
  UINTN       ConfigSize;
  UINT16      FileVersion;

  if (Config == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Step 1: Set all defaults
  LOG_INFO ("Loading configuration...");
  SetDefaultConfig(Config);

  // Step 2: Try to read config file
  Status = FindAndReadConfig(&ConfigData, &ConfigSize);
  if (EFI_ERROR(Status)) {
    if (Status == EFI_NOT_FOUND) {
      LOG_INFO ("Config file not found, using defaults and generating template");
      DEBUG((DEBUG_WARN, "Xbox360: Config file not found, generating template...\n"));
      
      Status = GenerateDefaultConfigFile();
      if (!EFI_ERROR(Status)) {
        LOG_INFO ("Config template created at \\EFI\\Xbox360\\config.ini");
        DEBUG((DEBUG_INFO, "Xbox360: Config template created at \\EFI\\Xbox360\\config.ini\n"));
        DEBUG((DEBUG_INFO, "Xbox360: Edit and reboot to customize\n"));
      } else {
        LOG_WARN ("Could not create config file: %r (using defaults)", Status);
        DEBUG((DEBUG_WARN, "Xbox360: Could not create config file (using defaults)\n"));
      }
    } else {
      LOG_WARN ("Failed to read config file: %r (using defaults)", Status);
    }
    
    // Step 2.5: Always try to generate example file
    Status = GenerateExampleFile();
    if (!EFI_ERROR(Status)) {
      DEBUG((DEBUG_INFO, "Xbox360: Example config updated at \\EFI\\Xbox360\\config.ini.example\n"));
    } else {
      DEBUG((DEBUG_WARN, "Xbox360: Could not update example config (non-critical)\n"));
    }
    
    // Use defaults
    LOG_INFO ("Configuration loaded with defaults");
    return EFI_SUCCESS;
  }

  // Step 3: Parse version
  FileVersion = ParseConfigVersion(ConfigData);
  
  LOG_INFO ("Config file found, version: %d.%d", (FileVersion >> 8), (FileVersion & 0xFF));
  DEBUG((DEBUG_INFO, "Xbox360: Config file found, version: %d.%d\n",
    (FileVersion >> 8), (FileVersion & 0xFF)));

  // Step 4: Parse configuration
  ParseIniConfig(ConfigData, Config);

  // Step 5: Validate and sanitize
  ValidateAndSanitizeConfig(Config);

  FreePool(ConfigData);

  // Step 6: Always try to generate example file
  Status = GenerateExampleFile();
  if (!EFI_ERROR(Status)) {
    DEBUG((DEBUG_INFO, "Xbox360: Example config updated at \\EFI\\Xbox360\\config.ini.example\n"));
  } else {
    DEBUG((DEBUG_WARN, "Xbox360: Could not update example config (non-critical)\n"));
  }

  LOG_INFO ("Configuration loaded and validated successfully");
  DEBUG((DEBUG_INFO, "Xbox360: Configuration loaded successfully\n"));
  return EFI_SUCCESS;
}

//
// =============================================================================
// Dynamic Device List Management
// =============================================================================
//

/**
  Get pointer to global configuration.

  @retval Pointer to global configuration structure
**/
XBOX360_CONFIG *
GetGlobalConfig (
  VOID
  )
{
  return &mGlobalConfig;
}
