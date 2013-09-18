/*
 * Copyright (C) 2011 STMicroelectronics Limited
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Code to handle the arch clocks on the STxH205.
 */

#include <linux/init.h>
#include <linux/stm/clk.h>

static char __initdata *sys_clks_on[] = {
	"CLK_A0_SH4_ICK",
	"CLK_A0_IC_REG_LP_ON",
	"CLK_A0_IC_STNOC",
	"CLK_A0_IC_TS_DMA",
	"CLK_A0_IC_IF",
	"CLK_A0_MASTER",
	"CLK_A1_IC_DDRCTRL"
};

int __init arch_clk_init(void)
{
	int ret, i;

	ret = plat_clk_init();
	if (ret)
		return ret;

	ret = plat_clk_alias_init();
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(sys_clks_on); ++i) {
		struct clk *clk = clk_get(NULL, sys_clks_on[i]);
		clk_enable(clk);
	}

	return ret;
}
