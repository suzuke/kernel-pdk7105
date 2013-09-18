/*
 * (c) 2011 STMicroelectronics Limited
 *
 * Author: Stuart Menefy <stuart.menefy@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/ethtool.h>
#include <linux/dma-mapping.h>
#include <linux/phy.h>
#include <linux/stm/miphy.h>
#include <linux/stm/device.h>
#include <linux/clk.h>
#include <linux/stm/emi.h>
#include <linux/stm/pad.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/stxh205.h>
#include <linux/delay.h>
#include <linux/ahci_platform.h>
#include <asm/irq-ilc.h>
#include "pio-control.h"

static u64 stxh205_dma_mask = DMA_BIT_MASK(32);

/* --------------------------------------------------------------------
 *           Ethernet MAC resources (PAD and Retiming)
 * --------------------------------------------------------------------*/

#define DATA_IN(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.function = _func, \
		.priv = &(struct stxh205_pio_config) { \
			.retime = _retiming, \
		}, \
	}

#define DATA_OUT(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = _func, \
		.priv = &(struct stxh205_pio_config) { \
			.retime = _retiming, \
		}, \
	}

/* Give TXER a name so we can refer to it later. */
#define TXER(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = _func, \
		.name = "TXER", \
		.priv = &(struct stxh205_pio_config) { \
			.retime = _retiming, \
		}, \
	}

/*
 * On some boards the MDIO line is missing a pull-up resistor. Enabling
 * weak internal pull-up overcomes the issue.
 */
#define DATA_OUT_PU(_port, _pin, _func, _retiming)	\
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_custom, \
		.function = _func, \
		.priv = &(struct stxh205_pio_config) { \
			.retime = _retiming, \
			.mode = &(struct stm_pio_control_mode_config) { \
				.oe = 1, \
				.pu = 1, \
				.od = 0, \
			}, \
		}, \
	}

#define CLOCK_IN(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_in, \
		.function = _func, \
		.priv = &(struct stxh205_pio_config) { \
			.retime = _retiming, \
		}, \
	}

#define CLOCK_OUT(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_out, \
		.function = _func, \
		.priv = &(struct stxh205_pio_config) { \
			.retime = _retiming, \
		}, \
	}

#define PHY_CLOCK(_port, _pin, _func, _retiming) \
	{ \
		.gpio = stm_gpio(_port, _pin), \
		.direction = stm_pad_gpio_direction_unknown, \
		.function = _func, \
		.name = "PHYCLK", \
		.priv = &(struct stxh205_pio_config) { \
		.retime = _retiming, \
		}, \
	}


