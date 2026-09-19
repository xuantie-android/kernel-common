# Kleaf may pass an absolute $(src), while classic in-tree Kbuild passes a
# source-relative path.  Keep the DDK includes valid in both cases.
img_basedir := $(if $(KBUILD_EXTMOD),$(abspath $(KBUILD_EXTMOD)),$(if $(filter /%,$(src)),$(src),$(srctree)/$(src)))
include $(img_basedir)/config_kernel.mk

obj-$(CONFIG_DRM_POWERVR_ROGUE) += pvrsrvkm.o

ccflags-y += \
 -include config_kernel.h \
 -I$(img_basedir)/include/drm \
 -I$(img_basedir) \
 -I$(img_basedir)/include \
 -I$(img_basedir)/km \
 -D__linux__ \
 -Wno-error

include $(img_basedir)/pvrsrvkm.mk

# TH1520 has a real display controller in the mainline kernel.  The DDK's
# legacy nulldisp helper targets an old DRM atomic API and is not needed for
# the render-node/services bridge used by the matching Android userspace.
