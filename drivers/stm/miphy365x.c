/*
 * STMicroelectronics MiPHY 3-65 style code
 *
 * Copyright (C) 2012 STMicroelectronics Limited
 * Author: Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
 * Orignially copied from miphy.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <asm/processor.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stm/platform.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/miphy.h>
#include "miphy.h"

#define DRIVER_VERSION "MiPHY3-65x Driver Version 1.00"

#define MIPHY_RESET			0x00
#define RST_RX				(1<<4)

#define MIPHY_STATUS			0x01
#define MIPHY_CONTROL			0x02
#define DIS_LINK_RST			(1<<4)

#define MIPHY_INT_STATUS		0x04
#define BITUNLOCK_INT			(1<<1)
#define SYMBUNLOCK_INT			(1<<2)
#define FIFOOVERLAP_INT			(1<<3)


#define MIPHY_BOUNDARY_1		0x10
#define POWERSEL_SEL			(1<<2)
#define SPDSEL_SEL			(1<<0)

#define MIPHY_BOUNDARY_3		0x12
#define RX_LSPD				(1<<5)

#define MIPHY_COMPENS_CONTROL_1		0x40

#define MIPHY_IDLL_TEST			0x72
#define START_CLK_HF			(1<<6)
#define STOP_CLK_HF			(1<<7)

#define MIPHY_DES_BITLOCK_CFG		0x85
#define CLEAR_BIT_UNLOCK_FLAG		(1<<0)
#define UPDATE_TRANS_DENSITY		(1<<1)

#define MIPHY_DES_BITLOCK		0x86

#define MIPHY_DES_BITLOCK_STATUS	0x88
#define BIT_LOCK			(1<<0)
#define BIT_LOCK_FAILED			(1<<1)
#define BIT_UNLOCK			(1<<2)
#define TRANS_DENSITY_UPDATED		(1<<3)

#define MIPHY_VERSION			0xfb
#define MIPHY_REVISION			0xfc

/***************************** MiPHY3-65 ops ********************************/