static struct stm_pad_config stxh205_ethernet_mii_pad_config = {
	.gpios_num = 20,
	.gpios = (struct stm_pad_gpio []) {
		DATA_OUT(0, 0, 1, RET_BYPASS(0)),/* TXD[0] */
		DATA_OUT(0, 1, 1, RET_BYPASS(0)),/* TXD[1] */
		DATA_OUT(0, 2, 1, RET_BYPASS(0)),/* TXD[2] */
		DATA_OUT(0, 3, 1, RET_BYPASS(0)),/* TXD[3] */
		TXER(0, 4, 1, RET_BYPASS(0)),/* TXER */
		DATA_OUT(0, 5, 1, RET_BYPASS(0)),/* TXEN */
		CLOCK_IN(0, 6, 1, RET_NICLK(0)),/* TXCLK */
		DATA_IN(0, 7, 1, RET_BYPASS(0)),/* COL */
		DATA_OUT_PU(1, 0, 1, RET_BYPASS(0)),/* MDIO*/
		CLOCK_OUT(1, 1, 1, RET_NICLK(0)),/* MDC */
		DATA_IN(1, 2, 1, RET_BYPASS(0)),/* CRS */
		DATA_IN(1, 3, 1, RET_BYPASS(0)),/* MDINT */
		DATA_IN(1, 4, 1, RET_BYPASS(0)),/* RXD[0] */
		DATA_IN(1, 5, 1, RET_BYPASS(0)),/* RXD[1] */
		DATA_IN(1, 6, 1, RET_BYPASS(0)),/* RXD[2] */
		DATA_IN(1, 7, 1, RET_BYPASS(0)),/* RXD[3] */
		DATA_IN(2, 0, 1, RET_BYPASS(0)),/* RXDV */
		DATA_IN(2, 1, 1, RET_BYPASS(0)),/* RX_ER */
		CLOCK_IN(2, 2, 1, RET_NICLK(0)),/* RXCLK */
		PHY_CLOCK(2, 3, 1, RET_NICLK(0)),/* PHYCLK */
	},
	.sysconfs_num = 7,
	.sysconfs = (struct stm_pad_sysconf []) {
		/* ETH_POWERDOWN_REQ */
		STM_PAD_SYSCONF(SYSCONF(23), 0, 0, 0),
		/* ETH_MII_PHY_SEL */
		STM_PAD_SYSCONF(SYSCONF(23), 2, 4, 0),
		/* ETH_ENMII */
		STM_PAD_SYSCONF(SYSCONF(23), 5, 5, 1),
		/* ETH_SEL_TXCLK_NOT_CLK125 */
		STM_PAD_SYSCONF(SYSCONF(23), 6, 6, 1),
		/* ETH_SEL_INTERNAL_NOTEXT_PHYCLK */
		STM_PAD_SYSCONF(SYSCONF(23), 7, 7, 1),
		/* ETH_SEL_TX_RETIMING_CLK */
		STM_PAD_SYSCONF(SYSCONF(23), 8, 8, 0),
		/* ETH_GMAC_EN */
		STM_PAD_SYSCONF(SYSCONF(23), 9, 9, 1),
	},
};

static struct stm_pad_config stxh205_ethernet_rmii_pad_config = {
	.gpios_num = 11,
	.gpios = (struct stm_pad_gpio []) {
		DATA_OUT(0, 0, 1, RET_BYPASS(0)),/* TXD[0] */
		DATA_OUT(0, 1, 1, RET_BYPASS(0)),/* TXD[1] */
		DATA_OUT(0, 5, 1, RET_BYPASS(0)),/* TXEN */
		DATA_OUT_PU(1, 0, 1, RET_BYPASS(0)),/* MDIO */
		CLOCK_OUT(1, 1, 1, RET_NICLK(0)),/* MDC */
		DATA_IN(1, 3, 1, RET_BYPASS(0)),/* MDINT */
		DATA_IN(1, 4, 1, RET_BYPASS(0)),/* RXD.0 */
		DATA_IN(1, 5, 1, RET_BYPASS(0)),/* RXD.1 */
		DATA_IN(2, 0, 1, RET_BYPASS(0)),/* RXDV */
		DATA_IN(2, 1, 1, RET_BYPASS(0)),/* RX_ER */
		PHY_CLOCK(2, 3, 2, RET_NICLK(0)),/* PHYCLK */
	},
	.sysconfs_num = 7,
	.sysconfs = (struct stm_pad_sysconf []) {
		/* ETH_POWERDOWN_REQ */
		STM_PAD_SYSCONF(SYSCONF(23), 0, 0, 0),
		/* ETH_MII_PHY_SEL */
		STM_PAD_SYSCONF(SYSCONF(23), 2, 4, 4),
		/* ETH_ENMII */
		STM_PAD_SYSCONF(SYSCONF(23), 5, 5, 1),
		/* ETH_SEL_TXCLK_NOT_CLK125 */
		STM_PAD_SYSCONF(SYSCONF(23), 6, 6, 1),
		/* ETH_SEL_INTERNAL_NOTEXT_PHYCLK */
		STM_PAD_SYSCONF(SYSCONF(23), 7, 7, 1),
		/* ETH_SEL_TX_RETIMING_CLK */
		STM_PAD_SYSCONF(SYSCONF(23), 8, 8, 1),
		/* ETH_GMAC_EN */
		STM_PAD_SYSCONF(SYSCONF(23), 9, 9, 1),
	},
};

