/*
 * -------------------------------------------------------------------------
 * <linux_root>/include/linux/pm_sys.h>
 * -------------------------------------------------------------------------
 * Copyright (C) 2011  STMicroelectronics
 * Author: Francesco M. Virlinzi  <francesco.virlinzi at st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License V.2 ONLY.  See linux/COPYING for more information.
 *
 * ------------------------------------------------------------------------- */

#ifndef __stm_pm_sys_h__
#define __stm_pm_sys_h__

#include <linux/list.h>

#define stm_sysconf_pr		10
#define stm_gpio_pr		20
#define stm_clk_pr		30
#define stm_emi_pr		40

struct stm_system {
	unsigned int priority;
	char *name;
	struct list_head node;
	int (*suspend)(void);
	int (*resume)(void);
	int (*freeze)(void);
	int (*restore)(void);
};

#ifdef CONFIG_PM
int stm_register_system(struct stm_system *sys);
#else
static inline int stm_register_system(struct stm_system *sys)
{
	return 0;
}
#endif

#endif
