/*******************************************************************************
 *
 * File name   : clock-oslayer.h
 * Description : Low Level API - OS Specifics
 *
 * COPYRIGHT (C) 2009 STMicroelectronics - All Rights Reserved
 * This file is under the GPL 2 License.
 *
 ******************************************************************************/


#ifndef __CLKLLA_OSLAYER_H
#define __CLKLLA_OSLAYER_H

#include <linux/io.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/gpio.h>
#include <linux/stm/pio.h>
#include <linux/stm/platform.h>
#include <asm-generic/errno-base.h>

#define clk_t	struct clk

#define CLK_RATE_PROPAGATES		0
/* Register access macros */
#define CLK_READ(addr)	  		ioread32((void *)addr)
#define CLK_WRITE(addr, val)		iowrite32(val, (void *)addr)

#define SYSCONF(type, num, lsb, msb)			\
	static struct sysconf_field *sys_##type##_##num##_##lsb##_##msb

#define SYSCONF_CLAIM(type, num, lsb, msb)		\
	sys_##type##_##num##_##lsb##_##msb =		\
		sysconf_claim(type, num, lsb, msb, "Clk lla")

#define SYSCONF_READ(type, num, lsb, msb)		\
	sysconf_read(sys_##type##_##num##_##lsb##_##msb)

#define SYSCONF_WRITE(type, num, lsb, msb, value)	\
	sysconf_write(sys_##type##_##num##_##lsb##_##msb, value)

#define __mdelay(x)			mdelay(x)
#define CLK_DELAYMS(x)			mdelay(x)
/*
 * New ST chips use a unique ID number for all the sysconf registers
 * On these chips (to maintain the sysconf API Linus has) the
 * platform_sys_claim and call_platform_sys_claim are used
 *
 *
 * The platform_sys_claim is used to be able to compile
 * file __without__ any include chip based
 * the platform_sys_claim has to be implemented
 * in a chip-oriented file
 */
struct sysconf_field *platform_sys_claim(int nr, int lsb, int msb);

#define call_platform_sys_claim(_nr, _lsb, _msb)		\
	sys_0_##_nr##_##_lsb##_##_msb =				\
		platform_sys_claim(_nr, _lsb, _msb)

static inline
void PIO_SET_MODE(unsigned long bank, unsigned long line, long mode)
{
	static int pin = -ENOSYS;

	if (pin == -ENOSYS)
		gpio_request(stm_gpio(bank, line), "Clk Observer");

	if (pin >= 0)
		stm_gpio_direction(pin, mode);
}

#define _CLK_OPS(_name, _desc, _init, _setparent, _setfreq, _recalc,	\
		     _enable, _disable, _observe, _measure, _obspoint)	\
static struct clk_ops  _name = {					\
	.init = _init,							\
	.set_parent = _setparent,					\
	.set_rate = _setfreq,						\
	.recalc = _recalc,						\
	.enable = _enable,						\
	.disable = _disable,						\
	.observe = _observe,						\
	.get_measure = _measure,					\
}

#define _CLK(_id, _ops, _nominal, _flags) 				\
[_id] = (clk_t){ .name = #_id,						\
		 .id = (_id),						\
		 .ops = (_ops),						\
		 .flags = (_flags),					\
}

#define _CLK_P(_id, _ops, _nominal, _flags, _parent) 			\
[_id] = (clk_t){ .name = #_id,  \
		 .id = (_id),						\
		 .ops = (_ops), \
		 .flags = (_flags),					\
		 .parent = (_parent),					\
}

#define _CLK_F(_id, _rate)	 					\
[_id] = (clk_t) {							\
		.name = #_id,  						\
		.id = (_id),						\
		.rate = (_rate),					\
}

#define _CLK_FIXED(a, b, c)		_CLK_F(a, b)

/* Low level API errors */
enum clk_err {
	CLK_ERR_NONE = 0,
	CLK_ERR_FEATURE_NOT_SUPPORTED = 0,
	CLK_ERR_BAD_PARAMETER = -EINVAL,
	CLK_ERR_INTERNAL = -EFAULT /* Internal & fatal error */
};

/* Retrieving chip cut (major) */
static inline unsigned long chip_major_version(void)
{
	return cpu_data->cut_major;
}

#endif /* #ifndef __CLKLLA_OSLAYER_H */
