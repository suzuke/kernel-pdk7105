/*
 * -------------------------------------------------------------------------
 * Copyright (C) 2011  STMicroelectronics
 * Author: Francesco M. Virlinzi  <francesco.virlinzi@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License V.2 ONLY.  See linux/COPYING for more information.
 *
 * ------------------------------------------------------------------------- */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/irqflags.h>
#include <linux/io.h>

#include <linux/stm/fli75xx.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/clk.h>
#include <linux/stm/wakeup_devices.h>
#include <linux/stm/synopsys_dwc_ddr32.h>

#include <asm/irq-ilc.h>

#include "stm_suspend.h"
#include <linux/stm/poke_table.h>


#define CGA				0xfde00000
#define CKGA_PLL(x)			((x) * 4)
#define CKGA_OSC_DIV_CFG(x)		(0x800 + (x) * 4)
#define CKGA_PLL0HS_DIV_CFG(x)		(0x900 + (x) * 4)
#define CKGA_PLL0LS_DIV_CFG(x)		(0xa10 + (x) * 4)
#define CKGA_PLL1_DIV_CFG(x)		(0xb00 + (x) * 4)
# define CKGA_PLL_BYPASS		(1 << 20)
# define CKGA_PLL_LOCK			(1 << 31)
#define CKGA_POWER_CFG			(0x010)
#define CKGA_CLKOPSRC_SWITCH_CFG(x)     (0x014 + ((x) * 0x10))

#define CLKA_ST40_HOST_ID		4
#define CLKA_IC_100_ID			14
#define CLKA_IC_150_ID			15
#define CLKA_IC_266_ID			0
#define CLKA_IC_200_ID			1

#define CLKA_ETH_PHY_ID			12 /* the pdf speaks on GMAC
					    * but @ 25 MHz it should be the Phy
					    */

/*
 * Fli75xx uses the Synopsys IP Dram Controller
 */
#define DDR0_BASE_REG	     0xFD320000	/* 32 */
#define DDR1_BASE_REG	     0xFD360000	/* 16 */

#define DDR_CLK_REG		0xfde80000

static struct clk *ca_ref_clk;
static struct clk *ca_pll_clk;
static struct clk *ca_ic_100_clk;
static unsigned long ca_ic_100_clk_rate;

static void __iomem *cga;
/* *************************
 * STANDBY INSTRUCTION TABLE
 * *************************
 */
static unsigned long fli7510_standby_table[] __cacheline_aligned = {
END_MARKER,
END_MARKER
};

/* *********************
 * MEM INSTRUCTION TABLE
 * *********************
 */
static unsigned long fli7510_mem_table[] __cacheline_aligned = {
synopsys_ddr32_in_self_refresh(DDR0_BASE_REG),

#if 0
OR32(DDR0_BASE_REG + DDR_PHY_IOCRV1, 1),
OR32(DDR0_BASE_REG + DDR_PHY_DXCCR, 1),
#endif

synopsys_ddr32_in_self_refresh(DDR1_BASE_REG),
#if 0
OR32(DDR1_BASE_REG + DDR_PHY_IOCRV1, 1),
OR32(DDR1_BASE_REG + DDR_PHY_DXCCR, 1),

OR32(DDR0_BASE_REG + DDR_PHY_PIR, 1 << 7),
OR32(DDR1_BASE_REG + DDR_PHY_PIR, 1 << 7),
#endif
END_MARKER,

#if 0
UPDATE32(DDR0_BASE_REG + DDR_PHY_PIR, ~(1 << 7), 0),
UPDATE32(DDR1_BASE_REG + DDR_PHY_PIR, ~(1 << 7), 0),
/*WHILE_NE32(SYS_BNK1_STA(5), 1, 1),*/

UPDATE32(DDR0_BASE_REG + DDR_PHY_IOCRV1, ~1, 0),
UPDATE32(DDR0_BASE_REG + DDR_PHY_DXCCR, ~1, 0),

UPDATE32(DDR1_BASE_REG + DDR_PHY_IOCRV1, ~1, 0),
UPDATE32(DDR1_BASE_REG + DDR_PHY_DXCCR, ~1, 0),
#endif


synopsys_ddr32_out_of_self_refresh(DDR0_BASE_REG),
synopsys_ddr32_out_of_self_refresh(DDR1_BASE_REG),

END_MARKER
};

