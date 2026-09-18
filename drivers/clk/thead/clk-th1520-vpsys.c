// SPDX-License-Identifier: GPL-2.0
/*
 * T-Head TH1520 video-processing subsystem clock gates.
 *
 * The AP clock controller supplies the programmable VENC parent clock. The
 * video-processing subsystem has additional gates and dividers in VP_SYSREG_R
 * which must be enabled before accessing VC8000E or GC620.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/thead,th1520-clk-vpsys.h>

struct th1520_vpsys_gate {
	const char *name;
	u8 bit;
};

static const struct th1520_vpsys_gate th1520_venc_gates[] = {
	[TH1520_VPSYS_VENC_ACLK] = { "th1520-venc-aclk", 7 },
	[TH1520_VPSYS_VENC_CCLK] = { "th1520-venc-cclk", 8 },
	[TH1520_VPSYS_VENC_PCLK] = { "th1520-venc-pclk", 9 },
};

#define TH1520_VPSYS_GATE_REG		0x00
#define TH1520_VPSYS_G2D_DIV_REG	0x10
#define TH1520_VPSYS_G2D_GATE_BIT	3

static int th1520_vpsys_clk_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device *dev = &pdev->dev;
	spinlock_t *lock;
	struct clk_hw *g2d_div;
	void __iomem *reg;
	unsigned int i;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	lock = devm_kzalloc(dev, sizeof(*lock), GFP_KERNEL);
	if (!lock)
		return -ENOMEM;
	spin_lock_init(lock);

	clk_data = devm_kzalloc(dev,
				 struct_size(clk_data, hws, TH1520_VPSYS_CLK_END),
				 GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;
	clk_data->num = TH1520_VPSYS_CLK_END;

	for (i = 0; i < ARRAY_SIZE(th1520_venc_gates); i++) {
		const struct th1520_vpsys_gate *gate = &th1520_venc_gates[i];

		clk_data->hws[i] = devm_clk_hw_register_gate(dev, gate->name,
							    "venc",
							    CLK_SET_RATE_PARENT,
							    reg, gate->bit, 0,
							    lock);
		if (IS_ERR(clk_data->hws[i]))
			return dev_err_probe(dev, PTR_ERR(clk_data->hws[i]),
					     "failed to register %s\n",
					     gate->name);
	}

	g2d_div = devm_clk_hw_register_divider(dev, "th1520-g2d-div",
					       "video-pll", CLK_SET_RATE_PARENT,
					       reg + TH1520_VPSYS_G2D_DIV_REG,
					       0, 4, CLK_DIVIDER_ONE_BASED,
					       lock);
	if (IS_ERR(g2d_div))
		return dev_err_probe(dev, PTR_ERR(g2d_div),
				     "failed to register G2D divider\n");

	clk_data->hws[TH1520_VPSYS_G2D_CCLK] =
		devm_clk_hw_register_gate(dev, "th1520-g2d-cclk",
					  "th1520-g2d-div", CLK_SET_RATE_PARENT,
					  reg + TH1520_VPSYS_GATE_REG,
					  TH1520_VPSYS_G2D_GATE_BIT, 0,
					  lock);
	if (IS_ERR(clk_data->hws[TH1520_VPSYS_G2D_CCLK]))
		return dev_err_probe(dev,
			PTR_ERR(clk_data->hws[TH1520_VPSYS_G2D_CCLK]),
			"failed to register G2D clock\n");

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   clk_data);
}

static const struct of_device_id th1520_vpsys_clk_of_match[] = {
	{ .compatible = "thead,th1520-clk-vpsys" },
	{ }
};
MODULE_DEVICE_TABLE(of, th1520_vpsys_clk_of_match);

static struct platform_driver th1520_vpsys_clk_driver = {
	.probe = th1520_vpsys_clk_probe,
	.driver = {
		.name = "th1520-vpsys-clk",
		.of_match_table = th1520_vpsys_clk_of_match,
	},
};
module_platform_driver(th1520_vpsys_clk_driver);

MODULE_AUTHOR("LoveSy <shana@zju.edu.cn>");
MODULE_DESCRIPTION("T-Head TH1520 video-processing clock gates");
MODULE_LICENSE("GPL");
