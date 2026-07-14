#!/usr/bin/env bash
# ============================================================================
# build_milkv_duos.sh
#
# Build OpenVela for Milk-V Duos (SG2000) using the contest2026_059_0error
# board/chip configuration.
#
# Usage:
#   chmod +x build_milkv_duos.sh
#   ./build_milkv_duos.sh
#
# Output:
#   nuttx.img  - bootable kernel image
# ============================================================================

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
BOARD_CONFIG="contest2026_059_0error/boards/sg2000/milkv_duos/configs/nsh"
TOOLCHAIN_BIN="/home/wsb/openvela/prebuilts/sg2000_toolchain/host-tools/gcc/riscv64-elf-x86_64/bin"
JOBS=${JOBS:-$(nproc)}
LOAD_ADDR="0x80200000"

ROOTDIR="/home/wsb/openvela_0error"
cd "${ROOTDIR}"

echo "============================================================"
echo "  OpenVela Milk-V Duos (SG2000) Build Script"
echo "============================================================"
echo "  Config:    ${BOARD_CONFIG}"
echo "  Toolchain: sg2000 (riscv64-unknown-elf)"
echo "  Load addr: ${LOAD_ADDR}"
echo "  Jobs:      ${JOBS}"
echo "============================================================"

# ---------------------------------------------------------------------------
# Step 0: Toolchain environment setup
# ---------------------------------------------------------------------------
echo ""
echo "[0/6] Setting up toolchain environment..."

# Prepend sg2000 toolchain to PATH
export PATH="${TOOLCHAIN_BIN}:${PATH}"

# Force use of sg2000 toolchain prefix
export CROSSDEV=riscv64-unknown-elf-

# Keep ccache writes inside the workspace
export CCACHE_DIR="${ROOTDIR}/.ccache"
mkdir -p "${CCACHE_DIR}"

# Source Vela environment (temporarily disable -e, envsetup.sh may fail in
# some environments during source)
set +e
source build/envsetup.sh
set -e

# Remove default riscv-none-elf from PATH to prevent conflict
export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v 'riscv-none-elf' | tr '\n' ':')

# Verify toolchain
if ! command -v riscv64-unknown-elf-gcc &>/dev/null; then
    echo "ERROR: riscv64-unknown-elf-gcc not found in PATH"
    echo "Make sure the sg2000 toolchain exists at:"
    echo "  ${TOOLCHAIN_BIN}"
    exit 1
fi

echo "  Using compiler: $(riscv64-unknown-elf-gcc --version | head -1)"
echo "  CROSSDEV=${CROSSDEV}"

# ---------------------------------------------------------------------------
# Ensure vendor/sg2000 symlink exists so that defconfig paths resolve.
# The defconfig uses CONFIG_ARCH_BOARD_CUSTOM_DIR="../vendor/sg2000/boards/..."
# (relative to nuttx/).  The actual files live under contest2026_059_0error/.
# ---------------------------------------------------------------------------
if [ ! -e "${ROOTDIR}/vendor/sg2000" ]; then
    echo "  Creating vendor/sg2000 -> contest2026_059_0error symlink..."
    ln -sf ../contest2026_059_0error "${ROOTDIR}/vendor/sg2000"
fi

# ---------------------------------------------------------------------------
# Step 1: Build kernel
# ---------------------------------------------------------------------------
echo ""
echo "[1/6] Building kernel..."

# Clean build artifacts from previous run.
echo "  Cleaning previous build..."
rm -f nuttx/.config nuttx/.config.orig nuttx/Make.defs
rm -f nuttx/nuttx nuttx/nuttx.bin nuttx/nuttx.img

# ---------------------------------------------------------------------------
# Pre-create Make.defs as a real file (not symlink) so that configure.sh
# and the export step can both use it correctly.
# ---------------------------------------------------------------------------
cp "contest2026_059_0error/boards/sg2000/milkv_duos/scripts/Make.defs" nuttx/Make.defs

# ---------------------------------------------------------------------------
# Run configure.sh to set up symlinks and Kconfig files.
# ---------------------------------------------------------------------------
echo "  Running configure.sh..."
rm -f nuttx/.config nuttx/.config.orig

set +e
./nuttx/tools/configure.sh "${BOARD_CONFIG}" 2>/dev/null
set -e

# Fix broken Make.defs symlink created by configure.sh
rm -f nuttx/Make.defs
cp "contest2026_059_0error/boards/sg2000/milkv_duos/scripts/Make.defs" nuttx/Make.defs

# Re-seed .config from defconfig to ensure vendor-specific values are intact
cp "contest2026_059_0error/boards/sg2000/milkv_duos/configs/nsh/defconfig" nuttx/.config

