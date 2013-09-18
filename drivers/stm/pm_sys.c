/*
 * -------------------------------------------------------------------------
 * <linux_root>/drivers/stm/pm_sys.c
 * -------------------------------------------------------------------------
 * Copyright (C) 2011  STMicroelectronics
 * Author: Francesco M. Virlinzi  <francesco.virlinzi at st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License V.2 ONLY.  See linux/COPYING for more information.
 *
 * ------------------------------------------------------------------------- */
#include <linux/irqflags.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/sysdev.h>

#include <linux/stm/pm_sys.h>

static DEFINE_MUTEX(subsys_mutex);

static LIST_HEAD(subsys_list);

static pm_message_t system_prev_state = PMSG_ON;

int stm_register_system(struct stm_system *sys)
{
	struct stm_system *entry = NULL;
	int ret = 0;

	/*
	 * Preliminary check
	 */
	if (!sys || !sys->name)
		return -EINVAL;

	mutex_lock(&subsys_mutex);

	list_for_each_entry(entry, &subsys_list, node) {
		if (!strcmp(entry->name, sys->name)) {
			ret = -EEXIST;
			goto _exit;
		}
		if (entry->priority > sys->priority)
			break;
	}

	pr_info("[STM][PM-Sys]: %s @ %d\n", sys->name, sys->priority);

	if (entry)
		list_add(&sys->node, entry->node.prev);
	else
		list_add(&sys->node, &subsys_list);

_exit:
	mutex_unlock(&subsys_mutex);

	return ret;
}

static int stm_subsys_suspend(struct sys_device *dev, pm_message_t state)
{
	int ret = 0;
	struct stm_system *sys;

	/*
	 * Suspend from lowest to the highest priority
	 */
	list_for_each_entry_reverse(sys, &subsys_list, node) {

		if (state.event == PM_EVENT_FREEZE && sys->freeze)
			ret |= sys->freeze();
		if (state.event == PM_EVENT_SUSPEND && sys->suspend)
			ret |= sys->suspend();
	}

	system_prev_state = state;
	return ret;
}

static int stm_subsys_resume(struct sys_device *dev)
{
	int ret = 0;
	struct stm_system *sys;

	/*
	 * Resume from highest to the lowest priority
	 */
	list_for_each_entry(sys, &subsys_list, node) {

		if (system_prev_state.event == PM_EVENT_FREEZE && sys->restore)
			ret |= sys->restore();
		if (system_prev_state.event == PM_EVENT_SUSPEND && sys->resume)
			ret |= sys->resume();
	}

	system_prev_state = PMSG_ON;

	return ret;
}

static struct sysdev_class stm_subsystem_sysdev_class = {
	.name = "stm_subsystem",
	.suspend = stm_subsys_suspend,
	.resume = stm_subsys_resume,
};

static struct sys_device stm_subsystem_sysdev_dev = {
	.id = 0,
	.cls = &stm_subsystem_sysdev_class,
};

static int __init stm_subsys_init(void)
{
	int ret;

	ret = sysdev_class_register(&stm_subsystem_sysdev_class);
	if (ret)
		return ret;

	return sysdev_register(&stm_subsystem_sysdev_dev);
}
module_init(stm_subsys_init);