/* TODO */
static struct stm_pad_config stxh205_ethernet_reverse_mii_pad_config = {
	.gpios_num = 20,
	.gpios = (struct stm_pad_gpio []) {
		DATA_OUT(0, 0, 1, RET_BYPASS(0)),/* TXD[0] */
		DATA_OUT(0, 1, 1, RET_BYPASS(0)),/* TXD[1] */
		DATA_OUT(0, 2, 1, RET_BYPASS(0)),/* TXD[2] */
		DATA_OUT(0, 3, 1, RET_BYPASS(0)),/* TXD[3] */
		TXER(0, 4, 1, RET_BYPASS(0)),/* TXER */
		DATA_OUT(0, 5, 1, RET_BYPASS(0)),/* TXEN */
		CLOCK_IN(0, 6, 1, RET_NICLK(0)),/* TXCLK */
		DATA_OUT(0, 7, 1, RET_BYPASS(0)),/* COL */
		DATA_OUT_PU(1, 0, 1, RET_BYPASS(0)),/* MDIO*/
		CLOCK_IN(1, 1, 1, RET_NICLK(0)),/* MDC */
		DATA_OUT(1, 2, 1, RET_BYPASS(0)),/* CRS */
		DATA_IN(1, 3, 1, RET_BYPASS(0)),/* MDINT */
		DATA_IN(1, 4, 1, RET_BYPASS(0)),/* RXD[0] */
		DATA_IN(1, 5, 1, RET_BYPASS(0)),/* RXD[1] */
		DATA_IN(1, 6, 1, RET_BYPASS(0)),/* RXD[2] */
		DATA_IN(1, 7, 1, RET_BYPASS(0)),/* RXD[3] */
		DATA_IN(2, 0, 1, RET_BYPASS(0)),/* RXDV */
		DATA_IN(2, 1, 1, RET_BYPASS(0)),/* RX_ER */
		CLOCK_IN(2, 2, 1, RET_NICLK(0)),/* RXCLK */
		PHY_CLOCK(2, 3, 1, RET_NICLK(0)),/* PHYCLK */
	},
	.sysconfs_num = 7,
	.sysconfs = (struct stm_pad_sysconf []) {
		/* ETH_POWERDOWN_REQ */
		STM_PAD_SYSCONF(SYSCONF(23), 0, 0, 0),
		/* ETH_MII_PHY_SEL */
		STM_PAD_SYSCONF(SYSCONF(23), 2, 4, 0),
		/* ETH_ENMII */
		STM_PAD_SYSCONF(SYSCONF(23), 5, 5, 0),
		/* ETH_SEL_TXCLK_NOT_CLK125 */
		STM_PAD_SYSCONF(SYSCONF(23), 6, 6, 1),
		/* ETH_SEL_INTERNAL_NOTEXT_PHYCLK */
		STM_PAD_SYSCONF(SYSCONF(23), 7, 7, 1),
		/* ETH_SEL_TX_RETIMING_CLK */
		STM_PAD_SYSCONF(SYSCONF(23), 8, 8, 0),
		/* ETH_GMAC_EN */
		STM_PAD_SYSCONF(SYSCONF(23), 9, 9, 1),
	},
};

static struct plat_stmmacenet_data stxh205_ethernet_platform_data = {
	.pbl = 32,
	.has_gmac = 1,
	.enh_desc = 1,
	.tx_coe = 1,
	.bugged_jumbo = 1,
	.pmt = 1,
	.init = &stmmac_claim_resource,
};

static struct platform_device stxh205_ethernet_device = {
	.name = "stmmaceth",
	.id = 0,
	.num_resources = 4,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM(0xfda88000, 0x8000),
		STM_PLAT_RESOURCE_IRQ_NAMED("macirq", ILC_IRQ(21), -1),
		STM_PLAT_RESOURCE_IRQ_NAMED("eth_wake_irq", ILC_IRQ(22), -1),
		STM_PLAT_RESOURCE_IRQ_NAMED("eth_lpi", ILC_IRQ(23), -1),
	},
	.dev = {
		.dma_mask = &stxh205_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &stxh205_ethernet_platform_data,
	},
};

