/*
 * (c) 2010-2011 STMicroelectronics Limited
 *
 * Author: Pawel Moll <pawel.moll@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */



#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/stm/emi.h>
#include <linux/stm/pad.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/device.h>
#include <linux/stm/fli75xx.h>
#include <asm/irq-ilc.h>



/* EMI resources ---------------------------------------------------------- */
static void fli75xx_emi_power(struct stm_device_state *device_state,
		enum stm_device_power_state power)
{
	int i;
	int value = (power == stm_device_power_on) ? 0 : 1;

	stm_device_sysconf_write(device_state, "EMI_PWR", value);
	for (i = 5; i; --i) {
		if (stm_device_sysconf_read(device_state, "EMI_ACK")
			== value)
			break;
		mdelay(10);
	}

	return;
}

static struct platform_device fli75xx_emi = {
	.name = "emi",
	.id = -1,
	.num_resources = 3,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM_NAMED("emi memory", 0, 128 * 1024 * 1024),
		STM_PLAT_RESOURCE_MEM_NAMED("emi4 config", 0xfd100000, 0x874),
		STM_PLAT_RESOURCE_MEM_NAMED("emiss config", 0xfd201000, 0x80),
	},
	.dev.platform_data = &(struct stm_device_config){
		.sysconfs_num = 2,
		.sysconfs = (struct stm_device_sysconf []){
			STM_DEVICE_SYSCONF(CFG_PWR_DWN_CTL,
				0, 0, "EMI_PWR"),
			STM_DEVICE_SYSCONF(CFG_EMI_ROPC_STATUS,
				16, 16, "EMI_ACK"),
		},
		.power = fli75xx_emi_power,

	}
};


/* NAND Resources --------------------------------------------------------- */

static struct platform_device fli75xx_nand_emi_device = {
	.name			= "stm-nand-emi",
	.dev.platform_data	= &(struct stm_plat_nand_emi_data) {
	},
};

static struct stm_plat_nand_flex_data fli75xx_nand_flex_data;
static struct stm_plat_nand_bch_data fli75xx_nand_bch_data;

static struct platform_device fli75xx_nandi_device = {
	.num_resources		= 3,
	.resource		= (struct resource[]) {
		STM_PLAT_RESOURCE_MEM_NAMED("nand_mem", 0xFD101000, 0x1000),
		STM_PLAT_RESOURCE_MEM_NAMED("nand_dma", 0xfd200800, 0x0800),
		STM_PLAT_RESOURCE_IRQ_NAMED("nand_irq", ILC_IRQ(35), -1),
	},
};

void __init fli75xx_configure_nand(struct stm_nand_config *config)
{
	struct stm_plat_nand_emi_data *emi_data;

	switch (config->driver) {
	case stm_nand_emi:
		/* Configure platform device for stm-nand-emi driver */
		emi_data = fli75xx_nand_emi_device.dev.platform_data;
		emi_data->nr_banks = config->nr_banks;
		emi_data->banks = config->banks;
		emi_data->emi_rbn_gpio = config->rbn.emi_gpio;
		platform_device_register(&fli75xx_nand_emi_device);
		break;
	case stm_nand_flex:
	case stm_nand_afm:
		/* Configure platform device for stm-nand-flex/afm driver */
		emiss_nandi_select(STM_NANDI_HAMMING);
		fli75xx_nand_flex_data.nr_banks = config->nr_banks;
		fli75xx_nand_flex_data.banks = config->banks;
		fli75xx_nand_flex_data.flex_rbn_connected =
			config->rbn.flex_connected;
		fli75xx_nandi_device.dev.platform_data =
			&fli75xx_nand_flex_data;
		fli75xx_nandi_device.name =
			(config->driver == stm_nand_flex) ?
			"stm-nand-flex" : "stm-nand-afm";
		platform_device_register(&fli75xx_nandi_device);
		break;
	case stm_nand_bch:
		/* Configure device for stm-nand-bch driver */
		/* BCH controller not available on fli7510 */
		BUG_ON(cpu_data->type == CPU_FLI7510);
		BUG_ON(config->nr_banks > 1);
		emiss_nandi_select(STM_NANDI_BCH);
		fli75xx_nand_bch_data.bank = config->banks;
		fli75xx_nand_bch_data.bch_ecc_cfg = config->bch_ecc_cfg;
		fli75xx_nandi_device.dev.platform_data =
			&fli75xx_nand_bch_data;
		fli75xx_nandi_device.name = "stm-nand-bch";
		platform_device_register(&fli75xx_nandi_device);
		break;
	default:
		BUG();
		return;
	}
}

/* SPI FSM setup ---------------------------------------------------------- */

