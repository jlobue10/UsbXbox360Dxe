/** @file
  Xbox 360 Controller Input Processing Implementation

  This module implements input processing for Xbox 360 controllers, including
  button mapping, analog stick processing (keys/mouse/scroll modes), and
  USB interrupt handling.

  Copyright (c) 2024-2025. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "Xbox360Input.h"
#include "Xbox360Log.h"
#include "Xbox360Config.h"
#include "KeyBoard.h"
#include "EfiKey.h"
#include "AsusAllyDevice.h"
#include "LegionGoDevice.h"

// Number of Xbox 360 buttons (16 bits, 0-15)
//
#define XBOX360_BUTTON_COUNT  16


STATIC
VOID
QueueButtonTransition (
  IN USB_KB_DEV  *UsbKeyboardDevice,
  IN UINT8       KeyCode,
  IN BOOLEAN     IsPressed
  )
{
  USB_KEY  UsbKey;

  UsbKey.KeyCode = KeyCode;
  UsbKey.Down    = IsPressed;
  Enqueue (&UsbKeyboardDevice->UsbKeyQueue, &UsbKey, sizeof (UsbKey));

  if (!IsPressed && (UsbKeyboardDevice->RepeatKey == KeyCode)) {
    UsbKeyboardDevice->RepeatKey = 0;
  }
}

STATIC
VOID
ProcessButtonChanges (
  IN USB_KB_DEV  *UsbKeyboardDevice,
  IN UINT16      OldButtons,
  IN UINT16      NewButtons
  )
{
  UINTN           Index;
  XBOX360_CONFIG  *Config;

  Config = GetGlobalConfig ();

  // Iterate through all 16 button bits (0-15)
  for (Index = 0; Index < XBOX360_BUTTON_COUNT; Index++) {
    UINT16   Mask;
    BOOLEAN  WasPressed;
    BOOLEAN  IsPressed;
    UINT8    KeyMapping;

    // Create button mask for this bit position
    Mask       = (UINT16)(1 << Index);
    WasPressed = ((OldButtons & Mask) != 0);
    IsPressed  = ((NewButtons & Mask) != 0);

    // Skip if button state hasn't changed
    if (WasPressed == IsPressed) {
      continue;
    }

    // Get key mapping from user-configurable button map
    KeyMapping = Config->ButtonMap[Index];
    
    // Check if this is a mouse button function code
    if (KeyMapping == FUNCTION_CODE_MOUSE_LEFT && UsbKeyboardDevice->SimplePointerInstalled) {
      UsbKeyboardDevice->SimplePointerState.LeftButton = IsPressed;
    } else if (KeyMapping == FUNCTION_CODE_MOUSE_RIGHT && UsbKeyboardDevice->SimplePointerInstalled) {
      UsbKeyboardDevice->SimplePointerState.RightButton = IsPressed;
    } else if (KeyMapping == FUNCTION_CODE_MOUSE_MIDDLE && UsbKeyboardDevice->SimplePointerInstalled) {
      // Middle button support (reserved for future)
    } else if (KeyMapping != 0xFF) {
      // Standard keyboard key
      QueueButtonTransition (
        UsbKeyboardDevice,
        KeyMapping,
        IsPressed
        );
    }
  }
}

/**
  Apply response curve to normalized stick input (0.0 to 1.0).
  
  @param  Normalized  Input value (0.0 to 1.0)
  @param  Curve       Curve type: 1=Linear, 2=Square, 3=S-curve
  
  @return Curved output value (0.0 to 1.0)
**/
STATIC
INT32
ApplyResponseCurve (
  IN INT32  Normalized,  // Fixed-point: 0 to 10000 (represents 0.0 to 1.0)
  IN UINT8  Curve
  )
{
  INT32  Result;
  
  if (Normalized <= 0) {
    return 0;
  }
  if (Normalized >= 10000) {
    return 10000;
  }
  
  switch (Curve) {
    case 1:  // Linear
      Result = Normalized;
      break;
      
    case 2:  // Square (default, recommended)
      // Result = Normalized^2
      Result = (Normalized * Normalized) / 10000;
      break;
      
    case 3:  // S-curve (smoothstep: 3t^2 - 2t^3)
      // Smoothstep function for smooth acceleration
      // Formula: t * t * (3 - 2 * t)
      {
        INT32  T2;  // t^2
        INT32  T3;  // t^3
        
        T2 = (Normalized * Normalized) / 10000;
        T3 = (T2 * Normalized) / 10000;
        
        // Result = 3*t^2 - 2*t^3
        Result = (3 * T2 - 2 * T3);
      }
      break;
      
    default:
      Result = Normalized;
      break;
  }
  
  // Clamp to valid range
  if (Result < 0) {
    Result = 0;
  }
  if (Result > 10000) {
    Result = 10000;
  }
  
  return Result;
}

