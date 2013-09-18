/*****************************************************************************
 *
 * File name   : clock-common.c
 * Description : Low Level API - Common functions (SOC independant)
 *
 * COPYRIGHT (C) 2009 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License v2.  See linux/COPYING for more information.
 *
 *****************************************************************************/

/* ----- Modification history (most recent first)----
28/jun/12 fabrice.charpentier@st.com
	  clk_fs216c65_get_params() bug fix for 64bits division under Linux.
19/jun/12 Ravinder SINGH
	  clk_pll1600c45_get_phi_params() fix.
30/apr/12 fabrice.charpentier@st.com
	  FS660C32 fine tuning to get better result.
26/apr/12 fabrice.charpentier@st.com
	  FS216 & FS432 fine tuning to get better result.
24/apr/12 fabrice.charpentier@st.com
	  FS216, FS432 & FS660: changed sdiv search order from highest to lowest
	  as recommended by Anand K.
18/apr/12 fabrice.charpentier@st.com
	  Added FS432C65 algo.
13/apr/12 fabrice.charpentier@st.com
	  FS216C65 MD order changed to recommended -16 -> -1 then -17.
05/apr/12 fabrice.charpentier@st.com
	  FS216C65 fully revisited to have 1 algo only for Linux & OS21.
28/mar/12 fabrice.charpentier@st.com
	  FS660C32 algos merged from Liege required improvements.
25/nov/11 fabrice.charpentier@st.com
	  Functions rename to support several algos for a same PLL/FS.
28/oct/11 fabrice.charpentier@st.com
	  Added PLL1600 CMOS045 support for Lille
27/oct/11 fabrice.charpentier@st.com
	  PLL1200 functions revisited. API changed.
27/jul/11 fabrice.charpentier@st.com
	  FS660 algo enhancement.
14/mar/11 fabrice.charpentier@st.com
	  Added PLL1200 functions.
07/mar/11 fabrice.charpentier@st.com
	  clk_pll3200c32_get_params() revisited.
11/mar/10 fabrice.charpentier@st.com
	  clk_pll800c65_get_params() fully revisited.
10/dec/09 francesco.virlinzi@st.com
	  clk_pll1600c65_get_params() now same code for OS21 & Linux.
13/oct/09 fabrice.charpentier@st.com
	  clk_fs216c65_get_rate() API changed. Now returns error code.
30/sep/09 fabrice.charpentier@st.com
	  Introducing clk_pll800c65_get_rate() & clk_pll1600c65_get_rate() to
	  replace clk_pll800_freq() & clk_pll1600c65_freq().
*/

#include <linux/clk.h>
#include <asm-generic/div64.h>
#include <linux/clkdev.h>

int __init clk_register_table(struct clk *clks, int num, int enable)
{
	int i;

	for (i = 0; i < num; i++) {
		struct clk *clk = &clks[i];
		int ret;
		struct clk_lookup *cl;

		/*
		 * Some devices have clockgen outputs which are unused.
		 * In this case the LLA may still have an entry in its
		 * tables for that clock, and try and register that clock,
		 * so we need some way to skip it.
		 */
		if (!clk->name)
			continue;

		ret = clk_register(clk);
		if (ret)
			return ret;

		/*
		 * We must ignore the result of clk_enables as some of
		 * the LLA enables functions claim to support an
		 * enables function, but then fail if you call it!
		 */
		if (enable) {
			ret = clk_enable(clk);
			if (ret)
				pr_warning("Failed to enable clk %s, "
					   "ignoring\n", clk->name);
		}

		cl = clkdev_alloc(clk, clk->name, NULL);
		if (!cl)
			return -ENOMEM;
		clkdev_add(cl);
	}

	return 0;
}


/*
 * Linux specific functions
 */

/* Return the number of set bits in x. */
static unsigned int population(unsigned int x)
{
	/* This is the traditional branch-less algorithm for population count */
	x = x - ((x >> 1) & 0x55555555);
	x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
	x = (x + (x >> 4)) & 0x0f0f0f0f;
	x = x + (x << 8);
	x = x + (x << 16);

	return x >> 24;
}

