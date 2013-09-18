/*
 * arch/sh/boards/mach-ngb7167/setup.c
 *
 * Copyright (C) 2011 STMicroelectronics Limited
 * Author: Nunzio Raciti (nunzio.raciti@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * STMicroelectronics STi7167-NGB support.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/lirc.h>
#include <linux/gpio.h>
#include <linux/phy.h>
#include <linux/tm1668.h>
#include <linux/stm/platform.h>
#include <linux/stm/stx7105.h>
#include <linux/stm/pci-glue.h>
#include <linux/stm/emi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/phy_fixed.h>
#include <asm/irq-ilc.h>


#define NGB7167_PIO_PHY_RESET stm_gpio(15, 5)
#define NGB7167_GPIO_FLASH_WP stm_gpio(12, 6)


static void __init ngb7167_setup(char **cmdline_p)
{
	printk(KERN_INFO "STMicroelectronics STi7167-NGB"
			" board initialisation\n");

	stx7105_early_device_init();

	stx7105_configure_asc(2, &(struct stx7105_asc_config) {
			.routing.asc2 = stx7105_asc2_pio4,
			.hw_flow_control = 1,
			.is_console = 1, });
}

static struct tm1668_key ngb7167_front_panel_keys[] = {
	{ 0x00001000, KEY_UP, "Up (SWF2)" },
	{ 0x00800000, KEY_DOWN, "Down (SWF7)" },
	{ 0x00008000, KEY_LEFT, "Left (SWF6)" },
	{ 0x00000010, KEY_RIGHT, "Right (SWF5)" },
	{ 0x00000080, KEY_ENTER, "Enter (SWF1)" },
	{ 0x00100000, KEY_ESC, "Escape (SWF4)" },
};

static struct tm1668_character ngb7167_front_panel_characters[] = {
	TM1668_7_SEG_HEX_DIGITS,
	TM1668_7_SEG_HEX_DIGITS_WITH_DOT,
	TM1668_7_SEG_SEGMENTS,
};

static struct platform_device ngb7167_front_panel = {
	.name = "tm1668",
	.id = -1,
	.dev.platform_data = &(struct tm1668_platform_data) {
		.gpio_dio = stm_gpio(11, 2),
		.gpio_sclk = stm_gpio(11, 3),
		.gpio_stb = stm_gpio(11, 4),
		.config = tm1668_config_6_digits_12_segments,

		.keys_num = ARRAY_SIZE(ngb7167_front_panel_keys),
		.keys = ngb7167_front_panel_keys,
		.keys_poll_period = DIV_ROUND_UP(HZ, 5),

		.brightness = 8,
		.characters_num = ARRAY_SIZE(ngb7167_front_panel_characters),
		.characters = ngb7167_front_panel_characters,
		.text = "7167",
	},
};

static struct fixed_phy_status stmmac0_fixed_phy_status = {
	.link = 1,
	.speed = 100,
	.duplex = 1,
};

/* NOR Flash */
static struct platform_device ngb7167_nor_flash = {
	.name		= "physmap-flash",
	.id		= -1,
	.num_resources	= 1,
	.resource	= (struct resource[]) {
		{
			.start		= 0x00000000,
			.end		= 64 * 1024 * 1024 - 1,
			.flags		= IORESOURCE_MEM,
		}
	},
	.dev.platform_data = &(struct physmap_flash_data) {
		.width		= 2,
		.set_vpp	= NULL,
		.nr_parts	= 3,
		.parts		= (struct mtd_partition []) {
			{
				.name = "NOR Flash 1",
				.size = 0x00080000,
				.offset = 0x00000000,
			}, {
				.name = "NOR Flash 2",
				.size = 0x00200000,
				.offset = MTDPART_OFS_NXTBLK,
			}, {
				.name = "NOR Flash 3",
				.size = MTDPART_SIZ_FULL,
				.offset = MTDPART_OFS_NXTBLK,
			}
		},
	},
};

static struct platform_device *ngb7167_devices[] __initdata = {
	&ngb7167_front_panel,
	&ngb7167_nor_flash,
};