static struct stm_pad_config fli7510_spifsm_pad_config = {
	.gpios_num = 6,
	.gpios = (struct stm_pad_gpio[]) {
		STM_PAD_PIO_OUT_NAMED(17, 2, 1, "spi-fsm-clk"),
		STM_PAD_PIO_OUT_NAMED(17, 4, 1, "spi-fsm-cs"),
		STM_PAD_PIO_BIDIR_NAMED(17, 3, 1, "spi-fsm-mosi"),
		STM_PAD_PIO_BIDIR_NAMED(17, 5, 1, "spi-fsm-miso"),
		STM_PAD_PIO_BIDIR_NAMED(17, 0, 1, "spi-fsm-hold"),
		STM_PAD_PIO_BIDIR_NAMED(17, 1, 1, "spi-fsm-wp"),
	},
	.sysconfs_num = 1,
	.sysconfs = (struct stm_pad_sysconf []) {
		/* Enable SPIBoot/FSM controller */
		STM_PAD_SYSCONF(CFG_COMMS_CONFIG_2, 13, 13, 1),
	},
};

static struct stm_pad_config fli7520_spifsm_pad_config = {
	.gpios_num = 6,
	.gpios = (struct stm_pad_gpio[]) {
		STM_PAD_PIO_OUT_NAMED(21, 2, 1, "spi-fsm-clk"),
		STM_PAD_PIO_OUT_NAMED(20, 2, 1, "spi-fsm-cs"),
		STM_PAD_PIO_BIDIR_NAMED(21, 3, 1, "spi-fsm-mosi"),
		STM_PAD_PIO_BIDIR_NAMED(20, 5, 1, "spi-fsm-miso"),
		STM_PAD_PIO_BIDIR_NAMED(18, 1, 1, "spi-fsm-hold"),
		STM_PAD_PIO_BIDIR_NAMED(18, 2, 1, "spi-fsm-wp"),
	},
	.sysconfs_num = 2,
	.sysconfs = (struct stm_pad_sysconf []) {
		/* Enable SPIBoot/FSM controller */
		STM_PAD_SYSCONF(CFG_COMMS_CONFIG_2, 13, 13, 1),

		/* Select MII/RMII/SPI routing on PIO18/20/21 */
		STM_PAD_SYSCONF(CFG_COMMS_CONFIG_2, 17, 17, 0),
	},
};

static struct platform_device fli75xx_spifsm_device = {
	.name		= "stm-spi-fsm",
	.id		= 0,
	.num_resources	= 1,
	.resource	= (struct resource[]) {
		STM_PLAT_RESOURCE_MEM(0xfd102000, 0x1000),
	},
};

void __init fli75xx_configure_spifsm(struct stm_plat_spifsm_data *data)
{
	struct sysconf_field *sc = NULL;

	fli75xx_spifsm_device.dev.platform_data = data;

	switch (cpu_data->type) {
	case CPU_FLI7510:
	case CPU_FLI7560:
		data->pads = &fli7510_spifsm_pad_config;
		sc = sysconf_claim(CFG_MODE_PIN_STATUS, 7, 8, "boot-device");
		break;
	case CPU_FLI7520:
	case CPU_FLI7530:
	case CPU_FLI7540:
		data->pads = &fli7520_spifsm_pad_config;
		sc = sysconf_claim(CFG_MODE_PIN_STATUS, 9, 10, "boot-device");
		break;
	default:
		BUG();
	}

	/* SoC/IP Capabilities */
	data->capabilities.quad_mode = 0;
	data->capabilities.no_read_repeat = 1;
	data->capabilities.no_write_repeat = 1;
	data->capabilities.read_status_bug = spifsm_no_read_status;
	data->capabilities.boot_from_spi = (sysconf_read(sc) == 0x2) ? 1 : 0;

	sysconf_release(sc);

	platform_device_register(&fli75xx_spifsm_device);
}


/* FDMA resources --------------------------------------------------------- */

static struct stm_plat_fdma_fw_regs stm_fdma_firmware_75xx = {
	.rev_id    = 0x8000 + (0x000 << 2), /* 0x8000 */
	.cmd_statn = 0x8000 + (0x450 << 2), /* 0x9140 */
	.req_ctln  = 0x8000 + (0x460 << 2), /* 0x9180 */
	.ptrn      = 0x8000 + (0x560 << 2), /* 0x9580 */
	.cntn      = 0x8000 + (0x562 << 2), /* 0x9588 */
	.saddrn    = 0x8000 + (0x563 << 2), /* 0x958c */
	.daddrn    = 0x8000 + (0x564 << 2), /* 0x9590 */
};

