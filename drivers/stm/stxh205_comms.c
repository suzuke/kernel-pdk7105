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
#include <linux/i2c.h>
#include <linux/stm/pad.h>
#include <linux/stm/stxh205.h>
#include <asm/irq-ilc.h>



/* ASC resources ---------------------------------------------------------- */

static struct stm_pad_config stxh205_asc_pad_configs[] = {
	[0] = {
		.gpios_num = 4,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_OUT(10, 0, 2),	/* TX */
			STM_PAD_PIO_IN(10, 1, 2),	/* RX */
			STM_PAD_PIO_IN_NAMED(10, 2, 2, "CTS"),
			STM_PAD_PIO_OUT_NAMED(10, 3, 2, "RTS"),
		},
	},
	[1] = {
		.gpios_num = 4,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_OUT(11, 0, 3),	/* TX */
			STM_PAD_PIO_IN(11, 1, 3),	/* RX */
			STM_PAD_PIO_IN_NAMED(11, 4, 3, "CTS"),
			STM_PAD_PIO_OUT_NAMED(11, 2, 3, "RTS"),
		},
	},
	[2] = {
		.gpios_num = 4,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_OUT(15, 4, 1),	/* TX */
			STM_PAD_PIO_IN(15, 5, 1),	/* RX */
			STM_PAD_PIO_IN_NAMED(15, 6, 1, "CTS"),
			STM_PAD_PIO_OUT_NAMED(15, 7, 1, "RTS"),
		},
	},
	[3] = {
		/* ASC10 / UART10 */
		.gpios_num = 4,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_OUT(3, 5, 2),	/* TX */
			STM_PAD_PIO_IN(3, 6, 2),	/* RX */
			STM_PAD_PIO_IN_NAMED(3, 7, 2, "CTS"),
			STM_PAD_PIO_OUT_NAMED(3, 4, 2, "RTS"),
		},
	},
	[4] = {
		/* ASC11 / UART11 */
		.gpios_num = 4,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_OUT(2, 6, 3),	/* TX */
			STM_PAD_PIO_IN(2, 7, 3),	/* RX */
			STM_PAD_PIO_IN_NAMED(3, 0, 3, "CTS"),
			STM_PAD_PIO_OUT_NAMED(3, 1, 3, "RTS"),
		},
	},
};

static struct platform_device stxh205_asc_devices[] = {
	[0] = {
		.name		= "stm-asc",
		/* .id set in stxh205_configure_asc() */
		.num_resources	= 4,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd730000, 0x100),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(40), -1),
			STM_PLAT_RESOURCE_DMA_NAMED("rx_half_full", 11),
			STM_PLAT_RESOURCE_DMA_NAMED("tx_half_empty", 15),
		},
		.dev.platform_data = &(struct stm_plat_asc_data) {
			.pad_config = &stxh205_asc_pad_configs[0],
		},
	},
	[1] = {
		.name		= "stm-asc",
		/* .id set in stxh205_configure_asc() */
		.num_resources	= 4,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd731000, 0x100),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(41), -1),
			STM_PLAT_RESOURCE_DMA_NAMED("rx_half_full", 12),
			STM_PLAT_RESOURCE_DMA_NAMED("tx_half_empty", 16),
		},
		.dev.platform_data = &(struct stm_plat_asc_data) {
			.pad_config = &stxh205_asc_pad_configs[1],
		},
	},
	[2] = {
		.name		= "stm-asc",
		/* .id set in stxh205_configure_asc() */
		.num_resources	= 4,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd732000, 0x100),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(42), -1),
			STM_PLAT_RESOURCE_DMA_NAMED("rx_half_full", 40),
			STM_PLAT_RESOURCE_DMA_NAMED("tx_half_empty", 43),
		},
		.dev.platform_data = &(struct stm_plat_asc_data) {
			.pad_config = &stxh205_asc_pad_configs[2],
		},
	},
	[3] = {
		/* ASC10 / UART10 */
		.name		= "stm-asc",
		/* .id set in stxh205_configure_asc() */
		.num_resources	= 4,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe530000, 0x100),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(43), -1),
			STM_PLAT_RESOURCE_DMA_NAMED("rx_half_full", 13),
			STM_PLAT_RESOURCE_DMA_NAMED("tx_half_empty", 17),
		},
		.dev.platform_data = &(struct stm_plat_asc_data) {
			.pad_config = &stxh205_asc_pad_configs[3],
			.clk_id = "sbc_comms_clk",
		},
	},
	[4] = {
		/* ASC11 / UART11 */
		.name		= "stm-asc",
		/* .id set in stxh205_configure_asc() */
		.num_resources	= 4,
		.resource	= (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe531000, 0x100),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(44), -1),
			STM_PLAT_RESOURCE_DMA_NAMED("rx_half_full", 14),
			STM_PLAT_RESOURCE_DMA_NAMED("tx_half_empty", 18),
		},
		.dev.platform_data = &(struct stm_plat_asc_data) {
			.pad_config = &stxh205_asc_pad_configs[4],
			.clk_id = "sbc_comms_clk",
		},
	},
};

