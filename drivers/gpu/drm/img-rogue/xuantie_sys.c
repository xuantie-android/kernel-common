/*************************************************************************/ /*!
@File
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System Configuration functions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/pwrseq/consumer.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <linux/thermal.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/sysfs.h>
#include <linux/utsname.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <drm/drm_device.h>

#include "pvrsrv.h"
#include "pvr_drv.h"
#include "proc_stats.h"
#include "pvrversion.h"
#include "rgxhwperf.h"
#include "rgxinit.h"
#include "process_stats.h"
#include "xuantie_sys.h"

#ifdef SUPPORT_RGX
static IMG_HANDLE ghGpuUtilSysFS;
#endif
static int xuantie_gpu_period_ms = -1;
static int xuantie_gpu_loading_max_percent = -1;
static int xuantie_gpu_last_server_error = 0;
static int xuantie_gpu_last_rgx_error = 0;

struct gpu_sysfs_private_data {
	struct device *dev;
	struct timer_list timer;
	struct workqueue_struct *workqueue;
	struct work_struct work;
};
static struct gpu_sysfs_private_data xuantie_gpu_sysfs_private_data;

/******************定义log的读写属性*************************************/
static ssize_t xuantie_gpu_log_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	ssize_t len = 0;
	PDLLIST_NODE pNext, pNode;
	struct device *dev = kobj_to_dev(kobj);
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = drm_dev->dev_private;
	PVRSRV_DEVICE_NODE *psDevNode = priv->dev_node;
	PVRSRV_DEVICE_HEALTH_STATUS eHealthStatus = OSAtomicRead(&psDevNode->eHealthStatus);
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	// 驱动信息
	int dev_id = (int)psDevNode->sDevId.ui32InternalID;
	int dev_connection_num = 0;
	int dev_loading_percent = -1;

	// 内存信息
	IMG_UINT32 dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_COUNT];

	// 实例/会话/通道信息
	int instance_id = 0;

	// 异常信息
	char* server_state[3] = {"UNDEFINED", "OK", "BAD"};
	char* rgx_state[5] = {"UNDEFINED", "OK", "NOT RESPONDING", "DEAD", "FAULT"};
	int rgx_err = 0;

	if (psDevNode->pvDevice != NULL)
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = psDevNode->pvDevice;
#ifdef SUPPORT_RGX
		if (!PVRSRV_VZ_MODE_IS(GUEST) &&
			psDevInfo->pfnGetGpuUtilStats &&
			eHealthStatus == PVRSRV_DEVICE_HEALTH_STATUS_OK)
		{
			RGXFWIF_GPU_UTIL_STATS sGpuUtilStats;
			PVRSRV_ERROR eError = PVRSRV_OK;

			eError = psDevInfo->pfnGetGpuUtilStats(psDevNode,
													ghGpuUtilSysFS,
													&sGpuUtilStats);

			if ((eError == PVRSRV_OK) &&
				((IMG_UINT32)sGpuUtilStats.ui64GpuStatCumulative))
			{
				IMG_UINT64 util;
				IMG_UINT32 rem;

				util = 100 * sGpuUtilStats.ui64GpuStatActive;
				util = OSDivide64(util, (IMG_UINT32)sGpuUtilStats.ui64GpuStatCumulative, &rem);

				dev_loading_percent = (int)util;
			}
		}