static struct stm_plat_fdma_hw fli75xx_fdma_hw = {
	.slim_regs = {
		.id       = 0x0000 + (0x000 << 2), /* 0x0000 */
		.ver      = 0x0000 + (0x001 << 2), /* 0x0004 */
		.en       = 0x0000 + (0x002 << 2), /* 0x0008 */
		.clk_gate = 0x0000 + (0x003 << 2), /* 0x000c */
	},
	.dmem = {
		.offset = 0x8000,
		.size   = 0x800 << 2, /* 2048 * 4 = 8192 */
	},
	.periph_regs = {
		.sync_reg = 0x8000 + (0xfe2 << 2), /* 0xbf88 */
		.cmd_sta  = 0x8000 + (0xff0 << 2), /* 0xbfc0 */
		.cmd_set  = 0x8000 + (0xff1 << 2), /* 0xbfc4 */
		.cmd_clr  = 0x8000 + (0xff2 << 2), /* 0xbfc8 */
		.cmd_mask = 0x8000 + (0xff3 << 2), /* 0xbfcc */
		.int_sta  = 0x8000 + (0xff4 << 2), /* 0xbfd0 */
		.int_set  = 0x8000 + (0xff5 << 2), /* 0xbfd4 */
		.int_clr  = 0x8000 + (0xff6 << 2), /* 0xbfd8 */
		.int_mask = 0x8000 + (0xff7 << 2), /* 0xbfdc */
	},
	.imem = {
		.offset = 0xc000,
		.size   = 0x1000 << 2, /* 4096 * 4 = 16384 */
	},
};

static struct stm_plat_fdma_data fli75xx_fdma_platform_data = {
	.hw = &fli75xx_fdma_hw,
	.fw = &stm_fdma_firmware_75xx,
};

/*
 * Normally device 0 would be the real-time fdma and device 1 would be the
 * non-real-time fdma. Here they are swapped as output pins 0-31 on fdma-xbar
 * are routed to the non-real-time fdma and output pins 32-63 are routed to the
 * real-time fdma. You must ensure that that firmware to load is named correctly
 */
static struct platform_device fli75xx_fdma_devices[] = {
	{
		.name = "stm-fdma",
		.id = 0,
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd910000, 0x10000),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(38), -1),
		},
		.dev.platform_data = &fli75xx_fdma_platform_data,
	}, {
		.name = "stm-fdma",
		.id = 1,
		.num_resources = 2,
		.resource = (struct resource[2]) {
			STM_PLAT_RESOURCE_MEM(0xfd660000, 0x10000),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(36), -1),
		},
		.dev.platform_data = &fli75xx_fdma_platform_data,
	}
};

static struct platform_device fli75xx_fdma_xbar_device = {
	.name = "stm-fdma-xbar",
	.id = -1,
	.num_resources = 1,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM(0xfd980000, 0x1000),
	},
};



/* Hardware RNG resources ------------------------------------------------- */

static struct platform_device fli75xx_rng_hwrandom_device = {
	.name = "stm-hwrandom",
	.id = -1,
	.num_resources = 1,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM(0xfd3e0000, 0x1000),
	}
};

static struct platform_device fli75xx_rng_devrandom_device = {
	.name = "stm-rng",
	.id = -1,
	.num_resources = 1,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM(0xfd3e0000, 0x1000),
	}
};



/* PIO ports resources ---------------------------------------------------- */


#define FLI75XX_PIO_ENTRY(_id, _start)	 				\
	[_id] = {							\
		.name = "stm-gpio",					\
		.id = _id,						\
		.num_resources = 1,					\
		.resource = (struct resource[]) {			\
			STM_PLAT_RESOURCE_MEM(_start, 0x100),		\
		},							\
	}

#define FLI75XX_PIO_IRQ_ENTRY(_id, _start, _ilc_irq) 			\
	[_id] = {							\
		.name = "stm-gpio",					\
		.id = _id,						\
		.num_resources = 2,					\
		.resource = (struct resource[]) {			\
			STM_PLAT_RESOURCE_MEM(_start, 0x100),		\
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(_ilc_irq), -1),	\
		},							\
	}


