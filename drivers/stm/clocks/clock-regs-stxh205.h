/*****************************************************************************
 *
 * File name   : clock-regs-stxh205.h
 * Description : Low Level API - Base addresses & register definitions.
 *
 * COPYRIGHT (C) 2009 STMicroelectronics - All Rights Reserved
 * May be copied or modified under the terms of the GNU General Public
 * License V2 _ONLY_.  See linux/COPYING for more information.
 *
 *****************************************************************************/

/* ----- Modification history (most recent first)----

28/sep/11 ravinder-dlh.singh@st.com
	  More updates for ST_H205 add and remove H205_PS1/PS2 stuff
14/jul/11 ravinder-dlh.singh@st.com
	  Updates for ST_H205 based on feedback from Francesco Virlinzi
17/jun/11 ravinder-dlh.singh@st.com
	  Updates for ST_H205 added.
02/may/11 ravinder-dlh.singh@st.com
	  Updates for H205 to align VALID_int with STAPI delivery.
28/march/11 pooja.agarwal@st.com
	  Original version
*/

#ifndef __CLOCK_LLA_REGS_H
#define __CLOCK_LLA_REGS_H

#define CKGA0_BASE_ADDRESS				0xfde98000
#define CKGA1_BASE_ADDRESS				0xfdab8000

#define CKGB_BASE_ADDRESS				0xfd541000
#define CKGC_BASE_ADDRESS				0xfd541000



/* --- CKGA registers --- */
#define CKGA_PLL0_REG0_CFG              0x000
#define CKGA_PLL0_REG1_CFG              0x004
#define CKGA_PLL0_REG2_CFG              0x008
#define CKGA_PLL1_REG0_CFG              0x00c
#define CKGA_PLL1_REG1_CFG              0x010
#define CKGA_PLL1_REG2_CFG              0x014
#define CKGA_POWER_CFG	                0x018
#define CKGA_CLKOPSRC_SWITCH_CFG        0x01c
#define CKGA_CLKOPSRC_SWITCH_CFG2       0x020
#define CKGA_PLL0_ENABLE_FB             0x024
#define CKGA_PLL1_ENABLE_FB             0x028
#define CKGA_OSC_ENABLE_FB              0x02c

#define CKGA_CLKOBS_MUX0_CFG            0x030
#define CKGA_CLKOBS_MASTER_MAXCOUNT     0x034
#define CKGA_CLKOBS_CMD                 0x038
#define CKGA_CLKOBS_STATUS              0x03c
#define CKGA_CLKOBS_SLAVE0_COUNT        0x040
#define CKGA_OSCMUX_DEBUG               0x044
#define CKGA_CLKOBS_MUX1_CFG            0x048
#define CKGA_LOW_POWER_CTRL             0x04C
#define CKGA_LOW_POWER_CFG              0x050

#define CKGA_OSC_DIV0_CFG		0x800
#define CKGA_PLL0HS_DIV0_CFG		0x900
#define CKGA_PLL0LS_DIV0_CFG		0xA08
#define CKGA_PLL1HS_DIV0_CFG		0x980
#define CKGA_PLL1LS_DIV0_CFG		0xAD8

/* --- CLOCKGENB (Video FS0 (bank 0) and FS1 (bank1) ) registers --- */

#define CKGB_FS_CFG(_bk) 		(0x60 + (_bk) * 0xd8)
#define CKGB_FS_MD(_bk, _chan)		(0x64 + (_bk) * 0xd8 + (_chan) * 0x10)
#define CKGB_FS_PE(_bk, _chan)		(0x68 + (_bk) * 0xd8 + (_chan) * 0x10)
#define CKGB_FS_SDIV(_bk, _chan)	(0x6c + (_bk) * 0xd8 + (_chan) * 0x10)
#define CKGB_FS_NSDIV(_bk)		(0x70 + (_bk) * 0xd8)
#define CKGB_FS_EN_PRG(_bk, _chan)	(0x70 + (_bk) * 0xd8 + (_chan) * 0x10)

/* --- CLOCKGENC (General purpose/ Audio FS2 registers --- */


#define CKGC_FS_CFG	   		0x018
#define CKGC_FS_MD(_chan)		(0x1C + (0x10 * _chan))
#define CKGC_FS_PE(_chan)		(0x20 + (0x10 * _chan))
#define CKGC_FS_SDIV(_chan)		(0x24 + (0x10 * _chan))
#define CKGC_FS_NSDIV			(0x28)
#define CKGC_FS_EN_PRG(_chan)		(0x28 + (0x10 * _chan))

#endif  /* End __CLOCK_LLA_REGS_H */
