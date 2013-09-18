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

#include <linux/stm/stxh205.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/clk.h>
#include <linux/stm/wakeup_devices.h>

#include <asm/irq-ilc.h>

#include "stm_suspend.h"
#include <linux/stm/poke_table.h>
#include <linux/stm/synopsys_dwc_ddr32.h>

#define DDR3SS_REG		0xfde50000

#define SYSCFG_BANK_1		0xFDA50000
#define SYSCFG_BANK_1_REG(x)	(SYSCFG_BANK_1 + (x - 100) * 4)
#define SYSTEM_CONFIG_169	SYSCFG_BANK_1_REG(169)
#define SYSTEM_STATUS_154	SYSCFG_BANK_1_REG(154)
#define SYSTEM_STATUS_160	SYSCFG_BANK_1_REG(160)

#define CKGA_PLL_CFG(pll_id, reg_id)	(0x4 * (reg_id) + (pll_id) * 0xc)
#define   CKGA_PLL_CFG_LOCK		(1 << 31)
#define CKGA_POWER_CFG			0x018
#define CKGA_CLKOPSRC_SWITCH_CFG	0x01C
#define CKGA_CLKOPSRC_SWITCH_CFG2	0x020

#define CLK_A1_BASE		0xFDAB8000
#define CLK_A1_ETH_PHY		0xa
#define CLK_A1_GMAC		0xe

static void __iomem *cga0;
static void __iomem *cga1;

static struct clk *a0_ref_clk;
static struct clk *a0_ic_lp_on_clk;

static struct clk *a1_pll0_hs_clk;
static struct clk *a1_ddr_clk;
static struct clk *a1_pll1_ls_clk;
static struct clk *a1_eth_phy_clk;

static struct stm_wakeup_devices stxh205_wkd;

/* *************************
 * STANDBY INSTRUCTION TABLE
 * *************************
 */
static unsigned long stxh205_standby_table[] __cacheline_aligned = {
END_MARKER,

END_MARKER
};

/* *********************
 * MEM INSTRUCTION TABLE
 * *********************
 */
static unsigned long stxh205_mem_table[] __cacheline_aligned = {
synopsys_ddr32_in_self_refresh(DDR3SS_REG),
synopsys_ddr32_phy_standby_enter(DDR3SS_REG),

OR32(CLK_A1_BASE + CKGA_POWER_CFG, 3),
 /* END. */
END_MARKER,

/*
 * Turn-on DDR clock:
 * The DDR subsystem uses the channel coming from A1.HS_0
 * this means there is _no_ ClockGen_D...
 *
 * - turn on the A1.PLLs
 * - wait both the PLLs are locked
 */
UPDATE32(CLK_A1_BASE + CKGA_POWER_CFG, 0, ~0x3),
WHILE_NE32(SYSTEM_STATUS_160, 3, 3),

synopsys_ddr32_phy_standby_exit(DDR3SS_REG),
synopsys_ddr32_out_of_self_refresh(DDR3SS_REG),

END_MARKER
};

static int stxh205_suspend_begin(suspend_state_t state)
{
	pr_info("[STM][PM] Analyzing the wakeup devices\n");

	stm_check_wakeup_devices(&stxh205_wkd);

	return 0;
}