static struct platform_device fli7510_pio_devices[] = {
	FLI75XX_PIO_IRQ_ENTRY(0, 0xfd5c0000, 75),
	FLI75XX_PIO_IRQ_ENTRY(1, 0xfd5c4000, 76),
	FLI75XX_PIO_IRQ_ENTRY(2, 0xfd5c8000, 77),
	FLI75XX_PIO_IRQ_ENTRY(3, 0xfd5cc000, 78),
	FLI75XX_PIO_IRQ_ENTRY(4, 0xfd5d0000, 79),
	FLI75XX_PIO_IRQ_ENTRY(5, 0xfd5d4000, 80),
	FLI75XX_PIO_ENTRY(6, 0xfd5d8000),
	FLI75XX_PIO_ENTRY(7, 0xfd5dc000),
	FLI75XX_PIO_ENTRY(8, 0xfd5e0000),
	FLI75XX_PIO_ENTRY(9, 0xfd5e4000),
	FLI75XX_PIO_IRQ_ENTRY(10, 0xfd984000, 125),
	FLI75XX_PIO_ENTRY(11, 0xfd988000),
	FLI75XX_PIO_ENTRY(12, 0xfd98c000),
	FLI75XX_PIO_IRQ_ENTRY(13, 0xfd990000, 2),
	FLI75XX_PIO_IRQ_ENTRY(14, 0xfd994000, 3),
	FLI75XX_PIO_IRQ_ENTRY(15, 0xfd998000, 81),
	FLI75XX_PIO_IRQ_ENTRY(16, 0xfd99c000, 82),
	FLI75XX_PIO_ENTRY(17, 0xfd9a0000),
	FLI75XX_PIO_ENTRY(18, 0xfd9a4000),
	FLI75XX_PIO_ENTRY(19, 0xfd9a8000),
	FLI75XX_PIO_ENTRY(20, 0xfd9ac000),
	FLI75XX_PIO_ENTRY(21, 0xfd9b0000),
	FLI75XX_PIO_IRQ_ENTRY(22, 0xfd9b4000, 83),
	FLI75XX_PIO_IRQ_ENTRY(23, 0xfd9b8000, 84),
	FLI75XX_PIO_IRQ_ENTRY(24, 0xfd9bc000, 85),
	FLI75XX_PIO_ENTRY(25, 0xfd9c0000),
	FLI75XX_PIO_ENTRY(26, 0xfd9c4000),
	FLI75XX_PIO_ENTRY(27, 0xfd9c8000),
};

static struct platform_device fli7520_pio_devices[] = {
	/* Warning, no PIO ports 0 to 4... */
	FLI75XX_PIO_IRQ_ENTRY(5,  0xfd5c0000, 75),
	FLI75XX_PIO_IRQ_ENTRY(6,  0xfd5c4000, 76),
	FLI75XX_PIO_IRQ_ENTRY(7,  0xfd5c8000, 77),
	FLI75XX_PIO_IRQ_ENTRY(8,  0xfd5cc000, 78),
	FLI75XX_PIO_IRQ_ENTRY(9,  0xfd5d0000, 79),
	FLI75XX_PIO_IRQ_ENTRY(9,  0xfd5d0000, 79),
	FLI75XX_PIO_IRQ_ENTRY(10, 0xfd984000, 125),
	FLI75XX_PIO_ENTRY(11, 0xfd988000),
	FLI75XX_PIO_ENTRY(12, 0xfd98c000),
	FLI75XX_PIO_IRQ_ENTRY(13, 0xfd990000, 2),
	FLI75XX_PIO_IRQ_ENTRY(14, 0xfd994000, 3),
	FLI75XX_PIO_IRQ_ENTRY(15, 0xfd998000, 81),
	FLI75XX_PIO_IRQ_ENTRY(16, 0xfd99c000, 82),
	FLI75XX_PIO_ENTRY(17, 0xfd9a0000),
	FLI75XX_PIO_ENTRY(18, 0xfd9a4000),
	FLI75XX_PIO_ENTRY(19, 0xfd9a8000),
	FLI75XX_PIO_ENTRY(20, 0xfd9ac000),
	FLI75XX_PIO_ENTRY(21, 0xfd9b0000),
	FLI75XX_PIO_IRQ_ENTRY(22, 0xfd9b4000, 83),
	FLI75XX_PIO_IRQ_ENTRY(23, 0xfd9b8000, 84),
	FLI75XX_PIO_IRQ_ENTRY(24, 0xfd9bc000, 85),
	FLI75XX_PIO_ENTRY(25, 0xfd9c0000),
	FLI75XX_PIO_ENTRY(26, 0xfd9c4000),
	FLI75XX_PIO_ENTRY(27, 0xfd9c8000),
	FLI75XX_PIO_ENTRY(28, 0xfd9cc000),
	FLI75XX_PIO_ENTRY(29, 0xfd9d0000),
};

static int fli75xx_pio_config(unsigned gpio,
		enum stm_pad_gpio_direction direction, int function, void *priv)
{
	switch (direction) {
	case stm_pad_gpio_direction_in:
		BUG_ON(function != -1);
		stm_gpio_direction(gpio, STM_GPIO_DIRECTION_IN);
		break;
	case stm_pad_gpio_direction_out:
		BUG_ON(function < 0);
		BUG_ON(function > 1);
		stm_gpio_direction(gpio, function ?
				STM_GPIO_DIRECTION_ALT_OUT :
				STM_GPIO_DIRECTION_OUT);
		break;
	case stm_pad_gpio_direction_bidir:
		BUG_ON(function < 0);
		BUG_ON(function > 1);
		stm_gpio_direction(gpio, function ?
				STM_GPIO_DIRECTION_ALT_BIDIR :
				STM_GPIO_DIRECTION_BIDIR);
		break;
	default:
		BUG();
		break;
	}

