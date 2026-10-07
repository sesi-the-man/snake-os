#!/bin/bash

set -e

sectorsToBlocks() {
  echo $(( ( "$1" * 512 ) / 1024 ))
}

sectorsToBytes() {
  echo $(( "$1" * 512 ))
}

export disk_timestamp="2023/01/01T12:15:05"

rm -rf ${BUILD_DIR}/custom_image
mkdir -p ${BUILD_DIR}/custom_image
cd ${BUILD_DIR}/custom_image

# Size the disk image to fit the boot files plus a little headroom (min 20 MiB)
IMG_MB=$(( $(du -scm ${BASE_DIR}/images/zImage ${BASE_DIR}/images/*.dtb ${BASE_DIR}/images/rpi-firmware | tail -1 | cut -f1) + 4 ))
if [ "${IMG_MB}" -lt 20 ]; then IMG_MB=20; fi

# Create disk image.
dd if=/dev/zero of=disk.img bs=1M count=${IMG_MB}  # block size (1MB) * count = size allocated for image

### needed: apt install fdisk
/sbin/sfdisk disk.img <<EOF
  label: dos
  label-id: 0xba5eba11

  disk.img1 : type=c, bootable
EOF

# Create boot partition.
START=$(/sbin/fdisk -l -o Start disk.img|tail -n 1)
SECTORS=$(/sbin/fdisk -l -o Sectors disk.img|tail -n 1)
### needed: apt install dosfstools
/sbin/mkfs.vfat --invariant -i ba5eba11 -n BOOT disk.img --offset $START $(sectorsToBlocks $SECTORS)
OFFSET=$(sectorsToBytes $START)

# Copy boot files.
mkdir -p boot/overlays overlays
cp ${BASE_DIR}/images/rpi-firmware/cmdline.txt boot/cmdline.txt
cp ${BASE_DIR}/images/rpi-firmware/config.txt boot/config.txt
cp ${BASE_DIR}/images/rpi-firmware/bootcode.bin boot/bootcode.bin
cp ${BASE_DIR}/images/rpi-firmware/fixup_cd.dat boot/fixup_cd.dat
cp ${BASE_DIR}/images/rpi-firmware/start_cd.elf boot/start_cd.elf
cp ${BASE_DIR}/images/rpi-firmware/overlays/* overlays/
cp ${BASE_DIR}/images/*.dtb boot/
cp ${BASE_DIR}/images/zImage boot/zImage

chmod 0755 `find boot overlays`
touch -d "${disk_timestamp}" `find boot overlays`
### needed: apt install mtools
mcopy -bpm -i "disk.img@@$OFFSET" boot/* ::
# mcopy doesn't copy directories deterministically, so rely on sorted shell globbing instead.
mcopy -bpm -i "disk.img@@$OFFSET" overlays/* ::overlays
mv disk.img ${BASE_DIR}/images/sdcard.img

cd -