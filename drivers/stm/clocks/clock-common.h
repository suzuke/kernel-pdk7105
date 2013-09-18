/************************************************************************
File  : Low Level clock API
	Common LLA functions (SOC independant)

Author: F. Charpentier <fabrice.charpentier@st.com>

Copyright (C) 2008-12 STMicroelectronics
************************************************************************/

#ifndef __CLKLLA_COMMON_H
#define __CLKLLA_COMMON_H


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

int clk_pll800c65_get_params(unsigned long input, unsigned long output,
			     unsigned long *mdiv, unsigned long *ndiv,
			     unsigned long *pdiv);

/* ========================================================================
   Name:	clk_pll800c65_get_rate()
   Description: Convert input/mdiv/ndiv/pvid values to frequency for PLL800
   Params:      'input' freq (Hz), mdiv/ndiv/pvid values
   Output:      '*rate' updated
   Return:      Error code.
   ======================================================================== */

int clk_pll800c65_get_rate(unsigned long input, unsigned long mdiv,
			   unsigned long ndiv, unsigned long pdiv,
			   unsigned long *rate);

/* ========================================================================
   Name:	clk_pll1200c32_get_params()
   Description: PHI freq to parameters computation for PLL1200.
   Input:       input=input freq (Hz),output=output freq (Hz)
		WARNING: Output freq is given for PHI (FVCO/ODF).
   Output:      updated *idf, *ldf, & *odf
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_pll1200c32_get_params(unsigned long input, unsigned long output,
			      unsigned long *idf, unsigned long *ldf,
			      unsigned long *odf);

/* ========================================================================
   Name:	clk_pll1200c32_get_rate()
   Description: Convert input/idf/ldf/odf values to PHI output freq.
   Params:      'input' freq (Hz), idf/ldf/odf REGISTERS values
   Output:      '*rate' updated with value of PHI output (FVCO/ODF).
   Return:      Error code.
   ======================================================================== */

int clk_pll1200c32_get_rate(unsigned long input, unsigned long idf,
			    unsigned long ldf, unsigned long odf,
			    unsigned long *rate);

/* ========================================================================
   Name:	clk_pll1600c45_get_params(), PL1600 CMOS45
   Description: FVCO output freq to parameters computation function.
   Input:       input,output=input/output freqs (Hz)
   Output:      updated *idf, *ndiv and *cp
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_pll1600c45_get_params(unsigned long input, unsigned long output,
			      unsigned long *idf, unsigned long *ndiv,
			      unsigned long *cp);

/* ========================================================================
   Name:	clk_pll1600c45_get_phi_params()
   Description: PLL1600 C45 PHI freq computation function
   Input:       input,output=input/output freqs (Hz)
   Output:      updated *idf, *ndiv, *odf and *cp
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_pll1600c45_get_phi_params(unsigned long input, unsigned long output,
				  unsigned long *idf, unsigned long *ndiv,
				  unsigned long *odf, unsigned long *cp);

/* ========================================================================
   Name:	clk_pll1600c45_get_rate()
   Description: Convert input/idf/ndiv REGISTERS values to FVCO frequency
   Params:      'input' freq (Hz), idf/ndiv REGISTERS values
   Output:      '*rate' updated with value of FVCO output.
   Return:      Error code.
   ======================================================================== */

int clk_pll1600c45_get_rate(unsigned long input, unsigned long idf,
			    unsigned long ndiv, unsigned long *rate);

/* ========================================================================
   Name:	clk_pll1600c45_get_phi_rate()
   Description: Convert input/idf/ndiv/odf REGISTERS values to frequency
   Params:      'input' freq (Hz), idf/ndiv/odf REGISTERS values
   Output:      '*rate' updated with value of PHI output.
   Return:      Error code.
   ======================================================================== */

int clk_pll1600c45_get_phi_rate(unsigned long input, unsigned long idf,
				unsigned long ndiv, unsigned long odf,
				unsigned long *rate);

/* ========================================================================
   Name:	clk_pll1600c65_get_params()
   Description: Freq to parameters computation for PLL1600 CMOS65
   Input:       input,output=input/output freqs (Hz)
   Output:      updated *mdiv (rdiv) & *ndiv (ddiv)
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_pll1600c65_get_params(unsigned long input, unsigned long output,
			      unsigned long *mdiv, unsigned long *ndiv);

/* ========================================================================
   Name:	clk_pll1600c65_get_rate()
   Description: Convert input/mdiv/ndiv values to frequency for PLL1600
   Params:      'input' freq (Hz), mdiv/ndiv values
		Info: mdiv also called rdiv, ndiv also called ddiv
   Output:      '*rate' updated with value of HS output.
   Return:      Error code.
   ======================================================================== */

