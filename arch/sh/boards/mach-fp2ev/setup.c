/*
 * arch/sh/boards/mach-fp2ev/setup.c
 *
 * Copyright (C) 2012 STMicroelectronics Limited
 * Authors: 	Angus Clark <Angus.Clark@st.com>
 * 		Chris Smith <chris.smith@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * STMicroelectronics Freeman Lite Development Board support.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/phy.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/physmap.h>
#include <linux/stm/emi.h>
#include <linux/stm/platform.h>
#include <linux/stm/fli75xx.h>
#include <asm/irq-ilc.h>
#include <sound/stm.h>


#define FP2EV_PIO_RESET_OUTN stm_gpio(11, 6)

static void __init fp2ev_setup(char **cmdline_p)
{
	printk(KERN_INFO "STMicroelectronics Freeman Premier 2 "
	       "Evaluation Board initialisation\n");

	fli75xx_early_device_init();

	/* CNB2 ("UART 1" connector) */
	fli75xx_configure_asc(0, &(struct fli75xx_asc_config) {
			.hw_flow_control = 1,
			.is_console = 1, });

	/* CNB4 ("UART 2" connector) */
	fli75xx_configure_asc(1, &(struct fli75xx_asc_config) {
			.hw_flow_control = 0,
			.is_console = 0, });

	/* CNB22 ("UART 3" connector) */
	fli75xx_configure_asc(2, &(struct fli75xx_asc_config) {
			.hw_flow_control = 0,
			.is_console = 0, });
}



static struct platform_device fp2ev_led_df1 = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &(struct gpio_led_platform_data) {
		.num_leds = 1,
		.leds = (struct gpio_led[]) {
			{
				.name = "DF1 orange",
				.default_trigger = "heartbeat",
				.gpio = stm_gpio(8, 5),
			},
		},
	},
};



static struct stmmac_mdio_bus_data stmmac_mdio_bus = {
	.bus_id = 0,
	.phy_mask = 0,
};

/* Serial Flash */
static struct stm_plat_spifsm_data fp2ev_spifsm_flash = {
	.name = "m25px64",
	.nr_parts = 2,
	.parts = (struct mtd_partition []) {
		{
			.name = "Serial Flash 1",
			.size = 0x00080000,
			.offset = 0,
		}, {
			.name = "Serial Flash 2",
			.size = MTDPART_SIZ_FULL,
			.offset = MTDPART_OFS_NXTBLK,
		},
	},
	.capabilities = {
		/* Capabilities may be overriden by SoC configuration */
		.dual_mode = 1,
		.quad_mode = 1,
	},

};

/* NAND Flash */
static struct stm_nand_bank_data fp2ev_nand_flash = {
	.csn		= 0,
	.options	= NAND_NO_AUTOINCR | NAND_USE_FLASH_BBT,
	.nr_partitions	= 3,
	.partitions	= (struct mtd_partition []) {
		{
			.name	= "NAND Flash 1",
			.offset	= 0,
			.size 	= 0x00800000
		}, {
			.name	= "NAND Flash 2",
			.offset = MTDPART_OFS_NXTBLK,
			.size	= 0x01000000
		}, {
			.name	= "NAND Flash 3",
			.offset = MTDPART_OFS_NXTBLK,
			.size	= MTDPART_SIZ_FULL
		},
	},
	.timing_data	=  &(struct stm_nand_timing_data) {
		.sig_setup	= 50,		/* times in ns */
		.sig_hold	= 50,
		.CE_deassert	= 0,
		.WE_to_RBn	= 100,
		.wr_on		= 10,
		.wr_off		= 40,
		.rd_on		= 10,
		.rd_off		= 40,
		.chip_delay	= 30,		/* in us */
	},
};

static struct platform_device *fp2ev_devices[] __initdata = {
	&fp2ev_led_df1,
};

static int __init fp2ev_device_init(void)
{
	/* This is a board-level reset line, which goes to the
	 * Ethernet PHY, audio amps & number of extension connectors */
	if (gpio_request(FP2EV_PIO_RESET_OUTN, "RESET_OUTN") == 0) {
		gpio_direction_output(FP2EV_PIO_RESET_OUTN, 0);
		udelay(10000); /* 10ms is the Ethernet PHY requirement */
		gpio_set_value(FP2EV_PIO_RESET_OUTN, 1);
	} else {
		printk(KERN_ERR "fp2ev: Failed to claim RESET_OUTN PIO!\n");
	}

	fli75xx_configure_pwm(&(struct fli75xx_pwm_config) {
			.out0_enabled = 1,
#if 0
			/* Connected to DF1 LED, currently used as a
			 * GPIO-controlled one (see above) */
			.out1_enabled = 1,
#endif
#if 0
			/* PWM driver doesn't support these yet... */
			.out2_enabled = 1,
			.out3_enabled = 1,
#endif
			});

	fli75xx_configure_ssc_i2c(0);
	fli75xx_configure_ssc_i2c(1);
	fli75xx_configure_ssc_i2c(2);
	fli75xx_configure_ssc_i2c(3);
	/* Leave SSC4 unconfigured, using SPI-FSM for Serial Flash */

	fli75xx_configure_spifsm(&fp2ev_spifsm_flash);

	fli75xx_configure_nand(&(struct stm_nand_config) {
			.driver = stm_nand_bch,
			.nr_banks = 1,
			.banks = &fp2ev_nand_flash,
			.rbn.flex_connected = 1});

	fli75xx_configure_usb(0, &(struct fli75xx_usb_config) {
			.ovrcur_mode = fli75xx_usb_ovrcur_active_low, });

	fli75xx_configure_ethernet(&(struct fli75xx_ethernet_config) {
			.mode = fli75xx_ethernet_mode_mii,
			.ext_clk = 0,
			.phy_bus = 0,
			.phy_addr = 1,
			.mdio_bus_data = &stmmac_mdio_bus,
		});

	fli75xx_configure_lirc();

	fli75xx_configure_mmc();

	fli75xx_configure_audio(&(struct fli75xx_audio_config) {
			.pcm_player_0_output_mode =
					fli75xx_pcm_player_0_output_8_channels,
			.spdif_player_output_enabled = 1, });

	return platform_add_devices(fp2ev_devices,
			ARRAY_SIZE(fp2ev_devices));
}
arch_initcall(fp2ev_device_init);



static void __iomem *fp2ev_ioport_map(unsigned long port, unsigned int size)
{
	/* If we have PCI then this should never be called because we
	 * are using the generic iomap implementation. If we don't
	 * have PCI then there are no IO mapped devices, so it still
	 * shouldn't be called. */
	BUG();
	return (void __iomem *)CCN_PVR;
}

struct sh_machine_vector mv_fp2ev __initmv = {
	.mv_name	= "fp2ev",
	.mv_setup	= fp2ev_setup,
	.mv_nr_irqs	= NR_IRQS,
	.mv_ioport_map	= fp2ev_ioport_map,
};
