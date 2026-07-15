/** @file
  Xbox 360 Driver Logging System Implementation

  This module implements logging functionality with daily log rotation,
  automatic cleanup of old log files, and ESP partition detection.

  Copyright (c) 2024-2025. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "Xbox360Log.h"

#if XBOX360_LOG_ENABLED

//
// Module-level global variables
//
STATIC UINT32     mLogSequence = 0;
STATIC BOOLEAN    mLogInitialized = FALSE;
STATIC CHAR16     mCurrentLogFileName[64];       // Cache current log file name
STATIC EFI_HANDLE mDriverImageHandle = NULL;    // Driver's own image handle for locating ESP

/**
  Get current time as EFI_TIME structure.

  @param  Time    Pointer to EFI_TIME structure to fill

  @retval EFI_SUCCESS      Time retrieved successfully
  @retval EFI_DEVICE_ERROR Time service unavailable
**/
STATIC
EFI_STATUS
GetCurrentTime (
  OUT EFI_TIME  *Time
  )
{
  EFI_STATUS  Status;

  if (gRT == NULL || gRT->GetTime == NULL) {
    // Fallback: use zero time if runtime services unavailable
    ZeroMem (Time, sizeof(EFI_TIME));
    Time->Year = 2025;
    Time->Month = 1;
    Time->Day = 1;
    return EFI_DEVICE_ERROR;
  }

  Status = gRT->GetTime (Time, NULL);
  return Status;
}

/**
  Format EFI_TIME to string: "YYYY-MM-DD HH:MM:SS"

  @param  Time        EFI_TIME structure
  @param  Buffer      Output buffer (at least 20 chars)
  @param  BufferSize  Size of output buffer

  @retval None
**/
STATIC
VOID
FormatTimeString (
  IN  EFI_TIME  *Time,
  OUT CHAR8     *Buffer,
  IN  UINTN     BufferSize
  )
{
  AsciiSPrint (
    Buffer,
    BufferSize,
    "%04d-%02d-%02d %02d:%02d:%02d",
    Time->Year,
    Time->Month,
    Time->Day,
    Time->Hour,
    Time->Minute,
    Time->Second
  );
}

/**
  Get current log file name based on date: "driver_YYYYMMDD.log"

  @param  FileName    Output buffer for file name (at least 32 chars)

  @retval None
**/
STATIC
VOID
GetCurrentLogFileName (
  OUT CHAR16  *FileName
  )
{
  EFI_TIME  Time;
  
  GetCurrentTime (&Time);
  
  UnicodeSPrint (
    FileName,
    64 * sizeof(CHAR16),
    L"driver_%04d%02d%02d.log",
    Time.Year,
    Time.Month,
    Time.Day
  );
}

/**
  Parse date from log file name: "driver_YYYYMMDD.log"

  @param  FileName    File name to parse
  @param  Year        Output year
  @param  Month       Output month
  @param  Day         Output day

  @retval TRUE   Successfully parsed
  @retval FALSE  Invalid file name format
**/
STATIC
BOOLEAN
ParseLogFileDate (
  IN  CHAR16  *FileName,
  OUT UINT16  *Year,
  OUT UINT8   *Month,
  OUT UINT8   *Day
  )
{
  CHAR16  *Ptr;
  CHAR16  YearStr[5], MonthStr[3], DayStr[3];

  // Check prefix: "driver_"
  if (StrnCmp (FileName, L"driver_", 7) != 0) {
    return FALSE;
  }

  Ptr = FileName + 7;  // Skip "driver_"

  // Extract YYYYMMDD (8 digits)
  if (StrLen (Ptr) < 8) {
    return FALSE;
  }

  // Parse year (YYYY)
  StrnCpyS (YearStr, 5, Ptr, 4);
  YearStr[4] = L'\0';
  *Year = (UINT16)StrDecimalToUintn (YearStr);

  // Parse month (MM)
  StrnCpyS (MonthStr, 3, Ptr + 4, 2);
  MonthStr[2] = L'\0';
  *Month = (UINT8)StrDecimalToUintn (MonthStr);

  // Parse day (DD)
  StrnCpyS (DayStr, 3, Ptr + 6, 2);
  DayStr[2] = L'\0';
  *Day = (UINT8)StrDecimalToUintn (DayStr);

  // Validate ranges
  if (*Year < 2020 || *Year > 2099 || *Month < 1 || *Month > 12 || *Day < 1 || *Day > 31) {
    return FALSE;
  }

  return TRUE;
}