void __init stxh205_configure_ethernet(struct stxh205_ethernet_config *config)
{
	static int configured;
	struct stxh205_ethernet_config default_config;
	struct plat_stmmacenet_data *plat_data;
	struct stm_pad_config *pad_config;
	int interface;
	struct clk *clk;

	BUG_ON(configured++);

	if (!config)
		config = &default_config;

	plat_data = &stxh205_ethernet_platform_data;

	switch (config->mode) {
	case stxh205_ethernet_mode_mii:
		pad_config = &stxh205_ethernet_mii_pad_config;
		if (config->ext_clk)
			stm_pad_set_pio_ignored(pad_config, "PHYCLK");
		else
			stm_pad_set_pio_out(pad_config, "PHYCLK", 1);
		if (config->no_txer)
			stm_pad_set_pio_ignored(pad_config, "TXER");
		interface = PHY_INTERFACE_MODE_MII;
		break;
	case stxh205_ethernet_mode_rmii:
		pad_config = &stxh205_ethernet_rmii_pad_config;

		if (config->ext_clk) {
			stm_pad_set_pio_in(pad_config, "PHYCLK", 2);
			/* ETH_SEL_INTERNAL_NOTEXT_PHYCLK */
			pad_config->sysconfs[4].value = 0;
		} else {
			stm_pad_set_pio_out(pad_config, "PHYCLK", 1);
			/* ETH_SEL_INTERNAL_NOTEXT_PHYCLK */
			pad_config->sysconfs[4].value = 1;
		}
		interface = PHY_INTERFACE_MODE_RMII;
		break;
	case stxh205_ethernet_mode_reverse_mii:
		pad_config = &stxh205_ethernet_reverse_mii_pad_config;
		if (config->ext_clk)
			stm_pad_set_pio_ignored(pad_config, "PHYCLK");
		else
			stm_pad_set_pio_out(pad_config, "PHYCLK", 1);
		if (config->no_txer)
			stm_pad_set_pio_ignored(pad_config, "TXER");
		interface = PHY_INTERFACE_MODE_MII;
		break;
	default:
		BUG();
		return;
	}

	plat_data->custom_cfg = (void *) pad_config;
	plat_data->interface = interface;
	plat_data->bus_id = config->phy_bus;
	plat_data->phy_addr = config->phy_addr;
	plat_data->mdio_bus_data = config->mdio_bus_data;

	clk = clk_get(NULL, "stmmac_clk");
	if (!IS_ERR(clk))
		clk_enable(clk);
	clk = clk_get(NULL, "stmmac_phy_clk");
	if (!IS_ERR(clk))
		clk_enable(clk);
	platform_device_register(&stxh205_ethernet_device);
}

/* USB resources ---------------------------------------------------------- */

#define USB_HOST_PWR		"USB_HOST_PWR"
#define USB_PWR_ACK		"USB_PWR_ACK"

static DEFINE_MUTEX(stxh205_usb_phy_mutex);
static int stxh205_usb_phy_count;
struct stm_pad_state *stxh205_usb_phy_state;