#endif
		rgx_err = psDevInfo->sErrorCounts.ui32WGPErrorCount + psDevInfo->sErrorCounts.ui32TRPErrorCount;
	}

	if (dev_loading_percent > xuantie_gpu_loading_max_percent)
		xuantie_gpu_loading_max_percent = dev_loading_percent;

	PVRSRVFindProcessMemStats(0, PVRSRV_DRIVER_STAT_TYPE_COUNT, IMG_TRUE, dev_mem_state);

	if (!psDevNode->hConnectionsLock)
	{
		len += scnprintf(buf + len, PAGE_SIZE - len,
			"[GPU] Version: %s\n"
			"Build Info: %s %s %s %s\n"
			"----------------------------------------MODULE PARAM-----------------------------------------\n"
			"updatePeriod_ms\n"
			"%d\n"
			"----------------------------------------MODULE STATUS----------------------------------------\n"
			"DevId     DevInstanceNum      DevLoading_%%   DevLoadingMax_%%\n"
			"%-10d%-20d%-15d%-15d\n"
			"----------------------------------------MEM INFO(KB)-----------------------------------------\n"
			"KMalloc           VMalloc           PTMemoryUMA       VMapPTUMA\n"
			"%-18d%-18d%-18d%-18d\n"
			"PTMemoryLMA       IORemapPTLMA      GPUMemLMA         GPUMemUMA\n"
			"%-18d%-18d%-18d%-18d\n"
			"GPUMemUMAPool     MappedGPUMemUMA/LMA                 DmaBufImport\n"
			"%-18d%-36d%-18d\n"
			"----------------------------------------INSTANCE INFO----------------------------------------\n"
			"Id   ProName             ProId     ThdId\n"
			"---------------------------------------EXCEPTION INFO----------------------------------------\n"
			"Server_State      Server_Error      RGX_State         RGX_Error\n"
			"%-18s%-18d%-18s%-18d\n",
			PVRVERSION_STRING, utsname()->sysname, utsname()->release, utsname()->version, utsname()->machine, xuantie_gpu_period_ms,
			dev_id, 0, dev_loading_percent, xuantie_gpu_loading_max_percent,
			dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_KMALLOC] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_VMALLOC],
			dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_UMA] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_VMAP_PT_UMA] >> 10,
			dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_LMA] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_IOREMAP_PT_LMA] >> 10,
			dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_LMA] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA] >> 10,
			dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA_POOL] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_MAPPED_GPUMEM_UMA_LMA] >> 10,
			dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_DMA_BUF_IMPORT] >> 10,
			server_state[psPVRSRVData->eServicesState], PVRSRV_KM_ERRORS - xuantie_gpu_last_server_error,
			rgx_state[eHealthStatus], rgx_err - xuantie_gpu_last_rgx_error);
		return len;
	}

	OSLockAcquire(psDevNode->hConnectionsLock);
	dllist_foreach_node(&psDevNode->sConnections, pNode, pNext)
	{
		dev_connection_num++;
	}

	// 格式化输出
	len += scnprintf(buf + len, PAGE_SIZE - len,
		"[GPU] Version: %s\n"
		"Build Info: %s %s %s %s\n"
		"----------------------------------------MODULE PARAM-----------------------------------------\n"
		"updatePeriod_ms\n"
		"%d\n"
		"----------------------------------------MODULE STATUS----------------------------------------\n"
		"DevId     DevInstanceNum      DevLoading_%%   DevLoadingMax_%%\n"
		"%-10d%-20d%-15d%-15d\n"
		"----------------------------------------MEM INFO(KB)-----------------------------------------\n"
		"KMalloc           VMalloc           PTMemoryUMA       VMapPTUMA\n"
		"%-18d%-18d%-18d%-18d\n"
		"PTMemoryLMA       IORemapPTLMA      GPUMemLMA         GPUMemUMA\n"
		"%-18d%-18d%-18d%-18d\n"
		"GPUMemUMAPool     MappedGPUMemUMA/LMA                 DmaBufImport\n"
		"%-18d%-36d%-18d\n"
		"----------------------------------------INSTANCE INFO----------------------------------------\n"
		"Id   ProName             ProId     ThdId\n",
		PVRVERSION_STRING, utsname()->sysname, utsname()->release, utsname()->version, utsname()->machine, xuantie_gpu_period_ms,
		dev_id, dev_connection_num, dev_loading_percent, xuantie_gpu_loading_max_percent,
		dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_KMALLOC] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_VMALLOC],
		dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_UMA] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_VMAP_PT_UMA] >> 10,
		dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_LMA] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_IOREMAP_PT_LMA] >> 10,
		dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_LMA] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA] >> 10,
		dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA_POOL] >> 10, dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_MAPPED_GPUMEM_UMA_LMA] >> 10,
		dev_mem_state[PVRSRV_DRIVER_STAT_TYPE_DMA_BUF_IMPORT] >> 10);

	dllist_foreach_node(&psDevNode->sConnections, pNode, pNext)
	{
		CONNECTION_DATA *sData = IMG_CONTAINER_OF(pNode, CONNECTION_DATA, sConnectionListNode);
		len += scnprintf(buf + len, PAGE_SIZE - len,
			"%-5d%-20s%-10d%-10d\n",
			instance_id, sData->pszProcName, sData->pid, sData->tid);
		instance_id++;
	}

	len += scnprintf(buf + len, PAGE_SIZE - len,
		"---------------------------------------EXCEPTION INFO----------------------------------------\n"
		"Server_State      Server_Error      RGX_State         RGX_Error\n"
		"%-18s%-18d%-18s%-18d\n",
		server_state[psPVRSRVData->eServicesState], PVRSRV_KM_ERRORS - xuantie_gpu_last_server_error,
		rgx_state[eHealthStatus], rgx_err - xuantie_gpu_last_rgx_error);

	OSLockRelease(psDevNode->hConnectionsLock);
	return len;
}

