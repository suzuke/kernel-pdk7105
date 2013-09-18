/*
 *
 * Cpufreq driver for the STM platforms.
 *
 * Copyright (C) 2008-2011 STMicroelectronics
 * Author: Francesco M. Virlinzi <francesco.virlinzi@st.com>
 *
 * This program is under the terms of the
 * General Public License version 2 ONLY
 *
 */
#ifndef _LINUX_STM_CPUFREQ_
#define _LINUX_STM_CPUFREQ_

#define STM_FREQ_NOMORE_DIVISOR		0

struct stm_cpufreq {
	struct clk *cpu_clk;
	int (*update)(unsigned int set);
	unsigned char *divisors;
};

int stm_cpufreq_register(struct stm_cpufreq *soc_cpufreq);
int stm_cpufreq_remove(struct stm_cpufreq *soc_cpufreq);

#endif