/**
  Calculate mouse movement from analog stick input.
  
  @param  X           Stick X-axis value (-32768 ~ 32767)
  @param  Y           Stick Y-axis value (-32768 ~ 32767)
  @param  Config      Stick configuration
  @param  OutDeltaX   Output: Mouse X delta (pixels)
  @param  OutDeltaY   Output: Mouse Y delta (pixels)
**/
STATIC
VOID
CalculateMouseMovement (
  IN  INT16         X,
  IN  INT16         Y,
  IN  STICK_CONFIG  *Config,
  OUT INT32         *OutDeltaX,
  OUT INT32         *OutDeltaY
  )
{
  INT32  AbsX;
  INT32  AbsY;
  INT32  Magnitude;
  INT32  Normalized;
  INT32  Curved;
  INT32  Speed;
  INT32  DeltaX;
  INT32  DeltaY;
  
  if (Config == NULL || OutDeltaX == NULL || OutDeltaY == NULL) {
    return;
  }
  
  *OutDeltaX = 0;
  *OutDeltaY = 0;
  
  // Calculate magnitude
  AbsX = (X < 0) ? -X : X;
  AbsY = (Y < 0) ? -Y : Y;
  Magnitude = (AbsX > AbsY) ? AbsX : AbsY;
  
  // Check deadzone
  if (Magnitude < Config->Deadzone) {
    return;
  }
  
  // Normalize to 0-10000 (0.0 to 1.0 in fixed-point)
  // Apply saturation
  if (Magnitude > Config->Saturation) {
    Magnitude = Config->Saturation;
  }
  
  // Normalized = (Magnitude - Deadzone) / (Saturation - Deadzone)
  // Prevent division-by-zero if Saturation equals Deadzone
  if (Config->Saturation <= Config->Deadzone) {
    return;  // No movement if configuration is invalid
  }
  
  Normalized = ((Magnitude - Config->Deadzone) * 10000) / 
               (Config->Saturation - Config->Deadzone);
  
  if (Normalized < 0) {
    Normalized = 0;
  }
  if (Normalized > 10000) {
    Normalized = 10000;
  }
  
  // Apply response curve
  Curved = ApplyResponseCurve(Normalized, Config->MouseCurve);
  
  // Calculate speed: Curved * Sensitivity * MaxSpeed
  // Sensitivity: 1-100, MaxSpeed: pixels per poll
  Speed = (Curved * Config->MouseSensitivity * Config->MouseMaxSpeed) / 
          (10000 * 100);
  
  // Ensure minimum movement when curved input is non-zero
  if (Speed < 1 && Curved > 0) {
    Speed = 1;  // Minimum movement
  }
  
  // Calculate directional movement
  if (AbsX > AbsY) {
    // Horizontal primary
    DeltaX = (X > 0) ? Speed : -Speed;
    DeltaY = (Y != 0) ? ((Speed * AbsY) / AbsX) : 0;
    if (Y > 0) {
      // Y positive (stick up) = screen up (negative Y)
      DeltaY = -DeltaY;
    }
  } else {
    // Vertical primary
    // Y positive (stick up) = screen up (negative Y)
    DeltaY = (Y > 0) ? -Speed : Speed;
    DeltaX = (X != 0) ? ((Speed * AbsX) / AbsY) : 0;
    if (X < 0) {
      DeltaX = -DeltaX;
    }
  }
  
  *OutDeltaX = DeltaX;
  *OutDeltaY = DeltaY;
}

