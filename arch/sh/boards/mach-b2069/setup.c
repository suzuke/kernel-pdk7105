/*
 * arch/sh/boards/st/mach-b2069/setup.c
 *
 * Copyright (C) 2012 STMicroelectronics Limited
 * Author: Suresh Keshri (suresh.keshri@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * STMicroelectronics STx7111 KC board support.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/lirc.h>
#include <linux/gpio.h>
#include <linux/phy.h>
#include <linux/input.h>
#include <linux/stm/platform.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/stm/stx7111.h>
#include <linux/stm/emi.h>
#include <linux/stm/pci-glue.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <asm/irq-ilc.h>


#define KC7111_PIO_PHY_RESET stm_gpio(1, 6)

static void __init kc7111_setup(char** cmdline_p)
{
	printk(KERN_INFO "STMicroelectronics STx7111 KC initialisation\n");

	stx7111_early_device_init();

	stx7111_configure_asc(2, &(struct stx7111_asc_config) {
			.hw_flow_control = 0,
			.is_console = 1, });

}

static struct platform_device kc7111_leds = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &(struct gpio_led_platform_data) {
		.num_leds = 1,
		.leds = (struct gpio_led[]) {
			{
				.name = "HB red",
				.default_trigger = "heartbeat",
				.gpio = stm_gpio(3, 7),
			},
		},
	},
};

static struct gpio_keys_button kc7111_buttons[] = {
	{
		.code = BTN_0,
		.gpio = stm_gpio(6, 4),
		.desc = "SW1",
	},
	{
		.code = BTN_1,
		.gpio = stm_gpio(6, 5),
		.desc = "SW2",
	},
	{
		.code = BTN_2,
		.gpio = stm_gpio(6, 6),
		.desc = "SW3",
	},
};

static struct gpio_keys_platform_data kc7111_button_data = {
	.buttons = kc7111_buttons,
	.nbuttons = ARRAY_SIZE(kc7111_buttons),
};

static struct platform_device kc7111_button_device = {
	.name = "gpio-keys",
	.id = -1,
	.num_resources = 0,
	.dev = {
		.platform_data = &kc7111_button_data,
	}
};

static struct spi_board_info kc7111_serial_flash_board_info =  {
	.modalias       = "m25p80",
	.bus_num	= 0,
	.max_speed_hz   = 7000000,
	.chip_select	= stm_gpio(6, 7),
	.mode		= SPI_MODE_3,
	.platform_data  = &(struct flash_platform_data) {
		.name = "m25p80",
		.type = "m25p64",
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
	},
};

static int kc7111_phy_reset(void *bus)
{
	gpio_set_value(KC7111_PIO_PHY_RESET, 0);
	udelay(100);
	gpio_set_value(KC7111_PIO_PHY_RESET, 1);

	return 1;
}

static struct stmmac_mdio_bus_data stmmac_mdio_bus = {
	.bus_id = 0,
	.phy_reset = kc7111_phy_reset,
	.phy_mask = 0,
};

static struct platform_device *kc7111_devices[] __initdata = {
	&kc7111_leds,
	&kc7111_button_device,
};

static int __init kc7111_devices_init(void)
{
	struct sysconf_field *sc;
	unsigned long nor_bank_base = 0;
	unsigned long nor_bank_size = 0;

	/* Configure Flash according to boot-device */
	sc = sysconf_claim(SYS_STA, 1, 16, 17, "boot_device");
	switch (sysconf_read(sc)) {
	case 0x0:
		/* Boot-from-NOR: */
		pr_info("NOR FLASH not available on board\n");
		break;
	case 0x1:
		/* Boot-from-NAND */
		pr_info("NAND FLASH not available on board\n");
		break;
	case 0x2:
		/* Boot-from-SPI */
		pr_info("Configuring FLASH for boot-from-SPI\n");
		/* NOR mapped to EMIB, with physical offset of 0x06000000! */
		nor_bank_base = emi_bank_base(1);
		nor_bank_size = emi_bank_base(2) - nor_bank_base;
		break;
	default:
		BUG();
		break;
	}
	sysconf_release(sc);

	spi_register_board_info(&kc7111_serial_flash_board_info, 1);
	stx7111_configure_pwm(&(struct stx7111_pwm_config) {
				.out0_enabled = 1,
				.out1_enabled = 0,
				});

	stx7111_configure_ssc_spi(0, NULL);
	stx7111_configure_ssc_i2c(1, NULL);
	stx7111_configure_ssc_i2c(2, NULL);
	stx7111_configure_ssc_i2c(3, NULL);

	stx7111_configure_usb(&(struct stx7111_usb_config) {
				.invert_ovrcur = 1,
				});

	gpio_request(KC7111_PIO_PHY_RESET, "eth_phy_reset");
	gpio_direction_output(KC7111_PIO_PHY_RESET, 1);

	stx7111_configure_ethernet(&(struct stx7111_ethernet_config) {
				.mode = stx7111_ethernet_mode_mii,
				.ext_clk = 0,
				.phy_bus = 0,
				.phy_addr = 0,
				.mdio_bus_data = &stmmac_mdio_bus,
	      });

	stx7111_configure_lirc(&(struct stx7111_lirc_config) {
				.rx_mode = stx7111_lirc_rx_mode_ir,
				.tx_enabled = 1,
				.tx_od_enabled = 0,
				});


	return platform_add_devices(kc7111_devices,
				    ARRAY_SIZE(kc7111_devices));
}
arch_initcall(kc7111_devices_init);

static void __iomem *kc7111_ioport_map(unsigned long port, unsigned int size)
{
	/*
	 * If we have PCI then this should never be called because we
	 * are using the generic iomap implementation. If we don't
	 * have PCI then there are no IO mapped devices, so it still
	 * shouldn't be called.
	 */
	BUG();
	return (void __iomem *)CCN_PVR;
}

static void __init kc7111_init_irq(void)
{
}

struct sh_machine_vector mv_kc7111 __initmv = {
	.mv_name		= "STx7111 KC",
	.mv_setup		= kc7111_setup,
	.mv_nr_irqs		= NR_IRQS,
	.mv_init_irq		= kc7111_init_irq,
	.mv_ioport_map		= kc7111_ioport_map,
	STM_PCI_IO_MACHINE_VEC
};