	return 0;
}

/* MMC/SD resources ------------------------------------------------------ */

static struct stm_pad_config fli75xx_mmc_pad_config = {
	.gpios_num = 15,
	.gpios = (struct stm_pad_gpio []) {
		STM_PAD_PIO_OUT_NAMED(23, 2, 1, "MMCCLK"),/* MMC clock */
		STM_PAD_PIO_OUT(23, 3, 1),	/* MMC command */
		STM_PAD_PIO_IN(23, 4, -1),	/* Card Detect */
		STM_PAD_PIO_IN(23, 5, -1),	/* Over Current */
		STM_PAD_PIO_IN(23, 6, -1),	/* MMC Write Protection */
		STM_PAD_PIO_OUT(23, 7, 1),	/* PWR*/
		STM_PAD_PIO_OUT(27, 0, 1),	/* LED*/
		STM_PAD_PIO_BIDIR(24, 0, 1),	/* MMC/SD Data 0*/
		STM_PAD_PIO_BIDIR(24, 1, 1),	/* MMC/SD Data 1*/
		STM_PAD_PIO_BIDIR(24, 2, 1),	/* MMC/SD Data 2*/
		STM_PAD_PIO_BIDIR(24, 3, 1),	/* MMC/SD Data 3*/
		STM_PAD_PIO_BIDIR(24, 4, 1),	/* MMC Data 4*/
		STM_PAD_PIO_BIDIR(24, 5, 1),	/* MMC Data 5*/
		STM_PAD_PIO_BIDIR(24, 6, 1),	/* MMC Data 6*/
		STM_PAD_PIO_BIDIR(24, 7, 1),	/* MMC Data 7*/
		STM_PAD_PIO_OUT(20, 5, 1),	/* Open drain mode
						 * (for external card) */
	},
};

static int mmc_pad_resources(struct sdhci_host *sdhci)
{
	if (!devm_stm_pad_claim(sdhci->mmc->parent, &fli75xx_mmc_pad_config,
				dev_name(sdhci->mmc->parent)))
		return -ENODEV;

	return 0;
}

static struct sdhci_pltfm_data fli75xx_mmc_platform_data = {
		.init = mmc_pad_resources,
		.quirks = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
};

static struct platform_device fli75xx_mmc_device = {
		.name = "sdhci",
		.id = 0,
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xFD9F0000, 0x400),
			STM_PLAT_RESOURCE_IRQ_NAMED("mmcirq", ILC_IRQ(109), -1),
		},
		.dev = {
			.platform_data = &fli75xx_mmc_platform_data,
		}
};

void __init fli75xx_configure_mmc(void)
{
	struct sysconf_field *sc;

	/* Selects the polarity of HSMMC_CARD_DET as input signal inverted*/
	sc = sysconf_claim(TRS_PU_CFG_0, 0, 2, 2, "mmc");
	sysconf_write(sc, 1);

	/* Selects the mode for PIO24, bits 17:18 MMC when "1" "0"  */
	sc = sysconf_claim(TRS_PU_CFG_0, 0, 17, 18, "mmc");
	sysconf_write(sc, 1);

	platform_device_register(&fli75xx_mmc_device);
}

/* sysconf resources ------------------------------------------------------ */

#ifdef CONFIG_DEBUG_FS

#define SYSCONF_REG(field) _SYSCONF_REG(#field, field)
#define _SYSCONF_REG(name, group, num) case num: str = name; break

static void fli75xx_sysconf_PRB_PU_CFG_1(char *name, int size,
		int group, int num)
{
	char *str = "???";

	switch (num) {
	SYSCONF_REG(CFG_RESET_CTL);
	SYSCONF_REG(CFG_BOOT_CTL);
	SYSCONF_REG(CFG_SYS1);
	SYSCONF_REG(CFG_MPX_CTL);
	SYSCONF_REG(CFG_PWR_DWN_CTL);
	SYSCONF_REG(CFG_SYS2);
	SYSCONF_REG(CFG_MODE_PIN_STATUS);
	SYSCONF_REG(CFG_PCI_ROPC_STATUS);
	}

	strlcpy(name, str, size);
}

