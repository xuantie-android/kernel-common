# TH1520 Android DDK 1.17 external-module experiment

This branch preserves a tested driver-source experiment, not a replacement
kernel image. Build this directory as an external module against the accepted
Android Linux 7.1 kernel configuration/output. Do not build or flash the entire
6.6 reference kernel as an Android update.

Base: RevyOS th1520-linux-kernel
`b0820dbe0b985258d2221c96778dab893651324f`, DDK 1.17@6210866.
The TH1520 power-sequencer, RISC-V cache and Linux 7.1 compatibility changes were
restored from the existing September 2–3 LoveSy source archive, not re-created.

New integration changes:

- Resolve the DDK source directory through KBUILD_EXTMOD for external builds.
- Select USE_PVRSYNC_DEVNODE and pvr_sync_ioctl_dev.o for Android's
  `/dev/pvr_sync` ABI. Linux's DRM-only sync frontend is not sufficient for
  the published XuanTie Android libraries.
- Guard the DRM-only close wrapper consistently with its callers.

The misc-device frontend is copied unchanged from TI/Imagination's
ti-img-rogue-driver commit `582e5b3bdafe5159e20cd4c5475cd913cbbeeef0`,
`android/k5.10/1.18.6276027`, services/server/env/linux/pvr_sync_ioctl_dev.c.
Its copyright and dual MIT/GPLv2 license are retained. The companion common
sync implementation is byte-identical to this 1.17 tree. No 1.18 GPU bridge or
firmware structures were imported; the frontend uses this tree's ioctl structs.

Validation on 2026-09-19: module build and all 285 imported-symbol CRC checks
pass, KCFI enabled; GPU BVNC 36.52.104.182 and matching firmware initialize;
native Vulkan device/queue/buffer/fence/submit/wait/readback pass with zero
GPU-fill mismatches. ANGLE over proprietary Vulkan completes the offscreen
1080x2160 rendering probe and reports GLES 3.1 without capability overrides.

Not accepted for production: native proprietary EGL still aborts on a destroyed
mutex in RGXCreateDeviceMemContext; AHardwareBuffer/window-system and real
SystemUI integration are unvalidated. AA output hashes differ from the open
driver, with numerical tolerances not yet assessed. No whole-image, CTS or
long-run acceptance is claimed. The production open driver was restored after
each test. Restoring its module requires its existing `exp_hw_support=1` option.
