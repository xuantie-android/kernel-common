// SPDX-License-Identifier: GPL-2.0
/*
 * Early watchdog restart fallback for the T-Head TH1520.
 *
 * The normal TH1520 restart path is provided by the AON RPC auxiliary
 * driver.  That driver is not necessarily available when an early initcall
 * panics, so keep the same direct WDT0 reset sequence registered from the
 * architecture initcall level.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/reboot.h>

#define TH1520_RST_REQ_EN_0		0xfffff44140ULL
#define TH1520_WDT0_BASE		0xffefc30000ULL
#define TH1520_WDT_CR			0x0
#define TH1520_WDT_TORR			0x4
#define TH1520_WDT0_SYS_RST_REQ		BIT(8)

static void __iomem *th1520_rst_req_en;
static void __iomem *th1520_wdt0;

static int th1520_wdt_restart(struct notifier_block *nb,
			      unsigned long action, void *data)
{
	u32 value = readl(th1520_rst_req_en);

	writel(value | TH1520_WDT0_SYS_RST_REQ, th1520_rst_req_en);
	writel(1, th1520_wdt0 + TH1520_WDT_CR);
	writel(1, th1520_wdt0 + TH1520_WDT_TORR);
	mdelay(1000);

	return NOTIFY_DONE;
}

static struct notifier_block th1520_wdt_restart_nb = {
	.notifier_call = th1520_wdt_restart,
	.priority = 128,
};

static int __init th1520_wdt_restart_init(void)
{
	int ret;

	if (!of_machine_is_compatible("thead,th1520"))
		return 0;

	th1520_rst_req_en = ioremap(TH1520_RST_REQ_EN_0, sizeof(u32));
	th1520_wdt0 = ioremap(TH1520_WDT0_BASE, 8);
	if (!th1520_rst_req_en || !th1520_wdt0) {
		pr_err("TH1520: failed to map early watchdog restart registers\n");
		if (th1520_rst_req_en)
			iounmap(th1520_rst_req_en);
		if (th1520_wdt0)
			iounmap(th1520_wdt0);
		return -ENOMEM;
	}

	ret = register_restart_handler(&th1520_wdt_restart_nb);
	if (ret) {
		pr_err("TH1520: failed to register early watchdog restart: %d\n",
		       ret);
		iounmap(th1520_rst_req_en);
		iounmap(th1520_wdt0);
	}

	return ret;
}
arch_initcall(th1520_wdt_restart_init);
