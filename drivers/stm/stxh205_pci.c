/*
 * (c) 2012 STMicroelectronics Limited
 *
 * Author: David McKay <david.mckay@s.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/stm/pad.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/emi.h>
#include <linux/stm/stxh205.h>
#include <asm/irq-ilc.h>

#define PCIE_SYS_INT		(1<<5)
#define PCIE_P1_SSC_EN		(1<<4)
#define PCIE_APP_REQ_RETRY_EN	(1<<3)
#define PCIE_APP_LTSSM_ENABLE	(1<<2)
#define PCIE_APP_INIT_RST	(1<<1)
#define PCIE_DEVICE_TYPE	(1<<0)

#define PCIE_DEFAULT_VAL	(PCIE_DEVICE_TYPE)

static void stxh205_pcie_init(void *handle)
{
	struct sysconf_field *sc = (struct sysconf_field *) handle;

	sysconf_write(sc, PCIE_DEFAULT_VAL);

	mdelay(1);
}

static void stxh205_pcie_enable_ltssm(void *handle)
{
	struct sysconf_field *sc = (struct sysconf_field *) handle;

	sysconf_write(sc, PCIE_DEFAULT_VAL | PCIE_APP_LTSSM_ENABLE);
}

static void stxh205_pcie_disable_ltssm(void *handle)
{
	struct sysconf_field *sc = (struct sysconf_field *) handle;

	sysconf_write(sc, PCIE_DEFAULT_VAL);
}

/* Ops to drive the platform specific bits of the interface */
static struct stm_plat_pcie_ops stxh205_pcie_ops = {
	.init          = stxh205_pcie_init,
	.enable_ltssm  = stxh205_pcie_enable_ltssm,
	.disable_ltssm = stxh205_pcie_disable_ltssm,
};

static struct stm_plat_pcie_config stxh205_plat_pcie_config = {
	.ahb_val = 0x264207,
	.ops = &stxh205_pcie_ops,
};

/* PCI express support */
#define PCIE_MEM_START 0x20000000
#define PCIE_MEM_SIZE  0x20000000
#define MSI_FIRST_IRQ 	(NR_IRQS - 29)
#define MSI_LAST_IRQ 	(NR_IRQS - 1)

static struct platform_device stxh205_pcie_device = {
	.name = "pcie_stm",
	.id = -1,
	.num_resources = 8,
	.resource = (struct resource[]) {
		/* Main PCI window, 960 MB */
		STM_PLAT_RESOURCE_MEM_NAMED("pcie memory",
					    PCIE_MEM_START, PCIE_MEM_SIZE),
		/* There actually is no IO for the PCI express cell, so this
		 * is a dummy really.
		 */
		{
			.name = "pcie io",
			.start = 0x400,
			.end = 0x1fff,
			.flags = IORESOURCE_IO,
		},
		STM_PLAT_RESOURCE_MEM_NAMED("pcie cntrl", 0xfd900000, 0x1000),
		STM_PLAT_RESOURCE_MEM_NAMED("pcie ahb", 0xfd908000, 0x8),
		STM_PLAT_RESOURCE_IRQ_NAMED("pcie inta", ILC_IRQ(53), -1),
		STM_PLAT_RESOURCE_IRQ_NAMED("pcie syserr", ILC_IRQ(55), -1),
		STM_PLAT_RESOURCE_IRQ_NAMED("msi mux", ILC_IRQ(56), -1),
		{
			.start = MSI_FIRST_IRQ,
			.end  = MSI_LAST_IRQ,
			.name = "msi range",
			.flags = IORESOURCE_IRQ,
		}
	},
	.dev.platform_data = &stxh205_plat_pcie_config,
};


void __init stxh205_configure_pcie(struct stxh205_pcie_config *config)
{
	struct sysconf_field *sc;

	sc = sysconf_claim(SYSCONF(447), 0, 5, "pcie");

	BUG_ON(!sc);

	stxh205_plat_pcie_config.ops_handle = sc;
	stxh205_plat_pcie_config.reset_gpio = config->reset_gpio;
	stxh205_plat_pcie_config.reset = config->reset;
	/* There is only one PCIe controller on the stxh205 */
	stxh205_plat_pcie_config.miphy_num = 0;

	platform_device_register(&stxh205_pcie_device);
}


