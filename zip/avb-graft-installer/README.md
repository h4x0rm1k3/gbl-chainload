# AVB graft installer (Track 3 flashable ZIP)

A flashable ZIP that grafts stock embedded vbmeta from your device's on-disk
recovery/dtbo partition into your custom recovery/dtbo image, then flashes
the grafted result back. Run from inside an existing custom recovery
(TWRP/OFOX/etc.).

## Assembly

This directory is a *template*. Add your custom images:

    cp /path/to/twrp.img zip/avb-graft-installer/recovery_custom.img
    cp /path/to/dtbo.img zip/avb-graft-installer/dtbo_custom.img   # optional

Then build the static graft binary and bundle:

    # Cross-compile inside the project Docker image:
    docker run --rm -v "$(pwd):/work" -w /work/tools/avb-graft-recovery \
      --user "$(id -u):$(id -g)" gbl-chainload-build:latest make

    mkdir -p zip/avb-graft-installer/tools
    cp tools/avb-graft-recovery/graft-vbmeta zip/avb-graft-installer/tools/

    cd zip/avb-graft-installer
    zip -r ../../dist/avb-graft-installer.zip .

## Flash

In your custom recovery, install `avb-graft-installer.zip`. Reboot to system.

## OTA recovery

After each OTA, your custom recovery + grafted state are overwritten by stock.
Reboot to a state that lets you flash again (e.g. TWRP via fastboot stage),
then re-run this installer with the same ZIP. The script reads the new on-disk
stock as the donor each time.