static struct stm_pad_config stxh205_usb_phy_pad_config = {
	.sysconfs_num = 19,
	.sysconfs = (struct stm_pad_sysconf []) {
		STM_PAD_SYSCONF(SYSCONF(501),  1,  3, 4), /* COMPDISTUNE0 */
		STM_PAD_SYSCONF(SYSCONF(501),  4,  6, 4), /* COMPDISTUNE1 */
		STM_PAD_SYSCONF(SYSCONF(501),  7,  7, 1), /* OTGDISABLE0 */
		STM_PAD_SYSCONF(SYSCONF(501),  8, 10, 4), /* OTGTUNE0 */
		STM_PAD_SYSCONF(SYSCONF(501), 11, 11, 1), /* SLEEPM0 */
		STM_PAD_SYSCONF(SYSCONF(501), 12, 12, 1), /* SLEEPM1 */
		STM_PAD_SYSCONF(SYSCONF(501), 13, 15, 3), /* SQRXTUNE0 */
		STM_PAD_SYSCONF(SYSCONF(501), 16, 18, 3), /* SQRXTUNE1 */
		STM_PAD_SYSCONF(SYSCONF(501), 19, 22, 3), /* TXFSLSTUNE0 */
		STM_PAD_SYSCONF(SYSCONF(501), 23, 23, 0), /* TXPREEMPHASISTUNE0 */
		STM_PAD_SYSCONF(SYSCONF(501), 24, 24, 0), /* TXPREEMPHASISTUNE1 */
		STM_PAD_SYSCONF(SYSCONF(501), 25, 25, 0), /* TXRISETUNE0 */
		STM_PAD_SYSCONF(SYSCONF(501), 26, 26, 0), /* TXRISETUNE1 */
		STM_PAD_SYSCONF(SYSCONF(501), 27, 30, 8), /* TXVREFTUNE0 */
		STM_PAD_SYSCONF(SYSCONF(502),  0,  3, 8), /* TXVREFTUNE1 */
		STM_PAD_SYSCONF(SYSCONF(502),  4,  7, 3), /* TXFSLSTUNE1 */
		STM_PAD_SYSCONF(SYSCONF(502),  8,  9, 3), /* TXHSXVTUNE0 */
		STM_PAD_SYSCONF(SYSCONF(502), 10, 11, 3), /* TXHSXVTUNE1 */
		STM_PAD_SYSCONF(SYSCONF(501),  0,  0, 0), /* COMMONONN */
	}
};

static int stxh205_usb_init(struct stm_device_state *device_state)
{
	int result = 0;

	mutex_lock(&stxh205_usb_phy_mutex);

	if (stxh205_usb_phy_count == 0) {
		stxh205_usb_phy_state = stm_pad_claim(
			&stxh205_usb_phy_pad_config, "USBPHY");
		if (!stxh205_usb_phy_state) {
			result = -EBUSY;
			goto err;
		}
	}
	stxh205_usb_phy_count++;

err:
	mutex_unlock(&stxh205_usb_phy_mutex);

	return result;
}

static int stxh205_usb_exit(struct stm_device_state *device_state)
{
	mutex_lock(&stxh205_usb_phy_mutex);

	if (--stxh205_usb_phy_count == 0)
		stm_pad_release(stxh205_usb_phy_state);

	mutex_unlock(&stxh205_usb_phy_mutex);

	return 0;
}

static void stxh205_usb_power(struct stm_device_state *device_state,
		enum stm_device_power_state power)
{
	int i;
	int value = (power == stm_device_power_on) ? 0 : 1;

	stm_device_sysconf_write(device_state, USB_HOST_PWR, value);

	for (i = 5; i; --i) {
		if (stm_device_sysconf_read(device_state, USB_PWR_ACK)
			== value)
			break;
		mdelay(10);
	}
}

static struct stm_plat_usb_data stxh205_usb_platform_data[] = {
	[0] = {
		/* Threshold value set in configure as dynamically
		 * probed
		 */
		.flags = STM_PLAT_USB_FLAGS_STRAP_8BIT |
			 STM_PLAT_USB_FLAGS_STBUS_CONFIG_LDST64 |
			 STM_PLAT_USB_FLAGS_STBUS_CONFIG_CHUNK2,
		.device_config = &(struct stm_device_config){
			.init = stxh205_usb_init,
			.exit = stxh205_usb_exit,
			.power = stxh205_usb_power,
			.sysconfs_num = 2,
			.sysconfs = (struct stm_device_sysconf []) {
				STM_DEVICE_SYSCONF(SYSCONF(449), 0, 0,
					USB_HOST_PWR),
				STM_DEVICE_SYSCONF(SYSCONF(423), 3, 3,
					USB_PWR_ACK),
			},
			.pad_config = &(struct stm_pad_config) {
				.gpios_num = 2,
				.gpios = (struct stm_pad_gpio []) {
					/* Overcurrent detection */
					STM_PAD_PIO_IN(4, 2, 1),
					/* USB power enable */
					STM_PAD_PIO_OUT(4, 3, 1),
				},
			},
		},
	},
	[1] = {
		.flags = STM_PLAT_USB_FLAGS_STRAP_8BIT |
			 STM_PLAT_USB_FLAGS_STBUS_CONFIG_LDST64 |
			 STM_PLAT_USB_FLAGS_STBUS_CONFIG_CHUNK2,
		.device_config = &(struct stm_device_config){
			.init = stxh205_usb_init,
			.exit = stxh205_usb_exit,
			.power = stxh205_usb_power,
			.sysconfs_num = 2,
			.sysconfs = (struct stm_device_sysconf []) {
				STM_DEVICE_SYSCONF(SYSCONF(449), 1, 1,
					USB_HOST_PWR),
				STM_DEVICE_SYSCONF(SYSCONF(423), 4, 4,
					USB_PWR_ACK),
			},
			.pad_config = &(struct stm_pad_config) {
				.gpios_num = 2,
				.gpios = (struct stm_pad_gpio []) {
					/* Overcurrent detection */
					STM_PAD_PIO_IN(4, 4, 1),
					/* USB power enable */
					STM_PAD_PIO_OUT(4, 5, 1),
				},
			},
		},
	},
};

