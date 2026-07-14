#!/bin/bash
# Builds CloudType and deploys it to both test images. The 800K floppy
# image is also wrapped in a DiskCopy 4.2 header (see
# make_diskcopy_image.py) and copied onto the 20MB image, so booting
# the 20MB image on real BlueSCSI hardware and running the Disk Copy
# 4.2 utility already included there lets you write an actual physical
# floppy from the file sitting on disk.
set -e

cd "$(dirname "$0")"

HD_IMAGE="vmac/CloudType_20M.dsk"
FLOPPY_IMAGE="vmac/Mac-800K.dsk"
FLOPPY_IMAGE_NAME="CloudType 800K"
BIN="app/build/CloudType.bin"
GUIDE="doc/START_HERE.md"
GUIDE_NAME="START_HERE.md"
WRAPPED_FLOPPY_TEMP="vmac/.CloudType_Floppy_wrapped.img"

echo "Building..."
cmake --build app/build

echo "Wrapping the 800K floppy image in a DiskCopy 4.2 header..."
python3 make_diskcopy_image.py "$FLOPPY_IMAGE" "$WRAPPED_FLOPPY_TEMP" "CloudType"

echo "Deploying to 20MB image..."
hmount "$HD_IMAGE"
hdel CloudType 2>/dev/null || true
hcopy -m "$BIN" :CloudType
hdel "$FLOPPY_IMAGE_NAME" 2>/dev/null || true
hcopy -r "$WRAPPED_FLOPPY_TEMP" ":$FLOPPY_IMAGE_NAME"
hattrib -t dImg -c dCpy ":$FLOPPY_IMAGE_NAME"
hdel "$GUIDE_NAME" 2>/dev/null || true
hcopy -t "$GUIDE" ":$GUIDE_NAME"
hattrib -t TEXT -c ArtT ":$GUIDE_NAME"
humount

rm -f "$WRAPPED_FLOPPY_TEMP"

echo "Deploying to 800K image..."
hmount "$FLOPPY_IMAGE"
hdel CloudType 2>/dev/null || true
hcopy -m "$BIN" :CloudType
hdel "$GUIDE_NAME" 2>/dev/null || true
hcopy -t "$GUIDE" ":$GUIDE_NAME"
hattrib -t TEXT -c ArtT ":$GUIDE_NAME"
humount

echo "Done."
