/*
 * Copyright (C) 2005 STMicroelectronics Limited
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Code to handle the clockgen hardware on the STx7100.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/stm/clk.h>
#include <linux/io.h>
#include <asm-generic/div64.h>

#include "clock-oslayer.h"
#include "clock-common.h"

static void __iomem *clkgena_base;
static void __iomem *clkgenc_base;

#define CLOCKGEN_PLL0_CFG	0x08
#define CLOCKGEN_PLL0_CLK1_CTRL	0x14
#define CLOCKGEN_PLL0_CLK2_CTRL	0x18
#define CLOCKGEN_PLL0_CLK3_CTRL	0x1c
#define CLOCKGEN_PLL0_CLK4_CTRL	0x20
#define CLOCKGEN_PLL1_CFG	0x24

/* to enable/disable and reduce the coprocessor clock*/
#define CLOCKGEN_CLK_DIV	0x30
#define CLOCKGEN_CLK_EN		0x34

			        /* 0  1  2  3  4  5  6  7  */
static unsigned char ratio1[8] = { 1, 2, 3, 4, 6, 8, 0, 0 };
static unsigned char ratio2[8] = { 1, 2, 3, 4, 6, 8, 0, 0 };
static unsigned char ratio3[8] = { 4, 2, 4, 4, 6, 8, 0, 0 };
static unsigned char ratio4[8] = { 1, 2, 3, 4, 6, 8, 0, 0 };

static int pll_freq(unsigned long addr)
{
	unsigned long freq, data, ndiv, pdiv, mdiv;

	data = readl(clkgena_base+addr);
	mdiv = (data >>  0) & 0xff;
	ndiv = (data >>  8) & 0xff;
	pdiv = (data >> 16) & 0x7;
	freq = (((2 * (CONFIG_SH_EXTERNAL_CLOCK / 1000) * ndiv) / mdiv) /
		(1 << pdiv)) * 1000;

	return freq;
}

static int pll_clk_init(struct clk *clk)
{
	clk->rate = pll_freq(
		(strcmp(clk->name, "pll0_clk") ?
		CLOCKGEN_PLL1_CFG : CLOCKGEN_PLL0_CFG));
	return 0;
}

static struct clk_ops pll_clk_ops = {
	.init = pll_clk_init,
};

static struct clk pll_clk[] = {
	{
		.name		= "pll0_clk",
		.ops		= &pll_clk_ops,
	}, {
		.name		= "pll1_clk",
		.ops		= &pll_clk_ops,
	}
};

struct clokgenA {
	unsigned long ctrl_reg;
	unsigned int div;
	unsigned char *ratio;
};


enum clockgenA_ID {
	SH4_CLK_ID = 0,
	SH4IC_CLK_ID,
	MODULE_ID,
	SLIM_ID,
	LX_AUD_ID,
	LX_VID_ID,
	LMISYS_ID,
	LMIVID_ID,
	IC_ID,
	IC_100_ID,
	EMI_ID
};

static int clockgenA_clk_recalc(struct clk *clk)
{
	struct clokgenA *cga = (struct clokgenA *)clk->private_data;
	clk->rate = clk->parent->rate / cga->div;
	return 0;
}

static int clockgenA_clk_set_rate(struct clk *clk, unsigned long value)
{
	unsigned long data = readl(clkgena_base + CLOCKGEN_CLK_DIV);
	unsigned long val = 1 << (clk->id - 5);

	if (clk->id != LMISYS_ID && clk->id != LMIVID_ID)
		return -1;
	writel(0xc0de, clkgena_base);
	if (clk->rate > value) {/* downscale */
		writel(data | val, clkgena_base + CLOCKGEN_CLK_DIV);
		clk->rate /= 1024;
	} else {/* upscale */
		writel(data & ~val, clkgena_base + CLOCKGEN_CLK_DIV);
		clk->rate *= 1024;
	}
	writel(0x0, clkgena_base);
	return 0;
}

