// SPDX-License-Identifier: GPL-2.0-only
/* Bounded LPi4A PWM0 backlight experiment. Not a production PWM driver.
 * TH1520 Peripheral Interface manual: FPOUT=1 makes the first PWM_FP
 * clocks high; INACTOUT=0 keeps a disabled channel low. PWM1 is never written.
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>

#define PWM_BASE 0xffec01c000ULL
#define PIN_BASE 0xffec007000ULL
#define CTRL 0x00
#define PERIOD 0x08
#define FIRST_PHASE 0x0c
#define STATUS 0x10
#define START BIT(0)
#define UPDATE BIT(2)
#define CONTINUOUS BIT(5)
#define FPOUT BIT(8)
#define INACTOUT BIT(9)
#define PIN_MUX 0x410
#define PIN_PAD 0x44
#define PIN_MUX_MASK GENMASK(11, 8)
#define PIN_PAD_MASK GENMASK(15, 0)

static unsigned int duration_ms = 3000;
module_param(duration_ms, uint, 0400);
MODULE_PARM_DESC(duration_ms, "Backlight duration: 1/2% up to 300000 ms, 20% up to 5000 ms");
static bool connect_output;
module_param(connect_output, bool, 0400);
MODULE_PARM_DESC(connect_output, "Explicitly connect PWM0 to backlight after checks; default false");
static bool hold_output;
module_param(hold_output, bool, 0400);
MODULE_PARM_DESC(hold_output, "Explicit 20% debugging mode: hold output until module unload");
static unsigned int probe_duty_permille = 10;
module_param(probe_duty_permille, uint, 0400);
MODULE_PARM_DESC(probe_duty_permille, "Internal probe duty 0..500; connected output permits 10, 20, or bounded 200");

static struct gpiod_lookup_table gpio_lookup = {
    .dev_id = "th1520-pwm0-diag",
    .table = {
        GPIO_LOOKUP("ffe7f38000.gpio", 2, "enable", GPIO_ACTIVE_HIGH),
        { },
    },
};

/* Retain ownership only for the explicitly requested continuous debug mode.
 * Module init completes normally; rmmod performs the same off/restore path
 * as a bounded test. Probe and failed-init cleanup remain immediate. */
static struct device *dev;
static struct gpio_desc *gpio;
static struct clk *clk;
static void __iomem *pwm, *pin;
static u32 saved[4], fan[4], mux, pad;
static bool region, enabled_clk, saved_regs, saved_pin, lookup_added;
static void pwm_diag_cleanup(int ret);

