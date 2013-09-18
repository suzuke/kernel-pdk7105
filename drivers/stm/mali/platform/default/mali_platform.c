/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for a default platform
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"
#include <linux/err.h>
#include <linux/stm/clk.h>

static struct clk *mali_clk;

_mali_osk_errcode_t __init mali_platform_init(void)
{
	char *mali_clk_n = "gpu_clk";

	mali_clk = clk_get(NULL, mali_clk_n);
	if (IS_ERR(mali_clk))
		MALI_DEBUG_PRINT(2, ("PM clk %s not found\n", mali_clk_n));
	else
		clk_enable(mali_clk);

	MALI_SUCCESS;
}

_mali_osk_errcode_t __exit mali_platform_deinit(void)
{
	if (mali_clk)
		clk_disable(mali_clk);
	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
	if (mali_clk) {
		MALI_DEBUG_PRINT(4, ("PM mode_change %s\n",
				power_mode ? "SLEEP" : "ON"));
		if (power_mode == MALI_POWER_MODE_ON)
			clk_enable(mali_clk);
		else
			clk_disable(mali_clk);
	}
	MALI_SUCCESS;
}

void mali_gpu_utilization_handler(u32 utilization)
{
}

void set_mali_parent_power_domain(void* dev)
{
}