/**
  Compare two log file dates.

  @param  File1   First file name
  @param  File2   Second file name

  @retval < 0     File1 is older than File2
  @retval   0     Same date
  @retval > 0     File1 is newer than File2
**/
STATIC
INTN
CompareLogFileDates (
  IN CHAR16  *File1,
  IN CHAR16  *File2
  )
{
  UINT16  Year1, Year2;
  UINT8   Month1, Month2, Day1, Day2;
  INTN    Result;

  if (!ParseLogFileDate (File1, &Year1, &Month1, &Day1)) {
    return -1;  // Invalid file, sort to front for deletion
  }

  if (!ParseLogFileDate (File2, &Year2, &Month2, &Day2)) {
    return 1;
  }

  // Compare year, then month, then day
  Result = Year1 - Year2;
  if (Result != 0) {
    return Result;
  }

  Result = Month1 - Month2;
  if (Result != 0) {
    return Result;
  }

  return Day1 - Day2;
}

/**
  Clean up old log files, keeping only the most recent N files.

  @param  Root    Root directory of ESP partition

  @retval None
**/
STATIC
VOID
CleanupOldLogFiles (
  IN EFI_FILE_PROTOCOL  *Root
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *XboxDir;
  EFI_FILE_INFO      *FileInfo;
  UINTN              BufferSize;
  CHAR16             *LogFiles[32];  // Max 32 log files to track
  UINTN              LogFileCount;
  UINTN              Index, DeleteCount;
  EFI_FILE_PROTOCOL  *FileToDelete;

  // Open Xbox360 directory
  Status = Root->Open (
                   Root,
                   &XboxDir,
                   L"\\EFI\\Xbox360",
                   EFI_FILE_MODE_READ,
                   0
                   );
  if (EFI_ERROR (Status)) {
    return;
  }

  // Collect all log file names
  LogFileCount = 0;
  BufferSize = SIZE_OF_EFI_FILE_INFO + 256;
  FileInfo = AllocateZeroPool (BufferSize);
  
  if (FileInfo == NULL) {
    XboxDir->Close (XboxDir);
    return;
  }

  // Enumerate directory
  XboxDir->SetPosition (XboxDir, 0);
  
  while (LogFileCount < 32) {
    BufferSize = SIZE_OF_EFI_FILE_INFO + 256;
    Status = XboxDir->Read (XboxDir, &BufferSize, FileInfo);
    
    if (EFI_ERROR (Status) || BufferSize == 0) {
      break;  // End of directory
    }

    // Check if it's a log file (driver_*.log)
    if ((FileInfo->Attribute & EFI_FILE_DIRECTORY) == 0 &&
        StrnCmp (FileInfo->FileName, L"driver_", 7) == 0 &&
        StrStr (FileInfo->FileName, L".log") != NULL)
    {
      LogFiles[LogFileCount] = AllocateCopyPool (
                                 StrSize (FileInfo->FileName),
                                 FileInfo->FileName
                                 );
      if (LogFiles[LogFileCount] != NULL) {
        LogFileCount++;
      }
    }
  }

  FreePool (FileInfo);

  // If we have more than MAX_FILES, delete oldest ones
  if (LogFileCount > XBOX360_LOG_MAX_FILES) {
    // Sort files by date (bubble sort, good enough for small list)
    for (Index = 0; Index < LogFileCount - 1; Index++) {
      UINTN j;
      for (j = 0; j < LogFileCount - Index - 1; j++) {
        if (CompareLogFileDates (LogFiles[j], LogFiles[j + 1]) > 0) {
          // Swap
          CHAR16 *Temp = LogFiles[j];
          LogFiles[j] = LogFiles[j + 1];
          LogFiles[j + 1] = Temp;
        }
      }
    }

    // Delete oldest files (keep only last MAX_FILES)
    DeleteCount = LogFileCount - XBOX360_LOG_MAX_FILES;
    for (Index = 0; Index < DeleteCount; Index++) {
      CHAR16 FilePath[128];
      UnicodeSPrint (FilePath, sizeof(FilePath), L"\\EFI\\Xbox360\\%S", LogFiles[Index]);
      
      Status = Root->Open (
                       Root,
                       &FileToDelete,
                       FilePath,
                       EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                       0
                       );
      if (!EFI_ERROR (Status)) {
        FileToDelete->Delete (FileToDelete);
      }
    }
  }

  // Free allocated file names
  for (Index = 0; Index < LogFileCount; Index++) {
    if (LogFiles[Index] != NULL) {
      FreePool (LogFiles[Index]);
    }
  }

  XboxDir->Close (XboxDir);
}