/**
  Calculate scroll delta from stick Y-axis input.
  
  @param  Y           Stick Y-axis value (-32768 ~ 32767)
  @param  Config      Stick configuration
  
  @return Scroll delta (negative = down, positive = up)
**/
STATIC
INT32
CalculateScrollDelta (
  IN INT16         Y,
  IN STICK_CONFIG  *Config
  )
{
  INT32  AbsY;
  INT32  Magnitude;
  INT32  Normalized;
  INT32  ScrollDelta;
  
  if (Config == NULL) {
    return 0;
  }
  
  AbsY = (Y < 0) ? -Y : Y;
  
  // Check deadzone
  if (AbsY < Config->Deadzone) {
    return 0;
  }
  
  // Normalize to 0-100
  Magnitude = AbsY;
  if (Magnitude > Config->Saturation) {
    Magnitude = Config->Saturation;
  }
  
  // Prevent division-by-zero if Saturation equals Deadzone
  if (Config->Saturation <= Config->Deadzone) {
    return 0;  // No scroll if configuration is invalid
  }
  
  Normalized = ((Magnitude - Config->Deadzone) * 100) / 
               (Config->Saturation - Config->Deadzone);
  
  // Apply sensitivity (1-100)
  ScrollDelta = (Normalized * Config->ScrollSensitivity) / 100;
  
  // Minimum scroll delta
  if (ScrollDelta < 1) {
    ScrollDelta = 1;
  }
  
  // Maximum scroll delta (prevent excessive scrolling)
  if (ScrollDelta > 10) {
    ScrollDelta = 10;
  }
  
  // Return with direction (Y positive = scroll up = negative delta)
  return (Y > 0) ? -ScrollDelta : ScrollDelta;
}

/**
  Calculate analog stick direction based on X/Y values and configuration.
  
  @param  X       Stick X-axis value (-32768 ~ 32767)
  @param  Y       Stick Y-axis value (-32768 ~ 32767)
  @param  Config  Stick configuration
  
  @return Direction bitmask: BIT0=Up, BIT1=Down, BIT2=Left, BIT3=Right
**/
STATIC
UINT8
CalculateStickDirection (
  IN INT16         X,
  IN INT16         Y,
  IN STICK_CONFIG  *Config
  )
{
  INT32  Magnitude;
  INT32  AbsX;
  INT32  AbsY;
  UINT8  Direction;
  
  if (Config == NULL) {
    return 0;
  }
  
  // Calculate magnitude (approximate: use max of abs values for efficiency)
  AbsX = (X < 0) ? -X : X;
  AbsY = (Y < 0) ? -Y : Y;
  Magnitude = (AbsX > AbsY) ? AbsX : AbsY;
  
  // Check deadzone
  if (Magnitude < Config->Deadzone) {
    return 0;
  }
  
  Direction = 0;
  
  if (Config->DirectionMode == 8) {
    // 8-way mode: Independent check for each direction
    // Threshold: ~38% (sin(22.5°) ≈ 0.38)
    #define THRESHOLD_38 12500  // 32767 * 0.38
    
    if (Y > THRESHOLD_38)   Direction |= STICK_DIR_UP;
    if (Y < -THRESHOLD_38)  Direction |= STICK_DIR_DOWN;
    if (X < -THRESHOLD_38)  Direction |= STICK_DIR_LEFT;
    if (X > THRESHOLD_38)   Direction |= STICK_DIR_RIGHT;
  } else {
    // 4-way mode: Choose primary direction
    if (AbsX > AbsY) {
      // Horizontal primary
      if (X > Config->Deadzone) {
        Direction = STICK_DIR_RIGHT;
      } else if (X < -(INT32)Config->Deadzone) {
        Direction = STICK_DIR_LEFT;
      }
    } else {
      // Vertical primary
      if (Y > Config->Deadzone) {
        Direction = STICK_DIR_UP;
      } else if (Y < -(INT32)Config->Deadzone) {
        Direction = STICK_DIR_DOWN;
      }
    }
  }
  
  return Direction;
}