#ifdef DEBUG
/* Pretty-ish print of Miphy registers, helpful for debugging */
void stm_miphy_dump_registers(struct stm_miphy *miphy)
{
       printk(KERN_INFO "MIPHY_RESET (0x0): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_RESET));
       printk(KERN_INFO "MIPHY_STATUS (0x1): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_STATUS));
       printk(KERN_INFO "MIPHY_CONTROL (0x1): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_CONTROL));
       printk(KERN_INFO "MIPHY_INT_STATUS (0x4): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_INT_STATUS));
       printk(KERN_INFO "MIPHY_BOUNDARY_1 (0x10): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_BOUNDARY_1));
       printk(KERN_INFO "MIPHY_BOUNDARY_3 (0x12): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_BOUNDARY_3));
       printk(KERN_INFO "MIPHY_COMPENS_CONTROL_1 (0x40): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_COMPENS_CONTROL_1));
       printk(KERN_INFO "MIPHY_IDLL_TEST (0x72): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_IDLL_TEST));
       printk(KERN_INFO "MIPHY_DES_BITLOCK_CFG (0x85): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_DES_BITLOCK_CFG));
       printk(KERN_INFO "MIPHY_DES_BITLOCK (0x86): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_DES_BITLOCK));
       printk(KERN_INFO "MIPHY_DES_BITLOCK_STATUS (0x88): 0x%.2x\n",
			   stm_miphy_read(miphy, MIPHY_DES_BITLOCK_STATUS));
}
#endif /* DEBUG */

static void miphy365x_tap_start_port0(const struct stm_miphy_device *miphy_dev)
{
	int timeout;
	void (*reg_write)(int port, u8 addr, u8 data);
	u8 (*reg_read)(int port, u8 addr);

	reg_read = miphy_dev->reg_read;
	reg_write = miphy_dev->reg_write;

	/* TODO: Get rid of this */
	if (cpu_data->type == CPU_STX7108) {
		/*Force SATA port 1 in Slumber Mode */
		reg_write(1, 0x11, 0x8);
		/*Force Power Mode selection from MiPHY soft register 0x11 */
		reg_write(1, 0x10, 0x4);
	}

	/* Force Macro1 in reset and request PLL calibration reset */

	/* Force PLL calibration reset, PLL reset and assert
	 * Deserializer Reset */
	reg_write(0, 0x00, 0x16);
	reg_write(0, 0x11, 0x0);
	/* Force macro1 to use rx_lspd, tx_lspd (by default rx_lspd
	 * and tx_lspd set for Gen1)  */
	reg_write(0, 0x10, 0x1);

	/* Force Recovered clock on first I-DLL phase & all
	 * Deserializers in HP mode */

	/* Force Rx_Clock on first I-DLL phase on macro1 */
	reg_write(0, 0x72, 0x40);
	/* Force Des in HP mode on macro1 */
	reg_write(0, 0x12, 0x00);

	/* Wait for HFC_READY = 0 */
	timeout = 50; /* Jeeeezzzzz.... */
	while (timeout-- && (reg_read(0, 0x01) & 0x3))
		udelay(2000);
	if (timeout < 0)
		pr_err("%s(): HFC_READY timeout!\n", __func__);

	/* Restart properly Process compensation & PLL Calibration */

	/* Set properly comsr definition for 30 MHz ref clock */
	reg_write(0, 0x41, 0x1E);
	/* comsr compensation reference */
	reg_write(0, 0x42, 0x28);
	/* Set properly comsr definition for 30 MHz ref clock */
	reg_write(0, 0x41, 0x1E);
	/* comsr cal gives more suitable results in fast PVT for comsr
	   used by TX buffer to build slopes making TX rise/fall fall
	   times. */
	reg_write(0, 0x42, 0x33);
	/* Force VCO current to value defined by address 0x5A */
	reg_write(0, 0x51, 0x2);
	/* Force VCO current to value defined by address 0x5A */
	reg_write(0, 0x5A, 0xF);
	/* Enable auto load compensation for pll_i_bias */
	reg_write(0, 0x47, 0x2A);
	/* Force restart compensation and enable auto load for
	 * Comzc_Tx, Comzc_Rx & Comsr on macro1 */
	reg_write(0, 0x40, 0x13);

	/* Wait for comzc & comsr done */
	while ((reg_read(0, 0x40) & 0xC) != 0xC)
		cpu_relax();

	/* Recommended settings for swing & slew rate FOR SATA GEN 1
	 * from CPG */
	reg_write(0, 0x20, 0x00);
	/* (Tx Swing target 500-550mV peak-to-peak diff) */
	reg_write(0, 0x21, 0x2);
	/* (Tx Slew target120-140 ps rising/falling time) */
	reg_write(0, 0x22, 0x4);

	/* Force Macro1 in partial mode & release pll cal reset */
	reg_write(0, 0x00, 0x10);
	udelay(10);

#if 0
	/* SSC Settings. SSC will be enabled through Link */
	reg_write(0, 0x53, 0x00); /* pll_offset */
	reg_write(0, 0x54, 0x00); /* pll_offset */
	reg_write(0, 0x55, 0x00); /* pll_offset */
	reg_write(0, 0x56, 0x04); /* SSC Ampl=0.48% */
	reg_write(0, 0x57, 0x11); /* SSC Ampl=0.48% */
	reg_write(0, 0x58, 0x00); /* SSC Freq=31KHz */
	reg_write(0, 0x59, 0xF1); /* SSC Freq=31KHz */
	/*SSC Settings complete*/
#endif

	reg_write(0, 0x50, 0x8D);
	reg_write(0, 0x50, 0x8D);

	/*  Wait for phy_ready */
	/*  When phy is in ready state ( register 0x01 of macro1 to 0x13) */

	while ((reg_read(0, 0x01) & 0x03) != 0x03)
		cpu_relax();

	/* Enable macro1 to use rx_lspd  & tx_lspd from link interface */
	reg_write(0, 0x10, 0x00);
	/* Release Rx_Clock on first I-DLL phase on macro1 */
	reg_write(0, 0x72, 0x00);

	/* Deassert deserializer reset */
	reg_write(0, 0x00, 0x00);
	/* des_bit_lock_en is set */
	reg_write(0, 0x02, 0x08);

	/* bit lock detection strength */
	reg_write(0, 0x86, 0x61);
}

/*
 * MiPhy Port 1 Start function for SATA
 */
static void miphy365x_tap_start_port1(const struct stm_miphy_device *miphy_dev)
{
	int timeout;
	void (*reg_write)(int port, u8 addr, u8 data);
	u8 (*reg_read)(int port, u8 addr);

	reg_read = miphy_dev->reg_read;
	reg_write = miphy_dev->reg_write;

	/* Force PLL calibration reset, PLL reset and assert Deserializer
	 * Reset */
	reg_write(1, 0x00, 0x2);
	/* Force restart compensation and enable auto load for Comzc_Tx,
	 * Comzc_Rx & Comsr on macro2 */
	reg_write(1, 0x40, 0x13);

	/* Force PLL reset  */
	reg_write(0, 0x00, 0x2);
	/* Set properly comsr definition for 30 MHz ref clock */
	reg_write(0, 0x41, 0x1E);
	/* to get more optimum result on comsr calibration giving faster
	 * rise/fall time in SATA spec Gen1 useful for some corner case.*/
	reg_write(0, 0x42, 0x33);
	/* Force restart compensation and enable auto load for Comzc_Tx,
	 * Comzc_Rx & Comsr on macro1 */
	reg_write(0, 0x40, 0x13);

	/*Wait for HFC_READY = 0*/
	timeout = 50; /* Jeeeezzzzz.... */
	while (timeout-- && (reg_read(0, 0x01) & 0x3))
		udelay(2000);
	if (timeout < 0)
		pr_err("%s(): HFC_READY timeout!\n", __func__);

	reg_write(1, 0x11, 0x0);
	/* Force macro2 to use rx_lspd, tx_lspd  (by default rx_lspd and
	 * tx_lspd set for Gen1) */
	reg_write(1, 0x10, 0x1);
	/* Force Rx_Clock on first I-DLL phase on macro2*/
	reg_write(1, 0x72, 0x40);
	/* Force Des in HP mode on macro2 */
	reg_write(1, 0x12, 0x00);

	while ((reg_read(1, 0x40) & 0xC) != 0xC)
		cpu_relax();

	/*RECOMMENDED SETTINGS for Swing & slew rate FOR SATA GEN 1 from CPG*/
	reg_write(1, 0x20, 0x00);
	/*(Tx Swing target 500-550mV peak-to-peak diff) */
	reg_write(1, 0x21, 0x2);
	/*(Tx Slew target120-140 ps rising/falling time) */
	reg_write(1, 0x22, 0x4);
	/*Force Macr21 in partial mode & release pll cal reset */
	reg_write(1, 0x00, 0x10);
	udelay(10);
	/* Release PLL reset  */
	reg_write(0, 0x00, 0x0);

	/*  Wait for phy_ready */
	/*  When phy is in ready state ( register 0x01 of macro1 to 0x13)*/
	while ((reg_read(1, 0x01) & 0x03) != 0x03)
		cpu_relax();

	/* Enable macro1 to use rx_lspd  & tx_lspd from link interface */
	reg_write(1, 0x10, 0x00);
	/* Release Rx_Clock on first I-DLL phase on macro1 */
	reg_write(1, 0x72, 0x00);

	/* Deassert deserializer reset */
	reg_write(1, 0x00, 0x00);
	/*des_bit_lock_en is set */
	reg_write(1, 0x02, 0x08);

	/*bit lock detection strength */
	reg_write(1, 0x86, 0x61);
}

static int miphy365x_tap_sata_start(int port, struct stm_miphy_device *miphy_dev)
{
	int rval = 0;
	switch (port) {
	case 0:
		miphy365x_tap_start_port0(miphy_dev);
		break;
	case 1:
		miphy365x_tap_start_port1(miphy_dev);
		break;
	default:
		rval = -EINVAL;
	}
	return rval;
}

static int miphy365x_tap_pcie_start(int port, struct stm_miphy_device *miphy_dev)
{
	/* The tap interface versions do not support PCIE */
	return -1;
}

/*
 * MiPhy Port 0 & 1 Start function for SATA
 */

static int miphy365x_uport_sata_start(int port, struct stm_miphy_device *miphy_dev)
{
	unsigned int regvalue;
	int timeout;
	void (*reg_write)(int port, u8 addr, u8 data);
	u8 (*reg_read)(int port, u8 addr);

	if (port < 0 || port > 1)
		return -EINVAL;

	reg_write = miphy_dev->reg_write;
	reg_read = miphy_dev->reg_read;

	/* Force PLL calibration reset, PLL reset
	 * and assert Deserializer Reset */
	reg_write(port, 0x00, 0x16);
	reg_write(port, 0x11, 0x0);
	/* Force macro1 to use rx_lspd, tx_lspd
	 * (by default rx_lspd and tx_lspd set for Gen1) */
	reg_write(port, 0x10, 0x1);
	/* Force Rx_Clock on first I-DLL phase on macro1 */
	reg_write(port, 0x72, 0x40);
	/* Force Des in HP mode on macro1 */
	reg_write(port, 0x12, 0x00);

	/*Wait for HFC_READY = 0*/
	timeout = 50;
	while (timeout-- && (reg_read(port, 0x01) & 0x3))
		udelay(2000);
	if (timeout < 0)
		pr_err("%s(): HFC_READY timeout!\n", __func__);

	/*Set properly comsr definition for 30 MHz ref clock */
	reg_write(port, 0x41, 0x1E);
	 /*Set properly comsr definition for 30 MHz ref clock */
	reg_write(port, 0x42, 0x33);
	/* Force VCO current to value defined by address 0x5A
	 * and disable PCIe100Mref bit */
	reg_write(port, 0x51, 0x2);
	/* Enable auto load compensation for pll_i_bias */
	reg_write(port, 0x47, 0x2A);

	/* Force restart compensation and enable auto load for
	 * Comzc_Tx, Comzc_Rx & Comsr on macro1 */
	reg_write(port, 0x40, 0x13);
	while ((reg_read(port, 0x40) & 0xC) != 0xC)
		cpu_relax();

	/* STOS_SemaphoreWait(MiPHY_Int);  Wait for Compensation
	 * Completion Interrupt : Not using MiPHY Interrupt for now */
	/* Recommended Settings for Swing & slew rate FOR SATA GEN 1 from CCI
	 * conf gen sel = 00b to program Gen1 banked registers &
	 * VDDT filter ON */
	reg_write(port, 0x20, 0x10);
	/*(Tx Swing target 500-550mV peak-to-peak diff) */
	reg_write(port, 0x21, 0x3);
	/*(Tx Slew target120-140 ps rising/falling time) */
	reg_write(port, 0x22, 0x4);
	/*Force Macro1 in partial mode & release pll cal reset */
	reg_write(port, 0x00, 0x10);
	udelay(100);
	/* SSC Settings. SSC will be enabled through Link */
	/*  pll_offset */
	reg_write(port, 0x53, 0x00);
	/*  pll_offset */
	reg_write(port, 0x54, 0x00);
	/*  pll_offset */
	reg_write(port, 0x55, 0x00);
	/*  SSC Ampl.=0.4%  */
	reg_write(port, 0x56, 0x03);
	/*  SSC Ampl.=0.4% */
	reg_write(port, 0x57, 0x63);
	/*  SSC Freq=31KHz */
	reg_write(port, 0x58, 0x00);
	/*  SSC Freq=31KHz   */
	reg_write(port, 0x59, 0xF1);
	/*SSC Settings complete*/
	reg_write(port, 0x50, 0x8D);
	/*MIPHY PLL ratio */
	reg_read(port, 0x52);
	/*  Wait for phy_ready */
	/* When phy is in ready state ( register 0x01 reads 0x13)*/
	regvalue = reg_read(port, 0x01);
	timeout = 50;
	while (timeout-- && ((regvalue & 0x03) != 0x03)) {
		regvalue = reg_read(port, 0x01);
		udelay(2000);
	}
	if (timeout < 0)
		pr_err("%s(): HFC_READY timeout!\n", __func__);
	if ((regvalue & 0x03) == 0x03) {
		/* Enable macro1 to use rx_lspd  &
		 * tx_lspd from link interface */
		reg_write(port, 0x10, 0x00);
		/* Release Rx_Clock on first I-DLL phase on macro1 */
		reg_write(port, 0x72, 0x00);
		/* Assert deserializer reset */
		reg_write(port, 0x00, 0x10);
		/* des_bit_lock_en is set */
		reg_write(port, 0x02, 0x08);
		/* bit lock detection strength */
		reg_write(port, 0x86, 0x61);
		/* Deassert deserializer reset */
		reg_write(port, 0x00, 0x00);
	}
	return 0;
}


static int miphy365x_uport_pcie_start(int port, struct stm_miphy_device *miphy_dev)
{
	/* The hardware sets everything up for us, so far nothing to do here */
	return 0;
}


static int miphy365x_start(struct stm_miphy *miphy)
{
	struct stm_miphy_device *dev;
	void (*reg_write)(int port, u8 addr, u8 data);
	int port;
	int rval = -ENODEV;

	dev = miphy->dev;
	reg_write = dev->reg_write;
	port = miphy->port;

	switch (miphy->mode) {
	case SATA_MODE:
		switch (dev->type) {
		case TAP_IF:
			rval = miphy365x_tap_sata_start(port, dev);
			break;
		case UPORT_IF:
			rval = miphy365x_uport_sata_start(port, dev);
			break;
		default:
			BUG();
		}
		break;
	case PCIE_MODE:
		switch (dev->type) {
		case TAP_IF:
			rval = miphy365x_tap_pcie_start(port, dev);
			break;
		case UPORT_IF:
			rval = miphy365x_uport_pcie_start(port, dev);
			break;
		default:
			BUG();
		}
		break;
	default:
		BUG();
	}

	/* Clear the contents of interrupt control register,
	   excluding fifooverlap_int */
	reg_write(miphy->port, MIPHY_INT_STATUS, 0x77);

	return rval;
}

static void miphy365x_assert_deserializer(struct stm_miphy *miphy, int assert)
{
	struct stm_miphy_device *dev = miphy->dev;
	void (*reg_write)(int port, u8 addr, u8 data) = dev->reg_write;
	reg_write(miphy->port, 0x00, assert ? 0x10 : 0x00);
}

static int miphy365x_sata_status(struct stm_miphy *miphy)
{
	struct stm_miphy_device *dev = miphy->dev;
	u8 (*reg_read)(int port, u8 addr) = dev->reg_read;
	u8 miphy_int_status;
	miphy_int_status = reg_read(miphy->port, 0x4);
	return (miphy_int_status & 0x8) || (miphy_int_status & 0x2);
}


/*****************************End of MiPHY3-65 ops****************************/

/* MiPHY3-65 Ops */
static struct stm_miphy_style_ops miphy365x_ops = {
	.miphy_start 			= miphy365x_start,
	.miphy_assert_deserializer	= miphy365x_assert_deserializer,
	.miphy_sata_status		= miphy365x_sata_status,
};

static int miphy365x_probe(struct stm_miphy *miphy)
{
	pr_info("MiPHY driver style %s probed successfully\n",
		ID_MIPHY365X);
	return 0;
}

static int miphy365x_remove(void)
{
	return 0;
}

static struct stm_miphy_style miphy365x_style = {
	.style_id	= ID_MIPHY365X,
	.probe		= miphy365x_probe,
	.remove		= miphy365x_remove,
	.miphy_ops	= &miphy365x_ops,
};

static int __init miphy365x_init(void)
{
	int result;

	result = miphy_register_style(&miphy365x_style);
	if (result)
		pr_err("MiPHY driver style %s register failed (%d)",
				ID_MIPHY365X, result);

	return result;
}
postcore_initcall(miphy365x_init);

MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@st.com>");
MODULE_DESCRIPTION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