/**
  Check if current log file needs rotation (exceeds size limit).
  If yes, cleanup old files.

  @param  Root        Root directory of ESP partition
  @param  CurrentLog  Current log file name

  @retval None
**/
STATIC
VOID
CheckLogRotation (
  IN EFI_FILE_PROTOCOL  *Root,
  IN CHAR16             *CurrentLog
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *LogFile;
  EFI_FILE_INFO      *FileInfo;
  UINTN              FileInfoSize;
  UINT64             FileSize;
  CHAR16             FilePath[128];

  UnicodeSPrint (FilePath, sizeof(FilePath), L"\\EFI\\Xbox360\\%S", CurrentLog);

  // Try to open existing log file
  Status = Root->Open (
                   Root,
                   &LogFile,
                   FilePath,
                   EFI_FILE_MODE_READ,
                   0
                   );
  if (EFI_ERROR (Status)) {
    return;  // File doesn't exist yet, no rotation needed
  }

  // Get file size
  FileInfoSize = SIZE_OF_EFI_FILE_INFO + 256;
  FileInfo = AllocateZeroPool (FileInfoSize);
  if (FileInfo == NULL) {
    LogFile->Close (LogFile);
    return;
  }

  Status = LogFile->GetInfo (LogFile, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
  if (EFI_ERROR (Status)) {
    FreePool (FileInfo);
    LogFile->Close (LogFile);
    return;
  }

  FileSize = FileInfo->FileSize;
  FreePool (FileInfo);
  LogFile->Close (LogFile);

  // Check if rotation needed
  if (FileSize < XBOX360_LOG_MAX_SIZE) {
    return;  // Still within size limit
  }

  // Cleanup old files if size limit exceeded
  CleanupOldLogFiles (Root);
}

/**
  Write one formatted log entry to the daily log file under \EFI\Xbox360
  on the given volume.

  @param  Root         Opened root directory of the volume (not closed here).
  @param  LogFileName  Daily log file name.
  @param  LogBuffer    Formatted log entry (ASCII, NUL-terminated).
  @param  TimeStr      Timestamp string used for the session separator.

  @retval TRUE   Entry written to this volume.
  @retval FALSE  This volume did not accept the log entry.
**/
STATIC
BOOLEAN
WriteLogEntryToRoot (
  IN EFI_FILE_PROTOCOL  *Root,
  IN CHAR16             *LogFileName,
  IN CHAR8              *LogBuffer,
  IN CHAR8              *TimeStr
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *XboxDir;
  EFI_FILE_PROTOCOL  *LogFile;
  EFI_FILE_INFO      *FileInfo;
  UINTN              FileInfoSize;
  UINTN              BufferSize;
  CHAR16             LogFilePath[128];

  // Ensure Xbox360 directory exists
  Status = Root->Open (
                   Root,
                   &XboxDir,
                   L"\\EFI\\Xbox360",
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                   EFI_FILE_DIRECTORY
                   );
  if (!EFI_ERROR (Status)) {
    XboxDir->Close (XboxDir);
  }

  // Check if log rotation needed (only check once per session)
  if (!mLogInitialized) {
    CheckLogRotation (Root, LogFileName);
    CopyMem (mCurrentLogFileName, LogFileName, sizeof (mCurrentLogFileName));
  }

  // Open/create log file
  UnicodeSPrint (LogFilePath, sizeof (LogFilePath), L"\\EFI\\Xbox360\\%S", LogFileName);

  Status = Root->Open (
                   Root,
                   &LogFile,
                   LogFilePath,
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                   0
                   );
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  // Seek to end for append
  FileInfoSize = SIZE_OF_EFI_FILE_INFO + 256;
  FileInfo = AllocateZeroPool (FileInfoSize);
  if (FileInfo != NULL) {
    Status = LogFile->GetInfo (LogFile, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
    if (!EFI_ERROR (Status)) {
      LogFile->SetPosition (LogFile, FileInfo->FileSize);
    }
    FreePool (FileInfo);
  }

  // On first log of this session, add separator
  if (!mLogInitialized) {
    CHAR8  Separator[128];
    UINTN  SepSize;

    AsciiSPrint (
      Separator,
      sizeof (Separator),
      "\n========== Driver Loaded: %a ==========\n",
      TimeStr
      );
    SepSize = AsciiStrLen (Separator);
    LogFile->Write (LogFile, &SepSize, Separator);
    mLogInitialized = TRUE;
  }

  // Write log entry
  BufferSize = AsciiStrLen (LogBuffer);
  LogFile->Write (LogFile, &BufferSize, LogBuffer);
  LogFile->Flush (LogFile);
  LogFile->Close (LogFile);

  return TRUE;
}

/**
  Write a formatted log entry to the daily log file.

  @param  Level    Log level (INFO/WARN/ERROR)
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
  )
{
  EFI_STATUS                       Status;
  UINTN                            HandleCount;
  EFI_HANDLE                       *HandleBuffer;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  CHAR8                            LogBuffer[512];
  CHAR8                            MessageBuffer[384];
  CHAR8                            TimeStr[20];
  VA_LIST                          Args;
  CONST CHAR8                      *LevelStr;
  EFI_TIME                         Time;
  CHAR16                           LogFileName[64];
  BOOLEAN                          Written;

  //
  // The FAT driver acquires its locks at TPL_CALLBACK. Logging from a higher
  // TPL (e.g. the USB async interrupt callback at TPL_NOTIFY) would make the
  // FAT driver raise to a *lower* TPL, which is illegal and wedges the
  // system, so drop the message instead.
  //
  if (EfiGetCurrentTpl () > TPL_CALLBACK) {
    return;
  }

  // Increment sequence
  mLogSequence++;

  // Get current time
  GetCurrentTime (&Time);
  FormatTimeString (&Time, TimeStr, sizeof(TimeStr));

  // Format message
  VA_START (Args, Format);
  AsciiVSPrint (MessageBuffer, sizeof(MessageBuffer), Format, Args);
  VA_END (Args);

  // Determine level string
  switch (Level) {
    case LOG_LEVEL_INFO:
      LevelStr = "INFO ";
      break;
    case LOG_LEVEL_WARN:
      LevelStr = "WARN ";
      break;
    case LOG_LEVEL_ERROR:
      LevelStr = "ERROR";
      break;
    default:
      LevelStr = "???? ";
      break;
  }

  // Format complete log entry: [timestamp] [SEQ] LEVEL: message\n
  AsciiSPrint (
    LogBuffer,
    sizeof(LogBuffer),
    "[%a] [%04d] %a: %a\n",
    TimeStr,
    mLogSequence,
    LevelStr,
    MessageBuffer
  );

  // Get current log file name (based on today's date)
  GetCurrentLogFileName (LogFileName);

  // Try to use driver's loaded image to find the correct ESP partition
  if (mDriverImageHandle != NULL) {
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;

    Status = gBS->HandleProtocol (
                    mDriverImageHandle,
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **)&LoadedImage
                    );

    if (!EFI_ERROR (Status) && LoadedImage->DeviceHandle != NULL) {
      // Try to get file system from the device where driver was loaded
      Status = gBS->HandleProtocol (
                      LoadedImage->DeviceHandle,
                      &gEfiSimpleFileSystemProtocolGuid,
                      (VOID **)&Fs
                      );

      if (!EFI_ERROR (Status)) {
        Status = Fs->OpenVolume (Fs, &Root);

        if (!EFI_ERROR (Status)) {
          // This is the partition where the driver was loaded from
          Written = WriteLogEntryToRoot (Root, LogFileName, LogBuffer, TimeStr);
          Root->Close (Root);
          if (Written) {
            return;  // Success
          }
        }
      }
    }
  }

  // Fallback: Try to find ESP partition by enumerating all file systems
  HandleBuffer = NULL;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID **)&Fs
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = Fs->OpenVolume (Fs, &Root);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Written = WriteLogEntryToRoot (Root, LogFileName, LogBuffer, TimeStr);
    Root->Close (Root);
    if (Written) {
      break;  // Success
    }
  }

  gBS->FreePool (HandleBuffer);
}

/**
  Manual cleanup function (can be called on driver unload).

  @retval None
**/
VOID
EFIAPI
Xbox360LogCleanup (
  VOID
  )
{
  EFI_STATUS                       Status;
  UINTN                            HandleCount;
  EFI_HANDLE                       *HandleBuffer;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID **)&Fs
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = Fs->OpenVolume (Fs, &Root);
    if (EFI_ERROR (Status)) {
      continue;
    }

    CleanupOldLogFiles (Root);
    Root->Close (Root);
    break;  // Only need to clean once
  }

  gBS->FreePool (HandleBuffer);
}

/**
  Set the driver's image handle for logging system.
  This allows the logging system to locate the correct ESP partition.

  @param  ImageHandle  Driver's image handle

  @retval None
**/
VOID
EFIAPI
Xbox360LogSetImageHandle (
  IN EFI_HANDLE  ImageHandle
  )
{
  mDriverImageHandle = ImageHandle;
}

#else // !XBOX360_LOG_ENABLED

//
// File logging is compiled out (RELEASE builds): writing to the ESP from a
// boot-critical driver is a flash-wear and FAT-corruption hazard, and must
// never happen from event callbacks. Keep the public entry points as no-op
// stubs so callers need no conditional compilation.
//

VOID
EFIAPI
Xbox360Log (
  IN UINT8        Level,
  IN CONST CHAR8  *Format,
  ...
  )
{
}

VOID
EFIAPI
Xbox360LogCleanup (
  VOID
  )
{
}

VOID
EFIAPI
Xbox360LogSetImageHandle (
  IN EFI_HANDLE  ImageHandle
  )
{
}

#endif // XBOX360_LOG_ENABLED

