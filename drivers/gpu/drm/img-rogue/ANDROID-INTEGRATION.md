# Android-sync DDK module for TH1520

This directory is an exact source import of the previously board-tested
`drivers/gpu/drm/img-rogue` subtree at adaptation commit
`a745203085cd3277936cf791852291f09abb7324` (LoveSy), based on RevyOS Linux6.6
`b0820dbe0b985258d2221c96778dab893651324f`. The upstream provenance and copyright
remain in ANDROID-EXPERIMENT.md and the source files. The old experiment's
userspace blockers are historical: allocator publication, WSI and format fixes
have since been tested with real Android windows, GC620 HWC, UHID and encoding.

Build as an **external module** against the same prepared 7.1 kernel output and
toolchain used for the image, with CONFIG_DRM_POWERVR_ROGUE=m. This does not
replace or enable the upstream drm/imagination driver. An image must select one
driver for the GPU at boot, not load both in sequence.

Android requires USE_PVRSYNC_DEVNODE and the `/dev/pvr_sync` frontend. The older
DRM-only Linux sync build is not interchangeable even though it has the same
module filename. Do not import the complete 6.6 reference kernel into Android.

The test module's SHA256 was
`2f2b302e5926a156922f199d27291110b7a37d18cb11f68fbecb791c1f9a6350`,
with all285 imported-symbol CRCs matching the running7.1 kernel and KCFI enabled.
Rebuilding against a different kernel requires repeating that ABI check and
board validation. Runtime tests do not by themselves certify a cold-boot image.