static int clockgenA_clk_init(struct clk *clk)
{
	struct clokgenA *cga = (struct clokgenA *)clk->private_data;
	if (cga->ratio) {
		unsigned long data = readl(clkgena_base + cga->ctrl_reg) & 0x7;
		unsigned char ratio = cga->ratio[data];
		BUG_ON(!ratio);
		cga->div = 2*ratio;
	}
	clk->rate = clk->parent->rate / cga->div;
	return 0;
}

static int clockgenA_clk_XXable(struct clk *clk, int enable)
{
	unsigned long tmp, value;
	struct clokgenA *cga = (struct clokgenA *)clk->private_data;

	if (clk->id != LMISYS_ID && clk->id != LMIVID_ID)
		return 0;

	tmp   = readl(clkgena_base+cga->ctrl_reg) ;
	value = 1 << (clk->id - 5);
	writel(0xc0de, clkgena_base);
	if (enable) {
		writel(tmp | value, clkgena_base + cga->ctrl_reg);
		clockgenA_clk_init(clk); /* to evaluate the rate */
	} else {
		writel(tmp & ~value, clkgena_base + cga->ctrl_reg);
		clk->rate = 0;
	}
	writel(0x0, clkgena_base);
	return 0;
}

static int clockgenA_clk_enable(struct clk *clk)
{
	return clockgenA_clk_XXable(clk, 1);
}

static int clockgenA_clk_disable(struct clk *clk)
{
	return clockgenA_clk_XXable(clk, 0);
}

static struct clk_ops clokgenA_ops = {
	.init		= clockgenA_clk_init,
	.recalc		= clockgenA_clk_recalc,
	.set_rate	= clockgenA_clk_set_rate,
	.enable		= clockgenA_clk_enable,
	.disable	= clockgenA_clk_disable,
};

#define CLKGENA(_id, clock, pll, _ctrl_reg, _div, _ratio)	\
[_id] = {							\
	.name	= #clock "_clk",				\
	.parent	= &(pll),					\
	.ops	= &clokgenA_ops,				\
	.id	= (_id),					\
	.private_data = &(struct clokgenA){			\
		.div = (_div),					\
		.ctrl_reg = (_ctrl_reg),			\
		.ratio = (_ratio)				\
		},						\
	}

static struct clk clkgena_clks[] = {
CLKGENA(SH4_CLK_ID,   st40, pll_clk[0], CLOCKGEN_PLL0_CLK1_CTRL, 1, ratio1),
CLKGENA(SH4IC_CLK_ID, st40_ic, pll_clk[0], CLOCKGEN_PLL0_CLK2_CTRL, 1, ratio2),
CLKGENA(MODULE_ID,    st40_per, pll_clk[0], CLOCKGEN_PLL0_CLK3_CTRL, 1, ratio3),
CLKGENA(SLIM_ID,      slim,     pll_clk[0], CLOCKGEN_PLL0_CLK4_CTRL, 1, ratio4),

CLKGENA(LX_AUD_ID,	st231aud, pll_clk[1], CLOCKGEN_CLK_EN, 1, NULL),
CLKGENA(LX_VID_ID,	st231vid, pll_clk[1], CLOCKGEN_CLK_EN, 1, NULL),
CLKGENA(LMISYS_ID,	lmisys,   pll_clk[1], 0, 1, NULL),
CLKGENA(LMIVID_ID,	lmivid,   pll_clk[1], 0, 1, NULL),
CLKGENA(IC_ID,	ic,	pll_clk[1], 0, 2, NULL),
CLKGENA(IC_100_ID,	ic_100,   pll_clk[1], 0, 4, NULL),
CLKGENA(EMI_ID,	emi,    pll_clk[1], 0, 4, NULL)
};


/*
 * Audio Clock Stuff
 */
enum clockgenC_ID {
	CLKC_REF,
	CLKC_FS0_CH1,
	CLKC_FS0_CH2,
	CLKC_FS0_CH3,
	CLKC_FS0_CH4,
};

/* --- Audio config registers --- */
#define CKGC_FS_CFG(_bk)		(0x100 * (_bk))
#define CKGC_FS_MD(_bk, _chan)	  		\
		(0x100 * (_bk) + 0x10 + 0x10 * (_chan))