static struct platform_device stxh205_usb_devices[] = {
	[0] = {
		.name = "stm-usb",
		.id = 0,
		.dev = {
			.dma_mask = &stxh205_dma_mask,
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.platform_data = &stxh205_usb_platform_data[0],
		},
		.num_resources = 6,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM_NAMED("wrapper",
					0xfe000000, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ohci",
					0xfe0ffc00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ehci",
					0xfe0ffe00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("protocol",
					0xfe0fff00, 0x100),
			STM_PLAT_RESOURCE_IRQ_NAMED("ehci", ILC_IRQ(59), -1),
			STM_PLAT_RESOURCE_IRQ_NAMED("ohci", ILC_IRQ(62), -1),
		},
	},
	[1] = {
		.name = "stm-usb",
		.id = 1,
		.dev = {
			.dma_mask = &stxh205_dma_mask,
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.platform_data = &stxh205_usb_platform_data[1],
		},
		.num_resources = 6,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM_NAMED("wrapper",
					0xfe100000, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ohci",
					0xfe1ffc00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("ehci",
					0xfe1ffe00, 0x100),
			STM_PLAT_RESOURCE_MEM_NAMED("protocol",
					0xfe1fff00, 0x100),
			STM_PLAT_RESOURCE_IRQ_NAMED("ehci", ILC_IRQ(60), -1),
			STM_PLAT_RESOURCE_IRQ_NAMED("ohci", ILC_IRQ(63), -1),
		},
	},
};

void __init stxh205_configure_usb(int port)
{
	static int configured[ARRAY_SIZE(stxh205_usb_devices)];
	static void *lmi16reg;
	static int lmi_is_16;

	BUG_ON(port < 0 || port >= ARRAY_SIZE(stxh205_usb_devices));

	BUG_ON(configured[port]++);

	if (!lmi16reg) {
		/* Have a quick peek at the relevant LMI register to see if
		 * the LMI is configured in 16 bit mode or not
		 */
		lmi16reg = ioremap(0xfde50084, 4);
		if (lmi16reg) {
			lmi_is_16 =  readl(lmi16reg) & 0x1;
			iounmap(lmi16reg);
		}
	}

	stxh205_usb_platform_data[port].flags |=
		lmi_is_16 ? STM_PLAT_USB_FLAGS_STBUS_CONFIG_THRESHOLD16
			  : STM_PLAT_USB_FLAGS_STBUS_CONFIG_THRESHOLD128;

	platform_device_register(&stxh205_usb_devices[port]);
}

static void stxh205_pcie_mp_select(int port)
{
}
static enum miphy_mode stxh205_miphy_modes[1] = {SATA_MODE};
struct stm_plat_pcie_mp_data stxh205_pcie_mp_platform_data = {
	.style_id = ID_MIPHYA40X,
	.miphy_first = 0,
	.miphy_count = 1,
	.miphy_modes = stxh205_miphy_modes,
	.mp_select = stxh205_pcie_mp_select,
};

