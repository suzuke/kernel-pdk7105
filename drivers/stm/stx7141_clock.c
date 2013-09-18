/*
 * Copyright (C) 2011 STMicroelectronics Limited
 *
 * May be copied or modified under the terms of the GNU General Public
 * License v2.  See linux/COPYING for more information.
 *
 * Code to handle the clock aliases on the STx7141.
 */

#include <linux/init.h>
#include <linux/stm/clk.h>

int __init plat_clk_alias_init(void)
{
	/* core clocks */
	clk_add_alias("cpu_clk", NULL, "CLKA_SH4_ICK", NULL);
	clk_add_alias("module_clk", NULL, "CLKA_IC_IF_100", NULL);
	clk_add_alias("comms_clk", NULL, "CLKA_IC_IF_100", NULL);

	/* EMI clock */
	clk_add_alias("emi_clk", NULL, "CLKA_EMI_MASTER", NULL);

	/* fdmas clocks */
	clk_add_alias("fdma_slim_clk", "stm-fdma.0", "CLKA_FDMA0", NULL);
	clk_add_alias("fdma_slim_clk", "stm-fdma.1", "CLKA_FDMA1", NULL);
	clk_add_alias("fdma_hi_clk", NULL, "CLKA_IC_IF_100",  NULL);
	clk_add_alias("fdma_low_clk", NULL, "CLKA_IC_IF_200", NULL);
	clk_add_alias("fdma_ic_clk", NULL, "CLKA_IC_IF_100", NULL);

	/* USB clocks */
	clk_add_alias("usb_48_clk", NULL, "CLKF_USB48", NULL);
	clk_add_alias("usb_ic_clk", NULL, "CLKA_IC_IF_100", NULL);
	clk_add_alias("usb_phy_clk", NULL, "CLKF_USB_PHY", NULL);

	clk_add_alias("pcm_player_clk", "snd_pcm_player.0", "CLKC_FS0_CH1",
		NULL);
	clk_add_alias("pcm_player_clk", "snd_pcm_player.1", "CLKC_FS0_CH2",
		NULL);
	clk_add_alias("pcm_player_clk", "snd_pcm_player.2", "CLKC_FS0_CH4",
		NULL);
	clk_add_alias("spdif_player_clk", NULL, "CLKC_FS0_CH3", NULL);

	return 0;
}