/* Note these three variables are global, and shared with the stasc driver
 * for console bring up prior to platform initialisation. */

/* the serial console device */
int __initdata stm_asc_console_device;

/* Platform devices to register */
unsigned int __initdata stm_asc_configured_devices_num;
struct platform_device __initdata
		*stm_asc_configured_devices[ARRAY_SIZE(stxh205_asc_devices)];

void __init stxh205_configure_asc(int asc, struct stxh205_asc_config *config)
{
	static int configured[ARRAY_SIZE(stxh205_asc_devices)];
	static int tty_id;
	struct stxh205_asc_config default_config = {};
	struct platform_device *pdev;
	struct stm_plat_asc_data *plat_data;

	BUG_ON(asc < 0 || asc >= ARRAY_SIZE(stxh205_asc_devices));

	BUG_ON(configured[asc]);
	configured[asc] = 1;

	if (!config)
		config = &default_config;

	pdev = &stxh205_asc_devices[asc];
	plat_data = pdev->dev.platform_data;

	pdev->id = tty_id++;
	plat_data->hw_flow_control = config->hw_flow_control;
	plat_data->txfifo_bug = 1;

	if (!config->hw_flow_control) {
		/* Don't claim RTS/CTS pads */
		struct stm_pad_config *pad_config;
		pad_config = &stxh205_asc_pad_configs[asc];
		stm_pad_set_pio_ignored(pad_config, "RTS");
		stm_pad_set_pio_ignored(pad_config, "CTS");
	}

	if (config->is_console)
		stm_asc_console_device = pdev->id;

	stm_asc_configured_devices[stm_asc_configured_devices_num++] = pdev;
}

/* Add platform device as configured by board specific code */
static int __init stxh205_add_asc(void)
{
	return platform_add_devices(stm_asc_configured_devices,
			stm_asc_configured_devices_num);
}
arch_initcall(stxh205_add_asc);

/* SSC resources ---------------------------------------------------------- */

/* Pad configuration for I2C mode */
static struct stm_pad_config stxh205_ssc_i2c_pad_configs[] = {
	/*
	 * Configuration for SSC0-3 is created in stxh205_configure_ssc_*(),
	 * according to passed routing information.
	 */
	[4] = {
		/* SSC10 */
		.gpios_num = 2,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_BIDIR_NAMED(3, 5, 1, "SCL"),
			STM_PAD_PIO_BIDIR_NAMED(3, 6, 1, "SDA"),
		},
	},
	[5] = {
		/* SSC11 */
		.gpios_num = 2,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_BIDIR_NAMED(2, 6, 2, "SCL"),
			STM_PAD_PIO_BIDIR_NAMED(2, 7, 2, "SDA"),
		},
	},
	[6] = {
		/* SSC12 */
		.gpios_num = 2,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_BIDIR_NAMED(1, 7, 2, "SCL"),
			STM_PAD_PIO_BIDIR_NAMED(1, 6, 2, "SDA"),
		},
	},
};

