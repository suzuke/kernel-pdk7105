/*
 * (C) Copyright 2012 STMicroelectronics R&D Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef __ARCH_CONFIG_H__
#define __ARCH_CONFIG_H__

#include <asm/irq-ilc.h>

#ifdef CONFIG_CPU_SUBTYPE_STX7108
#define MALI_BASE_ADDR		0xfe710000
#define MALI_IRQ_GP		ILC_IRQ(115)
#define MALI_IRQ_GP_MMU		ILC_IRQ(116)
#define MALI_IRQ_PP0		ILC_IRQ(113)
#define MALI_IRQ_PP0_MMU	ILC_IRQ(114)
#define MALI_ONLY_ONE_PP
#else
#error *** Architecture not supported ***
#endif

/* Configuration for the STM platforms */

static _mali_osk_resource_t arch_configuration [] =
{
	{
		.type = MALI400GP,
		.description = "Mali-400 GP",
		.base = MALI_BASE_ADDR,
		.irq = MALI_IRQ_GP,
		.mmu_id = 1
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDR + 0x8000,
		.irq = MALI_IRQ_PP0,
		.description = "Mali-400 PP 0",
		.mmu_id = 2
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDR + 0x3000,
		.irq = MALI_IRQ_GP_MMU,
		.description = "Mali-400 MMU for GP",
		.mmu_id = 1
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDR + 0x4000,
		.irq = MALI_IRQ_PP0_MMU,
		.description = "Mali-400 MMU for PP 0",
		.mmu_id = 2
	},
#ifndef MALI_ONLY_ONE_PP
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDR + 0xA000,
		.irq = MALI_IRQ_PP1,
		.description = "Mali-400 PP 1",
		.mmu_id = 3
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDR + 0x5000,
		.irq = MALI_IRQ_PP1_MMU,
		.description = "Mali-400 MMU for PP 1",
		.mmu_id = 3
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDR + 0xC000,
		.irq = MALI_IRQ_PP2,
		.description = "Mali-400 PP 2",
		.mmu_id = 4
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDR + 0x6000,
		.irq = MALI_IRQ_PP2_MMU,
		.description = "Mali-400 MMU for PP 2",
		.mmu_id = 4
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDR + 0xE000,
		.irq = MALI_IRQ_PP3,
		.description = "Mali-400 PP 3",
		.mmu_id = 5
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDR + 0x7000,
		.irq = MALI_IRQ_PP3_MMU,
		.description = "Mali-400 MMU for PP 3",
		.mmu_id = 5
	},
#endif
	{
		.type = OS_MEMORY,
		.description = "Mali SDRAM",
		.alloc_order = 0,
		.base = 0x00000000,
		.size = CONFIG_STM_MALI_OS_MEMORY_SIZE,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | \
			_MALI_PP_READABLE | _MALI_PP_WRITEABLE | \
			_MALI_GP_READABLE | _MALI_GP_WRITEABLE
	},
	{
		.type = MEM_VALIDATION,
		.description = "External Memory range",
		.base = 0x40000000,
		.size = 0x90000000,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | \
			_MALI_PP_WRITEABLE | _MALI_PP_READABLE
	},
	{
		.type = MALI400L2,
		.base = MALI_BASE_ADDR + 0x1000,
		.description = "Mali-400 L2 cache"
	},
};

#endif /* __ARCH_CONFIG_H__ */