static void fli75xx_sysconf_PRB_PU_CFG_2(char *name, int size,
		int group, int num)
{
	char *str = "???";

	switch (num) {
	SYSCONF_REG(CFG_ST40_HOST_BOOT_ADDR);
	SYSCONF_REG(CFG_ST40_CTL_BOOT_ADDR);
	SYSCONF_REG(CFG_SYS10);
	SYSCONF_REG(CFG_RNG_BIST_CTL);
	SYSCONF_REG(CFG_SYS12);
	SYSCONF_REG(CFG_SYS13);
	SYSCONF_REG(CFG_SYS14);
	SYSCONF_REG(CFG_EMI_ROPC_STATUS);
	}

	strlcpy(name, str, size);
}

static void fli75xx_sysconf_TRS_SPARE_REGS_0(char *name, int size,
		int group, int num)
{
	char *str = "???";

	switch (num) {
	SYSCONF_REG(CFG_COMMS_CONFIG_1);
	SYSCONF_REG(CFG_TRS_CONFIG);
	SYSCONF_REG(CFG_COMMS_CONFIG_2);
	SYSCONF_REG(CFG_USB_SOFT_JTAG);
	SYSCONF_REG(CFG_TRS_SPARE_REG5_NOTUSED_0);
	SYSCONF_REG(CFG_TRS_CONFIG_2);
	SYSCONF_REG(CFG_COMMS_TRS_STATUS);
	SYSCONF_REG(CFG_EXTRA_ID1_LSB);
	}

	strlcpy(name, str, size);
}

static void fli75xx_sysconf_TRS_SPARE_REGS_1(char *name, int size,
		int group, int num)
{
	char *str = "???";

	switch (num) {
	SYSCONF_REG(CFG_SPARE_1);
	SYSCONF_REG(CFG_SPARE_2);
	SYSCONF_REG(CFG_SPARE_3);
	SYSCONF_REG(CFG_TRS_SPARE_REG4_NOTUSED);
	SYSCONF_REG(CFG_TRS_SPARE_REG5_NOTUSED_1);
	SYSCONF_REG(CFG_TRS_SPARE_REG6_NOTUSED);
	SYSCONF_REG(CFG_DEVICE_ID);
	SYSCONF_REG(CFG_EXTRA_ID1_MSB);
	}

	strlcpy(name, str, size);
}

static void fli75xx_sysconf_VDEC_PU_CFG_0(char *name, int size,
		int group, int num)
{
	char *str = "???";

	switch (num) {
	SYSCONF_REG(CFG_TOP_SPARE_REG1);
	SYSCONF_REG(CFG_TOP_SPARE_REG2);
	SYSCONF_REG(CFG_TOP_SPARE_REG3);
	SYSCONF_REG(CFG_ST231_DRA2_DEBUG);
	SYSCONF_REG(CFG_ST231_AUD1_DEBUG);
	SYSCONF_REG(CFG_ST231_AUD2_DEBUG);
	SYSCONF_REG(CFG_REG7_0);
	SYSCONF_REG(CFG_INTERRUPT);
	}

	strlcpy(name, str, size);
}

static void fli75xx_sysconf_VDEC_PU_CFG_1(char *name, int size,
		int group, int num)
{
	char *str = "???";

	switch (num) {
	SYSCONF_REG(CFG_ST231_DRA2_PERIPH_REG1);
	SYSCONF_REG(CFG_ST231_DRA2_BOOT_REG2);
	SYSCONF_REG(CFG_ST231_AUD1_PERIPH_REG3);
	SYSCONF_REG(CFG_ST231_AUD1_BOOT_REG4);
	SYSCONF_REG(CFG_ST231_AUD2_PERIPH_REG5);
	SYSCONF_REG(CFG_ST231_AUD2_BOOT_REG6);
	SYSCONF_REG(CFG_REG7_1);
	SYSCONF_REG(CFG_INTERRUPT_REG8);
	}

	strlcpy(name, str, size);
}

static void fli75xx_sysconf_VOUT_SPARE_REGS(char *name, int size,
		int group, int num)
{
	char *str = "???";

	switch (num) {
	SYSCONF_REG(CFG_REG1_VOUT_PIO_ALT_SEL);
	SYSCONF_REG(CFG_REG2_VOUT_PIO_ALT_SEL);
	SYSCONF_REG(CFG_VOUT_SPARE_REG3);
	SYSCONF_REG(CFG_REG4_DAC_CTRL);
	SYSCONF_REG(CFG_REG5_VOUT_DEBUG_PAD_CTL);
	SYSCONF_REG(CFG_REG6_TVOUT_DEBUG_CTL);
	SYSCONF_REG(CFG_REG7_UNUSED);
	}

	strlcpy(name, str, size);
}

static void fli75xx_sysconf_CKG_DDR(char *name, int size,
		int group, int num)
{
	char *str = "???";

	switch (num) {
	SYSCONF_REG(CKG_DDR_CTL_PLL_DDR_FREQ);
	SYSCONF_REG(CKG_DDR_STATUS_PLL_DDR);
	}

	strlcpy(name, str, size);
}