# Run olddefconfig to fill in all Kconfig implied values
echo "  Running olddefconfig to resolve Kconfig dependencies..."
make -C nuttx olddefconfig 2>&1

# Remove stale symlinks from a previous failed build
echo "  Cleaning stale symlinks..."
make -C nuttx clean_dirlinks 2>/dev/null || true

# ---------------------------------------------------------------------------
# Build the kernel
# ---------------------------------------------------------------------------
echo "  Building kernel..."
make -C nuttx EXTRAFLAGS="-Wno-cpp -Wno-deprecated-declarations" -j${JOBS}

echo "  Kernel built successfully."

# ---------------------------------------------------------------------------
# Step 2: Create export package
# ---------------------------------------------------------------------------
echo ""
echo "[2/6] Creating export package..."

make -C nuttx export

EXPORT_TARBALL=$(ls -t nuttx/nuttx-export-*.tar.gz 2>/dev/null | head -1)
if [ -z "${EXPORT_TARBALL}" ]; then
    echo "ERROR: Export tarball not found"
    exit 1
fi
echo "  Export: ${EXPORT_TARBALL}"

# ---------------------------------------------------------------------------
# Step 3: Import into apps
# ---------------------------------------------------------------------------
echo ""
echo "[3/6] Importing into apps..."

cd apps
./tools/mkimport.sh -z -x ../${EXPORT_TARBALL}
cd ..

echo "  Import done."

# ---------------------------------------------------------------------------
# Step 4: Build apps
# ---------------------------------------------------------------------------
echo ""
echo "[4/6] Building apps..."

make -C apps import TOPDIR="${ROOTDIR}/apps/import" -j${JOBS}

echo "  Apps built successfully."

# ---------------------------------------------------------------------------
# Step 5: Package apps into romfs
# ---------------------------------------------------------------------------
echo ""
echo "[5/6] Packaging apps into romfs..."

if [ ! -d apps/bin ]; then
    echo "WARNING: apps/bin directory not found, creating empty initrd"
    mkdir -p apps/bin
    touch apps/bin/dummy
fi

genromfs -f initrd.img -d apps/bin -V "NuttXBootVol"

echo "  initrd.img created ($(du -h initrd.img | cut -f1))"

# ---------------------------------------------------------------------------
# Step 6: Create bootable image
# ---------------------------------------------------------------------------
echo ""
echo "[6/6] Creating bootable image..."

KERNEL_ELF="nuttx/nuttx"
KERNEL_BIN="nuttx/nuttx.bin"
if [ ! -f "${KERNEL_ELF}" ]; then
    echo "ERROR: ${KERNEL_ELF} not found"
    exit 1
fi

# ---------------------------------------------------------------------------
# Compute initrd padding.
# ---------------------------------------------------------------------------
EDATA=$(riscv64-unknown-elf-nm "${KERNEL_ELF}" 2>/dev/null | grep " _edata$" | awk '{print $1}')
EBSS=$(riscv64-unknown-elf-nm "${KERNEL_ELF}" 2>/dev/null | grep " _ebss$" | awk '{print $1}')

IDLETHREAD_STACKSIZE=$(grep -oP 'CONFIG_IDLETHREAD_STACKSIZE=\K\d+' nuttx/.config 2>/dev/null || echo 3072)
SMP_NCPUS=$(grep -oP 'CONFIG_SMP_NCPUS=\K\d+' nuttx/.config 2>/dev/null || echo 1)
SMP_SHADOW_STACK=$(grep -oP 'CONFIG_ARCH_RV_SHADOW_STACK=\K[^ ]+' nuttx/.config 2>/dev/null || true)
if [ "${SMP_SHADOW_STACK}" = "y" ]; then
    SHADOW_SIZE=$((IDLETHREAD_STACKSIZE / 2))
else
    SHADOW_SIZE=0
fi
SMP_STACK_SIZE=$(( (IDLETHREAD_STACKSIZE + SHADOW_SIZE + 15) & ~15 ))
SMP_STACK_TOTAL=$(( SMP_STACK_SIZE * SMP_NCPUS ))

LOAD_ADDR_DEC=$((LOAD_ADDR))
KERNEL_BIN_SIZE=$(stat -c%s "${KERNEL_BIN}")
SAFETY_MARGIN=$((64 * 1024))
SCAN_WINDOW=$((4 * 1024 * 1024))

if [ -z "${EDATA}" ] || [ -z "${EBSS}" ]; then
    echo "WARNING: Cannot extract symbols, using default padding (4MB)"
    PAD_SIZE=4194304
