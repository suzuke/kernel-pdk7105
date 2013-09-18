/*
 * arch/sh/boards/mach-b2067/setup.c
 *
 * Copyright (C) 2012 STMicroelectronics Limited
 * Author: Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/phy.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <linux/tm1668.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/stm/platform.h>
#include <linux/stm/stxh205.h>
#include <linux/stm/sysconf.h>
#include <asm/irq-ilc.h>

#define B2067_GPIO_POWER_ON_ETH		stm_gpio(2, 5)
#define B2067_MII1_TXER			stm_gpio(0, 4)

static void __init b2067_setup(char **cmdline_p)
{
	printk(KERN_INFO "STMicroelectronics B2067 board initialisation\n");

	stxh205_early_device_init();

	/*
	 * UART_SW: On board DB9 connector JM5
	 */
	stxh205_configure_asc(STXH205_ASC(10), &(struct stxh205_asc_config) {
			.hw_flow_control = 0,
			.is_console = 1, });

	/*
	 * Header JB4 (with flow control) and JK1 (no flow control)
	 */
	stxh205_configure_asc(STXH205_ASC(1), &(struct stxh205_asc_config) {
			.hw_flow_control = 0,
		});
}

static struct platform_device b2067_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &(struct gpio_led_platform_data) {
		.num_leds = 1,
		.leds = (struct gpio_led[]) {
			{
				.name = "RED",
				.default_trigger = "heartbeat",
				.gpio = stm_gpio(3, 1),
			},
			/*
			 * Its not clear what's happening here, but it
			 * appears as if STxH238 has three balls connected
			 * on the b2067 as:
			 *   PIO32 - 7108_LMI_RET#
			 *   PIO33 - LED_GREEN
			 *   PIO32B - LED_GREEN
			 * The net effects appers to be a short between
			 * LMI_RET and LED GREEN which means any attemmpt
			 * to use the LED causes the system to crash.
			 *
			 * This has been fixed on B2067 revB, so if you know
			 * you're on a rev B uncomment the following block
			 * and change .num_leds above to 2.
			 * {
			 *	.name = "GREEN",
			 *	.gpio = stm_gpio(3, 3),
			 * },
			 */
		},
	},
};

static struct tm1668_key b2067_front_panel_keys[] = {
	{ 0x00001000, KEY_UP, "Up (SWF2)" },
	{ 0x00800000, KEY_DOWN, "Down (SWF7)" },
	{ 0x00008000, KEY_LEFT, "Left (SWF6)" },
	{ 0x00000010, KEY_RIGHT, "Right (SWF5)" },
	{ 0x00000080, KEY_OK, "Menu/OK (SWF1)" },
	{ 0x00100000, KEY_BACK, "Back (SWF4)" },
	{ 0x80000000, KEY_TV, "DOXTV (SWF9)" },
};

static struct tm1668_character b2067_front_panel_characters[] = {
	TM1668_7_SEG_HEX_DIGITS,
	TM1668_7_SEG_SEGMENTS,
	TM1668_7_SEG_LETTERS
};

static struct platform_device b2067_front_panel = {
	.name = "tm1668",
	.id = -1,
	.dev.platform_data = &(struct tm1668_platform_data) {
		.gpio_dio = stm_gpio(15, 4),
		.gpio_sclk = stm_gpio(14, 7),
		.gpio_stb = stm_gpio(14, 4),
		.config = tm1668_config_6_digits_12_segments,

		.keys_num = ARRAY_SIZE(b2067_front_panel_keys),
		.keys = b2067_front_panel_keys,
		.keys_poll_period = DIV_ROUND_UP(HZ, 5),

		.brightness = 8,
		.characters_num = ARRAY_SIZE(b2067_front_panel_characters),
		.characters = b2067_front_panel_characters,
		.text = "H238",
	},
};

/* Serial Flash */
static struct stm_plat_spifsm_data b2067_serial_flash =  {
	.name		= "n25q256",
	.nr_parts	= 2,
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
		/*
		 * Reset signal can be routed to U4 and U12 by fitting
		 * RM52 (default is DNF).
		 * Note rev A boards misconnected reset and hold signals.
		 */
		.reset_signal = 0,
	},
};

