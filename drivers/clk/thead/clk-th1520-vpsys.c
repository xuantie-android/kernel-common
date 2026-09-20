// SPDX-License-Identifier: GPL-2.0
/*
 * T-Head TH1520 video-processing subsystem clock gates.
 *
 * The AP clock controller supplies the programmable VENC parent clock. The
 * video-processing subsystem has additional gates and dividers in VP_SYSREG_R
 * which must be enabled before accessing VC8000E or GC620.
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
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
#define TH1520_VPSYS_G2D_DIV_MASK	GENMASK(3, 0)
#define TH1520_VPSYS_G2D_CDE	BIT(4)

/* One-based divider; the hardware does not support divisors 1 or 2. */
static const struct clk_div_table th1520_g2d_div_table[] = {
	{ 3, 3 }, { 4, 4 }, { 5, 5 }, { 6, 6 }, { 7, 7 },
	{ 8, 8 }, { 9, 9 }, { 10, 10 }, { 11, 11 }, { 12, 12 },
	{ 13, 13 }, { 14, 14 }, { 15, 15 }, { 0, 0 },
};

static unsigned long th1520_g2d_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_divider *div = to_clk_divider(hw);
	u32 val = readl(div->reg) & TH1520_VPSYS_G2D_DIV_MASK;

	return divider_recalc_rate(hw, parent_rate, val, div->table, 0, 4);
}

static int th1520_g2d_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct clk_divider *div = to_clk_divider(hw);

	return divider_determine_rate(hw, req, div->table, 4, 0);
}

static int th1520_g2d_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct clk_divider *div = to_clk_divider(hw);
	unsigned long flags;
	u32 reg;
	int value;

	value = divider_get_val(rate, parent_rate, div->table, 4, 0);
	if (value < 0)
		return value;

	spin_lock_irqsave(div->lock, flags);
	reg = readl(div->reg);
	if ((reg & (TH1520_VPSYS_G2D_DIV_MASK | TH1520_VPSYS_G2D_CDE)) ==
	    (value | TH1520_VPSYS_G2D_CDE))
		goto unlock;

	/* VP divider updates require CDE low -> divider write -> CDE high. */
	reg &= ~TH1520_VPSYS_G2D_CDE;
	writel(reg, div->reg);
	udelay(1);
	reg = (reg & ~TH1520_VPSYS_G2D_DIV_MASK) | value;
	writel(reg, div->reg);
	udelay(1);
	writel(reg | TH1520_VPSYS_G2D_CDE, div->reg);
unlock:
	spin_unlock_irqrestore(div->lock, flags);
	return 0;
}

static const struct clk_ops th1520_g2d_div_ops = {
	.recalc_rate = th1520_g2d_recalc_rate,
	.determine_rate = th1520_g2d_determine_rate,
	.set_rate = th1520_g2d_set_rate,
};

static int th1520_vpsys_clk_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device *dev = &pdev->dev;
	spinlock_t *lock;
	struct clk_divider *g2d_div;
	void __iomem *reg;
	unsigned int i;
	int ret;

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

	g2d_div = devm_kzalloc(dev, sizeof(*g2d_div), GFP_KERNEL);
	if (!g2d_div)
		return -ENOMEM;
	g2d_div->reg = reg + TH1520_VPSYS_G2D_DIV_REG;
	g2d_div->lock = lock;
	g2d_div->table = th1520_g2d_div_table;
	g2d_div->width = 4;
	/* Never retune the shared VIDEO_PLL in response to a G2D request. */
	g2d_div->hw.init = CLK_HW_INIT("th1520-g2d-div", "video-pll-vco",
				     &th1520_g2d_div_ops, 0);
	ret = devm_clk_hw_register(dev, &g2d_div->hw);
	if (ret)
		return dev_err_probe(dev, ret,
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