else
    EDATA_DEC=$((0x${EDATA}))
    EBSS_DEC=$((0x${EBSS}))
    IDLE_TOP_DEC=$((EBSS_DEC + SMP_STACK_TOTAL))
    INITRD_TARGET_OFFSET=$((IDLE_TOP_DEC - LOAD_ADDR_DEC + SAFETY_MARGIN))
    PAD_SIZE=$((INITRD_TARGET_OFFSET - KERNEL_BIN_SIZE))
    if [ ${PAD_SIZE} -le 0 ]; then
        PAD_SIZE=0
    fi
fi

echo "  Kernel ELF:      ${KERNEL_ELF}"
echo "  Kernel binary:   ${KERNEL_BIN} ($(du -h ${KERNEL_BIN} | cut -f1))"
echo "  _edata:          0x${EDATA}"
echo "  _ebss:           0x${EBSS}"
if [ -n "${EDATA}" ] && [ -n "${EBSS}" ]; then
    echo "  g_idle_topstack: _ebss + ${SMP_STACK_TOTAL} = 0x$(printf '%x' ${IDLE_TOP_DEC})"
    echo "  Initrd target:   load offset 0x$(printf '%x' ${INITRD_TARGET_OFFSET})"
fi
echo "  Padding size:    ${PAD_SIZE} bytes ($((PAD_SIZE / 1024)) KB)"

# Generate padding (0xFF bytes)
dd if=/dev/zero bs=1 count=${PAD_SIZE} 2>/dev/null | tr '\0' '\377' > .initrd_padding.bin

# Combine: kernel + padding + initrd
cat "${KERNEL_BIN}" .initrd_padding.bin initrd.img > nuttx_combined.bin

echo "  Combined size:   $(du -h nuttx_combined.bin | cut -f1)"

# Sanity-check the appended ROMFS
INITRD_OFFSET=$(grep -aob -- '-rom1fs-' nuttx_combined.bin | tail -1 | cut -d: -f1)
if [ -z "${INITRD_OFFSET}" ]; then
    echo "ERROR: No ROMFS magic found in combined image"
    exit 1
fi

if [ ${INITRD_OFFSET} -lt ${KERNEL_BIN_SIZE} ]; then
    echo "ERROR: Only the in-kernel ROMFS was found; appended initrd is missing"
    echo "       kernel size=${KERNEL_BIN_SIZE}, romfs offset=${INITRD_OFFSET}"
    exit 1
fi

if [ -n "${EDATA}" ] && [ -n "${EBSS}" ]; then
    EDATA_OFFSET=$((EDATA_DEC - LOAD_ADDR_DEC))
    IDLE_TOP_OFFSET=$((IDLE_TOP_DEC - LOAD_ADDR_DEC))
    SCAN_LIMIT_OFFSET=$((IDLE_TOP_OFFSET + SCAN_WINDOW))

    echo "  Initrd offset:   0x$(printf '%x' ${INITRD_OFFSET})"
    echo "  Scan window:     0x$(printf '%x' ${EDATA_OFFSET})..0x$(printf '%x' ${SCAN_LIMIT_OFFSET})"

    if [ ${INITRD_OFFSET} -le ${IDLE_TOP_OFFSET} ]; then
        echo "ERROR: initrd is before/inside idle stack; increase padding"
        exit 1
    fi

    if [ ${INITRD_OFFSET} -ge ${SCAN_LIMIT_OFFSET} ]; then
        echo "ERROR: initrd is outside sg2000_start.c scan window"
        echo "       Increase the scan window or reduce padding/image size"
        exit 1
    fi
fi

# Create uImage-style bootable image
TFTPBOOT_DIR="/home/wsb/tftpboot"
mkimage -A riscv -O linux -T kernel -C none \
        -a ${LOAD_ADDR} -e ${LOAD_ADDR} \
        -n "OpenVela" -d nuttx_combined.bin nuttx.img

# Copy bootable image to tftpboot directory
mkdir -p "${TFTPBOOT_DIR}"
cp nuttx.img "${TFTPBOOT_DIR}/nuttx.img"

# Cleanup intermediate files
rm -f initrd.img nuttx_combined.bin .initrd_padding.bin

echo "============================================================"
echo "  Build complete!"
echo "============================================================"
echo ""
echo "  Kernel ELF:     nuttx/nuttx"
echo "  Kernel binary:  nuttx/nuttx.bin"
echo "  Bootable image: ${TFTPBOOT_DIR}/nuttx.img"
echo "  Image size:     $(du -h ${TFTPBOOT_DIR}/nuttx.img | cut -f1)"
echo ""
echo "  Flash nuttx.img to address ${LOAD_ADDR}:"
echo "    fastboot flash nuttx nuttx.img"
echo ""
exit 0
