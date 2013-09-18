/*
 * Copyright (C) 2010 STMicroelectronics Limited
 * Author: Francesco Virlinzi <francesco.virlinzi@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License v2.  See linux/COPYING for more information.
 *
 * Code to handle the clock aliases on the Fli75xx.
 */

#include <linux/init.h>
#include <linux/stm/clk.h>

int __init plat_clk_alias_init(void)
{
	/* core clocks */
	clk_add_alias("cpu_clk", NULL, "CLKA_ST40_HOST", NULL);
	clk_add_alias("module_clk", NULL, "CLKA_IC_100", NULL);
	clk_add_alias("comms_clk", NULL, "CLKA_IC_100", NULL);

	/* EMI clock */
	clk_add_alias("emi_clk", NULL, "CLKA_EMI", NULL);

	/* fdmas clocks */
	clk_add_alias("fdma_slim_clk", NULL, "CLKA_FDMA", NULL);
	clk_add_alias("fdma_hi_clk", NULL, "CLKA_IC_100",  NULL);
	clk_add_alias("fdma_low_clk", NULL, "CLKA_IC_200", NULL);
	clk_add_alias("fdma_ic_clk", NULL, "CLKA_IC_100", NULL);

	/* USB clocks */
/*
 *	usb_phy_clk, usb_48_clk provided in the Clockgen_south:
 *	not currently covered
 *	Reference: ADCD 'FLI7510_DS_C7510' p 199
 */
	clk_add_alias("usb_ic_clk", NULL, "CLKA_IC_100", NULL);

	/* LPC clock */
/*
 *	clk_lpc provided in the Clockgen_south
 *	It should be the rtc_clk @ 32,768 Khz
 */
	/* ALSA clocks */
	clk_add_alias("pcm_player_clk", "snd_pcm_player.0", "CLKC_FS_DEC_1",
		NULL);
	clk_add_alias("pcm_player_clk", "snd_pcm_player.1", "CLKC_FS_DEC_2",
		NULL);
	clk_add_alias("pcm_player_clk", "snd_pcm_player.2", "CLKC_FS_DEC_2",
		NULL);
	clk_add_alias("pcm_player_clk", "snd_pcm_player.3", "CLKC_FS_DEC_2",
		NULL);
	clk_add_alias("pcm_player_clk", "snd_pcm_player.4", "CLKC_FS_DEC_2",
		NULL);
	clk_add_alias("spdif_player_clk", NULL, "CLKC_FS_FREE_RUN", NULL);

	return 0;
}