/**
  Process stick direction change and queue key transitions.
  
  @param  Device   USB keyboard device
  @param  OldDir   Old direction bitmask
  @param  NewDir   New direction bitmask
  @param  Config   Stick configuration
**/
STATIC
VOID
ProcessStickDirectionChange (
  IN USB_KB_DEV    *Device,
  IN UINT8         OldDir,
  IN UINT8         NewDir,
  IN STICK_CONFIG  *Config
  )
{
  UINT8  Changed;
  
  if (Device == NULL || Config == NULL) {
    return;
  }
  
  // Calculate which directions changed
  Changed = OldDir ^ NewDir;
  
  if (Changed == 0) {
    return;
  }
  
  // Handle UP
  if (Changed & STICK_DIR_UP) {
    if (Config->UpMapping != 0xFF) {
      QueueButtonTransition(
        Device,
        Config->UpMapping,
        (NewDir & STICK_DIR_UP) != 0
      );
    }
  }
  
  // Handle DOWN
  if (Changed & STICK_DIR_DOWN) {
    if (Config->DownMapping != 0xFF) {
      QueueButtonTransition(
        Device,
        Config->DownMapping,
        (NewDir & STICK_DIR_DOWN) != 0
      );
    }
  }
  
  // Handle LEFT
  if (Changed & STICK_DIR_LEFT) {
    if (Config->LeftMapping != 0xFF) {
      QueueButtonTransition(
        Device,
        Config->LeftMapping,
        (NewDir & STICK_DIR_LEFT) != 0
      );
    }
  }
  
  // Handle RIGHT
  if (Changed & STICK_DIR_RIGHT) {
    if (Config->RightMapping != 0xFF) {
      QueueButtonTransition(
        Device,
        Config->RightMapping,
        (NewDir & STICK_DIR_RIGHT) != 0
      );
    }
  }
}

/**
  Process analog stick changes for both sticks.
  
  @param  Device      USB keyboard device
  @param  OldLeftX    Old left stick X value
  @param  OldLeftY    Old left stick Y value
  @param  OldRightX   Old right stick X value
  @param  OldRightY   Old right stick Y value
**/
STATIC
VOID
ProcessStickChanges (
  IN USB_KB_DEV  *Device,
  IN INT16       OldLeftX,
  IN INT16       OldLeftY,
  IN INT16       OldRightX,
  IN INT16       OldRightY
  )
{
  UINT8  OldLeftDir, NewLeftDir;
  UINT8  OldRightDir, NewRightDir;
  
  if (Device == NULL) {
    return;
  }
  
  // Process left stick (Keys mode only)
  if (GetGlobalConfig()->LeftStick.Mode == STICK_MODE_KEYS) {
    OldLeftDir = CalculateStickDirection(
      OldLeftX, 
      OldLeftY, 
      &GetGlobalConfig()->LeftStick
    );
    NewLeftDir = CalculateStickDirection(
      Device->XboxState.LeftStickX, 
      Device->XboxState.LeftStickY, 
      &GetGlobalConfig()->LeftStick
    );
    
    if (OldLeftDir != NewLeftDir) {
      ProcessStickDirectionChange(
        Device, 
        OldLeftDir, 
        NewLeftDir, 
        &GetGlobalConfig()->LeftStick
      );
      Device->XboxState.LeftStickDir = NewLeftDir;
    }
  }
  
  // Process right stick (Keys mode only)
  if (GetGlobalConfig()->RightStick.Mode == STICK_MODE_KEYS) {
    OldRightDir = CalculateStickDirection(
      OldRightX, 
      OldRightY, 
      &GetGlobalConfig()->RightStick
    );
    NewRightDir = CalculateStickDirection(
      Device->XboxState.RightStickX, 
      Device->XboxState.RightStickY, 
      &GetGlobalConfig()->RightStick
    );
    
    if (OldRightDir != NewRightDir) {
      ProcessStickDirectionChange(
        Device, 
        OldRightDir, 
        NewRightDir, 
        &GetGlobalConfig()->RightStick
      );
      Device->XboxState.RightStickDir = NewRightDir;
    }
  }
  
  // Process mouse mode for either stick
  if (GetGlobalConfig()->LeftStick.Mode == STICK_MODE_MOUSE || 
      GetGlobalConfig()->RightStick.Mode == STICK_MODE_MOUSE) {
    INT32  DeltaX = 0;
    INT32  DeltaY = 0;
    
    // Calculate movement from active stick (left has priority)
    if (GetGlobalConfig()->LeftStick.Mode == STICK_MODE_MOUSE) {
      CalculateMouseMovement(
        Device->XboxState.LeftStickX,
        Device->XboxState.LeftStickY,
        &GetGlobalConfig()->LeftStick,
        &DeltaX,
        &DeltaY
      );
    } else if (GetGlobalConfig()->RightStick.Mode == STICK_MODE_MOUSE) {
      CalculateMouseMovement(
        Device->XboxState.RightStickX,
        Device->XboxState.RightStickY,
        &GetGlobalConfig()->RightStick,
        &DeltaX,
        &DeltaY
      );
    }
    
    //
    // Accumulate rather than assign: GetState zeroes the deltas after each
    // successful read, and accumulation lets other movement sources (the
    // Legion Go 2 touchpad) merge instead of being overwritten by a
    // stick-at-rest zero on the next report.
    //
    if (Device->SimplePointerInstalled) {
      Device->SimplePointerState.RelativeMovementX += DeltaX;
      Device->SimplePointerState.RelativeMovementY += DeltaY;
    }
  }
  
  // Process scroll mode for either stick
  if (GetGlobalConfig()->LeftStick.Mode == STICK_MODE_SCROLL ||
      GetGlobalConfig()->RightStick.Mode == STICK_MODE_SCROLL) {
    INT32  ScrollDelta = 0;
    
    if (GetGlobalConfig()->LeftStick.Mode == STICK_MODE_SCROLL) {
      ScrollDelta = CalculateScrollDelta(
        Device->XboxState.LeftStickY,
        &GetGlobalConfig()->LeftStick
      );
    } else if (GetGlobalConfig()->RightStick.Mode == STICK_MODE_SCROLL) {
      ScrollDelta = CalculateScrollDelta(
        Device->XboxState.RightStickY,
        &GetGlobalConfig()->RightStick
      );
    }
    
    if (Device->SimplePointerInstalled) {
      Device->SimplePointerState.RelativeMovementZ += ScrollDelta;
    }
  }
  
  //
  // Workaround to maintain consistent polling rate:
  // If in mouse/scroll mode but all deltas are zero, report EFI_NOT_READY
  // will cause system to reduce polling frequency, making movement choppy.
  // Solution: When button is pressed, it naturally maintains polling via HasUpdate.
  // When button is not pressed, we rely on the movement deltas being reported.
  // The key is that CalculateMouseMovement should return non-zero when stick
  // is outside deadzone, which is already handled by "Speed = 1" minimum.
  //
}