static ssize_t xuantie_gpu_log_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = drm_dev->dev_private;
	PVRSRV_DEVICE_NODE *psDevNode = priv->dev_node;

	xuantie_gpu_loading_max_percent = -1;
	xuantie_gpu_last_server_error = PVRSRV_KM_ERRORS;
	if (psDevNode->pvDevice != NULL)
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = psDevNode->pvDevice;
		xuantie_gpu_last_rgx_error = psDevInfo->sErrorCounts.ui32WGPErrorCount + psDevInfo->sErrorCounts.ui32TRPErrorCount;
	}
	return count;
}

static struct kobj_attribute sxuantie_gpu_log_attr = __ATTR(log, 0664, xuantie_gpu_log_show, xuantie_gpu_log_store);

/******************定义updatePeriod的读写属性*************************************/
static ssize_t xuantie_gpu_updatePeriod_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n(set 50~10000 to enable update period, set other value to disable)\n",
					xuantie_gpu_period_ms);
}

static ssize_t xuantie_gpu_updatePeriod_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	char *start = (char *)buf;
	int temp_period_ms = simple_strtoul(start, &start, 0);
	if (temp_period_ms >= 50 && temp_period_ms <= 10000) {
		xuantie_gpu_period_ms = temp_period_ms;
		mod_timer(&xuantie_gpu_sysfs_private_data.timer, jiffies + msecs_to_jiffies(xuantie_gpu_period_ms));
	} else {
		xuantie_gpu_period_ms = -1;
		timer_delete(&xuantie_gpu_sysfs_private_data.timer);
	}
	return count;
}

static struct kobj_attribute sxuantie_gpu_updateperiod_attr = __ATTR(updatePeriod_ms, 0664, xuantie_gpu_updatePeriod_show, xuantie_gpu_updatePeriod_store);

/******************定义sysfs属性info group*************************************/
static struct attribute *pxuantie_gpu_attrs[] = {
	&sxuantie_gpu_log_attr.attr,
	&sxuantie_gpu_updateperiod_attr.attr,
	NULL,   // must be NULL
};

static struct attribute_group sxuantie_gpu_attr_group = {
	.name = "info", // device下目录指定
	.attrs = pxuantie_gpu_attrs,
};

static void xuantie_gpu_work_func(struct work_struct *w)
{
	struct gpu_sysfs_private_data *data = container_of(w, struct gpu_sysfs_private_data, work);
	struct device *dev = data->dev;
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = drm_dev->dev_private;
	PVRSRV_DEVICE_NODE *psDevNode = priv->dev_node;
	PVRSRV_DEVICE_HEALTH_STATUS eHealthStatus = OSAtomicRead(&psDevNode->eHealthStatus);
	int current_loading_percent = -1;
	if (psDevNode->pvDevice != NULL)
	{
		PVRSRV_RGXDEV_INFO *psDevInfo = psDevNode->pvDevice;
#ifdef SUPPORT_RGX
		if (!PVRSRV_VZ_MODE_IS(GUEST) &&
			psDevInfo->pfnGetGpuUtilStats &&
			eHealthStatus == PVRSRV_DEVICE_HEALTH_STATUS_OK)
		{
			RGXFWIF_GPU_UTIL_STATS sGpuUtilStats;
			PVRSRV_ERROR eError = PVRSRV_OK;

			eError = psDevInfo->pfnGetGpuUtilStats(psDevNode,
													ghGpuUtilSysFS,
													&sGpuUtilStats);

			if ((eError == PVRSRV_OK) &&
				((IMG_UINT32)sGpuUtilStats.ui64GpuStatCumulative))
			{
				IMG_UINT64 util;
				IMG_UINT32 rem;

				util = 100 * sGpuUtilStats.ui64GpuStatActive;
				util = OSDivide64(util, (IMG_UINT32)sGpuUtilStats.ui64GpuStatCumulative, &rem);

				current_loading_percent = (int)util;
			}
		}
#endif
	}
	if (current_loading_percent > xuantie_gpu_loading_max_percent)
		xuantie_gpu_loading_max_percent = current_loading_percent;
	mod_timer(&data->timer, jiffies + msecs_to_jiffies(xuantie_gpu_period_ms));
}