/* Pad configuration for SPI mode */
static struct stm_pad_config stxh205_ssc_spi_pad_configs[] = {
	/*
	 * Configuration for SSC0-3 is created in stxh205_configure_ssc_*(),
	 * according to passed routing information.
	 */
	[4] = {
		/* SSC10 */
		.gpios_num = 3,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_OUT(3, 5, 1),	/* SCL */
			STM_PAD_PIO_OUT(3, 6, 1),	/* MTSR */
			STM_PAD_PIO_IN(3, 7, 3),	/* MRST */
		},
	},
	[5] = {
		/* SSC11 */
		.gpios_num = 3,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_OUT(2, 6, 2),	/* SCL */
			STM_PAD_PIO_OUT(2, 7, 2),	/* MTSR */
			STM_PAD_PIO_IN(3, 2, 2),	/* MRST */
		},
	},
	[6] = {
		/* SSC12 */
		.gpios_num = 3,
		.gpios = (struct stm_pad_gpio []) {
			STM_PAD_PIO_OUT(1, 7, 2),	/* SCL */
			STM_PAD_PIO_OUT(1, 6, 2),	/* MTSR */
			STM_PAD_PIO_IN(1, 2, 2),	/* MRST */
		},
	},
};

static struct platform_device stxh205_ssc_devices[] = {
	[0] = {
		/* .name & .id set in stxh205_configure_ssc_*() */
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd740000, 0x110),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(33), -1),
		},
		.dev.platform_data = &(struct stm_plat_ssc_data) {
			/* .pad_config_* set in stxh205_configure_ssc_*() */
		},
	},
	[1] = {
		/* .name & .id set in stxh205_configure_ssc_*() */
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd741000, 0x110),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(34), -1),
		},
		.dev.platform_data = &(struct stm_plat_ssc_data) {
			/* .pad_config_* set in stxh205_configure_ssc_*() */
		},
	},
	[2] = {
		/* .name & .id set in stxh205_configure_ssc_*() */
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd742000, 0x110),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(35), -1),
		},
		.dev.platform_data = &(struct stm_plat_ssc_data) {
			/* .pad_config_* set in stxh205_configure_ssc_*() */
		},
	},
	[3] = {
		/* .name & .id set in stxh205_configure_ssc_*() */
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfd743000, 0x110),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(36), -1),
		},
		.dev.platform_data = &(struct stm_plat_ssc_data) {
			/* .pad_config_* set in stxh205_configure_ssc_*() */
		},
	},
	[4] = {
		/* SSC10 */
		/* .name & .id set in stxh205_configure_ssc_*() */
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe540000, 0x110),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(37), -1),
		},
		.dev.platform_data = &(struct stm_plat_ssc_data) {
			/* .pad_config_* set in stxh205_configure_ssc_*() */
		},
	},
	[5] = {
		/* SSC11 */
		/* .name & .id set in stxh205_configure_ssc_*() */
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe541000, 0x110),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(38), -1),
		},
		.dev.platform_data = &(struct stm_plat_ssc_data) {
			/* .pad_config_* set in stxh205_configure_ssc_*() */
		},
	},
	[6] = {
		/* SSC12 */
		/* .name & .id set in stxh205_configure_ssc_*() */
		.num_resources = 2,
		.resource = (struct resource[]) {
			STM_PLAT_RESOURCE_MEM(0xfe542000, 0x110),
			STM_PLAT_RESOURCE_IRQ(ILC_IRQ(39), -1),
		},
		.dev.platform_data = &(struct stm_plat_ssc_data) {
			/* .pad_config_* set in stxh205_configure_ssc_*() */
		},
	},
};