/* Return the index of the most significant set in x.
 * The results are 'undefined' is x is 0 (0xffffffff as it happens
 * but this is a mere side effect of the algorithm. */
static unsigned int most_significant_set_bit(unsigned int x)
{
	/* propagate the MSSB right until all bits smaller than MSSB are set */
	x = x | (x >> 1);
	x = x | (x >> 2);
	x = x | (x >> 4);
	x = x | (x >> 8);
	x = x | (x >> 16);

	/* now count the number of set bits [clz is population(~x)] */
	return population(x) - 1;
}

#include "clock-oslayer.h"
#include "clock-common.h"


/*
 * PLL800
 */

/* ========================================================================
   Name:	clk_pll800c65_get_params()
   Description: Freq to parameters computation for PLL800 CMOS65
   Input:       input & output freqs (Hz)
   Output:      updated *mdiv, *ndiv & *pdiv (register values)
   Return:      'clk_err_t' error code
   ======================================================================== */

/*
 * PLL800 in FS mode computation algo
 *
 *             2 * N * Fin Mhz
 * Fout Mhz = -----------------		[1]
 *                M * (2 ^ P)
 *
 * Rules:
 *   6.25Mhz <= output <= 800Mhz
 *   FS mode means 3 <= N <= 255
 *   1 <= M <= 255
 *   1Mhz <= PFDIN (input/M) <= 50Mhz
 *   200Mhz <= FVCO (input*2*N/M) <= 800Mhz
 *   For better long term jitter select M minimum && P maximum
 */

