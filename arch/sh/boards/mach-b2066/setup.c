/*
 * arch/sh/boards/mach-b2066/setup.c
 *
 * Copyright (C) 2012 STMicroelectronics Limited
 * Author: Eric Chaloin <eric.chaloin@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/tm1668.h>
#include <linux/stm/platform.h>
#include <linux/stm/stx7108.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/emi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi_gpio.h>
#include <asm/irq-ilc.h>

/*
 * The Flash devices are configured according to the selected boot-mode:
 *
 *                                      boot-from-XXX
 * ----------------------------------------------------------------------------
 * Mode Pins                    NOR                      SPI
 * ----------------------------------------------------------------------------
 * SW1 1 (M2)                 OFF (W)                  ON  (E)
 *     2 (M3)                 ON  (E)                  OFF (W)
 *     1 (M4)                 OFF (W)                  ON  (E)
 *     2 (M5)                 ON  (E)                  OFF (W)
 *
 * Post-boot Access
 * ----------------------------------------------------------------------------
 * NOR                     EMIA (64MB)[1]           EMIB (8MB)  [2]
 * SPI                    SPI_PIO/FSM [3]           SPI_PIO/FSM [3]
 * ----------------------------------------------------------------------------
 *
 * Switch positions are given in terms of (N)orth, (E)ast, (S)outh, and (W)est,
 * when viewing the board with the LED display to the South and the power
 * connector to the North.
 *
 * [1] NOR Flash is connected to EMIA and EMIB.  Accesses via EMIB set the
 *     address line 'A26', imposing a 64MB offset.  With appropriately
 *     configured EMI banks (EMIA = 64MB and EMIB = 64MB) it would be possible
 *     to gain access to the entire 128MB NOR Flash.  However, as things stand
 *     (targetpack v12), EMIA is configured for 128MB, which limits access to
 *     the 64MB addressable range of a single EMI bank.  The code below detects
 *     the EMI bank setup and attempts to configure NOR Flash accordingly.
 *
 * [2] When boot-from-SPI is selected, access to NOR Flash is via EMIB.  As
 *     mentioned in [1], this imposes a 64MB offset from the start of NOR Flash.
 *     Furthermore, the current targetpack (v12), limits EMIB to 8MB only.
 *
 * [3] On stx7108 cut 2.0, the SPI-FSM Controller is used to access Serial
 *     Flash.  The controller is non-functional on stx7108 cut 1.0, so we must
 *     fall-back to a GPIO SPI bus and the generic "m25p80" driver.
 */


#define B2066_PIO_GMII0_NOTRESET stm_gpio(20, 0)
#define B2066_PIO_GMII1_NOTRESET stm_gpio(15, 4)
#define B2066_PIO_GMII1_PHY_CLKOUTNOTTX_CLK_SEL stm_gpio(19, 1)

/* All the power-related PIOs are pulled up, so "ON" by default,
 * there is no need to touch them unless one really wants to... */
#define B2066_PIO_POWER_ON_5V stm_gpio(21, 7)
#define B2066_PIO_POWER_ON_1V5LMI stm_gpio(22, 0)
#define B2066_PIO_POWER_ON_SATA stm_gpio(22, 2)
#define B2066_PIO_POWER_ON_SHUTDOWN stm_gpio(22, 3)

#define B2066_GPIO_SPI_HOLD stm_gpio(2, 2)
#define B2066_GPIO_SPI_WRITE_PRO stm_gpio(2, 3)


static void __init b2066_setup(char **cmdline_p)
{
	printk(KERN_INFO "STMicroelectronics b2066 board initialisation\n");

	stx7108_early_device_init();

	/* CN1 ("RS232") */
	stx7108_configure_asc(3, &(struct stx7108_asc_config) {
			.routing.asc3.txd = stx7108_asc3_txd_pio24_4,
			.routing.asc3.rxd = stx7108_asc3_rxd_pio24_5,
			/* First b2066 revision has RTS connected in
			 * a wrong way, therefore HW flow control
			 * is unusable */
#if 0
			.routing.asc3.cts = stx7108_asc3_cts_pio25_0,
			.routing.asc3.rts = stx7108_asc3_rts_pio24_7,
			.hw_flow_control = 1,
#endif
			.is_console = 1, });

	/* CN17 ("MODEM") */
	stx7108_configure_asc(1, &(struct stx7108_asc_config) {
			.hw_flow_control = 1, });
}