static struct stm_pad_config * __init stxh205_configure_ssc_pads(
		int ssc, int num_pads, int *routing, char **names)
{
	static struct {
		unsigned int port, pin, alt;
	} confs[][3][4] = {
		[0] = {
			[0] = {
				[stxh205_ssc0_sclk_pio9_2] = { 9, 2, 1 },
				[stxh205_ssc0_sclk_pio6_2] = { 6, 2, 2 },
			},
			[1] = {
				[stxh205_ssc0_mtsr_pio9_3] = { 9, 3, 1 },
				[stxh205_ssc0_mtsr_pio6_3] = { 6, 3, 2 },
			},
			[2] = {
				[stxh205_ssc0_mrst_pio9_7] = { 9, 7, 1 },
			}
		},
		[1] = {
			[0] = {
				[stxh205_ssc1_sclk_pio4_6] = { 4, 6, 1 },
				[stxh205_ssc1_sclk_pio12_0] = { 12, 0, 1 },
			},
			[1] = {
				[stxh205_ssc1_mtsr_pio4_7] = { 4, 7, 1 },
				[stxh205_ssc1_mtsr_pio12_1] = { 12, 1, 1 },
			},
			[2] = {
				[stxh205_ssc1_mrst_pio4_5] = { 4, 5, 2 },
				[stxh205_ssc1_mrst_pio11_7] = { 11, 7, 1 },
			}
		},
		[2] = {
			[0] = {
				[stxh205_ssc2_sclk_pio7_6] = { 7, 6, 2 },
				[stxh205_ssc2_sclk_pio8_6] = { 8, 6, 5 },
				[stxh205_ssc2_sclk_pio9_4] = { 9, 4, 2 },
				[stxh205_ssc2_sclk_pio_10_5] = { 10, 5, 4 },
			},
			[1] = {
				[stxh205_ssc2_mtsr_pio7_7] = { 7, 7, 2 },
				[stxh205_ssc2_mtsr_pio8_7] = { 8, 7, 5 },
				[stxh205_ssc2_mtsr_pio9_5] = { 9, 5, 2 },
				[stxh205_ssc2_mtsr_pio10_6] = { 10, 6, 4 },
			},
			[2] = {
				[stxh205_ssc2_mrst_pio7_0] = { 7, 0, 2 },
				[stxh205_ssc2_mrst_pio8_5] = { 8, 5, 5 },
				[stxh205_ssc2_mrst_pio9_7] = { 9, 7, 2 },
				[stxh205_ssc2_mrst_pio10_7] = { 10, 7, 4 },
			}
		},
		[3] = {
			[0] = {
				[stxh205_ssc3_sclk_pio13_4] = { 13, 4, 4 },
				[stxh205_ssc3_sclk_pio15_0] = { 15, 0, 1 },
				[stxh205_ssc3_sclk_pio15_5] = { 15, 5, 2 },
			},
			[1] = {
				[stxh205_ssc3_mtsr_pio13_5] = { 13, 5, 4 },
				[stxh205_ssc3_mtsr_pio15_1] = { 15, 1, 1 },
				[stxh205_ssc3_mtsr_pio15_6] = { 15, 6, 2 },
			},
			[2] = {
				[stxh205_ssc3_mrst_pio13_6] = { 13, 6, 4 },
				[stxh205_ssc3_mrst_pio15_2] = { 15, 2, 1 },
				[stxh205_ssc3_mrst_pio15_7] = { 15, 7, 2 },
			}
		}
	};
	struct stm_pad_config *pad_config;
	int i;

	BUG_ON((ssc > ARRAY_SIZE(confs)) || (num_pads > 3));

	pad_config = stm_pad_config_alloc(num_pads, 0);

	for (i = 0; i < num_pads; i++)
		stm_pad_config_add_pio_bidir_named(pad_config,
			confs[ssc][i][routing[i]].port,
			confs[ssc][i][routing[i]].pin,
			confs[ssc][i][routing[i]].alt,
			names[i]);

	return pad_config;
};

static int __initdata stxh205_ssc_configured[ARRAY_SIZE(stxh205_ssc_devices)];