int clk_pll1600c65_get_rate(unsigned long input, unsigned long mdiv,
			    unsigned long ndiv, unsigned long *rate);

/* ========================================================================
   Name:	clk_pll3200c32_get_params()
   Description: Freq to parameters computation for PLL3200 CMOS32
   Input:       input=input freq (Hz), output=FVCOBY2 freq (Hz)
   Output:      updated *idf & *ndiv, plus *cp value (charge pump)
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_pll3200c32_get_params(unsigned long input, unsigned long output,
			      unsigned long *idf, unsigned long *ndiv,
			      unsigned long *cp);

/* ========================================================================
   Name:	clk_pll3200c32_get_rate()
   Description: Convert input/idf/ndiv values to FVCOby2 frequency for PLL3200
   Params:      'input' freq (Hz), idf/ndiv values
   Output:      '*rate' updated with value of FVCOby2 output (PHIx / 1).
   Return:      Error code.
   ======================================================================== */

int clk_pll3200c32_get_rate(unsigned long input, unsigned long idf,
			    unsigned long ndiv, unsigned long *rate);

/* ========================================================================
   Name:	clk_fs216c65_get_params()
   Description: Freq to parameters computation for frequency synthesizers
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated *md, *pe & *sdiv
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_fs216c65_get_params(unsigned long input, unsigned long output,
			    unsigned long *md, unsigned long *pe,
			    unsigned long *sdiv);

/* ========================================================================
   Name:	clk_fs216c65_get_rate()
   Description: Parameters to freq computation for frequency synthesizers.
   ======================================================================== */

int clk_fs216c65_get_rate(unsigned long input, unsigned long pe, unsigned long md,
			  unsigned long sd, unsigned long *rate);

/* ========================================================================
   Name:	clk_fs660c32_vco_get_params()
   Description: Compute params for embeded PLL660
   Input:       input=input freq (Hz), output=output freq (Hz)
   Output:      updated *ndiv (register value). Note that PDIV is frozen to 1.
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_fs660c32_vco_get_params(unsigned long input, unsigned long output,
				unsigned long *ndiv);

/* ========================================================================
   Name:	clk_fs660c32_dig_get_params()
   Description: Compute params for digital part of FS660
   Input:       input=VCO freq, output=requested freq (Hz), *nsdiv
		(0/1 if silicon frozen, or 0xff if to be computed).
   Output:      updated *nsdiv, *md, *pe & *sdiv registers values.
   Return:      'clk_err_t' error code
   ======================================================================== */

int clk_fs660c32_dig_get_params(unsigned long input, unsigned long output,
				unsigned long *nsdiv, unsigned long *md,
				unsigned long *pe, unsigned long *sdiv);

/* ========================================================================
   Name:	clk_fs660c32_vco_get_rate()
   Description: Compute VCO frequency of FS660 embeded PLL (PLL660)
   Input: ndiv & pdiv registers values
   Output: updated *rate (Hz)
   ======================================================================== */

int clk_fs660c32_vco_get_rate(unsigned long input, unsigned long ndiv,
			      unsigned long *rate);

/* ========================================================================
   Name:	clk_fs660c32_get_rate()
   Description: Parameters to freq computation for frequency synthesizers.
   Inputs:	input=VCO frequency, nsdiv, md, pe, & sdivregisters values.
   Outputs:	*rate updated
   ======================================================================== */

int clk_fs660c32_get_rate(unsigned long input, unsigned long nsdiv,
			  unsigned long md, unsigned long pe,
			  unsigned long sdiv, unsigned long *rate);

/* ========================================================================
   Name:        clk_register_table
   Description: ?
   Returns:     'clk_err_t' error code
   ======================================================================== */

int clk_register_table(struct clk *clks, int num, int enable);

/* ========================================================================
   Name:        clk_best_div
   Description: Returned closest div factor
   Returns:     Best div factor
   ======================================================================== */

static inline unsigned long clk_best_div(unsigned long parent_rate,
					 unsigned long rate)
{
	return parent_rate / rate +
		((rate > (2 * (parent_rate % rate))) ? 0 : 1);
}

#endif /* #ifndef __CLKLLA_COMMON_H */