#define CKGC_FS_PE(_bk, _chan)		(0x4 + CKGC_FS_MD(_bk, _chan))
#define CKGC_FS_SDIV(_bk, _chan)	(0x8 + CKGC_FS_MD(_bk, _chan))
#define CKGC_FS_EN_PRG(_bk, _chan)      (0xc + CKGC_FS_MD(_bk, _chan))

static int clkgenc_fsyn_recalc(clk_t *clk_p)
{
	unsigned long cfg, dig_bit, en_bit;
	unsigned long pe, md, sdiv;
	static const unsigned char dig_table[] = {10, 11, 12, 13};
	static const unsigned char en_table[] = {6, 7, 8, 9};
	int channel;
	int err;

	/* Is FSYN analog UP ? */
	cfg = CLK_READ(clkgenc_base + CKGC_FS_CFG(0));
	if (!(cfg & (1 << 14))) {       /* Analog power down */
		clk_p->rate = 0;
		return 0;
	}

	/* Is FSYN digital part UP & enabled ? */
	dig_bit = dig_table[clk_p->id - CLKC_FS0_CH1];
	en_bit = en_table[clk_p->id - CLKC_FS0_CH1];

	if ((cfg & (1 << dig_bit)) == 0) {      /* digital part in standby */
		clk_p->rate = 0;
		return 0;
	}
	if ((cfg & (1 << en_bit)) == 0) {       /* disabled */
		clk_p->rate = 0;
		return 0;
	}

	/* FSYN up & running.
	   Computing frequency */
	channel = (clk_p->id - CLKC_FS0_CH1) % 4;
	pe = CLK_READ(clkgenc_base + CKGC_FS_PE(0, channel));
	md = CLK_READ(clkgenc_base + CKGC_FS_MD(0, channel));
	sdiv = CLK_READ(clkgenc_base + CKGC_FS_SDIV(0, channel));
	err = clk_fs216c65_get_rate(clk_p->parent->rate, pe, md,
				sdiv, &clk_p->rate);

	return err;
}

static int clkgenc_set_rate(clk_t *clk_p, unsigned long freq)
{
	unsigned long md, pe, sdiv;
	unsigned long reg_value = 0;
	int channel;
	static const unsigned char set_rate_table[] = {
		0x06, 0x0A, 0x12, 0x22 };

	if (!clk_p)
		return CLK_ERR_BAD_PARAMETER;
	if (clk_p->id == CLKC_REF)
		return CLK_ERR_BAD_PARAMETER;

	/* Computing FSyn params. Should be common function with FSyn type */
	if (clk_fs216c65_get_params(clk_p->parent->rate, freq, &md, &pe, &sdiv))
		return CLK_ERR_BAD_PARAMETER;

	reg_value = CLK_READ(clkgenc_base + CKGC_FS_CFG(0));
	channel = (clk_p->id - CLKC_FS0_CH1) % 4;
	reg_value |= set_rate_table[clk_p->id - CLKC_FS0_CH1];

	CLK_WRITE(clkgenc_base + CKGC_FS_EN_PRG(0, channel), 0x00);
	/* Select FS clock only for the clock specified */
	CLK_WRITE(clkgenc_base + CKGC_FS_CFG(0), reg_value);

	CLK_WRITE(clkgenc_base + CKGC_FS_PE(0, channel), pe);
	CLK_WRITE(clkgenc_base + CKGC_FS_MD(0, channel), md);
	CLK_WRITE(clkgenc_base + CKGC_FS_SDIV(0, channel), sdiv);
	CLK_WRITE(clkgenc_base + CKGC_FS_EN_PRG(0, channel), 0x01);
	CLK_WRITE(clkgenc_base + CKGC_FS_EN_PRG(0, channel), 0x00);

	return clkgenc_fsyn_recalc(clk_p);
}