static void fli75xx_sysconf_PCIE_SPARE_REGS(char *name, int size,
		int group, int num)
{
	char *str = "???";

	switch (num) {
	SYSCONF_REG(CFG_REG1_PCIE_CORE_MIPHY_INIT);
	SYSCONF_REG(CFG_REG2_SPARE_OUTPUT_REG);
	SYSCONF_REG(CFG_REG3_MIPHY_INIT);
	SYSCONF_REG(CFG_REG4_PCIE_CORE_LINK_CTRL);
	SYSCONF_REG(CFG_REG5_PCIE_SPARE_OUTPUT_REG);
	SYSCONF_REG(CFG_REG6_PCIE_CORE_MIPHY_PCS_CTRL);
	SYSCONF_REG(CFG_REG7_PCIE_CORE_PCS_MIPHY_STATUS);
	SYSCONF_REG(CFG_REG8_PCIE_SYS_ERR_INTERRUPT);
	}

	strlcpy(name, str, size);
}

#endif

#ifdef CONFIG_DEBUG_FS
#define FLI75XX_REG_NAME_FUNC(name) name
#else
#define FLI75XX_REG_NAME_FUNC(name) NULL
#endif

#define FLI75XX_SYSCONF_ENTRY(_id, _name, _start)			\
	{								\
		.name = "stm-sysconf",					\
		.id = _id,						\
		.num_resources = 1,					\
		.resource = (struct resource[]) {			\
			STM_PLAT_RESOURCE_MEM(_start, 0x20),		\
		},							\
		.dev.platform_data = &(struct stm_plat_sysconf_data) {	\
			.groups_num = 1,				\
			.groups = (struct stm_plat_sysconf_group []) {	\
				{					\
					.group = _name,			\
					.offset = 0,			\
					.name = #_name,			\
					.reg_name = 			\
			 FLI75XX_REG_NAME_FUNC(fli75xx_sysconf_##_name),\
				},					\
			},						\
		},							\
	}


static struct platform_device fli75xx_sysconf_devices[] = {
	FLI75XX_SYSCONF_ENTRY(0, PRB_PU_CFG_1, 0xfd220000),
	FLI75XX_SYSCONF_ENTRY(1, PRB_PU_CFG_2, 0xfd228000),
	FLI75XX_SYSCONF_ENTRY(2, TRS_SPARE_REGS_0, 0xfd9ec000),
	FLI75XX_SYSCONF_ENTRY(3, TRS_SPARE_REGS_1, 0xfd9f4000),
	FLI75XX_SYSCONF_ENTRY(4, VDEC_PU_CFG_0, 0xfd7a0000),
	FLI75XX_SYSCONF_ENTRY(5, VDEC_PU_CFG_1, 0xfd7c0000),
	/* Address probed in fli75xx_sysconf_setup() as different for ultra */
	FLI75XX_SYSCONF_ENTRY(6, VOUT_SPARE_REGS, 0xfd5e8000),
	FLI75XX_SYSCONF_ENTRY(7, CKG_DDR, 0xfde80000),
	/* Only present on 7540 (ultra), moves on cut 1 so probed below  */
	FLI75XX_SYSCONF_ENTRY(8, PCIE_SPARE_REGS, 0xfe1c0000),
};

/* Only the ULTRA has the PCIE block */
#define FLI75XX_NUM_SYSCONFS ARRAY_SIZE(fli75xx_sysconf_devices) - \
			    (cpu_data->type != CPU_FLI7540)

static void fli75xx_sysconf_setup(void)
{
	struct resource *mem_res = &fli75xx_sysconf_devices[6].resource[0];

	if ((cpu_data->type != CPU_FLI7510) &&
		(cpu_data->type != CPU_FLI7560)) {
		mem_res->start = 0xfd5d4000;
		mem_res->end = mem_res->start + 0x20 - 1;

		if (cpu_data->type == CPU_FLI7540 &&
		    cpu_data->cut_major >= 1) {
			mem_res = fli75xx_sysconf_devices[8].resource;
			mem_res->start = 0xfe180000;
			mem_res->end = mem_res->start + 0x20 - 1;
		}
	}
}



/* Early initialisation-- --------------------------------------------------*/

/* Initialise devices which are required early in the boot process. */
void __init fli75xx_early_device_init(void)
{
	struct sysconf_field *sc;
	unsigned long verid;
	char *chip_variant;
	unsigned long devid;
	unsigned long chip_revision;
	int gpios_num;

	if (cpu_data->type == CPU_FLI7520) {
		/* CPU should be detected as 520 so far... */
		WARN_ON(!CPU_FLI7520);

		verid = *((unsigned *)0xfd9e9078) >> 16;

		switch (verid) {
		case 0x1d60:
			cpu_data->type = CPU_FLI7520;
			chip_variant = "520";
			break;
		case 0x1d6a:
			cpu_data->type = CPU_FLI7530;
			chip_variant = "530";
			break;
		case 0x1d74:
			cpu_data->type = CPU_FLI7540;
			chip_variant = "540";
			break;
		default:
			printk(KERN_WARNING "Wrong chip variant data, "
					"assuming FLI7540!\n");
			cpu_data->type = CPU_FLI7540;
			chip_variant = "520/530/540";
			break;
		}
	}

	/* Initialise PIO and sysconf drivers */
	fli75xx_sysconf_setup();
	sysconf_early_init(fli75xx_sysconf_devices, FLI75XX_NUM_SYSCONFS);

	if (cpu_data->type == CPU_FLI7510) {
		/* The FLI7510 and FLI7560 can only be
		 * differentiated by devid */
		sc = sysconf_claim(CFG_DEVICE_ID, 0, 31, "devid");
		devid = (sysconf_read(sc) >> 12) & 0x3ff;
		sysconf_release(sc);

		switch (devid) {
		case 0x40:
			cpu_data->type = CPU_FLI7560;
			chip_variant = "560";
			break;
		default:
			printk(KERN_WARNING "Wrong devid, assuming FLI7510!\n");
		case 0x3c:
			chip_variant = "510";
			break;
		}
	}

	switch (cpu_data->type) {
	case CPU_FLI7510:
	case CPU_FLI7560:
		gpios_num = ARRAY_SIZE(fli7510_pio_devices);
		stm_gpio_early_init(fli7510_pio_devices, gpios_num,
					ILC_FIRST_IRQ + ILC_NR_IRQS);
		break;
	case CPU_FLI7520:
	case CPU_FLI7530:
	case CPU_FLI7540:
		gpios_num = ARRAY_SIZE(fli7520_pio_devices);
		stm_gpio_early_init(fli7520_pio_devices, gpios_num,
				ILC_FIRST_IRQ + ILC_NR_IRQS);
		break;
	default:
		BUG();
	}

	stm_pad_init(gpios_num * STM_GPIO_PINS_PER_PORT,
		     -1, 0, fli75xx_pio_config);

	sc = sysconf_claim(CFG_DEVICE_ID, 0, 31, "devid");
	devid = sysconf_read(sc);
	chip_revision = (devid >> 28);
	boot_cpu_data.cut_major = chip_revision;
	sysconf_release(sc);

	printk(KERN_INFO "Freeman %s version %ld.x, ST40%s core\n",
			chip_variant, chip_revision,
			FLI7510_ST40HOST_CORE ? "HOST" : "RT");

	/* We haven't configured the LPC, so the sleep instruction may
	 * do bad things. Thus we disable it here. */
	disable_hlt();
}



/* Pre-arch initialisation ------------------------------------------------ */

static int __init fli75xx_postcore_setup(void)
{
	int result;
	int i;

	result = platform_device_register(&fli75xx_emi);

	switch (cpu_data->type) {
	case CPU_FLI7510:
	case CPU_FLI7560:
		for (i = 0; i < ARRAY_SIZE(fli7510_pio_devices) &&
						result == 0; i++) {
			result = platform_device_register(
					&fli7510_pio_devices[i]);
		}
		break;
	case CPU_FLI7520:
	case CPU_FLI7530:
	case CPU_FLI7540:
		for (i = 0; i < ARRAY_SIZE(fli7520_pio_devices) &&
						result == 0; i++) {
			/* Skip non-existing ports... */
			if (fli7520_pio_devices[i].name)
				result = platform_device_register(
					&fli7520_pio_devices[i]);
		}
		break;
	default:
		BUG();
	}

	return result;
}
postcore_initcall(fli75xx_postcore_setup);



/* Late initialisation ---------------------------------------------------- */

static struct platform_device *fli75xx_devices[] __initdata = {
	&fli75xx_fdma_devices[0],
	&fli75xx_fdma_devices[1],
	&fli75xx_fdma_xbar_device,
	&fli75xx_rng_hwrandom_device,
	&fli75xx_rng_devrandom_device,
};

static int __init fli75xx_devices_setup(void)
{
	int err;
	int i;

	err = platform_add_devices(fli75xx_devices,
				   ARRAY_SIZE(fli75xx_devices));
	for (i = 0; i < FLI75XX_NUM_SYSCONFS && !err; i++)
		err = platform_device_register(fli75xx_sysconf_devices + i);

	return err;
}
device_initcall(fli75xx_devices_setup);