int __init stxh205_configure_ssc_i2c(int ssc, struct stxh205_ssc_config *config)
{
	static int i2c_busnum;
	struct stxh205_ssc_config default_config = {};
	struct stm_plat_ssc_data *plat_data;
	struct stm_pad_config *pad_config;

	BUG_ON(ssc < 0 || ssc >= ARRAY_SIZE(stxh205_ssc_devices));

	BUG_ON(stxh205_ssc_configured[ssc]);
	stxh205_ssc_configured[ssc] = 1;

	if (!config)
		config = &default_config;

	stxh205_ssc_devices[ssc].name = "i2c-stm";
	stxh205_ssc_devices[ssc].id = i2c_busnum;

	plat_data = stxh205_ssc_devices[ssc].dev.platform_data;

	if (ssc < 4) {
		pad_config = stxh205_configure_ssc_pads(ssc, 2,
			(int[]){
				config->routing.ssc0.sclk,
				config->routing.ssc0.mtsr
			},
			(char*[]) {"SCL", "SDA"});
	} else {
		pad_config = &stxh205_ssc_i2c_pad_configs[ssc];
	}

	plat_data->pad_config = pad_config;
	plat_data->i2c_fastmode = config->i2c_fastmode;

	/* I2C bus number reservation (to prevent any hot-plug device
	 * from using it) */
	i2c_register_board_info(i2c_busnum, NULL, 0);

	platform_device_register(&stxh205_ssc_devices[ssc]);

	return i2c_busnum++;
}

int __init stxh205_configure_ssc_spi(int ssc, struct stxh205_ssc_config *config)
{
	static int spi_busnum;
	struct stxh205_ssc_config default_config = {};
	struct stm_plat_ssc_data *plat_data;
	struct stm_pad_config *pad_config;

	BUG_ON(ssc < 0 || ssc >= ARRAY_SIZE(stxh205_ssc_devices));

	BUG_ON(stxh205_ssc_configured[ssc]);
	stxh205_ssc_configured[ssc] = 1;

	if (!config)
		config = &default_config;

	stxh205_ssc_devices[ssc].name = "spi-stm";
	stxh205_ssc_devices[ssc].id = spi_busnum;

	plat_data = stxh205_ssc_devices[ssc].dev.platform_data;

	if (ssc < 4) {
		pad_config = stxh205_configure_ssc_pads(ssc, 3,
			(int[]){
				config->routing.ssc0.sclk,
				config->routing.ssc0.mtsr,
				config->routing.ssc0.mrst
			},
			(char*[]) { "SDA", "MTSR", "MRST"});
	} else {
		pad_config = &stxh205_ssc_spi_pad_configs[ssc];
	}

	plat_data->spi_chipselect = config->spi_chipselect;
	plat_data->pad_config = pad_config;

	platform_device_register(&stxh205_ssc_devices[ssc]);

	return spi_busnum++;
}


/* LiRC resources --------------------------------------------------------- */

static struct platform_device stxh205_lirc_device = {
	.name = "lirc-stm",
	.id = -1,
	.num_resources = 2,
	.resource = (struct resource []) {
		STM_PLAT_RESOURCE_MEM(0xfe518000, 0x234),
		STM_PLAT_RESOURCE_IRQ(ILC_IRQ(5), -1),
	},
	.dev.platform_data = &(struct stm_plat_lirc_data) {
		/* The clock settings will be calculated by
		 * the driver from the system clock */
		.irbclock	= 0, /* use current_cpu data */
		.irbclkdiv	= 0, /* automatically calculate */
		.irbperiodmult	= 0,
		.irbperioddiv	= 0,
		.irbontimemult	= 0,
		.irbontimediv	= 0,
		.irbrxmaxperiod = 0x5000,
		.sysclkdiv	= 1,
		.rxpolarity	= 1,
	},
};