static int clkgenc_xable_fsyn(clk_t *clk_p, unsigned long enable)
{
	unsigned long val;
	int channel;
	/* Digital standby bits table.
	   Warning: enum order: CLKC_FS0_CH1 ... CLKC_FS0_CH3 */
	static const unsigned char dig_bit[] = {10, 11, 12, 13};
	static const unsigned char en_bit[] = {6, 7, 8, 9};

	if (!clk_p || clk_p->id ==  CLKC_REF)
		return CLK_ERR_BAD_PARAMETER;

	val = CLK_READ(clkgenc_base + CKGC_FS_CFG(0));

	/* Powering down/up digital part */
	if (enable) {
		val |= (1 << dig_bit[clk_p->id - CLKC_FS0_CH1]);
		val |= (1 << en_bit[clk_p->id - CLKC_FS0_CH1]);
	} else {
		val &= ~(1 << dig_bit[clk_p->id - CLKC_FS0_CH1]);
		val &= ~(1 << en_bit[clk_p->id - CLKC_FS0_CH1]);
	}

	/* Powering down/up analog part */
	if (enable)
		val |= (1 << 14);
	else {
		/* If all channels are off then power down FS0 */
		if ((val & 0x3fc0) == 0)
			val &= ~(1 << 14);
	}

	channel = (clk_p->id - CLKC_FS0_CH1) % 4;

	CLK_WRITE(clkgenc_base + CKGC_FS_EN_PRG(0, channel), 0x00);

	CLK_WRITE(clkgenc_base + CKGC_FS_CFG(0), val);

	CLK_WRITE(clkgenc_base + CKGC_FS_EN_PRG(0, channel), 0x10);
	CLK_WRITE(clkgenc_base + CKGC_FS_EN_PRG(0, channel), 0x00);

	/* Freq recalc required only if a channel is enabled */
	if (enable)
		return clkgenc_fsyn_recalc(clk_p);
	else
		clk_p->rate = 0;
	return 0;
}

static int clkgenc_enable(clk_t *clk_p)
{
	return clkgenc_xable_fsyn(clk_p, 1);
}

static int clkgenc_disable(clk_t *clk_p)
{
	return clkgenc_xable_fsyn(clk_p, 0);
}

_CLK_OPS(clkgenc,
	"Clockgen C/Audio",
	NULL,
	NULL,
	clkgenc_set_rate,
	NULL,
	clkgenc_enable,
	clkgenc_disable,
	NULL,
	NULL,
	NULL
);

static clk_t audio_clk_clocks[] = {
/* Clockgen C (AUDIO) */
_CLK_F(CLKC_REF, 30000000),
_CLK_P(CLKC_FS0_CH1, &clkgenc, 0, 0, &audio_clk_clocks[CLKC_REF]),
_CLK_P(CLKC_FS0_CH2, &clkgenc, 0, 0, &audio_clk_clocks[CLKC_REF]),
_CLK_P(CLKC_FS0_CH3, &clkgenc, 0, 0, &audio_clk_clocks[CLKC_REF]),
_CLK_P(CLKC_FS0_CH4, &clkgenc, 0, 0, &audio_clk_clocks[CLKC_REF]),
};

int __init plat_clk_init(void)
{
	int ret;
	unsigned long data;

	/**************/
	/* Clockgen A */
	/**************/
	clkgena_base = ioremap(0x19213000, 0x100);
	if (!clkgena_base)
		return -ENOMEM;

	clkgenc_base = ioremap(0x19210000, 0x100);
	if (!clkgenc_base)
		return -ENOMEM;

	ret = clk_register_table(pll_clk, ARRAY_SIZE(pll_clk), 1);
	if (ret)
		return ret;

	ret = clk_register_table(clkgena_clks, ARRAY_SIZE(clkgena_clks), 1);
	if (ret)
		return ret;

	/*
	 * Setup Audio Clock
	 */
	data = CLK_READ(clkgenc_base + CKGC_FS_CFG(0));
	data &= ~(1  << 23); /* reference from Sata */
	data |= (0xf <<  2); /* IP clocks from Fsynth */
	data &= ~(1  << 15); /* NDIV @ 27/30 MHz */
	data |= (0x3 << 16); /* BW_SEL very good */
	data &= ~1;	     /* Reset Out */

	CLK_WRITE(clkgenc_base + CKGC_FS_CFG(0), data);

	ret = clk_register_table(audio_clk_clocks,
		ARRAY_SIZE(audio_clk_clocks), 0);

	return ret;
}
