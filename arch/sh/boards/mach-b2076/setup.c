/*
 * arch/sh/boards/mach-b2076/setup.c
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
#include <linux/leds.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/stm/platform.h>
#include <linux/stm/stxh205.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/pci-glue.h>
#include <asm/irq-ilc.h>

#define B2076_MII1_NOTRESET		stm_gpio(3, 0)
#define B2076_MII1_TXER		stm_gpio(0, 4)

static void __init b2076_setup(char **cmdline_p)
{
	pr_info("STMicroelectronics B2076 board initialisation\n");

	stxh205_early_device_init();

	/*
	 * UART0: CN43
	 * UART1: CN41
	 * UART2: CN29
	 * UART10: CN22 -> b2001 CN3 (COM1)
	 * UART11: CN21 -> b2001 CN2 (COM0)
	 */
	stxh205_configure_asc(STXH205_ASC(10), &(struct stxh205_asc_config) {
			/*
			 * Enabling hw flow control conflicts with IRB_IN,
			 * keyscan and PWR_ENABLE GPIO pin.
			 */
			.hw_flow_control = 0,
			.is_console = 1, });
	/*
	 * We disable UART11 as the muxing with MII1_notRESET can cause
	 * problems even when flow control is disabled.
	 *
	 * Enabling hw flow control conflicts with FP_LED,
	 * keyscan and PWM10.
	 *
	 * stxh205_configure_asc(STXH205_ASC(11), &(struct stxh205_asc_config) {
	 * 		.hw_flow_control = 0,
	 * 		.is_console = 1, });
	 */
}

static struct platform_device b2076_gpio_i2c_hdmi = {
	.name = "i2c-gpio",
	.id = 3,
	.dev.platform_data = &(struct i2c_gpio_platform_data) {
		.sda_pin = stm_gpio(12, 1),
		.scl_pin = stm_gpio(12, 0),
		.sda_is_open_drain = 0,
		.scl_is_open_drain = 0,
		.scl_is_output_only = 1,
        },
};

static struct platform_device b2076_led = {
	.name = "leds-gpio",
	.id = -1,
	.dev.platform_data = &(struct gpio_led_platform_data) {
		.num_leds = 1,
		.leds = (struct gpio_led[]) {
			{
				/* Fit J30 1-2 and disable UART11 RTS */
				.name = "FP_LED",
				.default_trigger = "heartbeat",
				.gpio = stm_gpio(3, 1),
			},
		},
	},
};

/* Serial Flash */
static struct stm_plat_spifsm_data b2076_serial_flash =  {
	.name		= "mx25l25635e",
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
		.reset_signal = 1, /* SoC reset routed to device reset pad */
	},
};

