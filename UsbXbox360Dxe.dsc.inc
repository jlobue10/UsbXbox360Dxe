## @file
#  [Components] entry for UsbXbox360Dxe, !include'd into MdeModulePkg.dsc by
#  .github/workflows/build.yml and test_build.sh.
#
#  MdeModulePkg.dsc gives UEFI drivers UefiDebugLibConOut with ASSERTs and
#  DEBUG_ERROR prints enabled for every target, so the EDK2 libraries linked
#  into the driver (BaseLib, PrintLib, UefiUsbLib, ...) could still write
#  ASSERT / error text over the boot manager's screen from a RELEASE binary,
#  even though the driver's own DEBUG() and ESP logging are compiled out there
#  (MDEPKG_NDEBUG, see UsbXbox360Dxe.inf [BuildOptions]). RELEASE builds link
#  the null DebugLib instead so the shipped driver is completely silent; DEBUG
#  builds (UsbXbox360Dxe-debug.efi) keep the console DebugLib.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

  MdeModulePkg/Bus/Usb/UsbXbox360Dxe/UsbXbox360Dxe.inf {
    <LibraryClasses>
!if $(TARGET) == RELEASE
      DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
!endif
  }