/* NAND Flash */
static struct stm_nand_bank_data b2067_nand_flash = {
	.csn		= 1,	/* Controlled by JF3 */
	.options	= NAND_NO_AUTOINCR | NAND_USE_FLASH_BBT,
	.nr_partitions	= 2,
	.partitions	= (struct mtd_partition []) {
		{
			.name	= "NAND Flash 1",
			.offset	= 0,
			.size	= 0x00800000
		}, {
			.name	= "NAND Flash 2",
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

static int b2067_phy_reset(void *bus)
{
	/*
	 * IC+ IP101 datasheet specifies 10mS low period and device usable
	 * 2.5mS after rising edge. However experimentally it appear
	 * 10mS is required for reliable functioning.
	 */
	gpio_set_value(B2067_GPIO_POWER_ON_ETH, 0);
	mdelay(10);
	gpio_set_value(B2067_GPIO_POWER_ON_ETH, 1);
	mdelay(10);

	return 1;
}

static struct stmmac_mdio_bus_data stmmac_mdio_bus = {
	.bus_id = 0,
	.phy_reset = b2067_phy_reset,
	.phy_mask = 0,
	.probed_phy_irq = ILC_IRQ(25), /* MDINT */
};

static struct platform_device *b2067_devices[] __initdata = {
	&b2067_leds,
	&b2067_front_panel,
};

static int __init device_init(void)
{
	/* The "POWER_ON_ETH" line should be rather called "PHY_RESET",
	 * but it isn't... ;-) */
	gpio_request(B2067_GPIO_POWER_ON_ETH, "POWER_ON_ETH");
	gpio_direction_output(B2067_GPIO_POWER_ON_ETH, 0);

	stxh205_configure_ethernet(&(struct stxh205_ethernet_config) {
			.mode = stxh205_ethernet_mode_mii,
			.ext_clk = 1,
			.phy_bus = 0,
			.phy_addr = -1,
			.mdio_bus_data = &stmmac_mdio_bus,
		});

	/* PHY IRQ has to be triggered LOW */
	set_irq_type(ILC_IRQ(25), IRQ_TYPE_LEVEL_LOW);

	stxh205_configure_miphy(&(struct stxh205_miphy_config){
			.mode = SATA_MODE,
			.iface = UPORT_IF,
		});
	stxh205_configure_sata();

	stxh205_configure_usb(0);

	stxh205_configure_usb(1);

	/*
	 * SSC1: FE/DEMO
	 * U14: LNBH26PQR, STxH238: J_SCL/SDA (internal demod), JB6
	 */
	stxh205_configure_ssc_i2c(STXH205_SSC(1), &(struct stxh205_ssc_config) {
			.routing.ssc1.sclk = stxh205_ssc1_sclk_pio12_0,
			.routing.ssc1.mtsr = stxh205_ssc1_mtsr_pio12_1, });
	/*
	 * SSC3: SYS
	 * UQ1: STV6440, UK2: M24256 (EEPROM), JQ3
	 * Also JK2 (front panel) on rev A only
	 */
	stxh205_configure_ssc_i2c(STXH205_SSC(3), &(struct stxh205_ssc_config) {
			.routing.ssc3.sclk = stxh205_ssc3_sclk_pio15_5,
			.routing.ssc3.mtsr = stxh205_ssc3_mtsr_pio15_6, });
	/*
	 * SSC11: HDMI
	 * UG1: HDMI2C1 -> HDMI
	 */
	stxh205_configure_ssc_i2c(STXH205_SSC(11), NULL);

	stxh205_configure_lirc(&(struct stxh205_lirc_config) {
#ifdef CONFIG_LIRC_STM_UHF
			.rx_mode = stxh205_lirc_rx_mode_uhf, });
#else
			.rx_mode = stxh205_lirc_rx_mode_ir, });
#endif

	stxh205_configure_pwm(&(struct stxh205_pwm_config) {
			/*
			 * PWM10 is connected to 12V->1.2V power supply
			 * for "debug purposes". Enable at your own risk!
			 */
			.out10_enabled = 0 });

	stxh205_configure_mmc(&(struct stxh205_mmc_config) {
			.emmc = 0,
			.no_mmc_boot_data_error = 1,
		});

	stxh205_configure_nand(&(struct stm_nand_config) {
			.driver = stm_nand_flex,
			.nr_banks = 1,
			.banks = &b2067_nand_flash,
			.rbn.flex_connected = 1,});

	stxh205_configure_spifsm(&b2067_serial_flash);

	return platform_add_devices(b2067_devices,
			ARRAY_SIZE(b2067_devices));
}
arch_initcall(device_init);

static void __iomem *b2067_ioport_map(unsigned long port, unsigned int size)
{
	/* If we have PCI then this should never be called because we
	 * are using the generic iomap implementation. If we don't
	 * have PCI then there are no IO mapped devices, so it still
	 * shouldn't be called. */
	BUG();
	return NULL;
}

struct sh_machine_vector mv_b2067 __initmv = {
	.mv_name = "b2067",
	.mv_setup = b2067_setup,
	.mv_nr_irqs = NR_IRQS,
	.mv_ioport_map = b2067_ioport_map,
};

#if defined(CONFIG_HIBERNATION_ON_MEMORY)

#include "../../kernel/cpu/sh4/stm_hom.h"

static int b2067_board_freeze(void)
{
	gpio_set_value(B2067_GPIO_POWER_ON_ETH, 0);
	return 0;
}

static int b2067_board_defrost(void)
{
	b2067_phy_reset(NULL);
	return 0;
}

static struct stm_hom_board b2067_hom = {
	.freeze = b2067_board_freeze,
	.restore = b2067_board_defrost,
};

static int __init b2067_hom_register(void)
{
	return stm_hom_board_register(&b2067_hom);
}

module_init(b2067_hom_register);
#endif