static int __init ngb7167_device_init(void)
{
	struct sysconf_field *sc;

	sc = sysconf_claim(SYS_STA, 1, 15, 16, "boot_device");
	switch (sysconf_read(sc)) {
	case 0x0:
		/* Boot-from-NOR */
		pr_info("Configuring FLASH for boot-from-NOR\n");
		break;
	default:
		BUG();
		break;
	}
	sysconf_release(sc);

	/* I2C_xxxA - HDMI */
	stx7105_configure_ssc_i2c(0, &(struct stx7105_ssc_config) {
			.routing.ssc0.sclk = stx7105_ssc0_sclk_pio2_2,
			.routing.ssc0.mtsr = stx7105_ssc0_mtsr_pio2_3, });
	/* I2C_xxxB - optional connection with EU3 (LAN9303) */
	stx7105_configure_ssc_i2c(1, &(struct stx7105_ssc_config) {
			.routing.ssc1.sclk = stx7105_ssc1_sclk_pio2_5,
			.routing.ssc1.mtsr = stx7105_ssc1_mtsr_pio2_6, });
	/* I2C_xxxC - JF1, UD1H, UD12 (EEPROM), */
	stx7105_configure_ssc_i2c(2, &(struct stx7105_ssc_config) {
			.routing.ssc2.sclk = stx7105_ssc2_sclk_pio3_4,
			.routing.ssc2.mtsr = stx7105_ssc2_mtsr_pio3_5, });
	/* I2C_xxxD - U1 (STV6440), U3 (STV0297E), JF3  */
	stx7105_configure_ssc_i2c(3, &(struct stx7105_ssc_config) {
			.routing.ssc3.sclk = stx7105_ssc3_sclk_pio3_6,
			.routing.ssc3.mtsr = stx7105_ssc3_mtsr_pio3_7, });
	/* JD6  (8 pins connector) */
	stx7105_configure_usb(0, &(struct stx7105_usb_config) {
			.ovrcur_mode = stx7105_usb_ovrcur_active_low,
			.pwr_enabled = 1,
			.routing.usb0.ovrcur = stx7105_usb0_ovrcur_pio4_4,
			.routing.usb0.pwr = stx7105_usb0_pwr_pio4_5, });
	/* JD7 USB */
	stx7105_configure_usb(1, &(struct stx7105_usb_config) {
			.ovrcur_mode = stx7105_usb_ovrcur_active_low,
			.pwr_enabled = 1,
			.routing.usb1.ovrcur = stx7105_usb1_ovrcur_pio4_6,
			.routing.usb1.pwr = stx7105_usb1_pwr_pio4_7, });

	gpio_request(NGB7167_PIO_PHY_RESET, "eth_phy_reset");
	gpio_direction_output(NGB7167_PIO_PHY_RESET, 1);

	/* Use the FIXED PHY link support to start-up the SMSC9303
	 * switch 10/100. */
	BUG_ON(fixed_phy_add(PHY_POLL, 1, &stmmac0_fixed_phy_status));
	stx7105_configure_ethernet(0, &(struct stx7105_ethernet_config) {
			.mode = stx7105_ethernet_mode_mii,
			.ext_clk = 0,
			.phy_bus = 0,
			.phy_addr = 1,
		});

	/* RemoteControl configure */
	stx7105_configure_lirc(&(struct stx7105_lirc_config) {
			.rx_mode = stx7105_lirc_rx_mode_ir,
			.tx_enabled = 0,
			.tx_od_enabled = 0, });

	/* Audio Pins configure */
	stx7105_configure_audio(&(struct stx7105_audio_config) {
			.spdif_player_output_enabled = 1, });

	/*  Disable Flash WP.  */
	gpio_request(NGB7167_GPIO_FLASH_WP, "FLASH_WP");
	gpio_direction_output(NGB7167_GPIO_FLASH_WP, 1);

	/*e-sata configure*/
	stx7105_configure_sata(0);

	return platform_add_devices(ngb7167_devices,
			ARRAY_SIZE(ngb7167_devices));
}
arch_initcall(ngb7167_device_init);

static void __iomem *ngb7167_ioport_map(unsigned long port, unsigned int size)
{
	BUG();
	return (void __iomem *)CCN_PVR;
}

struct sh_machine_vector mv_ngb7167 __initmv = {
	.mv_name		= "ngb7167",
	.mv_setup		= ngb7167_setup,
	.mv_nr_irqs		= NR_IRQS,
	.mv_ioport_map		= ngb7167_ioport_map
};
