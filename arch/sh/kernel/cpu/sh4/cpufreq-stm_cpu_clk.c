/*
 *
 * Generic Cpufreq driver for the ST chips
 *
 * Copyright (C) 2011 STMicroelectronics
 * Author: Francesco M. Virlinzi <francesco.virlinzi@st.com>
 *
 * This program is under the terms of the
 * General Public License version 2 ONLY
 *
 */
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/stm/clk.h>

#include "cpufreq-stm.h"


static int stm_cpu_clk_update(unsigned int set);

static struct stm_cpufreq stm_cpu_clk_cpufreq = {
	.divisors = (unsigned char []) { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,	11, 12,
		13, 14, 15, 16, 17, 18, 19, 20,	21, 22, 23, 24, 25, 26, 27, 28,
		29, 30, 31, 32, STM_FREQ_NOMORE_DIVISOR },
	.update = stm_cpu_clk_update,
};

static int stm_cpu_clk_update(unsigned int set)
{
	unsigned long rate;

	rate = clk_get_rate(stm_cpu_clk_cpufreq.cpu_clk->parent);

	rate /= stm_cpu_clk_cpufreq.divisors[set];

	clk_set_rate(stm_cpu_clk_cpufreq.cpu_clk, rate);

	return 0;
}

static int __init stm_cpu_clk_cpufreq_init(void)
{
	stm_cpu_clk_cpufreq.cpu_clk = clk_get(NULL, "cpu_clk");
	if (!stm_cpu_clk_cpufreq.cpu_clk)
		return -EINVAL;

	return stm_cpufreq_register(&stm_cpu_clk_cpufreq);
}

static void stm_cpu_clk_cpufreq_exit(void)
{
	stm_cpufreq_remove(&stm_cpu_clk_cpufreq);
}

module_init(stm_cpu_clk_cpufreq_init);
module_exit(stm_cpu_clk_cpufreq_exit);