//
// Legion Go 2 touchpad-as-mouse tuning. The xinput data stream reports at
// ~40 Hz, so 10 reports ~= 250 ms (the classic tap-to-click window) and the
// synthetic click is held for 4 reports (~100 ms) so the menu's polling is
// guaranteed to observe the press before the release.
//
#define LGO_TAP_MAX_REPORTS   10
#define LGO_TAP_HOLD_FRAMES   4

/**
  Translate a Legion Go 2 absolute touchpad sample into Simple Pointer
  relative movement plus tap-to-click.

  Called once per converted gamepad-state report, AFTER the trigger and
  stick processing, so its pointer-state writes merge with (rather than get
  overwritten by) the stick-mouse path.

  @param  Device  The device instance
  @param  Touch   Touch sample from ConvertLegionGoToXbox360; ignored
                  unless Valid
**/
STATIC
VOID
ProcessLegionGoTouch (
  IN  USB_KB_DEV       *Device,
  IN  LEGION_GO_TOUCH  *Touch
  )
{
  BOOLEAN  Touching;
  INT32    DeltaX;
  INT32    DeltaY;

  if (!Touch->Valid || !Device->SimplePointerInstalled) {
    return;
  }

  //
  // 0/0 means "not touched" (the pad never reports 0 on both axes while a
  // finger rests on it -- same convention InputPlumber relies on).
  //
  Touching = (Touch->X != 0) && (Touch->Y != 0);

  if (Touching) {
    if (Device->LgoTouchWasTouching) {
      //
      // Relative movement between consecutive samples. The pad spans
      // ~0..1000 units; raw deltas give roughly two screen widths per
      // full-pad swipe at rEFInd's default mouse_speed, a comfortable rate.
      //
      DeltaX = (INT32)Touch->X - (INT32)Device->LgoTouchLastX;
      DeltaY = (INT32)Touch->Y - (INT32)Device->LgoTouchLastY;
      Device->SimplePointerState.RelativeMovementX += DeltaX;
      Device->SimplePointerState.RelativeMovementY += DeltaY;
    }
    Device->LgoTouchLastX = Touch->X;
    Device->LgoTouchLastY = Touch->Y;
    Device->LgoTouchReports++;
  } else {
    if (Device->LgoTouchWasTouching &&
        (Device->LgoTouchReports <= LGO_TAP_MAX_REPORTS)) {
      Device->LgoTapFrames = LGO_TAP_HOLD_FRAMES;
    }
    Device->LgoTouchReports = 0;
  }
  Device->LgoTouchWasTouching = Touching;

  //
  // Synthetic tap click: hold LeftButton for a few reports, then release.
  // The trigger path only writes the pointer buttons on trigger-state
  // changes, so forcing the state here doesn't fight it outside the rare
  // case of a tap while a mouse-mapped trigger is held.
  //
  if (Device->LgoTapFrames > 0) {
    Device->LgoTapFrames--;
    Device->SimplePointerState.LeftButton = (Device->LgoTapFrames > 0);
  }
}