void __init stxh205_configure_lirc(struct stxh205_lirc_config *config)
{
	static int configured;
	struct stxh205_lirc_config default_config = {};
	struct stm_plat_lirc_data *plat_data =
			stxh205_lirc_device.dev.platform_data;
	struct stm_pad_config *pad_config;

	BUG_ON(configured);
	configured = 1;

	if (!config)
		config = &default_config;

	pad_config = stm_pad_config_alloc(3, 2);
	BUG_ON(!pad_config);

	plat_data->txenabled = config->tx_enabled || config->tx_od_enabled;
	plat_data->pads = pad_config;

	/* IRB_ENABLE = is Enabled */
	stm_pad_config_add_sysconf(pad_config, LPM_SYSCONF_BANK,
						1, 6, 6, 1);

	/* IRB_LOWPOWERMODE = Normal mode */
	stm_pad_config_add_sysconf(pad_config, LPM_SYSCONF_BANK,
						1, 7, 7, 0);

	switch (config->rx_mode) {
	case stxh205_lirc_rx_disabled:
		/* Nothing to do */
		break;
	case stxh205_lirc_rx_mode_ir:
		plat_data->rxuhfmode = 0;
		stm_pad_config_add_pio_in(pad_config, 3, 4, 1);
		break;
	case stxh205_lirc_rx_mode_uhf:
		plat_data->rxuhfmode = 1;
		stm_pad_config_add_pio_in(pad_config, 2, 5, 3);
		break;
	default:
		BUG();
		break;
	}

	if (config->tx_enabled)
		stm_pad_config_add_pio_out(pad_config, 3, 5, 3);

	if (config->tx_od_enabled)
		stm_pad_config_add_pio_out(pad_config, 3, 6, 3);

	clk_add_alias_platform_device(NULL, &stxh205_lirc_device,
				      "sbc_comms_clk", NULL);

	platform_device_register(&stxh205_lirc_device);
}



/* PWM resources ---------------------------------------------------------- */

static struct stm_plat_pwm_data stxh205_pwm_platform_data = {
	.channel_pad_config = {
		[0] = &(struct stm_pad_config) {
			.gpios_num = 1,
			.gpios = (struct stm_pad_gpio []) {
				STM_PAD_PIO_OUT(3, 0, 1),
			},
		},
		[1] = &(struct stm_pad_config) {
			.gpios_num = 1,
			.gpios = (struct stm_pad_gpio []) {
				STM_PAD_PIO_OUT(2, 2, 4),
			},
		},
	},
};

static struct platform_device stxh205_pwm_device = {
	.name = "stm-pwm",
	.id = -1,
	.num_resources = 2,
	.resource = (struct resource[]) {
		STM_PLAT_RESOURCE_MEM(0xfe510000, 0x68),
		STM_PLAT_RESOURCE_IRQ(ILC_IRQ(51), -1),
	},
	.dev.platform_data = &stxh205_pwm_platform_data,
};

void __init stxh205_configure_pwm(struct stxh205_pwm_config *config)
{
	static int configured;

	BUG_ON(configured);
	configured = 1;

	if (config) {
		stxh205_pwm_platform_data.channel_enabled[0] =
				config->out10_enabled;
		stxh205_pwm_platform_data.channel_enabled[1] =
				config->out11_enabled;
	}

	platform_device_register(&stxh205_pwm_device);
}

static struct platform_device stxh205_lpc_device = {
	.name	= "stm-rtc",
	.id	= -1,
	.num_resources  = 2,
	.resource = (struct resource[]){
		STM_PLAT_RESOURCE_MEM(0xfd543000, 0x1000),
		STM_PLAT_RESOURCE_IRQ(ILC_IRQ(6), -1),
	},
	.dev.platform_data = &(struct stm_plat_rtc_lpc) {
		.need_wdt_reset = 1,
		.irq_edge_level = IRQ_TYPE_EDGE_FALLING,
		/*
		 * the lpc_clk is initialize @ 350KHz
		 */
		.force_clk_rate = 350000,
	}
};

/* Late initialisation ---------------------------------------------------- */

static struct platform_device *stxh205_devices[] __initdata = {
	&stxh205_lpc_device,
};

static int __init stxh205_devices_setup(void)
{
	return platform_add_devices(stxh205_devices,
			ARRAY_SIZE(stxh205_devices));
}
device_initcall(stxh205_devices_setup);