static void xuantie_gpu_timer_callback(struct timer_list *t)
{
	struct gpu_sysfs_private_data *data = container_of(t, struct gpu_sysfs_private_data, timer);
	queue_work(data->workqueue, &data->work);
}

int xuantie_sysfs_init(struct device *dev)
{
	int ret;
	ret = sysfs_create_group(&dev->kobj, &sxuantie_gpu_attr_group);
	if (ret) {
		dev_err(dev, "Failed to create gpu dev sysfs.\n");
		return ret;
	}

#if defined(SUPPORT_RGX) && !defined(NO_HARDWARE)
	if (SORgxGpuUtilStatsRegister(&ghGpuUtilSysFS) != PVRSRV_OK)
	{
		dev_err(dev, "Failed to register GpuUtil for sysfs.\n");
		return -ENOMEM;
	}
#endif

	xuantie_gpu_sysfs_private_data.workqueue = create_workqueue("gpu_sysfs_workqueue");
	if (!xuantie_gpu_sysfs_private_data.workqueue)
		return -ENOMEM;
	INIT_WORK(&xuantie_gpu_sysfs_private_data.work, xuantie_gpu_work_func);

	xuantie_gpu_sysfs_private_data.dev = dev;
	timer_setup(&xuantie_gpu_sysfs_private_data.timer, xuantie_gpu_timer_callback, 0);
	return ret;
}

void xuantie_sysfs_uninit(struct device *dev)
{
	if (xuantie_gpu_sysfs_private_data.dev == dev)
		timer_delete(&xuantie_gpu_sysfs_private_data.timer);

	if (xuantie_gpu_sysfs_private_data.workqueue)
		destroy_workqueue(xuantie_gpu_sysfs_private_data.workqueue);

#if defined(SUPPORT_RGX) && !defined(NO_HARDWARE)
	if (SORgxGpuUtilStatsUnregister(ghGpuUtilSysFS) != PVRSRV_OK)
	{
		dev_err(dev, "Failed to unregister GpuUtil for sysfs.\n");
	}
#endif

	sysfs_remove_group(&dev->kobj, &sxuantie_gpu_attr_group);
}

int xuantie_mfg_enable(struct gpu_plat_if *mfg)
{
	int ret;

	ret = pm_runtime_resume_and_get(mfg->dev);
	if (ret < 0)
		return ret;

	ret = pwrseq_power_on(mfg->pwrseq);
	if (ret)
		goto err_pm_runtime_put;

	return 0;

err_pm_runtime_put:
	pm_runtime_put_sync(mfg->dev);
	return ret;
}

void xuantie_mfg_disable(struct gpu_plat_if *mfg)
{
	int ret;

	ret = pwrseq_power_off(mfg->pwrseq);
	if (ret)
		dev_warn(mfg->dev, "failed to run GPU power-off sequence: %d\n", ret);
	pm_runtime_put_sync(mfg->dev);
}

struct gpu_plat_if *dt_hw_init(struct device *dev)
{
	struct gpu_plat_if *mfg;

	xuantie_debug("gpu_plat_if_create Begin\n");

	mfg = devm_kzalloc(dev, sizeof(*mfg), GFP_KERNEL);
	if (!mfg)
		return ERR_PTR(-ENOMEM);
	mfg->dev = dev;

	/*
	 * TH1520 requires core/sys clocks, the GPU clock-generator reset and
	 * the GPU core reset to be toggled in a strict order.  The AON-backed
	 * power sequencer owns those resources; falling back to the generic
	 * clock/reset path can hang the SoC before userspace starts.
	 */
	mfg->pwrseq = devm_pwrseq_get(dev, "gpu-power");
	if (IS_ERR(mfg->pwrseq))
		return dev_err_cast_probe(dev, mfg->pwrseq,
					  "failed to get required GPU power sequencer\n");

	mutex_init(&mfg->set_power_state);

	pm_runtime_enable(dev);

	xuantie_debug("gpu_plat_if_create End\n");

	return mfg;
}

void dt_hw_uninit(struct gpu_plat_if *mfg)
{
	pm_runtime_disable(mfg->dev);
}