static int __init pwm_diag_init(void)
{
    struct device_node *np;
    struct of_phandle_args args = {.args_count = 1, .args = {51}};
    u32 cycles;
    u32 trace[24];
    u64 trace_ns[24], started;
    unsigned int high = 0, fast_high = 0, i;
    int ret = 0;

    if (!of_machine_is_compatible("sipeed,lichee-pi-4a")) return -ENODEV;
    if (!duration_ms || duration_ms > 300000) return -EINVAL;
    if (hold_output && (!connect_output || probe_duty_permille != 200))
        return -EINVAL;
    if (probe_duty_permille > 500 ||
        (connect_output && probe_duty_permille != 10 && probe_duty_permille != 20 &&
         probe_duty_permille != 200))
        return -EINVAL;
    if (!hold_output && connect_output && probe_duty_permille == 200 && duration_ms > 5000)
        return -EINVAL;
    if (!request_mem_region(PWM_BASE, 0x20, "th1520-pwm0-diag")) return -EBUSY;
    region = true;
    pwm = ioremap(PWM_BASE, 0x40); // PWM1 registers are read-only snapshots.
    pin = ioremap(PIN_BASE, 0x500);
    if (!pwm || !pin) { ret = -ENOMEM; goto out; }
    np = of_find_compatible_node(NULL, NULL, "thead,th1520-clk-ap");
    if (!np) { ret = -ENODEV; goto out; }
    args.np = np;
    clk = of_clk_get_from_provider(&args);
    of_node_put(np);
    if (IS_ERR(clk)) { ret = PTR_ERR(clk); goto out; }
    /* The bootloader's PWM1 fan may use a physically enabled clock without
     * a Linux enable reference. Do not gate that clock on test completion. */
    if (!__clk_is_enabled(clk)) {
        ret = clk_prepare_enable(clk);
        if (ret) goto out;
        enabled_clk = true;
    }
    if (readl(pwm + CTRL) & START) { ret = -EBUSY; goto out; }
    mux = readl(pin + PIN_MUX);
    pad = readl(pin + PIN_PAD);
    if (mux & PIN_MUX_MASK) { ret = -EBUSY; goto out; }
    dev = root_device_register("th1520-pwm0-diag");
    if (IS_ERR(dev)) { ret = PTR_ERR(dev); dev = NULL; goto out; }
    gpiod_add_lookup_table(&gpio_lookup);
    lookup_added = true;
    gpio = gpiod_get(dev, "enable", GPIOD_OUT_LOW);
    if (IS_ERR(gpio)) { ret = PTR_ERR(gpio); gpio = NULL; goto out; }
    saved_pin = true;
    for (i = 0; i < 4; ++i) {
        saved[i] = readl(pwm + i * 4);
        fan[i] = readl(pwm + 0x20 + i * 4);
    }
    saved_regs = true;
    /* RevyOS distinguishes the 24 MHz counter clock from the APB gate's
     * rate metadata; both share gate bit 18. Use the actual counter source. */
    cycles = 24000000 / 40000; // 40 kHz, inside MT9201's 20 kHz–1 MHz range.
    writel(CONTINUOUS | FPOUT, pwm + CTRL);
    writel(cycles, pwm + PERIOD);
    writel(cycles * probe_duty_permille / 1000, pwm + FIRST_PHASE);
    writel(CONTINUOUS | FPOUT | UPDATE, pwm + CTRL);
    usleep_range(10, 20);
    started = ktime_get_ns();
    writel(readl(pwm + CTRL) | START, pwm + CTRL);
    for (i = 0; i < ARRAY_SIZE(trace); ++i) {
        trace[i] = readl(pwm + STATUS);
        trace_ns[i] = ktime_get_ns() - started;
        udelay(5);
    }
    for (i = 0; i < ARRAY_SIZE(trace); ++i)
        pr_info("th1520-pwm0-diag: startup t=%llu ns status=%08x\n", trace_ns[i], trace[i]);
    msleep(2);
    pr_info("th1520-pwm0-diag: ctrl=%08x period=%u fp=%u status=%08x fan_ctrl=%08x fan_status=%08x\n",
            readl(pwm + CTRL), readl(pwm + PERIOD), readl(pwm + FIRST_PHASE),
            readl(pwm + STATUS), readl(pwm + 0x20), readl(pwm + 0x30));
    /* Compare fast and jittered polling while the pad is held GPIO low. */
    started = ktime_get_ns();
    for (i = 0; i < 20000; ++i)
        fast_high += !!(readl(pwm + STATUS) & BIT(9));
    pr_info("th1520-pwm0-diag: fast high=%u/20000 elapsed=%llu ns\n",
            fast_high, ktime_get_ns() - started);
    for (i = 0; i < 10000; ++i) {
        high += !!(readl(pwm + STATUS) & BIT(9));
        /* Vary intervals to avoid locking sampling to one PWM phase. */
        udelay(1 + (i * 7) % 17);
    }
    pr_info("th1520-pwm0-diag: period=%u first=%u high_samples=%u/10000 (pad still GPIO low)\n",
            cycles, cycles * probe_duty_permille / 1000, high);
    pr_info("th1520-pwm0-diag: post-sample status=%08x\n", readl(pwm + STATUS));
    if (!connect_output) {
        pr_info("th1520-pwm0-diag: disconnected probe only, no backlight pulse\n");
        goto out;
    }
    if (!high || high > max(500U, probe_duty_permille * 15) ||
        fast_high < probe_duty_permille * 10 ||
        fast_high > probe_duty_permille * 30) {
        ret = -ERANGE;
        goto out;
    }
    writel(readl(pin + PIN_PAD) & ~BIT(9), pin + PIN_PAD);
    writel((readl(pin + PIN_MUX) & ~PIN_MUX_MASK) | BIT(8), pin + PIN_MUX);
    if (hold_output) {
        pr_info("th1520-pwm0-diag: HOLD 20%% START, mux=%08x pad=%08x; rmmod to switch off\n",
                readl(pin + PIN_MUX), readl(pin + PIN_PAD));
        return 0;
    }
    pr_info("th1520-pwm0-diag: backlight %u permille pulse START, duration=%u ms\n",
            probe_duty_permille, duration_ms);
    /* Keep the test interruptible: signalling the insmod process turns the
     * output off through the same cleanup path, without rebooting the board. */
    if (msleep_interruptible(duration_ms)) ret = -EINTR;
out:
    pwm_diag_cleanup(ret);
    return ret;
}

static void pwm_diag_cleanup(int ret)
{
    unsigned int i;

    if (!region) return;
    if (saved_regs) writel(readl(pwm + CTRL) & ~(START | INACTOUT), pwm + CTRL);
    if (saved_pin) {
        writel(readl(pin + PIN_MUX) & ~PIN_MUX_MASK, pin + PIN_MUX);
        gpiod_set_value_cansleep(gpio, 0);
        gpiod_put(gpio);
        writel((readl(pin + PIN_PAD) & ~PIN_PAD_MASK) | (pad & PIN_PAD_MASK), pin + PIN_PAD);
        writel((readl(pin + PIN_MUX) & ~PIN_MUX_MASK) | (mux & PIN_MUX_MASK), pin + PIN_MUX);
    }
    if (lookup_added) gpiod_remove_lookup_table(&gpio_lookup);
    if (dev) root_device_unregister(dev);
    if (saved_regs) {
        for (i = 1; i < 4; ++i) writel(saved[i], pwm + i * 4);
        writel(saved[0], pwm + CTRL);
        for (i = 0; i < 4; ++i)
            if (readl(pwm + 0x20 + i * 4) != fan[i])
                pr_err("th1520-pwm0-diag: fan register changed unexpectedly at %x\n", 0x20 + i * 4);
    }
    if (enabled_clk) clk_disable_unprepare(clk);
    if (!IS_ERR_OR_NULL(clk)) clk_put(clk);
    if (pwm) iounmap(pwm);
    if (pin) iounmap(pin);
    if (region) release_mem_region(PWM_BASE, 0x20);
    region = enabled_clk = saved_regs = saved_pin = lookup_added = false;
    pwm = pin = NULL;
    dev = NULL;
    gpio = NULL;
    clk = NULL;
    pr_info("th1520-pwm0-diag: complete, backlight off, result=%d\n", ret);
}
static void __exit pwm_diag_exit(void) { pwm_diag_cleanup(0); }
module_init(pwm_diag_init);
module_exit(pwm_diag_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LPi4A bounded low-duty PWM0 backlight diagnostic");
