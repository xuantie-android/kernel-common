// SPDX-License-Identifier: GPL-2.0
/*
 * T-HEAD TH1520 AON CPU regulator
 *
 * The two application-CPU rails are controlled together by the E902 AON
 * firmware.  Expose them as one logical regulator so cpufreq-dt can sequence
 * voltage and clock transitions through the standard OPP framework.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitops.h>
#include <linux/firmware/thead/thead,th1520-aon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#define TH1520_AON_APCPU_REGULATOR	3
#define TH1520_AON_DUAL_RAIL_VDD	BIT(0)
#define TH1520_AON_DUAL_RAIL_VDDM	BIT(1)

struct th1520_aon_regulator_request {
	struct th1520_aon_rpc_msg_hdr hdr;
	__be16 resource;
	__be16 dual_rail;
	__be32 vdd;
	__be32 vddm;
	__be16 reserved[6];
} __packed __aligned(1);

struct th1520_aon_regulator_response {
	struct th1520_aon_rpc_ack_common ack;
	__be16 resource;
	__be16 dual_rail;
	__be32 vdd;
	__be32 vddm;
	__be16 reserved[6];
} __packed __aligned(1);

struct th1520_aon_cpu_voltage {
	u32 vdd;
	u32 vddm;
};

/* Voltage pairs signed off by the vendor for the Lichee Module 4A. */
static const struct th1520_aon_cpu_voltage th1520_aon_cpu_voltages[] = {
	{  600000,  800000 },
	{  700000,  800000 },
	{  800000,  800000 },
	{ 1000000, 1000000 },
};

struct th1520_aon_cpu_regulator {
	struct th1520_aon_chan *aon_chan;
	struct regulator_desc desc;
};

static int th1520_aon_cpu_set_voltage(struct regulator_dev *rdev,
				       int min_uV, int max_uV,
				       unsigned int *selector)
{
	struct th1520_aon_cpu_regulator *cpu_reg = rdev_get_drvdata(rdev);
	struct th1520_aon_regulator_request msg = {};
	const struct th1520_aon_cpu_voltage *voltage = NULL;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(th1520_aon_cpu_voltages); i++) {
		if (th1520_aon_cpu_voltages[i].vdd >= min_uV &&
		    th1520_aon_cpu_voltages[i].vdd <= max_uV) {
			voltage = &th1520_aon_cpu_voltages[i];
			break;
		}
	}

	if (!voltage)
		return -EINVAL;

	msg.hdr.svc = TH1520_AON_RPC_SVC_PM;
	msg.hdr.func = TH1520_AON_PM_FUNC_SET_RESOURCE_REGULATOR;
	msg.hdr.size = TH1520_AON_RPC_MSG_NUM;
	msg.resource = cpu_to_be16(TH1520_AON_APCPU_REGULATOR);
	msg.dual_rail = cpu_to_be16(TH1520_AON_DUAL_RAIL_VDD |
					    TH1520_AON_DUAL_RAIL_VDDM);
	msg.vdd = cpu_to_be32(voltage->vdd);
	msg.vddm = cpu_to_be32(voltage->vddm);

	ret = th1520_aon_call_rpc(cpu_reg->aon_chan, &msg);
	if (ret)
		return ret;

	if (selector)
		*selector = i;
	return 0;
}

static int th1520_aon_cpu_get_voltage(struct regulator_dev *rdev)
{
	struct th1520_aon_cpu_regulator *cpu_reg = rdev_get_drvdata(rdev);
	struct th1520_aon_regulator_request msg = {};
	struct th1520_aon_regulator_response response;
	u32 vdd, vddm;
	int ret;

	msg.hdr.svc = TH1520_AON_RPC_SVC_PM;
	msg.hdr.func = TH1520_AON_PM_FUNC_GET_RESOURCE_REGULATOR;
	msg.hdr.size = TH1520_AON_RPC_MSG_NUM;
	msg.resource = cpu_to_be16(TH1520_AON_APCPU_REGULATOR);
	/* The vendor protocol uses bit 0 as the query's dual-rail flag. */
	msg.dual_rail = cpu_to_be16(TH1520_AON_DUAL_RAIL_VDD);

	ret = th1520_aon_call_rpc_response(cpu_reg->aon_chan, &msg, &response,
					   sizeof(response));
	if (ret)
		return ret;

	vdd = be32_to_cpu(response.vdd);
	vddm = be32_to_cpu(response.vddm);
	dev_dbg(&rdev->dev, "CPU rails are %u/%u uV\n", vdd, vddm);

	return vdd;
}

static int th1520_aon_cpu_list_voltage(struct regulator_dev *rdev,
					unsigned int selector)
{
	if (selector >= ARRAY_SIZE(th1520_aon_cpu_voltages))
		return -EINVAL;

	return th1520_aon_cpu_voltages[selector].vdd;
}

static int th1520_aon_cpu_is_enabled(struct regulator_dev *rdev)
{
	return 1;
}

static const struct regulator_ops th1520_aon_cpu_regulator_ops = {
	.set_voltage = th1520_aon_cpu_set_voltage,
	.get_voltage = th1520_aon_cpu_get_voltage,
	.list_voltage = th1520_aon_cpu_list_voltage,
	.is_enabled = th1520_aon_cpu_is_enabled,
};

static int th1520_aon_regulator_probe(struct auxiliary_device *adev,
				       const struct auxiliary_device_id *id)
{
	struct th1520_aon_cpu_regulator *cpu_reg;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	struct device *dev = &adev->dev;

	cpu_reg = devm_kzalloc(dev, sizeof(*cpu_reg), GFP_KERNEL);
	if (!cpu_reg)
		return -ENOMEM;

	cpu_reg->aon_chan = adev->dev.platform_data;
	cpu_reg->desc.name = "th1520-aon-cpu";
	cpu_reg->desc.of_match = "cpu-regulator";
	cpu_reg->desc.ops = &th1520_aon_cpu_regulator_ops;
	cpu_reg->desc.type = REGULATOR_VOLTAGE;
	cpu_reg->desc.owner = THIS_MODULE;
	cpu_reg->desc.n_voltages = ARRAY_SIZE(th1520_aon_cpu_voltages);

	config.dev = dev;
	config.driver_data = cpu_reg;

	rdev = devm_regulator_register(dev, &cpu_reg->desc, &config);
	if (IS_ERR(rdev))
		return dev_err_probe(dev, PTR_ERR(rdev),
				     "Failed to register CPU regulator\n");

	return 0;
}

static const struct auxiliary_device_id th1520_aon_regulator_id_table[] = {
	{ .name = "th1520_pm_domains.regulator" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, th1520_aon_regulator_id_table);

static struct auxiliary_driver th1520_aon_regulator_driver = {
	.driver = {
		.name = "th1520-aon-regulator",
	},
	.probe = th1520_aon_regulator_probe,
	.id_table = th1520_aon_regulator_id_table,
};
module_auxiliary_driver(th1520_aon_regulator_driver);

MODULE_AUTHOR("LoveSy <shana@zju.edu.cn>");
MODULE_DESCRIPTION("T-HEAD TH1520 AON CPU regulator driver");
MODULE_LICENSE("GPL");
