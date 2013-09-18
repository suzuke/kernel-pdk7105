/*
 * Copyright (C) 2011 STMicroelectronics Limited
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Code to handle the clock aliases on the STxH205.
 */

#include <linux/init.h>
#include <linux/stm/clk.h>

int __init plat_clk_alias_init(void)
{

	/* core clocks */
	clk_add_alias("cpu_clk", NULL, "CLK_A0_SH4_ICK", NULL);
	clk_add_alias("module_clk", NULL, "CLK_A0_IC_REG_LP_ON", NULL);
	clk_add_alias("comms_clk", NULL, "CLK_A0_IC_REG_LP_ON", NULL);
	clk_add_alias("sbc_comms_clk", NULL, "CLK_SYSIN", NULL);

	/* EMI clock */
	clk_add_alias("emi_clk", NULL, "CLK_A0_SYS_EMISS", NULL);

	/* fdmas clocks */
	clk_add_alias("fdma_slim_clk", "stm-fdma.0",
		"CLK_A1_SLIM_FDMA_0", NULL);
	clk_add_alias("fdma_slim_clk", "stm-fdma.1",
		"CLK_A1_SLIM_FDMA_1", NULL);
	clk_add_alias("fdma_hi_clk", NULL, "CLK_A0_IC_TS_DMA",  NULL);
	clk_add_alias("fdma_low_clk", NULL, "CLK_A0_IC_REG_LP_ON", NULL);
	clk_add_alias("fdma_ic_clk", NULL, "CLK_A0_IC_REG_LP_ON", NULL);

	/* SDHCI clocks */
	clk_add_alias(NULL, "sdhci.0", "CLK_A1_CARD",  NULL);

	/* USB clocks */
	clk_add_alias("usb_48_clk", NULL, "CLK_B_CLOCK48", NULL);
	clk_add_alias("usb_ic_clk", NULL, "CLK_A0_IC_IF", NULL);
	/* usb_phy_clk got from USBPhy tree currently unsupported in the LLA */

	/* STM-MAC clocks */
	clk_add_alias("stmmac_clk", NULL, "CLK_A1_IC_GMAC", NULL);
	clk_add_alias("stmmac_phy_clk", NULL, "CLK_A1_ETH_PHY", NULL);

	/* Sata/PCIe clocks */
	clk_add_alias("sata_clk", NULL, "CLK_A0_IC_REG_LP_OFF", NULL);
	clk_add_alias("pcie_clk", NULL, "CLK_A0_IC_REG_LP_OFF", NULL);

	/* LPC */
	clk_add_alias("lpc_clk", NULL, "CLK_C_LPM", NULL);
	return 0;
}
