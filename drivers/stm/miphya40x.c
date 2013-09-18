/*
 * STMicroelectronics MiPHY A-40x style code
 *
 * Copyright (C) 2012 STMicroelectronics Limited
 * Author: Srinivas Kandagatla <srinivas.kandagatla@st.com>
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

#define DRIVER_VERSION "MiPHYA-40x Driver Version 1.00"

#define MIPHY_INT_STATUS		0x04
#define MIPHY_VERSION			0xfb
#define MIPHY_REVISION			0xfc

static int miphya40x_sata_start(int port, struct stm_miphy_device *miphy_dev)
{
	u8 version, revision;
	void (*reg_write)(int port, u8 addr, u8 data);
	u8 (*reg_read)(int port, u8 addr);

	if (port < 0 || port > 0)
		return -EINVAL;

	reg_write = miphy_dev->reg_write;
	reg_read = miphy_dev->reg_read;
	version = reg_read(port, MIPHY_VERSION);
	revision = reg_read(port, MIPHY_REVISION);

	/* MIPHYA-40LP series */
	/* Force macro1 to use rx_lspd, tx_lspd
	 * (by default rx_lspd and tx_lspd set for Gen2) */
	reg_write(port, 0x10, 0x1);
	reg_write(port, 0x12, 0xE9);
	reg_write(port, 0x53, 0x64);
	/* Force PLL calibration reset, PLL reset and
	 * assert Deserializer Reset */
	reg_write(port, 0x00, 0x86);
	/* Waiting for PLL to shut down*/
	if (miphy_dev->tx_pol_inv)
		/*For TXP and TXN swap */
		reg_write(port, 0x02, 0x10);

	while ((reg_read(port, 0x1) & 0x3) != 0x0)
		cpu_relax();
	/*Compensation Recalibration*/
	reg_write(port, 0x3E, 0x0F);
	reg_write(port, 0x3D, 0x61);
	mdelay(100);
	reg_write(port, 0x00, 0x00);
	/* Configure PLL */
	reg_write(port, 0x50, 0x9D);
	/* Configure PLL x2 ( this one allows to read
	 * correct value in feedback register) */
	reg_write(port, 0x50, 0x9D);
	reg_write(port, 0x51, 0x2);
	/*Unbanked settings */
	/*lock onpattern complete*/
	reg_write(port, 0xAA, 0xE);
	/*vertical bitlock enabled */
	reg_write(port, 0x86, 0x81);

	if (miphy_dev->tx_pol_inv)
		reg_write(port, 0x02, 0x14);
	else
		reg_write(port, 0x02, 0x4);

	/*IDLL Setup */
	reg_write(port, 0x77, 0xf);
	/*IDLL_DC_THRESHOLD*/
	reg_write(port, 0x77, 0x8);
	reg_write(port, 0x70, 0xF0);
	udelay(100);
	/*IDLL_MODE*/
	reg_write(port, 0x70, 0x70);

	/*VGA Settings*/
	/*VGA CONTROL*/
	reg_write(port, 0x95, 0x01);
	/*VGA GAIN SET TO GAIN 6*/
	reg_write(port, 0x96, 0x1F);
	/*VGA OFFSET SET TO 0*/
	reg_write(port, 0x97, 0x00);
	/*Rx Buffer Setup*/
	/* Bypass threshold optimization */
	reg_write(port, 0xAA, 0x6);
	/*Put threshold in zero volt crossing*/
	reg_write(port, 0xAE, 0x0);
	/*Put threshold in zero volt crossing*/
	reg_write(port, 0xAF, 0x0);
	/*Force Macro1 in partial mode & release pll cal reset */
	mdelay(100);
	/* Banked Settingsfor GEN-1 and 2*/

	reg_write(port, 0x10, 0x1);
	reg_write(port, 0x30, 0x10);
	/*Tx Buffer Settings */
	/*Slew*/
	reg_write(port, 0x32, 0x1);
	/*Swing*/
	reg_write(port, 0x31, 0x06);
	/*Preempahsis*/
	reg_write(port, 0x32, 0x21);
	/*Sigdet Settings*/

	/*MAN_SIGDET_CTRL=1*/
	reg_write(port, 0x80, 0xD);
	/* SIGDET_MODE_LP=0 */
	reg_write(port, 0x85, 0x01);
	/*EN_SIGDET_EQ=1*/
	reg_write(port, 0x85, 0x11);
	reg_write(port, 0xD9, 0x24);
	reg_write(port, 0xDA, 0x24);
	reg_write(port, 0xDB, 0x64);
	reg_write(port, 0x84, 0xE0);

	/*Rx Buffer Settings */
	reg_write(port, 0x85, 0x51);
	reg_write(port, 0x85, 0x51);
	reg_write(port, 0x83, 0x10);
	/*For Equa-7 RX =Equalization*/
	reg_write(port, 0x84, 0xE0);
	reg_write(port, 0xD0, 0x01);

	while (reg_read(port, 0x69) != 0x64)
		cpu_relax();

	while ((reg_read(port, 0x1) & 0x03) != 0x03)
		cpu_relax();
	reg_write(port, 0x10, 0x00);
	return 0;
}

static int miphya40x_pcie_start(int port, struct stm_miphy_device *miphy_dev)
{
	return 0;
}

static int miphya40x_start(struct stm_miphy *miphy)
{
	struct stm_miphy_device *dev = miphy->dev;
	int port = miphy->port;
	int rval = -ENODEV;

	switch (miphy->mode) {
	case SATA_MODE:
		rval = miphya40x_sata_start(port, dev);
		break;
	case PCIE_MODE:
		rval = miphya40x_pcie_start(port, dev);
		break;
	default:
		BUG();
	}

	return rval;
}

/* MiPHYA40x Ops */
static struct stm_miphy_style_ops miphya40x_ops = {
	.miphy_start 		= miphya40x_start,
};

static int miphya40x_probe(struct stm_miphy *miphy)
{
	pr_info("MiPHY driver style %s probed successfully\n",
		ID_MIPHYA40X);
	return 0;
}

static int miphya40x_remove(void)
{
	return 0;
}

static struct stm_miphy_style miphya40x_style = {
	.style_id 	= ID_MIPHYA40X,
	.probe		= miphya40x_probe,
	.remove		= miphya40x_remove,
	.miphy_ops	= &miphya40x_ops,
};

static int __init miphya40x_init(void)
{
	int result;

	result = miphy_register_style(&miphya40x_style);
	if (result)
		pr_err("MiPHY driver style %s register failed (%d)",
				ID_MIPHYA40X, result);

	return result;
}
postcore_initcall(miphya40x_init);

MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@st.com>");
MODULE_DESCRIPTION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