/* NAND Flash (via b2006a/b2007a VPMEM module) */
static struct stm_nand_bank_data b2076_nand_flash = {
	.csn		= 0,
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


/*
 * Need to fit J38 1-2 for this. Also disconnect CN21 to prevert the
 * level converter trying to drive UART11 CTS and disable keyscan
 * and sysclkin.
 */
static int b2076_phy_reset(void *bus)
{
#ifdef CONFIG_STM_B2076_PHY_RESET
	/*
	 * IC+ IP101 datasheet specifies 10mS low period and device
	 * usable 2.5mS after rising edge of notReset. However
	 * experimentally it appear 10mS is required for reliable
	 * functioning.
	 */
	gpio_set_value(B2076_MII1_NOTRESET, 0);
	mdelay(10);
	gpio_set_value(B2076_MII1_NOTRESET, 1);
	mdelay(10);
#endif

	return 1;
}

static struct stmmac_mdio_bus_data stmmac_mdio_bus = {
	.bus_id = 0,
	.phy_reset = b2076_phy_reset,
	.phy_mask = 0,
	.probed_phy_irq = ILC_IRQ(25), /* MDINT */
};

static struct platform_device *platform_devices[] __initdata = {
	&b2076_led,
};

static int __init device_init(void)
{
#ifdef CONFIG_STM_B2076_PHY_RESET
	/* This conflicts with PWM10, keyscan and ASC11 */
	gpio_request(B2076_MII1_NOTRESET, "MII1_NORESET");
	gpio_direction_output(B2076_MII1_NOTRESET, 0);
#endif

	/*
	 * Jumper settings for Ethernet:
	 * J39 2-3 and, if CONFIG_STM_B2039_B2076_PHY_RESET set
	 *      then J38 1-2.
	 */
	stxh205_configure_ethernet(&(struct stxh205_ethernet_config) {
			.mode = stxh205_ethernet_mode_mii,
			.ext_clk = 1,
			.phy_bus = 0,
			.phy_addr = -1,
			.mdio_bus_data = &stmmac_mdio_bus,
		});

	/* PHY IRQ has to be triggered LOW */
	set_irq_type(ILC_IRQ(25), IRQ_TYPE_LEVEL_LOW);

#ifdef CONFIG_STM_B2076_CN9_SATA
	stxh205_configure_miphy(&(struct stxh205_miphy_config){
			.mode = SATA_MODE,
			.iface = UPORT_IF,
		});
	stxh205_configure_sata();
#endif

#ifdef CONFIG_STM_B2076_CN9_PCIE
	stxh205_configure_miphy(&(struct stxh205_miphy_config){
			.mode = PCIE_MODE,
			.iface = UPORT_IF,
			});

	/* J29 has to be in position 2-3 for the #PERST signal */
	stxh205_configure_pcie(&(struct stxh205_pcie_config) {
			.reset_gpio = stm_gpio(4, 6),
			});
#endif



	/* Need to set J18 1-2 and J20 1-2. */
	stxh205_configure_usb(0);

	/* Need to set J13 1-2 and J22  1-2 */
	stxh205_configure_usb(1);

	/*
	 * SSC1: FRONTEND: CN19, CN18 (NIM)
	 * Fit jumpers J29 1-2 and J31 1-2
	 * See HDMI comment below.
	 */
	stxh205_configure_ssc_i2c(1, &(struct stxh205_ssc_config) {
			.routing.ssc1.sclk = stxh205_ssc1_sclk_pio4_6,
			.routing.ssc1.mtsr = stxh205_ssc1_mtsr_pio4_7, });

	/* SSC2: FRONTEND_EXT: CN28, CN31 (VPAV) */
	stxh205_configure_ssc_i2c(2, &(struct stxh205_ssc_config) {
			.routing.ssc2.sclk = stxh205_ssc2_sclk_pio9_4,
			.routing.ssc2.mtsr = stxh205_ssc2_mtsr_pio9_5, });
	/*
	 * SSC3: BACKEND: CN14 (GMII), CN10, CN37, CN27
	 * Fit jumpers: J46 2-3, J47 2-3, J48 2-3
	 * This bus can also use PIO15[5-7] if these jumpers are changed.
	 */
	stxh205_configure_ssc_i2c(3, &(struct stxh205_ssc_config) {
			.routing.ssc3.sclk = stxh205_ssc3_sclk_pio15_0,
			.routing.ssc3.mtsr = stxh205_ssc3_mtsr_pio15_1, });

	/*
	 * SSC1: PIO_HDMI_TX: U3 (HDMI2C1), CN8
	 * Fit jumpers J8 and J9
	 * This usage of SSC1 can't be used concurrently with the FRONTEND
	 * bus above. So drive this bus using gpio i2c.
	 *
	 * stxh205_configure_ssc_i2c(1, &(struct stxh205_ssc_config) {
	 *		.routing.ssc1.sclk = stxh205_ssc1_sclk_pio12_0,
	 *		.routing.ssc1.mtsr = stxh205_ssc1_mtsr_pio12_1, });
	 */
	BUG_ON(platform_device_register(&b2076_gpio_i2c_hdmi));

	stxh205_configure_lirc(&(struct stxh205_lirc_config) {
#ifdef CONFIG_LIRC_STM_UHF
			.rx_mode = stxh205_lirc_rx_mode_uhf, });
#else
			.rx_mode = stxh205_lirc_rx_mode_ir, });
#endif

	stxh205_configure_pwm(&(struct stxh205_pwm_config) {
			/*
			 * 10: conflicts with SBC_SYS_CLKINALT, UART11 CTS
			 *    keyscan and MII1 notReset.
			 * 11: conflicts with ETH_RXCLK
			 */
			.out10_enabled = 0,
			.out11_enabled = 0, });

	stxh205_configure_nand(&(struct stm_nand_config) {
			.driver = stm_nand_flex,
			.nr_banks = 1,
			.banks = &b2076_nand_flash,
			.rbn.flex_connected = 1,});

	stxh205_configure_spifsm(&b2076_serial_flash);

	stxh205_configure_mmc(&(struct stxh205_mmc_config) {
#ifdef CONFIG_STM_B2076_CN38_B2048A_MMC_EMMC
			.emmc = 1,
#endif
		});
	return platform_add_devices(platform_devices,
			ARRAY_SIZE(platform_devices));
}
arch_initcall(device_init);

static void __iomem *board_ioport_map(unsigned long port, unsigned int size)
{
	/* If we have PCI then this should never be called because we
	 * are using the generic iomap implementation. If we don't
	 * have PCI then there are no IO mapped devices, so it still
	 * shouldn't be called. */
	BUG();
	return NULL;
}

#ifdef CONFIG_PCI
int pcibios_map_platform_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return stm_pci_legacy_irq(dev);
}
#endif

struct sh_machine_vector mv_b2076 __initmv = {
	.mv_name = "b2076",
	.mv_setup = b2076_setup,
	.mv_nr_irqs = NR_IRQS,
	.mv_ioport_map = board_ioport_map,
};