static struct platform_device b2066_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &(struct gpio_led_platform_data) {
		.num_leds = 3,
		.leds = (struct gpio_led[]) {
			{
				.name = "LD5 (GREEN)",
				.gpio = stm_gpio(6, 4),
				.default_trigger = "heartbeat",
			}, {
				.name = "LD4 (GREEN)",
				.gpio = stm_gpio(6, 5),
			}, {
				.name = "FP_POWER_ON",
				.gpio = stm_gpio(22, 1),
				.active_low = 1,
			},
		},
	},
};

static struct tm1668_key b2066_front_panel_keys[] = {
	{ 0x00001000, KEY_UP, "Up (SWF2)" },
	{ 0x00800000, KEY_DOWN, "Down (SWF7)" },
	{ 0x00008000, KEY_LEFT, "Left (SWF6)" },
	{ 0x00000010, KEY_RIGHT, "Right (SWF5)" },
	{ 0x00000080, KEY_ENTER, "Enter (SWF1)" },
	{ 0x00100000, KEY_ESC, "Escape (SWF4)" },
};

static struct tm1668_character b2066_front_panel_characters[] = {
	TM1668_7_SEG_HEX_DIGITS,
	TM1668_7_SEG_SEGMENTS,
	{ 'M', 0x037 },
	{ 'O', 0x03f },
	{ 'C', 0x039 },
	{ 'A', 0x077 },
};

static struct platform_device b2066_front_panel = {
	.name = "tm1668",
	.id = -1,
	.dev.platform_data = &(struct tm1668_platform_data) {
		.gpio_sclk = stm_gpio(2, 4),
		.gpio_dio = stm_gpio(2, 5),
		.gpio_stb = stm_gpio(2, 6),
		.config = tm1668_config_6_digits_12_segments,

		.keys_num = ARRAY_SIZE(b2066_front_panel_keys),
		.keys = b2066_front_panel_keys,
		.keys_poll_period = DIV_ROUND_UP(HZ, 5),

		.brightness = 8,
		.characters_num = ARRAY_SIZE(b2066_front_panel_characters),
		.characters = b2066_front_panel_characters,
		.text = "MOCA",
	},
};

#if 0
static int b2066_mii0_phy_reset(void *bus)
{
	gpio_set_value(B2066_PIO_GMII0_NOTRESET, 0);
	udelay(10000); /* 10 miliseconds is enough for everyone ;-) */
	gpio_set_value(B2066_PIO_GMII0_NOTRESET, 1);

	return 1;
}

static int b2066_mii1_phy_reset(void *bus)
{
	gpio_set_value(B2066_PIO_GMII1_NOTRESET, 0);
	udelay(10000); /* 10 miliseconds is enough for everyone ;-) */
	gpio_set_value(B2066_PIO_GMII1_NOTRESET, 1);

	return 1;
}
#endif

static void b2066_mi11_txclk_select(int txclk_250_not_25_mhz)
{
	gpio_set_value(B2066_PIO_GMII1_PHY_CLKOUTNOTTX_CLK_SEL,
			!!txclk_250_not_25_mhz);
}

/* MoCA is managed by using fixed link support */
static struct fixed_phy_status stmmac0_fixed_phy_status = {
	.link = 1,
	.speed = 100,
	.duplex = 1,
};

/* Switch is managed by using fixed link support */
static struct fixed_phy_status stmmac1_fixed_phy_status = {
	.link = 1,
	.speed = 1000,
	.duplex = 1,
};

