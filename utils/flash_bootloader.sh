#! /bin/bash

source ./utils/common.sh

pushd $OUTPUT_DIR
mkdir -p sdb
mount /dev/sdb sdb

# bootloader
if [[ " ${BUILD_OPTIONS[@]} " =~ " BOOTLOADER " ]]; then
    cp $OUTPUT_DIR/bootloader_secboot_ddr5_$NAME.bin ./sdb/bootloader.bin
fi

sync
sync ./sdb
umount ./sdb
echo "Flashing completed. Please safely eject the SD card."
popd