int clk_pll800c65_get_params(unsigned long input, unsigned long output,
	unsigned long *mdiv, unsigned long *ndiv, unsigned long *pdiv)
{
	unsigned long m, n, pfdin, fvco;
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq;
	long new_deviation, pi;

	/* Output clock range: 6.25Mhz to 800Mhz */
	if (output < 6250000 || output > 800000000)
		return CLK_ERR_BAD_PARAMETER;

	input /= 1000;
	output /= 1000;

	for (pi = 5; pi >= 0 && deviation; pi--) {
		for (m = 1; (m < 255) && deviation; m++) {
			n = m * (1 << pi) * output / (input * 2);

			/* Checks */
			if (n < 3)
				continue;
			if (n > 255)
				break;
			pfdin = input / m; /* 1Mhz <= PFDIN <= 50Mhz */
			if (pfdin < 1000 || pfdin > 50000)
				continue;
			/* 200Mhz <= FVCO <= 800Mhz */
			fvco = (input * 2 * n) / m;
			if (fvco > 800000)
				continue;
			if (fvco < 200000)
				break;

			new_freq = (input * 2 * n) / (m * (1 << pi));
			new_deviation = new_freq - output;
			if (new_deviation < 0)
				new_deviation = -new_deviation;
			if (!new_deviation || new_deviation < deviation) {
				*mdiv	= m;
				*ndiv	= n;
				*pdiv	= pi;
				deviation = new_deviation;
			}
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return CLK_ERR_BAD_PARAMETER;
	return 0;
}

/* ========================================================================
   Name:	clk_pll800c65_get_rate()
   Description: Convert input/mdiv/ndiv/pvid values to frequency for PLL800
   Params:      'input' freq (Hz), mdiv/ndiv/pvid values
   Output:      '*rate' updated
   Return:      Error code.
   ======================================================================== */

int clk_pll800c65_get_rate(unsigned long input, unsigned long mdiv,
	unsigned long ndiv, unsigned long pdiv, unsigned long *rate)
{
	if (!mdiv)
		mdiv++; /* mdiv=0 or 1 => MDIV=1 */

	/* Note: input is divided by 1000 to avoid overflow */
	*rate = (((2 * (input/1000) * ndiv) / mdiv) / (1 << pdiv)) * 1000;

	return 0;
}

/*
 * PLL1200
 */

/* ========================================================================
   Name:	clk_pll1200c32_get_params()
   Description: PHI freq to parameters computation for PLL1200.
   Input:       input=input freq (Hz),output=output freq (Hz)
		WARNING: Output freq is given for PHI (FVCO/ODF).
   Output:      updated *idf, *ldf, & *odf
   Return:      'clk_err_t' error code
   ======================================================================== */

/* PLL output structure
 *   FVCO >> Divider (ODF) >> PHI
 *
 * PHI = (INFF * LDF) / (ODF * IDF) when BYPASS = L
 *
 * Rules:
 *   9.6Mhz <= input (INFF) <= 350Mhz
 *   600Mhz <= FVCO <= 1200Mhz
 *   9.52Mhz <= PHI output <= 1200Mhz
 *   1 <= i (register value for IDF) <= 7
 *   8 <= l (register value for LDF) <= 127
 *   1 <= odf (register value for ODF) <= 63
 */

int clk_pll1200c32_get_params(unsigned long input, unsigned long output,
			   unsigned long *idf, unsigned long *ldf,
			   unsigned long *odf)
{
	unsigned long i, l, o; /* IDF, LDF, ODF values */
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq;
	long new_deviation;

	/* Output clock range: 9.52Mhz to 1200Mhz */
	if (output < 9520000 || output > 1200000000)
		return CLK_ERR_BAD_PARAMETER;

	/* Computing Output Division Factor */
	if (output < 600000000) {
		o = 600000000 / output;
		if (600000000 % output)
			o = o + 1;
	} else
		o = 1;

	input /= 1000;
	output /= 1000;

	for (i = 1; (i <= 7) && deviation; i++) {
		l = i * output * o / input;

		/* Checks */
		if (l < 8)
			continue;
		if (l > 127)
			break;

		new_freq = (input * l) / (i * o);
		new_deviation = new_freq - output;
		if (new_deviation < 0)
			new_deviation = -new_deviation;
		if (!new_deviation || new_deviation < deviation) {
			*idf = i;
			*ldf = l;
			*odf = o;
			deviation = new_deviation;
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return CLK_ERR_BAD_PARAMETER;
	return 0;
}

/* ========================================================================
   Name:	clk_pll1200c32_get_rate()
   Description: Convert input/idf/ldf/odf values to PHI output freq.
		WARNING: Assuming NOT BYPASS.
   Params:      'input' freq (Hz), idf/ldf/odf REGISTERS values
   Output:      '*rate' updated with value of PHI output (FVCO/ODF).
   Return:      Error code.
   ======================================================================== */

int clk_pll1200c32_get_rate(unsigned long input, unsigned long idf,
			    unsigned long ldf, unsigned long odf,
			    unsigned long *rate)
{
	if (!idf)
		return CLK_ERR_BAD_PARAMETER;

	/* Note: input is divided by 1000 to avoid overflow */
	*rate = (((input / 1000) * ldf) / (odf * idf)) * 1000;

	return 0;
}

/*
 * PLL1600
 * WARNING: 2 types currently supported; CMOS065 & CMOS045
 */

/* ========================================================================
   Name:	clk_pll1600c45_get_params(), PL1600 CMOS45
   Description: FVCO output freq to parameters computation function.
   Input:       input,output=input/output freqs (Hz)
   Output:      updated *idf, *ndiv and *cp
   Return:      'clk_err_t' error code
   ======================================================================== */

/*
 * Spec used: CMOS045_PLL_PG_1600X_A_SSCG_FR_LSHOD25_7M4X0Y2Z_SPECS_1.1.pdf
 *
 * Rules:
 *   4Mhz <= input (INFF) <= 350Mhz
 *   800Mhz <= VCO freq (FVCO) <= 1800Mhz
 *   6.35Mhz <= output (PHI) <= 900Mhz
 *   1 <= IDF (Input Div Factor) <= 7
 *   8 <= NDIV (Loop Div Factor) <= 225
 *   1 <= ODF (Output Div Factor) <= 63
 *
 * PHI = (INFF*LDF) / (2*IDF*ODF)
 * FVCO = (INFF*LDF) / (IDF)
 * LDF = 2*NDIV (if FRAC_CONTROL=L)
 * => FVCO = INFF * 2 * NDIV / IDF
 */

int clk_pll1600c45_get_params(unsigned long input, unsigned long output,
			   unsigned long *idf, unsigned long *ndiv,
			   unsigned long *cp)
{
	unsigned long i, n = 0; /* IDF, NDIV values */
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq;
	long new_deviation;
	/* Charge pump table: highest ndiv value for cp=7 to 27 */
	static const unsigned char cp_table[] = {
		71, 79, 87, 95, 103, 111, 119, 127, 135, 143,
		151, 159, 167, 175, 183, 191, 199, 207, 215,
		223, 225
	};

	/* Output clock range: 800Mhz to 1800Mhz */
	if (output < 800000000 || output > 1800000000)
		return CLK_ERR_BAD_PARAMETER;

	input /= 1000;
	output /= 1000;

	for (i = 1; (i <= 7) && deviation; i++) {
		n = (i * output) / (2 * input);

		/* Checks */
		if (n < 8)
			continue;
		if (n > 225)
			break;

		new_freq = (input * 2 * n) / i;
		new_deviation = new_freq - output;
		if (new_deviation < 0)
			new_deviation = -new_deviation;
		if (!new_deviation || new_deviation < deviation) {
			*idf	= i;
			*ndiv	= n;
			deviation = new_deviation;
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return CLK_ERR_BAD_PARAMETER;

	/* Computing recommended charge pump value */
	for (*cp = 7; n > cp_table[*cp - 7]; (*cp)++)
		;

	return 0;
}

/* ========================================================================
   Name:	clk_pll1600c45_get_phi_params()
   Description: PLL1600 C45 PHI freq computation function
   Input:       input,output=input/output freqs (Hz)
   Output:      updated *idf, *ndiv, *odf and *cp
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_pll1600c45_get_phi_params(unsigned long input, unsigned long output,
			   unsigned long *idf, unsigned long *ndiv,
			   unsigned long *odf, unsigned long *cp)
{
	unsigned long o; /* ODF value */

	/* Output clock range: 6.35Mhz to 900Mhz */
	if (output < 6350000 || output > 900000000)
		return CLK_ERR_BAD_PARAMETER;

	/* Computing Output Division Factor */
	if (output < 400000000) {
		o = 400000000 / output;
		if (400000000 % output)
			o = o + 1;
	} else
		o = 1;
	*odf = o;

	/* Computing FVCO freq*/
	output = 2 * output * o;

	return clk_pll1600c45_get_params(input, output, idf, ndiv, cp);
}

/* ========================================================================
   Name:	clk_pll1600c45_get_rate()
   Description: Convert input/idf/ndiv REGISTERS values to FVCO frequency
   Params:      'input' freq (Hz), idf/ndiv REGISTERS values
   Output:      '*rate' updated with value of FVCO output.
   Return:      Error code.
   ======================================================================== */

int clk_pll1600c45_get_rate(unsigned long input, unsigned long idf,
			    unsigned long ndiv, unsigned long *rate)
{
	if (!idf)
		return CLK_ERR_BAD_PARAMETER;

	/* FVCO = (INFF*LDF) / (IDF)
	   LDF = 2*NDIV (if FRAC_CONTROL=L)
	   => FVCO = INFF * 2 * NDIV / IDF */

	/* Note: input is divided to avoid overflow */
	*rate = (((input / 1000) * 2 * ndiv) / idf) * 1000;

	return 0;
}

/* ========================================================================
   Name:	clk_pll1600c45_get_phi_rate()
   Description: Convert input/idf/ndiv/odf REGISTERS values to frequency
   Params:      'input' freq (Hz), idf/ndiv/odf REGISTERS values
   Output:      '*rate' updated with value of PHI output.
   Return:      Error code.
   ======================================================================== */

int clk_pll1600c45_get_phi_rate(unsigned long input, unsigned long idf,
			    unsigned long ndiv, unsigned long odf,
			    unsigned long *rate)
{
	if (!idf || !odf)
		return CLK_ERR_BAD_PARAMETER;

	/* PHI = (INFF*LDF) / (2*IDF*ODF)
	   LDF = 2*NDIV (if FRAC_CONTROL=L)
	   => PHI = (INFF*NDIV) / (IDF*ODF) */

	/* Note: input is divided to avoid overflow */
	*rate = (((input/1000) * ndiv) / (idf * odf)) * 1000;

	return 0;
}

/* ========================================================================
   Name:	clk_pll1600c65_get_params()
   Description: Freq to parameters computation for PLL1600 CMOS65
   Input:       input,output=input/output freqs (Hz)
   Output:      updated *mdiv (rdiv) & *ndiv (ddiv)
   Return:      'clk_err_t' error code
   ======================================================================== */

/*
 * Rules:
 *   600Mhz <= output (FVCO) <= 1800Mhz
 *   1 <= M (also called R) <= 7
 *   4 <= N <= 255
 *   4Mhz <= PFDIN (input/M) <= 75Mhz
 */

int clk_pll1600c65_get_params(unsigned long input, unsigned long output,
			   unsigned long *mdiv, unsigned long *ndiv)
{
	unsigned long m, n, pfdin;
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq;
	long new_deviation;

	/* Output clock range: 600Mhz to 1800Mhz */
	if (output < 600000000 || output > 1800000000)
		return CLK_ERR_BAD_PARAMETER;

	input /= 1000;
	output /= 1000;

	for (m = 1; (m <= 7) && deviation; m++) {
		n = m * output / (input * 2);

		/* Checks */
		if (n < 4)
			continue;
		if (n > 255)
			break;
		pfdin = input / m; /* 4Mhz <= PFDIN <= 75Mhz */
		if (pfdin < 4000 || pfdin > 75000)
			continue;

		new_freq = (input * 2 * n) / m;
		new_deviation = new_freq - output;
		if (new_deviation < 0)
			new_deviation = -new_deviation;
		if (!new_deviation || new_deviation < deviation) {
			*mdiv	= m;
			*ndiv	= n;
			deviation = new_deviation;
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return CLK_ERR_BAD_PARAMETER;
	return 0;
}

/* ========================================================================
   Name:	clk_pll1600c65_get_rate()
   Description: Convert input/mdiv/ndiv values to frequency for PLL1600
   Params:      'input' freq (Hz), mdiv/ndiv values
		Info: mdiv also called rdiv, ndiv also called ddiv
   Output:      '*rate' updated with value of HS output.
   Return:      Error code.
   ======================================================================== */

int clk_pll1600c65_get_rate(unsigned long input, unsigned long mdiv,
			    unsigned long ndiv, unsigned long *rate)
{
	if (!mdiv)
		return CLK_ERR_BAD_PARAMETER;

	/* Note: input is divided by 1000 to avoid overflow */
	*rate = ((2 * (input/1000) * ndiv) / mdiv) * 1000;

	return 0;
}

/*
 * PLL3200
 */

/* ========================================================================
   Name:	clk_pll3200c32_get_params()
   Description: Freq to parameters computation for PLL3200 CMOS32
   Input:       input=input freq (Hz), output=FVCOBY2 freq (Hz)
   Output:      updated *idf & *ndiv, plus *cp value (charge pump)
   Return:      'clk_err_t' error code
   ======================================================================== */

/* PLL output structure
 * VCO >> /2 >> FVCOBY2
 *                 |> Divider (ODF0) >> PHI0
 *                 |> Divider (ODF1) >> PHI1
 *                 |> Divider (ODF2) >> PHI2
 *                 |> Divider (ODF3) >> PHI3
 *
 * FVCOby2 output = (input*4*NDIV) / (2*IDF) (assuming FRAC_CONTROL==L)
 *
 * Rules:
 *   4Mhz <= input <= 350Mhz
 *   800Mhz <= output (FVCOby2) <= 1600Mhz
 *   1 <= i (register value for IDF) <= 7
 *   8 <= n (register value for NDIV) <= 200
 */

int clk_pll3200c32_get_params(unsigned long input, unsigned long output,
			   unsigned long *idf, unsigned long *ndiv,
			   unsigned long *cp)
{
	unsigned long i, n;
	unsigned long deviation = 0xffffffff;
	unsigned long new_freq;
	long new_deviation;
	/* Charge pump table: highest ndiv value for cp=6 to 25 */
	static const unsigned char cp_table[] = {
		48, 56, 64, 72, 80, 88, 96, 104, 112, 120,
		128, 136, 144, 152, 160, 168, 176, 184, 192
	};

	/* Output clock range: 800Mhz to 1600Mhz */
	if (output < 800000000 || output > 1600000000)
		return CLK_ERR_BAD_PARAMETER;

	input /= 1000;
	output /= 1000;

	for (i = 1; (i <= 7) && deviation; i++) {
		n = i * output / (2 * input);

		/* Checks */
		if (n < 8)
			continue;
		if (n > 200)
			break;

		new_freq = (input * 2 * n) / i;
		new_deviation = new_freq - output;
		if (new_deviation < 0)
			new_deviation = -new_deviation;
		if (!new_deviation || new_deviation < deviation) {
			*idf	= i;
			*ndiv	= n;
			deviation = new_deviation;
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return CLK_ERR_BAD_PARAMETER;

	/* Computing recommended charge pump value */
	for (*cp = 6; n > cp_table[*cp-6]; (*cp)++)
		;

	return 0;
}

/* ========================================================================
   Name:	clk_pll3200c32_get_rate()
   Description: Convert input/idf/ndiv values to FVCOby2 frequency for PLL3200
   Params:      'input' freq (Hz), idf/ndiv values
   Output:      '*rate' updated with value of FVCOby2 output (PHIx / 1).
   Return:      Error code.
   ======================================================================== */

int clk_pll3200c32_get_rate(unsigned long input, unsigned long idf,
			unsigned long ndiv, unsigned long *rate)
{
	if (!idf)
		return CLK_ERR_BAD_PARAMETER;

	/* Note: input is divided to avoid overflow */
	*rate = ((2 * (input/1000) * ndiv) / idf) * 1000;

	return 0;
}

/*
 * FS216
 */

/* ========================================================================
   Name:	clk_fs216c65_get_params()
   Description: Freq to parameters computation for FS216 C65
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated *md, *pe & *sdiv
   Return:      'clk_err_t' error code
   ======================================================================== */

#define P15			(uint64_t)(1 << 15)
#define FS216_SCALING_FACTOR	4096LL

int clk_fs216c65_get_params(unsigned long input, unsigned long output,
			    unsigned long *md, unsigned long *pe,
			    unsigned long *sdiv)
{
	unsigned long nd = 8; /* ndiv value: bin stuck at 0 => value = 8 */
	unsigned long ns = 1; /* nsdiv3 value: bin stuck at 1 => value = 1 */
	unsigned long sd; /* sdiv value = 1 << (sdiv_bin_value + 1) */
	long m; /* md value (-17 to -1) */
	uint64_t p, p2; /* pe value */
	int si;
	unsigned long new_freq, new_deviation;
	/* initial condition to say: "infinite deviation" */
	unsigned long deviation = 0xffffffff;
	unsigned long md_tmp; /* Temp md bin */
	int stop;

	for (si = 7; (si >= 0) && deviation; si--) {
		sd = (1 << (si + 1));
		/* Recommended search order: -16 to -1, then -17 */
		for (m = -16, stop = 0; !stop && deviation; m++) {
			if (!m) {
				m = -17; /* 0 is -17 */
				stop = 1;
			}
			p = P15 * 32 * nd * input * FS216_SCALING_FACTOR;
			p = div64_u64(p, sd * ns * output * FS216_SCALING_FACTOR);
			p2 = P15 * (uint64_t)(m + 33);
			if (p2 < p)
				continue; /* p must be >= 0 */
			p = p2 - p;

			if (p > 32768LL)
				/* Already too high. Let's move to next sdiv */
				break;

			md_tmp = (unsigned long)(m + 32);
			/* pe fine tuning: +/- 2 around computed pe value */
			if (p > 2)
				p2 = p - 2;
			else
				p2 = 0;
			for (; p2 < 32768ll && (p2 < (p + 2)); p2++) {
				clk_fs216c65_get_rate(input, (unsigned long)p2,
					md_tmp, si, &new_freq);

				if (new_freq < output)
					new_deviation = output - new_freq;
				else
					new_deviation = new_freq - output;
				/* Check if this is a better solution */
				if (new_deviation < deviation) {
					*md = (unsigned long)(m + 32);
					*pe = (unsigned long)p2;
					*sdiv = si;
					deviation = new_deviation;
				}
			}
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return CLK_ERR_BAD_PARAMETER;

	return 0;
}

/* ========================================================================
   Name:	clk_fs216c65_get_rate()
   Description: Parameters to freq computation for frequency synthesizers.
   ======================================================================== */

int clk_fs216c65_get_rate(unsigned long input, unsigned long pe,
		unsigned long md, unsigned long sd, unsigned long *rate)
{
	uint64_t res;
	unsigned long ns = 1; /* nsdiv3 stuck at 1 => val = 1 */
	unsigned long nd = 8; /* ndiv stuck at 0 => val = 8 */
	unsigned long s; /* sdiv value = 1 << (sdiv_bin + 1) */
	long m; /* md value (-17 to -1) */

	/* BIN to VAL */
	m = md - 32;
	s = 1 << (sd + 1);

	res = (uint64_t)(s * ns * P15 * (uint64_t)(m + 33));
	res = res - (s * ns * pe);
	*rate = div64_u64(P15 * nd * input * 32, res);

	return 0;
}

/* ========================================================================
   Name:	clk_fs660c32_vco_get_params()
   Description: Compute params for embeded PLL660
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated *ndiv (register value). Note that PDIV is frozen to 1.
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_fs660c32_vco_get_params(unsigned long input, unsigned long output,
			     unsigned long *ndiv)
{
/* Formula
   VCO frequency = (fin x ndiv) / pdiv
   ndiv = VCOfreq * pdiv / fin
   */
	unsigned long pdiv = 1, n;

	/* Output clock range: 384Mhz to 660Mhz */
	if (output < 384000000 || output > 660000000)
		return CLK_ERR_BAD_PARAMETER;

	if (input > 40000000)
		/* This means that PDIV would be 2 instead of 1.
		   Not supported today. */
		return CLK_ERR_BAD_PARAMETER;

	input /= 1000;
	output /= 1000;

	n = output * pdiv / input;
	/* FCh: opened point. Min value is 16. To be clarified */
	if (n < 16)
		n = 16;
	*ndiv = n - 16; /* Converting formula value to reg value */

	return 0;
}

/* ========================================================================
   Name:	clk_fs660c32_dig_get_params()
   Description: Compute params for digital part of FS660
   Input:       input=VCO freq, output=requested freq (Hz), *nsdiv
		(0/1 if silicon frozen, or 0xff if to be computed).
   Output:      updated *nsdiv, *md, *pe & *sdiv registers values.
   Return:      'clk_err_t' error code
   ======================================================================== */

#define P20		(uint64_t)(1 << 20)

/* We use Fixed-point arithmetic in order to avoid "float" functions.*/
#define SCALING_FACTOR	2048LL

int clk_fs660c32_dig_get_params(unsigned long input, unsigned long output,
				unsigned long *nsdiv, unsigned long *md,
				unsigned long *pe, unsigned long *sdiv)
{
	int si;
	unsigned long ns; /* nsdiv value (1 or 3) */
	unsigned long s; /* sdiv value = 1 << sdiv_reg_value */
	unsigned long m; /* md value */
	unsigned long new_freq, new_deviation;
	/* initial condition to say: "infinite deviation" */
	unsigned long deviation = 0xffffffff;
	uint64_t p, p2; /* pe value */

	/*
	 * *nsdiv is a register value ('BIN') which is translated
	 * to a decimal value according to following rules.
	 * In case nsdiv is hardwired, it must be set to 0xff before calling.
	 *
	 *    *nsdiv      ns.dec
	 * 	ff	  computed by this algo
	 *       0        3
	 *       1        1
	 */
	if (*nsdiv != 0xff) {
		ns = (*nsdiv ? 1 : 3);
		goto skip_ns_programming;
	}

	for (ns = 1; ns < 4; ns += 2)

skip_ns_programming:

	for (si = 8; (si >= 0) && deviation; si--) {
		s = (1 << si);
		for (m = 0; (m < 32) && deviation; m++) {
			p = (uint64_t)input * SCALING_FACTOR;
			p = p - SCALING_FACTOR * ((uint64_t)s * (uint64_t)ns * (uint64_t)output) -
				 ((uint64_t)s * (uint64_t)ns * (uint64_t)output) *
				 ((uint64_t)m * (SCALING_FACTOR / 32LL));
			p = p * (P20 / SCALING_FACTOR);
			p = div64_u64(p, (uint64_t)((uint64_t)s * (uint64_t)ns * (uint64_t)output));

			if (p > 32767LL)
				continue;

			/* pe fine tuning: +/- 2 around computed pe value */
			if (p > 2)
				p2 = p - 2;
			else
				p2 = 0;
			for (; p2 < 32768ll && (p2 <= (p + 2)); p2++) {
				if (clk_fs660c32_get_rate(input,
					(ns == 1) ? 1 : 0, m,
					(unsigned long)p2, si, &new_freq) != 0)
					continue;
				if (new_freq < output)
					new_deviation = output - new_freq;
				else
					new_deviation = new_freq - output;
				/* Check if this is a better solution */
				if (new_deviation < deviation) {
					*md = m;
					*pe = (unsigned long)p2;
					*sdiv = si;
					*nsdiv = (ns == 1) ? 1 : 0;
					deviation = new_deviation;
				}
			}
		}
	}

	if (deviation == 0xffffffff) /* No solution found */
		return CLK_ERR_BAD_PARAMETER;

	return 0;
}

/* ========================================================================
   Name:	clk_fs660c32_vco_get_rate()
   Description: Compute VCO frequency of FS660 embeded PLL (PLL660)
   Input: ndiv & pdiv registers values
   Output: updated *rate (Hz)
   ======================================================================== */

int clk_fs660c32_vco_get_rate(unsigned long input, unsigned long ndiv,
			   unsigned long *rate)
{
	unsigned long nd = ndiv + 16; /* ndiv value */
	unsigned long pdiv = 1; /* Frozen. Not configurable so far */

	*rate = (input * nd) / pdiv;

	return 0;
}

/* ========================================================================
   Name:	clk_fs660c32_get_rate()
   Description: Parameters to freq computation for frequency synthesizers.
   Inputs:	input=VCO frequency, nsdiv, md, pe, & sdiv 'BIN' values.
   Outputs:	*rate updated
   ======================================================================== */

int clk_fs660c32_get_rate(unsigned long input, unsigned long nsdiv,
			unsigned long md, unsigned long pe,
			unsigned long sdiv, unsigned long *rate)
{
	unsigned long s = (1 << sdiv); /* sdiv value = 1 << sdiv_reg_value */
	unsigned long ns;  /* nsdiv value (1 or 3) */
	uint64_t res;

	/*
	 * 'nsdiv' is a register value ('BIN') which is translated
	 * to a decimal value according to following rules.
	 *
	 *     nsdiv      ns.dec
	 *       0        3
	 *       1        1
	 */
	ns = (nsdiv == 1) ? 1 : 3;

	res = (P20 * (32 + md) + 32 * pe) * s * ns;
	*rate = (unsigned long)div64_u64(input * P20 * 32, res);
	return 0;
}