/* NOR Flash */
static struct platform_device b2066_nor_flash = {
	.name		= "physmap-flash",
	.id		= -1,
	.num_resources	= 1,
	.resource	= (struct resource[]) {
		{
			.start		= 0x00000000,
			.end		= 128*1024*1024 - 1,
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

/* Serial Flash (see notes at top):
 *	cut 1.0: SPI GPIO bus + m25p80 driver
 *	cut 2.0: SPI FSM driver
 */
static struct platform_device b2066_serial_flash_bus = {
	.name           = "spi_gpio",
	.id             = 8,
	.num_resources  = 0,
	.dev.platform_data = &(struct spi_gpio_platform_data) {
		.num_chipselect = 1,
		.sck = stm_gpio(1, 6),
		.mosi = stm_gpio(2, 0),
		.miso = stm_gpio(2, 1),
	}
};

static struct mtd_partition b2066_serial_flash_parts[] = {
	{
		.name = "Serial Flash 1",
		.size = 0x00200000,
		.offset = 0,
	}, {
		.name = "Serial Flash 2",
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_NXTBLK,
	},
};

static struct spi_board_info b2066_serial_flash =  {
	.modalias       = "m25p80",
	.bus_num        = 8,
	.controller_data = (void *)stm_gpio(1, 7),
	.max_speed_hz   = 7000000,
	.mode           = SPI_MODE_3,
	.platform_data  = &(struct flash_platform_data) {
		.name = "n25p128",	/* Check device on specific board */
		.parts = b2066_serial_flash_parts,
		.nr_parts = ARRAY_SIZE(b2066_serial_flash_parts),
	},
};

static struct stm_plat_spifsm_data b2066_spifsm_flash = {
	.parts = b2066_serial_flash_parts,
	.nr_parts = ARRAY_SIZE(b2066_serial_flash_parts),
};



static struct platform_device *b2066_devices[] __initdata = {
	&b2066_leds,
	&b2066_front_panel,
	&b2066_nor_flash
};



static int __init b2066_device_init(void)
{
	struct sysconf_field *sc;
	unsigned long boot_mode;
	unsigned long nor_bank_base = 0;
	unsigned long nor_bank_size = 0;

	/* Configure Flash according to boot-device */
	if (cpu_data->cut_major >= 2) {
		sc = sysconf_claim(SYS_STA_BANK1, 3, 2, 6, "boot_mode");
		boot_mode = sysconf_read(sc);
	} else {
		sc = sysconf_claim(SYS_STA_BANK1, 3, 2, 5, "boot_mode");
		boot_mode = sysconf_read(sc);
		boot_mode |= 0x10;	/* use non-BCH boot encodings */
	}
	switch (boot_mode) {
	case 0x15:
		pr_info("Configuring FLASH for boot-from-NOR\n");
		nor_bank_base = emi_bank_base(0);
		nor_bank_size = 128*1024*1024;
		break;
	case 0x1a:
		pr_info("Configuring FLASH for boot-from-SPI\n");
		nor_bank_base = emi_bank_base(1);
		nor_bank_size = emi_bank_base(2) - nor_bank_base;
		if (nor_bank_size > 64*1024*1024)
			nor_bank_size = 64*1024*1024;
		break;
	default:
		BUG();
		break;
	}
	sysconf_release(sc);

	/* Update NOR Flash resources */
	if (b2066_nor_flash.resource[0].end > nor_bank_size - 1)
		b2066_nor_flash.resource[0].end = nor_bank_size - 1;
	b2066_nor_flash.resource[0].start += nor_bank_base;
	b2066_nor_flash.resource[0].end += nor_bank_base;

	gpio_request(B2066_PIO_GMII0_NOTRESET, "GMII0_notRESET");
	gpio_direction_output(B2066_PIO_GMII0_NOTRESET, 0);

	gpio_request(B2066_PIO_GMII1_NOTRESET, "GMII1_notRESET");
	gpio_direction_output(B2066_PIO_GMII1_NOTRESET, 0);

	gpio_request(B2066_PIO_GMII1_PHY_CLKOUTNOTTX_CLK_SEL,
			"GMII1_PHY_CLKOUTnotTX_CLK_SEL");
	gpio_direction_output(B2066_PIO_GMII1_PHY_CLKOUTNOTTX_CLK_SEL, 1);

	/* FRONTEND: CN18 ("I2C FRONTEND"), CN20 ("I2C FRONTEND MODULE") */
	stx7108_configure_ssc_i2c(1, NULL);
	/* ZIGBEE MODULE: U55 (SPZB260) */
	stx7108_configure_ssc_spi(2, &(struct stx7108_ssc_config) {
			.routing.ssc2.sclk = stx7108_ssc2_sclk_pio1_3,
			.routing.ssc2.mtsr = stx7108_ssc2_mtsr_pio1_4,
			.routing.ssc2.mrst = stx7108_ssc2_mrst_pio1_5, });
	/* PIO_HDMI: CN4 ("HDMI" via U8),
	 * BACKEND: U21 (EEPROM), U29 (STTS75 tempereature sensor),
	 * U26 (STV6440), CN10 ("GMII MODULE"), CN12 ("I2C BACKEND") */
	stx7108_configure_ssc_i2c(6, NULL);
	/* SYS - EEPROM & MII0 */
	stx7108_configure_ssc_i2c(5, NULL);

	stx7108_configure_lirc(&(struct stx7108_lirc_config) {
			.rx_mode = stx7108_lirc_rx_mode_ir, });

	/* J1 ("FAN Connector") */
	stx7108_configure_pwm(&(struct stx7108_pwm_config) {
			.out0_enabled = 1, });

	stx7108_configure_usb(0);
	stx7108_configure_usb(1);
	stx7108_configure_usb(2);

	stx7108_configure_miphy(&(struct stx7108_miphy_config) {
			.modes = (enum miphy_mode[2]) {
				SATA_MODE, SATA_MODE },
			});
	stx7108_configure_sata(0, &(struct stx7108_sata_config) { });
	stx7108_configure_sata(1, &(struct stx7108_sata_config) { });

	/* GMAC 0 + MoCA device */
	BUG_ON(fixed_phy_add(PHY_POLL, 1, &stmmac0_fixed_phy_status));
	stx7108_configure_ethernet(0, &(struct stx7108_ethernet_config) {
			.mode = stx7108_ethernet_mode_mii,
			.ext_clk = 1,
			.phy_bus = 0,
			.phy_addr = 1,
		});

	/* GMAC 1 + RTK8363 switch */
	BUG_ON(fixed_phy_add(PHY_POLL, 2, &stmmac1_fixed_phy_status));
	stx7108_configure_ethernet(1, &(struct stx7108_ethernet_config) {
			.mode = stx7108_ethernet_mode_rgmii_gtx,
			.ext_clk = 0,
			.phy_bus = 0,
			.txclk_select = b2066_mi11_txclk_select,
			.phy_addr = 2,
		});

	/* Serial Flash support depends on silicon cut */
	if (cpu_data->cut_major >= 2) {
		/* Use SPI-FSM Controller */
		stx7108_configure_spifsm(&b2066_spifsm_flash);
	} else {
		/* Use SPI-GPIO and generic 'm25p80' driver */
		gpio_request(B2066_GPIO_SPI_HOLD, "SPI_HOLD");
		gpio_direction_output(B2066_GPIO_SPI_HOLD, 1);
		gpio_request(B2066_GPIO_SPI_WRITE_PRO, "SPI_WRITE_PRO");
		gpio_direction_output(B2066_GPIO_SPI_WRITE_PRO, 1);

		platform_device_register(&b2066_serial_flash_bus);
		spi_register_board_info(&b2066_serial_flash, 1);
	}

	return platform_add_devices(b2066_devices,
			ARRAY_SIZE(b2066_devices));
}
arch_initcall(b2066_device_init);


static void __iomem *b2066_ioport_map(unsigned long port, unsigned int size)
{
	/* Shouldn't be here! */
	BUG();
	return NULL;
}

struct sh_machine_vector mv_b2066 __initmv = {
	.mv_name = "b2066",
	.mv_setup = b2066_setup,
	.mv_nr_irqs = NR_IRQS,
	.mv_ioport_map = b2066_ioport_map,
};
