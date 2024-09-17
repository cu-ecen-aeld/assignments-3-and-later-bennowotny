#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath "$(dirname "$0")")
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
JOB_COUNT=16

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p "${OUTDIR}"
if [[ ! -d "${OUTDIR}" ]]
then
    echo "Failed to make directory '${OUTDIR}'"
    exit 1
fi

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone "${KERNEL_REPO}" --depth 1 --single-branch --branch "${KERNEL_VERSION}"
fi
if [ ! -e "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout "${KERNEL_VERSION}"

    # TODO: Add your kernel build steps here
    make "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" mrproper
    make "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" defconfig
    make "-j${JOB_COUNT}" "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" all
    # make "-j${JOB_COUNT}" "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" modules
    make "-j${JOB_COUNT}" "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" dtbs
fi

echo "Adding the Image in outdir"
cp -r "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/Image"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf "${OUTDIR}/rootfs"
fi

# TODO: Create necessary base directories
mkdir -p "${OUTDIR}/rootfs/bin" "${OUTDIR}/rootfs/dev" "${OUTDIR}/rootfs/etc" "${OUTDIR}/rootfs/home" "${OUTDIR}/rootfs/lib" "${OUTDIR}/rootfs/lib64" "${OUTDIR}/rootfs/proc" "${OUTDIR}/rootfs/sbin" "${OUTDIR}/rootfs/sys" "${OUTDIR}/rootfs/tmp" "${OUTDIR}/rootfs/usr" "${OUTDIR}/rootfs/var"
mkdir -p "${OUTDIR}/rootfs/usr/bin" "${OUTDIR}/rootfs/usr/lib" "${OUTDIR}/rootfs/usr/sbin"
mkdir -p "${OUTDIR}/rootfs/var/log"

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout "${BUSYBOX_VERSION}"
    # TODO:  Configure busybox
else
    cd busybox
fi

# TODO: Make and install busybox
make distclean
make defconfig
make "-j${JOB_COUNT}" "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}"
make "-j${JOB_COUNT}" "CONFIG_PREFIX=${OUTDIR}/rootfs" "ARCH=${ARCH}" "CROSS_COMPILE=${CROSS_COMPILE}" install

echo "Library dependencies"
"${CROSS_COMPILE}readelf" -a "${OUTDIR}/rootfs/bin/busybox" | grep "program interpreter"
"${CROSS_COMPILE}readelf" -a "${OUTDIR}/rootfs/bin/busybox" | grep "Shared library"

# TODO: Add library dependencies to rootfs
SYSROOT_LIBRARIES=$("${CROSS_COMPILE}gcc" -print-sysroot)
"${CROSS_COMPILE}readelf" -a "${OUTDIR}/rootfs/bin/busybox" | grep "Shared library" | sed 's/.*\[\(.*\)\].*/\1/g' | xargs -I{} cp "${SYSROOT_LIBRARIES}/lib64/{}" "${OUTDIR}/rootfs/lib64/"
cp "${SYSROOT_LIBRARIES}/lib/ld-linux-aarch64.so.1" "${OUTDIR}/rootfs/lib/"

# TODO: Make device nodes
sudo mknod -m 666 "${OUTDIR}/rootfs/dev/null" c 1 3
sudo mknod -m 620 "${OUTDIR}/rootfs/dev/console" c 5 1

# TODO: Clean and build the writer utility
pushd "${FINDER_APP_DIR}"
make clean
make "-j${JOB_COUNT}" "CROSS_COMPILE=${CROSS_COMPILE}"
mkdir -p "${OUTDIR}/rootfs/home"
cp "${FINDER_APP_DIR}/writer" "${OUTDIR}/rootfs/home/"

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
mkdir -p "${OUTDIR}/rootfs/home/conf"
cp "${FINDER_APP_DIR}/finder.sh" "${OUTDIR}/rootfs/home/"
cp "${FINDER_APP_DIR}/conf/username.txt" "${OUTDIR}/rootfs/home/conf"
cp "${FINDER_APP_DIR}/conf/assignment.txt" "${OUTDIR}/rootfs/home/conf"
cp "${FINDER_APP_DIR}/finder-test.sh" "${OUTDIR}/rootfs/home/"
cp "${FINDER_APP_DIR}/autorun-qemu.sh" "${OUTDIR}/rootfs/home/"
popd

# TODO: Chown the root directory
pushd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"

# TODO: Create initramfs.cpio.gz
gzip -f "${OUTDIR}/initramfs.cpio"
popd