/* Both the SATA AHCI controller and the PCIe controller
 * contain a microport interface to the single miphy. Which
 * one is actually in use depends on the SATA_SEL sysconf
 */
#define PCIE_UPORT_BASE		0xfd904000
#define SATA_UPORT_BASE		0xfd54a000
#define UPORT_REG_SIZE		0xff

static struct platform_device stxh205_pcie_mp_device = {
	.name	= "pcie-mp",
	.id	= 0,
	.num_resources = 1,
	.resource = (struct resource[]) {
		[0] = {
			.start = SATA_UPORT_BASE,
			.end   = SATA_UPORT_BASE + UPORT_REG_SIZE,
			.flags = IORESOURCE_MEM,
		},
	},
	.dev = {
		.platform_data = &stxh205_pcie_mp_platform_data,
	}
};

/* STiH205 has 1 × eSATA or 1 × PCI-express, and can be configured
 * to map PCIe, instead of eSATA, on PHY Lane */
void __init stxh205_configure_miphy(struct stxh205_miphy_config *config)
{
	struct sysconf_field *sel_sata;

	if (config->iface != UPORT_IF) {
		printk(KERN_ERR "MiPhy only supported in microport mode\n");
		return;
	}

	sel_sata = sysconf_claim(SYSCONF(445), 1, 1, "sata/pcie");
	if (!sel_sata) {
		printk(KERN_ERR "Cannot claim SELECT_SATA sysconf\n");
		return;
	}

	stxh205_miphy_modes[0] = config->mode;
	stxh205_pcie_mp_platform_data.rx_pol_inv = config->rx_pol_inv;
	stxh205_pcie_mp_platform_data.tx_pol_inv = config->tx_pol_inv;

	/* Select either PCIE or SATA mode */
	sysconf_write(sel_sata, config->mode == SATA_MODE);

	if (config->mode == PCIE_MODE) {
		struct sysconf_field *miphy_reset, *pcie_reset, *pcie_clk_sel;

		/* Change addresses to other port */
		stxh205_pcie_mp_device.resource[0].start = PCIE_UPORT_BASE,
		stxh205_pcie_mp_device.resource[0].end =
					PCIE_UPORT_BASE + UPORT_REG_SIZE;

		miphy_reset = sysconf_claim(SYSCONF(460), 18, 18, "miphy");
		pcie_reset = sysconf_claim(SYSCONF(461), 0, 0, "pcie");
		pcie_clk_sel = sysconf_claim(SYSCONF(468), 0, 0, "pcie");

		sysconf_write(miphy_reset, 0); /* Reset miphy */
		sysconf_write(pcie_reset, 0); /* Reset PCIe */
		sysconf_write(pcie_clk_sel, 1); /* Select 100MHz ext clock */
		sysconf_write(miphy_reset, 1); /* Release miphy */
		sysconf_write(pcie_reset, 1); /* Release PCIe */
	}

	platform_device_register(&stxh205_pcie_mp_device);
}


static int stxh205_ahci_init(struct device *dev, void __iomem *mmio)
{
#define SATA_OOBR       0xbc
	writel(0x80000000, mmio + SATA_OOBR);
	writel(0x8204080C, mmio + SATA_OOBR);
	writel(0x0204080C, mmio + SATA_OOBR);

	stm_miphy_claim(0, SATA_MODE, dev);
	return 0;
}

static struct ahci_platform_data stxh205_ahci_pdata = {
	.init = stxh205_ahci_init,
};

static u64 stxh205_ahci_dmamask = DMA_BIT_MASK(32);

struct platform_device stxh205_sata_device = {
	.name           = "ahci",
	.id             = -1,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM_NAMED("ahci", 0xFD548000, 0x1000),
		STM_PLAT_RESOURCE_IRQ_NAMED("ahci", ILC_IRQ(58), -1),
	},
	.num_resources  = 2,
	.dev            = {
		.platform_data          = &stxh205_ahci_pdata,
		.dma_mask               = &stxh205_ahci_dmamask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
};

void __init stxh205_configure_sata(void)
{
	platform_device_register(&stxh205_sata_device);
}
