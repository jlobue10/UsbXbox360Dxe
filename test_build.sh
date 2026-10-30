#!/bin/bash
# Test build script for UsbXbox360Dxe driver
# Based on .github/workflows/build.yml

set -e  # Exit on error
set -u  # Exit on undefined variable

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="build_test"
EDK2_BRANCH="edk2-stable202411"
# ARCH=AARCH64 builds the ARM64 driver (rEFInd drivers_aa64/). On an x86-64
# host that needs a cross compiler: GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
# (Debian/Ubuntu: gcc-aarch64-linux-gnu); on an ARM64 host the native gcc
# is used.
ARCH="${ARCH:-X64}"
TOOLCHAIN="GCC5"
BUILD_TYPE="${BUILD_TYPE:-DEBUG}"  # Use DEBUG for testing, change to RELEASE if needed
if [ "$ARCH" != "X64" ] && [ "$ARCH" != "AARCH64" ]; then
    echo -e "${RED}Error: ARCH must be X64 or AARCH64 (got '$ARCH')${NC}"
    exit 1
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}UsbXbox360Dxe Test Build Script${NC}"
echo -e "${GREEN}========================================${NC}"

# Get script directory (where the driver source is)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${YELLOW}[1/8] Checking dependencies...${NC}"
# Check required tools (nasm is only used by the X64 assembly in BaseLib/IoLib)
DEPS="git make gcc iasl python3"
[ "$ARCH" = "X64" ] && DEPS="$DEPS nasm"
for tool in $DEPS; do
    if ! command -v $tool &> /dev/null; then
        echo -e "${RED}Error: $tool is not installed${NC}"
        echo "Please install: sudo apt install build-essential uuid-dev iasl git nasm python3"
        exit 1
    fi
done
echo -e "${GREEN}✓ All dependencies found${NC}"

echo -e "${YELLOW}[2/8] Preparing build directory...${NC}"
# Create build directory but keep EDK2 if already exists
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Clean up old build artifacts but keep EDK2 source
if [ -d "edk2" ]; then
    echo "EDK2 already exists, cleaning old build artifacts..."
    if [ -d "edk2/Build" ]; then
        rm -rf edk2/Build
        echo "  - Removed old Build directory"
    fi
    if [ -d "edk2/MdeModulePkg/Bus/Usb/UsbXbox360Dxe" ]; then
        rm -rf edk2/MdeModulePkg/Bus/Usb/UsbXbox360Dxe
        echo "  - Removed old driver copy"
    fi
fi
echo -e "${GREEN}✓ Build directory ready: $BUILD_DIR${NC}"

echo -e "${YELLOW}[3/8] Cloning EDK2...${NC}"
if [ ! -d "edk2" ]; then
    git clone --depth 1 --branch "$EDK2_BRANCH" https://github.com/tianocore/edk2.git
    cd edk2
    git submodule update --init --depth 1
    cd ..
else
    echo "EDK2 already exists, skipping clone"
fi
echo -e "${GREEN}✓ EDK2 cloned${NC}"

echo -e "${YELLOW}[4/8] Setting up EDK2 environment...${NC}"
cd edk2
export WORKSPACE=$PWD
export PACKAGES_PATH=$WORKSPACE

# Set PYTHON_COMMAND to avoid unbound variable error
export PYTHON_COMMAND=/usr/bin/python3

# Create python symlink if needed
if [ ! -e /usr/bin/python ]; then
    echo "Creating python symlink..."
    sudo ln -sf /usr/bin/python3 /usr/bin/python
fi

# Build BaseTools if not already built
if [ ! -f "BaseTools/Source/C/bin/GenFw" ]; then
    echo "Building EDK2 BaseTools..."
    make -C BaseTools -j$(nproc)
else
    echo "BaseTools already built, skipping"
fi

# Temporarily disable 'set -u' for edksetup.sh as it uses undefined variables
set +u
source edksetup.sh
set -u

echo -e "${GREEN}✓ EDK2 environment setup complete${NC}"

echo -e "${YELLOW}[5/8] Copying driver source to EDK2...${NC}"
mkdir -p MdeModulePkg/Bus/Usb/UsbXbox360Dxe
cp "$SCRIPT_DIR"/*.c MdeModulePkg/Bus/Usb/UsbXbox360Dxe/ 2>/dev/null || true
cp "$SCRIPT_DIR"/*.h MdeModulePkg/Bus/Usb/UsbXbox360Dxe/ 2>/dev/null || true
cp "$SCRIPT_DIR"/*.inf MdeModulePkg/Bus/Usb/UsbXbox360Dxe/
cp "$SCRIPT_DIR"/*.uni MdeModulePkg/Bus/Usb/UsbXbox360Dxe/ 2>/dev/null || true
echo -e "${GREEN}✓ Driver source copied${NC}"

echo -e "${YELLOW}[6/8] Adding driver to build configuration...${NC}"
# Check if driver is already in MdeModulePkg.dsc
if ! grep -q "UsbXbox360Dxe/UsbXbox360Dxe.inf" MdeModulePkg/MdeModulePkg.dsc; then
    # Add driver to MdeModulePkg.dsc under [Components] section
    sed -i '/\[Components\]/a \  MdeModulePkg/Bus/Usb/UsbXbox360Dxe/UsbXbox360Dxe.inf' MdeModulePkg/MdeModulePkg.dsc
    echo "Driver added to build configuration"
else
    echo "Driver already in build configuration"
fi
echo -e "${GREEN}✓ Build configuration updated${NC}"

echo -e "${YELLOW}[7/8] Building driver (${BUILD_TYPE})...${NC}"
echo "This may take a few minutes..."
set +u
source edksetup.sh
set -u
build -a $ARCH -t $TOOLCHAIN -b $BUILD_TYPE -p MdeModulePkg/MdeModulePkg.dsc -m MdeModulePkg/Bus/Usb/UsbXbox360Dxe/UsbXbox360Dxe.inf

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Build successful!${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi

echo -e "${YELLOW}[8/8] Preparing output...${NC}"
cd "$SCRIPT_DIR"
mkdir -p output
EFI_PATH="$BUILD_DIR/edk2/Build/MdeModule/${BUILD_TYPE}_${TOOLCHAIN}/${ARCH}/UsbXbox360Dxe.efi"
# Release asset naming: the X64 driver keeps its historical name, the ARM64
# build carries the EFI architecture suffix (installed as
# drivers_aa64/UsbXbox360Dxe.efi on the ESP).
if [ "$ARCH" = "X64" ]; then
    OUT_NAME="UsbXbox360Dxe.efi"
else
    OUT_NAME="UsbXbox360Dxe_aa64.efi"
fi

if [ -f "$EFI_PATH" ]; then
    cp "$EFI_PATH" "output/$OUT_NAME"
    cp config.ini.example output/ 2>/dev/null || true
    
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Build completed successfully!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo -e "Output files:"
    echo -e "  ${GREEN}output/$OUT_NAME${NC}"
    echo -e "  ${GREEN}output/config.ini.example${NC}"
    echo -e ""
    echo -e "Build artifacts also available at:"
    echo -e "  ${YELLOW}$EFI_PATH${NC}"
    echo -e ""
    echo -e "File size: $(du -h "output/$OUT_NAME" | cut -f1)"
    echo -e "${GREEN}========================================${NC}"
else
    echo -e "${RED}Error: Built .efi file not found at expected location${NC}"
    echo "Expected: $EFI_PATH"
    exit 1
fi