static struct stm_wakeup_devices wkd;

static int fli75xx_suspend_begin(suspend_state_t state)
{
	pr_info("[STM][PM] Analyzing the wakeup devices\n");

	stm_check_wakeup_devices(&wkd);

	/*
	 * In case of 'standby' No change is done in the clock SubSystem
	 */
	if (state == PM_SUSPEND_STANDBY)
		return 0;

	ca_ic_100_clk_rate = clk_get_rate(ca_ic_100_clk);
	clk_set_parent(ca_ic_100_clk, ca_ref_clk);
	/* 15 MHz is safe to go... */
	clk_set_rate(ca_ic_100_clk, clk_get_rate(ca_ref_clk)/2);

	return 0;
}

static int fli75xx_suspend_core(suspend_state_t state, int suspending)
{
	static unsigned char *clka_pll0_div;
	static unsigned char *clka_pll1_div;
	static unsigned long *clka_switch_cfg;
	int i;
	long pwr = 0x3;		/* PLL_0/PLL_1 both OFF */
	long cfg_0, cfg_1;

	if (state == PM_SUSPEND_STANDBY)
		return 0;

	if (suspending)
		goto on_suspending;

	if (!clka_pll0_div) /* there was an error on suspending */
		return 0;
	/* Resuming... */
	iowrite32(0, cga + CKGA_POWER_CFG);
	while (!(ioread32(cga + CKGA_PLL(0)) & CKGA_PLL_LOCK))
		;
	while (!(ioread32(cga + CKGA_PLL(1)) & CKGA_PLL_LOCK))
		;

	/* applay the original parents */
	iowrite32(clka_switch_cfg[0], cga + CKGA_CLKOPSRC_SWITCH_CFG(0));
	iowrite32(clka_switch_cfg[1], cga + CKGA_CLKOPSRC_SWITCH_CFG(1));

	/* restore all the clocks settings */
	for (i = 0; i < 18; ++i) {
		iowrite32(clka_pll0_div[i], cga +
			((i < 4) ? CKGA_PLL0HS_DIV_CFG(i) :
				CKGA_PLL0LS_DIV_CFG(i)));
		iowrite32(clka_pll1_div[i], cga + CKGA_PLL1_DIV_CFG(i));
	}
	mdelay(10);
	pr_devel("[STM][PM] ClockGen A: restored\n");

	kfree(clka_pll0_div);
	kfree(clka_pll1_div);
	kfree(clka_switch_cfg);
	clka_switch_cfg = NULL;
	clka_pll0_div = clka_pll1_div = NULL;

	/* Restore ic_if_100 to previous rate */
	clk_set_parent(ca_ic_100_clk, ca_pll_clk);
	clk_set_rate(ca_ic_100_clk, ca_ic_100_clk_rate);

	return 0;


on_suspending:
	clka_pll0_div = kmalloc(sizeof(char) * 18, GFP_ATOMIC);
	clka_pll1_div = kmalloc(sizeof(char) * 18, GFP_ATOMIC);
	clka_switch_cfg = kmalloc(sizeof(long) * 2, GFP_ATOMIC);

	if (!clka_pll0_div || !clka_pll1_div || !clka_switch_cfg)
		goto error;

	/* save the original settings */
	clka_switch_cfg[0] = ioread32(cga + CKGA_CLKOPSRC_SWITCH_CFG(0));
	clka_switch_cfg[1] = ioread32(cga + CKGA_CLKOPSRC_SWITCH_CFG(1));

	for (i = 0; i < 18; ++i) {
		clka_pll0_div[i] = ioread32(cga +
			((i < 4) ? CKGA_PLL0HS_DIV_CFG(i) :
				CKGA_PLL0LS_DIV_CFG(i)));
		clka_pll1_div[i] = ioread32(cga + CKGA_PLL1_DIV_CFG(i));
		}
	pr_devel("[STM][PM] ClockGen A: saved\n");
	mdelay(10);

	/* to avoid the system is to much slow all
	 * the clocks are scaled @ 30 MHz
	 * the final setting is done in the tables
	 */
	for (i = 0; i < 18; ++i)
		iowrite32(0, cga + CKGA_OSC_DIV_CFG(i));

	/* almost all the clocks off, except some critical ones */
	cfg_0 = 0xffffff3f;	/*
				 * Not clear why:
				 * -  the Audio Dec. Clock can not disabled
				 */
	cfg_0 &= ~(0x3 << (2 * CLKA_ST40_HOST_ID));
	cfg_0 &= ~(0x3 << (2 * CLKA_IC_100_ID));
	cfg_0 &= ~(0x3 << (2 * CLKA_IC_150_ID));
	cfg_1 = 0xff;
	cfg_1 &= ~(0x3 << (2 * CLKA_IC_200_ID));

#if 0
	if (wkd.eth_phy_can_wakeup) {
		/* Pll_0 on */
		pwr &= ~1;
		/* eth_phy_clk under pll0 */
		cfg_0 &= ~(0x3 << (2 * CLKA_ETH_PHY_ID));
		cfg_0 |= (0x1 << (2 * CLKA_ETH_PHY_ID));
	}

	if (wkd.hdmi_can_wakeup) {
		/* Pll_1 on */
		pwr &= ~2;
		/* ic_if_100 under pll1 */
		cfg_0 &=  ~(0x3 << (2 * CLKA_IC_IF_100_ID));
		cfg_0 |= (0x2 << (2 * CLKA_IC_IF_100_ID));
	} else {
		if (!wkd.lirc_can_wakeup)
			clk_set_rate(ca_ic_100_clk,
				    clk_get_rate(ca_ref_clk)/32);
	}

#endif
	iowrite32(cfg_0, cga + CKGA_CLKOPSRC_SWITCH_CFG(0));
	iowrite32(cfg_1, cga + CKGA_CLKOPSRC_SWITCH_CFG(1));
	iowrite32(pwr, cga + CKGA_POWER_CFG);
	return 0;

error:
	kfree(clka_pll1_div);
	kfree(clka_pll0_div);
	kfree(clka_switch_cfg);

	clka_switch_cfg = NULL;
	clka_pll0_div = clka_pll1_div = NULL;

	return -ENOMEM;
}