/**
  Handler function for Xbox 360 controller asynchronous interrupt transfer.

  The wired Xbox 360 controller sends a fixed length vendor specific report. This handler
  maps the controller state into synthetic USB keyboard scan codes so the device can drive
  the UEFI Simple Text Input (Ex) protocols.

  @param  Data             A pointer to a buffer that is filled with key data which is
                           retrieved via asynchronous interrupt transfer.
  @param  DataLength       Indicates the size of the data buffer.
  @param  Context          Pointing to USB_KB_DEV instance.
  @param  Result           Indicates the result of the asynchronous interrupt transfer.

  @retval EFI_SUCCESS      Asynchronous interrupt transfer is handled successfully.
  @retval EFI_DEVICE_ERROR Hardware error occurs.

**/
EFI_STATUS
EFIAPI
KeyboardHandler (
  IN  VOID    *Data,
  IN  UINTN   DataLength,
  IN  VOID    *Context,
  IN  UINT32  Result
  )
{
  USB_KB_DEV           *UsbKeyboardDevice;
  EFI_USB_IO_PROTOCOL  *UsbIo;
  UINT8                *Report;
  UINT16               OldButtons;
  UINT16               NewButtons;
  UINT32               UsbStatus;
  EFI_STATUS           Status;
  UINT8                Xbox360Report[20];  // Converted report buffer
  LEGION_GO_TOUCH      LgoTouch;           // Touch sample (Legion Go 2 only)

  ASSERT (Context != NULL);

  LgoTouch.Valid = FALSE;

  UsbKeyboardDevice = (USB_KB_DEV *)Context;
  UsbIo             = UsbKeyboardDevice->UsbIo;

  //
  // Analyzes Result and performs corresponding action.
  //
  if (Result != EFI_USB_NOERROR) {
    //
    // Some errors happen during the process
    //
    LOG_WARN ("USB interrupt transfer error: Result=0x%08X", Result);
    
    REPORT_STATUS_CODE_WITH_DEVICE_PATH (
      EFI_ERROR_CODE | EFI_ERROR_MINOR,
      (EFI_PERIPHERAL_KEYBOARD | EFI_P_EC_INPUT_ERROR),
      UsbKeyboardDevice->DevicePath
      );

    //
    // Stop the repeat key generation if any
    //
    UsbKeyboardDevice->RepeatKey = 0;

    gBS->SetTimer (
           UsbKeyboardDevice->RepeatTimer,
           TimerCancel,
           USBKBD_REPEAT_RATE
           );

    if ((Result & EFI_USB_ERR_STALL) == EFI_USB_ERR_STALL) {
      UsbClearEndpointHalt (
        UsbIo,
        UsbKeyboardDevice->IntEndpointDescriptor.EndpointAddress,
        &UsbStatus
        );
    }

    //
    // Delete & Submit this interrupt again
    // Handler of DelayedRecoveryEvent triggered by timer will re-submit the interrupt.
    //
    UsbIo->UsbAsyncInterruptTransfer (
             UsbIo,
             UsbKeyboardDevice->IntEndpointDescriptor.EndpointAddress,
             FALSE,
             0,
             0,
             NULL,
             NULL
             );
    //
    // EFI_USB_INTERRUPT_DELAY is defined in USB standard for error handling.
    //
    gBS->SetTimer (
           UsbKeyboardDevice->DelayedRecoveryEvent,
           TimerRelative,
           EFI_USB_INTERRUPT_DELAY
           );

    return EFI_DEVICE_ERROR;
  }

  if (Data == NULL) {
    LOG_WARN ("KeyboardHandler: Data is NULL");
    return EFI_SUCCESS;
  }
  
  if (DataLength < 4) {
    LOG_WARN ("KeyboardHandler: DataLength too short (%d bytes)", (UINT32)DataLength);
    return EFI_SUCCESS;
  }

  Report = (UINT8 *)Data;

  //
  // Handle different device types
  //
  if (UsbKeyboardDevice->DeviceType == DEVICE_TYPE_ASUS_ALLY) {
    //
    // ASUS ROG Ally uses DirectInput HID reports
    // Convert to Xbox 360 format for unified processing
    //
    Status = ConvertAsusAllyToXbox360 (Data, DataLength, Xbox360Report);
    if (EFI_ERROR (Status)) {
      return EFI_SUCCESS;
    }

    // Use converted report for processing
    Report = Xbox360Report;
    DataLength = sizeof(Xbox360Report);
  } else if (UsbKeyboardDevice->DeviceType == DEVICE_TYPE_LEGION_GO) {
    //
    // Legion Go 2 DInput-family modes: convert the gamepad-state report of
    // either vendor stream (legacy 0x74-first, or the interface-2 xinput
    // data stream -- the only stream that carries the buttons in these
    // modes; see LegionGoDevice.h). Anything else -- config replies, other
    // sibling interfaces -- is ignored; the first such report is logged to
    // aid verifying layouts from field logs.
    //
    Status = ConvertLegionGoToXbox360 (Data, DataLength, Xbox360Report, &LgoTouch);
    if (EFI_ERROR (Status)) {
      if (!UsbKeyboardDevice->NonXInputReportLogged) {
        UsbKeyboardDevice->NonXInputReportLogged = TRUE;
        LOG_INFO (
          "Legion Go: ignoring unrecognized report(s): len=%d bytes [%02X %02X %02X %02X]",
          (UINT32)DataLength,
          Report[0],
          Report[1],
          Report[2],
          Report[3]
          );
      }
      return EFI_SUCCESS;
    }

    Report = Xbox360Report;
    DataLength = sizeof(Xbox360Report);
  } else {
    //
    // Standard Xbox 360 protocol: an input report is message type 0x00 with
    // packet size 0x14 (20 bytes). Anything else must be ignored -- LED and
    // rumble status messages, and above all the HID DirectInput reports sent
    // by table-listed devices that enumerate outside XInput mode (e.g. the
    // Lenovo Legion Go 2 in its DInput/detached/FPS modes). DirectInput
    // reports carry analog axes at these offsets, resting near 0x80; parsed
    // as button and trigger state below they become a constant stream of
    // phantom presses and mouse clicks that instantly "select" whatever the
    // boot menu has highlighted (observed on the Legion Go 2: rEFInd's menu
    // flashes and an OS boots with no user input -- rEFInd_GUI issue #23).
    // Log the first rejected report's shape so a proper converter for the
    // device can be written from field logs.
    //
    if ((Report[0] != 0x00) || (Report[1] != 0x14)) {
      if (!UsbKeyboardDevice->NonXInputReportLogged) {
        UsbKeyboardDevice->NonXInputReportLogged = TRUE;
        LOG_WARN (
          "Ignoring non-XInput report(s): len=%d bytes [%02X %02X %02X %02X %02X %02X %02X %02X]; this device likely needs a DirectInput converter",
          (UINT32)DataLength,
          Report[0],
          Report[1],
          Report[2],
          Report[3],
          (DataLength > 4) ? Report[4] : 0,
          (DataLength > 5) ? Report[5] : 0,
          (DataLength > 6) ? Report[6] : 0,
          (DataLength > 7) ? Report[7] : 0
          );
      }
      return EFI_SUCCESS;
    }
  }

  //
  // Parse button state (bytes 2-3)
  //
  OldButtons = UsbKeyboardDevice->XboxState.Buttons;
  NewButtons = (UINT16)(Report[2] | ((UINT16)Report[3] << 8));
  if (OldButtons != NewButtons) {
    ProcessButtonChanges (UsbKeyboardDevice, OldButtons, NewButtons);
    UsbKeyboardDevice->XboxState.Buttons = NewButtons;
  }

  //
  // Parse trigger state (bytes 4-5)
  //
  if (DataLength >= 6) {
    UINT8    LeftTrigger;
    UINT8    RightTrigger;
    BOOLEAN  LeftTriggerPressed;
    BOOLEAN  RightTriggerPressed;
    BOOLEAN  OldLeftTrigger;
    BOOLEAN  OldRightTrigger;

    LeftTrigger = Report[4];
    RightTrigger = Report[5];

    // Check triggers against threshold
    LeftTriggerPressed = (LeftTrigger > GetGlobalConfig()->TriggerThreshold);
    RightTriggerPressed = (RightTrigger > GetGlobalConfig()->TriggerThreshold);

    // Get previous trigger states
    OldLeftTrigger = UsbKeyboardDevice->XboxState.LeftTriggerActive;
    OldRightTrigger = UsbKeyboardDevice->XboxState.RightTriggerActive;

    // Handle left trigger state change
    if (LeftTriggerPressed != OldLeftTrigger) {
      UINT8 LeftTriggerMapping = GetGlobalConfig()->LeftTriggerKey;
      
      if (LeftTriggerMapping == FUNCTION_CODE_MOUSE_LEFT) {
        if (UsbKeyboardDevice->SimplePointerInstalled) {
          UsbKeyboardDevice->SimplePointerState.LeftButton = LeftTriggerPressed;
        }
      } else if (LeftTriggerMapping == FUNCTION_CODE_MOUSE_RIGHT) {
        if (UsbKeyboardDevice->SimplePointerInstalled) {
          UsbKeyboardDevice->SimplePointerState.RightButton = LeftTriggerPressed;
        }
      } else if (LeftTriggerMapping != 0xFF) {
        // Standard keyboard key
        QueueButtonTransition(
          UsbKeyboardDevice,
          LeftTriggerMapping,
          LeftTriggerPressed
        );
      }
      UsbKeyboardDevice->XboxState.LeftTriggerActive = LeftTriggerPressed;
    }

    // Handle right trigger state change
    if (RightTriggerPressed != OldRightTrigger) {
      UINT8 RightTriggerMapping = GetGlobalConfig()->RightTriggerKey;
      
      if (RightTriggerMapping == FUNCTION_CODE_MOUSE_LEFT) {
        if (UsbKeyboardDevice->SimplePointerInstalled) {
          UsbKeyboardDevice->SimplePointerState.LeftButton = RightTriggerPressed;
        }
      } else if (RightTriggerMapping == FUNCTION_CODE_MOUSE_RIGHT) {
        if (UsbKeyboardDevice->SimplePointerInstalled) {
          UsbKeyboardDevice->SimplePointerState.RightButton = RightTriggerPressed;
        }
      } else if (RightTriggerMapping != 0xFF) {
        // Standard keyboard key
        QueueButtonTransition(
          UsbKeyboardDevice,
          RightTriggerMapping,
          RightTriggerPressed
        );
      }
      UsbKeyboardDevice->XboxState.RightTriggerActive = RightTriggerPressed;
    }
  }

  //
  // Parse analog stick state (bytes 6-13)
  //
  if (DataLength >= 14) {
    INT16  OldLeftX, OldLeftY, OldRightX, OldRightY;
    
    // Save old values
    OldLeftX = UsbKeyboardDevice->XboxState.LeftStickX;
    OldLeftY = UsbKeyboardDevice->XboxState.LeftStickY;
    OldRightX = UsbKeyboardDevice->XboxState.RightStickX;
    OldRightY = UsbKeyboardDevice->XboxState.RightStickY;
    
    // Read new values (little-endian, signed 16-bit)
    UsbKeyboardDevice->XboxState.LeftStickX = 
      (INT16)(Report[6] | ((UINT16)Report[7] << 8));
    UsbKeyboardDevice->XboxState.LeftStickY = 
      (INT16)(Report[8] | ((UINT16)Report[9] << 8));
    UsbKeyboardDevice->XboxState.RightStickX = 
      (INT16)(Report[10] | ((UINT16)Report[11] << 8));
    UsbKeyboardDevice->XboxState.RightStickY = 
      (INT16)(Report[12] | ((UINT16)Report[13] << 8));
    
    // Process stick changes (direction keys mode)
    ProcessStickChanges(
      UsbKeyboardDevice,
      OldLeftX, OldLeftY, OldRightX, OldRightY
    );
  }

  //
  // Legion Go 2: touchpad-as-mouse (after stick processing, so both
  // pointer-movement sources accumulate instead of overwriting each other)
  //
  ProcessLegionGoTouch (UsbKeyboardDevice, &LgoTouch);

  UsbKeyboardDevice->RepeatKey = 0;
  if (UsbKeyboardDevice->RepeatTimer != NULL) {
    gBS->SetTimer (
           UsbKeyboardDevice->RepeatTimer,
           TimerCancel,
           USBKBD_REPEAT_RATE
           );
  }

  return EFI_SUCCESS;
}