static int stxh205_suspend_core(suspend_state_t state, int suspending)
{
	static long *switch_cfg;
	unsigned long cfg_0_0, cfg_0_1, cfg_1_0, cfg_1_1;
	unsigned long pwr_0, pwr_1;
	int i;

	if (suspending)
		goto on_suspending;

	if (!switch_cfg)
		return 0;

	iowrite32(0, cga0 + CKGA_POWER_CFG);
	/*
	 * A1.PLLs could be arelady enabled in the mem_poketable!
	 * Due to the DDR_Ctrl subsystem,,,
	 * But in case of standby it's re-done here
	 */
	iowrite32(0, cga1 + CKGA_POWER_CFG);

	/* Wait A0.Plls lock */
	for (i = 0; i < 2; ++i)
		while (!(ioread32(cga0 + CKGA_PLL_CFG(i, 1))
			 & CKGA_PLL_CFG_LOCK))
				;

	/* Wait A1.Plls lock */
	while ((ioread32(SYSTEM_STATUS_160) & 3) != 3)
		;

	/* apply the original parents */
	iowrite32(switch_cfg[0], cga0 + CKGA_CLKOPSRC_SWITCH_CFG);
	iowrite32(switch_cfg[1], cga0 + CKGA_CLKOPSRC_SWITCH_CFG2);
	iowrite32(switch_cfg[2], cga1 + CKGA_CLKOPSRC_SWITCH_CFG);
	iowrite32(switch_cfg[3], cga1 + CKGA_CLKOPSRC_SWITCH_CFG2);

	kfree(switch_cfg);

	switch_cfg = NULL;

	pr_debug("[STM][PM] ClockGens A: restored\n");
	return 0;

on_suspending:

	cfg_0_0 = 0xf00fffcc;
	cfg_0_1 = 0x3ffff;
	cfg_1_0 = 0xffffffff;
	cfg_1_1 = 0xfffffff;
	pwr_0 = pwr_1 = 0x3;

	switch_cfg = kmalloc(sizeof(long) * 2 * 2, GFP_ATOMIC);

	if (!switch_cfg)
		goto error;

	/* Save the original parents */
	switch_cfg[0] = ioread32(cga0 + CKGA_CLKOPSRC_SWITCH_CFG);
	switch_cfg[1] = ioread32(cga0 + CKGA_CLKOPSRC_SWITCH_CFG2);
	switch_cfg[2] = ioread32(cga1 + CKGA_CLKOPSRC_SWITCH_CFG);
	switch_cfg[3] = ioread32(cga1 + CKGA_CLKOPSRC_SWITCH_CFG2);

/* A0 */
	iowrite32(cfg_0_0, cga0 + CKGA_CLKOPSRC_SWITCH_CFG);
	iowrite32(cfg_0_1, cga0 + CKGA_CLKOPSRC_SWITCH_CFG2);
	iowrite32(pwr_0, cga0 + CKGA_POWER_CFG);

/* A1 */
	/*
	 * WOL is totally based on A1. To be working it needs:
	 * - eth.phy_clk and eth1.mac_clk enabled
	 * - eth.phy_clk @ 25 MHz
	 */
	if (stxh205_wkd.eth_phy_can_wakeup) {
		int pll_id = (a1_pll1_ls_clk == clk_get_parent(a1_eth_phy_clk) ?
			2 : 1);
		cfg_1_0 &= ~(0x3 << (CLK_A1_ETH_PHY * 2));
		cfg_1_0 &= ~(0x3 << (CLK_A1_GMAC * 2));
		cfg_1_0 |= (pll_id << (CLK_A1_ETH_PHY * 2));
		cfg_1_0 |= (pll_id << (CLK_A1_GMAC * 2));
		pwr_1 &= ~pll_id;
	}

/*
 * The DDR subsystem uses an clock-channel coming direclty from A1.
 * This mean we have to be really carefully in the A1 management
 *
 * Here check the system isn't goint to break the DDR Subsystem
 */
	/*
	 * maintain the DDR_clk parent as it is!
	 */
	cfg_1_0 &= ~0x3;
	cfg_1_0 |= (switch_cfg[2] & 0x3);

	iowrite32(cfg_1_0, cga1 + CKGA_CLKOPSRC_SWITCH_CFG);
	iowrite32(cfg_1_1, cga1 + CKGA_CLKOPSRC_SWITCH_CFG2);
	/* maintain the PLL (DDR is using) enabled */
	pwr_1 &= ~((clk_get_parent(a1_ddr_clk) == a1_pll0_hs_clk) ? 1 : 2);

	iowrite32(pwr_1, cga1 + CKGA_POWER_CFG);

	pr_debug("[STM][PM] ClockGens A: saved\n");
	return 0;
error:
	kfree(switch_cfg);

	switch_cfg = NULL;

	return -ENOMEM;
}

static int stxh205_suspend_pre_enter(suspend_state_t state)
{
	return stxh205_suspend_core(state, 1);
}

static int stxh205_suspend_post_enter(suspend_state_t state)
{
	return stxh205_suspend_core(state, 0);
}

static int stxh205_evttoirq(unsigned long evt)
{
	return ((evt == 0xa00) ? ilc2irq(evt) : evt2irq(evt));
}

static struct stm_platform_suspend_t stxh205_suspend __cacheline_aligned = {
	.ops.begin = stxh205_suspend_begin,

	.evt_to_irq = stxh205_evttoirq,
	.pre_enter = stxh205_suspend_pre_enter,
	.post_enter = stxh205_suspend_post_enter,

	.stby_tbl = (unsigned long)stxh205_standby_table,
	.stby_size = DIV_ROUND_UP(ARRAY_SIZE(stxh205_standby_table) *
			sizeof(long), L1_CACHE_BYTES),

	.mem_tbl = (unsigned long)stxh205_mem_table,
	.mem_size = DIV_ROUND_UP(ARRAY_SIZE(stxh205_mem_table)
			* sizeof(long), L1_CACHE_BYTES),

};

static int __init stxh205_suspend_setup(void)
{
	struct sysconf_field *sc[2];
	int i;

	sc[0] = sysconf_claim(SYSCONF(169), 2, 2, "PM");
	/* ClockGen_D.Pll lock status */
	sc[1] = sysconf_claim(SYSCONF(154), 2, 2, "PM");


	for (i = 0; i < ARRAY_SIZE(sc); ++i)
		if (!sc[i])
			goto error;

	a0_ic_lp_on_clk = clk_get(NULL, "CLK_A0_IC_REG_LP_ON");
	a0_ref_clk = clk_get(NULL, "CLK_A0_REF");

	a1_pll0_hs_clk = clk_get(NULL, "CLK_A1_PLL0HS");
	a1_ddr_clk = clk_get(NULL, "CLK_A1_IC_DDRCTRL");
	a1_pll1_ls_clk = clk_get(NULL, "CLK_A1_PLL1LS");
	a1_eth_phy_clk = clk_get(NULL, "CLK_A1_ETH_PHY");

	if (a0_ref_clk == ERR_PTR(-ENOENT) ||
	    a0_ic_lp_on_clk == ERR_PTR(-ENOENT) ||
	    a1_pll0_hs_clk == ERR_PTR(-ENOENT) ||
	    a1_pll0_hs_clk == ERR_PTR(-ENOENT) ||
	    a1_pll1_ls_clk == ERR_PTR(-ENOENT) ||
	    a1_eth_phy_clk == ERR_PTR(-ENOENT))
		goto error;

	cga0 = ioremap_nocache(0xfde98000, 0x1000);
	cga1 = ioremap_nocache(0xfdab8000, 0x1000);

	return stm_suspend_register(&stxh205_suspend);

error:
	pr_err("[STM][PM] Error to acquire the sysconf registers\n");
	for (i = 0; i < ARRAY_SIZE(sc); ++i)
		if (sc[i])
			sysconf_release(sc[i]);

	return -EBUSY;
}

module_init(stxh205_suspend_setup);