static int fli75xx_suspend_pre_enter(suspend_state_t state)
{
	return fli75xx_suspend_core(state, 1);
}

static int fli75xx_suspend_post_enter(suspend_state_t state)
{
	return fli75xx_suspend_core(state, 0);
}

static int fli75xx_evttoirq(unsigned long evt)
{
	return ((evt < 0x400) ? ilc2irq(evt) : evt2irq(evt));
}

static struct stm_platform_suspend_t fli75xx_suspend __cacheline_aligned = {

	.ops.begin = fli75xx_suspend_begin,

	.evt_to_irq = fli75xx_evttoirq,
	.pre_enter = fli75xx_suspend_pre_enter,
	.post_enter = fli75xx_suspend_post_enter,

	.stby_tbl = (unsigned long)fli7510_standby_table,
	.stby_size = DIV_ROUND_UP(ARRAY_SIZE(fli7510_standby_table) *
			sizeof(long), L1_CACHE_BYTES),

	.mem_tbl = (unsigned long)fli7510_mem_table,
	.mem_size = DIV_ROUND_UP(ARRAY_SIZE(fli7510_mem_table) * sizeof(long),
			L1_CACHE_BYTES),

};

static int __init fli75xx_suspend_setup(void)
{

	struct sysconf_field *sc[2];
	int i;

	sc[0] = sysconf_claim(CKG_DDR_CTL_PLL_DDR_FREQ, 0, 0, "PM");
	sc[1] = sysconf_claim(CKG_DDR_STATUS_PLL_DDR, 0, 0, "PM");

	for (i = ARRAY_SIZE(sc)-1; i; --i)
		if (!sc[i])
			goto error;

	cga = ioremap(CGA, 0x1000);

	ca_ic_100_clk = clk_get(NULL, "comms_clk");
	ca_pll_clk = ca_ic_100_clk->parent;
	ca_ref_clk = ca_pll_clk->parent;

	return stm_suspend_register(&fli75xx_suspend);

error:

	for (i = ARRAY_SIZE(sc)-1; i; --i)
		if (sc[i])
			sysconf_release(sc[i]);

	pr_err("[STM][PM] Error on Standby initialization\n");
	return 0;
}

module_init(fli75xx_suspend_setup);
