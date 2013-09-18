/*
 * -------------------------------------------------------------------------
 * Copyright (C) 2012  STMicroelectronics
 * Author: Francesco M. Virlinzi  <francesco.virlinzi@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License V.2 ONLY.  See linux/COPYING for more information.
 *
 * -------------------------------------------------------------------------
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/irqflags.h>
#include <linux/io.h>

#include <linux/stm/stxh205.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/platform.h>
#include <linux/stm/gpio.h>

#include <asm/irq-ilc.h>

#include "stm_hom.h"
#include <linux/stm/poke_table.h>
#include <linux/stm/synopsys_dwc_ddr32.h>


#define DDR3SS_REG		0xfde50000

#define PCLK	30000000
#define BAUDRATE_VAL_M1(bps)    	\
	((((bps * (1 << 14)) + (1 << 13)) / (PCLK / (1 << 6))))

#define SBC_MBX				0xfe4b4000
#define SBC_MBX_WRITE_STATUS(x)		(SBC_MBX + 0x4 + 0x4 * (x))

#define SBC_GPIO_PORT(_nr)		(0xfe610000 + (_nr) * 0x1000)

#define LMI_RET_GPIO_PORT		3
#define LMI_RET_GPIO_PIN		3
#define LMI_RETENTION_PIN	stm_gpio(LMI_RET_GPIO_PORT, LMI_RET_GPIO_PIN)

static unsigned long stxh205_hom_table[] __cacheline_aligned = {
synopsys_ddr32_in_hom(DDR3SS_REG),

/*
 * Enable retention mode gpio
 *
 */
POKE32(SBC_GPIO_PORT(LMI_RET_GPIO_PORT) + STM_GPIO_REG_CLR_POUT,
	 1 << LMI_RET_GPIO_PIN),

/*
 * Send message 'ENTER_PASSIVE' (0x5)
 */
POKE32(SBC_MBX_WRITE_STATUS(0), 0x5),
/* END. */
END_MARKER,

};

static void __iomem *early_console_base;

static void stxh205_hom_early_console(void)
{

	writel(0x1189 & ~0x80, early_console_base + 0x0c); /* ctrl */
	writel(BAUDRATE_VAL_M1(115200), early_console_base); /* baud */
	writel(20, early_console_base + 0x1c);  /* timeout */
	writel(1, early_console_base + 0x10); /* int */
	writel(0x1189, early_console_base + 0x0c); /* ctrl */

	mdelay(100);

	pr_info("Early console ready\n");
}

static int stxh205_hom_prepare(void)
{
	stm_freeze_board();

	return 0;
}

static int stxh205_hom_complete(void)
{
	/* Enable the INTC2 */
	writel(7, 0xfda30000 + 0x00);	/* INTPRI00 */
	writel(1, 0xfda30000 + 0x60);	/* INTMSKCLR00 */

	stm_restore_board();

	return 0;
}

static struct stm_mem_hibernation stxh205_hom = {

	.tbl_addr = (unsigned long)stxh205_hom_table,
	.tbl_size = DIV_ROUND_UP(ARRAY_SIZE(stxh205_hom_table) *
			sizeof(long), L1_CACHE_BYTES),

	.ops.prepare = stxh205_hom_prepare,
	.ops.complete = stxh205_hom_complete,
};

static int __init hom_stxh205_setup(void)
{
	int ret;

	ret = gpio_request(LMI_RETENTION_PIN, "LMI retention mode");
	if (ret) {
		pr_err("[STM]: [PM]: [HoM]: GPIO for retention mode"
			"not acquired\n");
		return ret;
	};

	gpio_direction_output(LMI_RETENTION_PIN, 1);

	ret = stm_hom_register(&stxh205_hom);
	if (ret) {
		gpio_free(LMI_RETENTION_PIN);
		return ret;
	}

	early_console_base = (void *)
		ioremap(stm_asc_configured_devices[stm_asc_console_device]
			->resource[0].start, 0x1000);

	pr_info("[STM]: [PM]: [HoM]: Early console @ %p\n",
		early_console_base);
	stxh205_hom.early_console = stxh205_hom_early_console;

	return ret;
}

module_init(hom_stxh205_setup);
