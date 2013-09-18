/*****************************************************************************
 *
 * File name   : clock-stxh205.h
 * Description : Low Level API - Clocks identifiers
 *
 * COPYRIGHT (C) 2009 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 *****************************************************************************/

/* ----- Modification history (most recent first)----

28/sep/11 ravinder-dlh.singh@st.com
	  More updates for ST_H205 add and remove H205_PS1/PS2 stuff
14/jul/11 ravinder-dlh.singh@st.com
	  Updates for ST_H205 based on feedback from Francesco Virlinzi
	  (Preliminary Linux update)
17/jun/11 ravinder-dlh.singh@st.com
	  Updates for ST_H205 added.
02/may/11 ravinder-dlh.singh@st.com
	  Updates for H205-PS1/PS2 to align VALID_int with STAPI delivery.
28/march/11 pooja.agarwal@st.com
	  Original version
*/

/* LLA version: YYYYMMDD */
#define LLA_VERSION 20120704

enum {
	CLK_SYSIN,
	CLK_SYSALT,

	/* Clockgen A-0 */
	CLK_A0_REF,		/* OSC clock */
	CLK_A0_PLL0HS,
	CLK_A0_PLL0LS,		/* LS = HS/2 */
	CLK_A0_PLL1HS,
	CLK_A0_PLL1LS,		/* LS = HS/2 */

	CLK_A0_SH4_ICK,
	CLK_A0_HQVDP_PROC,
	CLK_A0_IC_CPU,
	CLK_A0_LX_DMU_CPU,
	CLK_A0_LX_AUD_CPU,
	CLK_A0_SPARE_5,
	CLK_A0_IC_STNOC,
	CLK_A0_GDP_PROC,
	CLK_A0_SPARE_8,
	CLK_A0_NAND_CTRL,
	CLK_A0_IC_REG_LP_ON,
	CLK_A0_SECURE,
	CLK_A0_IC_TS_DMA,
	CLK_A0_TSOUT_SRC,
	CLK_A0_IC_REG_LP_OFF,
	CLK_A0_DMU_PREPROC,
	CLK_A0_THNS,
	CLK_A0_SPARE_17,
	CLK_A0_IC_IF,
	CLK_A0_PMB,
	CLK_A0_SPARE_20,
	CLK_A0_SYS_EMISS,
	CLK_A0_SPARE_22,
	CLK_A0_MASTER,


	/* Clockgen A-1 */
	CLK_A1_REF,		/* OSC clock */
	CLK_A1_PLL0HS,
	CLK_A1_PLL0LS,		/* LS = HS/2 */
	CLK_A1_PLL1HS,
	CLK_A1_PLL1LS,		/* LS = HS/2 */

	CLK_A1_IC_DDRCTRL,
	CLK_A1_SLIM_FDMA_0,
	CLK_A1_SLIM_FDMA_1,
	CLK_A1_BDISP_CK,
	CLK_A1_TP,
	CLK_A1_IC_COMPO_DISP,
	CLK_A1_IC_BDISP,
	CLK_A1_SPARE_7,
	CLK_A1_SPARE_8,
	CLK_A1_SPARE_9,
	CLK_A1_ETH_PHY,
	CLK_A1_SPARE_11,
	CLK_A1_IC_DMU,
	CLK_A1_SPARE_13,
	CLK_A1_IC_GMAC,
	CLK_A1_PTP_REF_CLK,
	CLK_A1_SPARE_16,
	CLK_A1_SPARE_17,
	CLK_A1_SPARE_18,
	CLK_A1_IQI,
	CLK_A1_SPARE_20,
	CLK_A1_CARD,
	CLK_A1_VDP_PROC,
	CLK_A1_SPARE_23,
	CLK_A1_SPARE_24,
	CLK_A1_SPARE_25,
	CLK_A1_SPARE_26,
	CLK_A1_SPARE_27,
	CLK_A1_SYS_MMC,

	/* Clockgen B/Video (FS0 and FS1) */
	CLK_B_REF,
	CLK_B_FS0_VCO,
	CLK_B_VID_HD_LOCAL,	/* CLK_FS0_CH1 */
	CLK_B_VID_SD_LOCAL,	/* CLK_FS0_CH2 */
	CLK_B_PCM0, 		/* CLK_FS0_CH3 */
	CLK_B_CLK_27_0,		/* CLK_FS0_CH4 */
	CLK_B_FS1_VCO,
	CLK_B_VID_SD_REMOTE,	/* CLK_FS1_CH1 */
	CLK_B_PCM2,		/* CLK_FS1_CH2 */
	CLK_B_CLOCK48,		/* CLK_FS1_CH3 */
	CLK_B_CLK_27_1,		/* CLK_FS1_CH4 */

	/* Clocks from Video Clock Controller */
	CLK_B_PIX_HD,		/* Channel 0 */
	CLK_B_DISP_HD,
	CLK_B_SPARE_2,
	CLK_B_DISP_SD,
	CLK_B_DENC_MAIN,
	CLK_B_GDP1,		/* GDP2 shares same clock) */
	CLK_B_GDP3,
	CLK_B_656,
	CLK_B_SPARE_8,
	CLK_B_PIX_MAIN,
	CLK_B_PIX_SD,
	CLK_B_SPARE_11,
	CLK_B_SPARE_12,
	CLK_B_GDP4,
	CLK_B_PIP,
	CLK_B_SPARE_15,		/* Channel 15 */

	/* Clockgen C / FS2 - General purpose/Audio */
	CLK_C_REF,
	CLK_C_FS_VCO,
	CLK_C_SPDIF,
	CLK_C_LPM,
	CLK_C_DSS,
	CLK_C_PCM1

};
