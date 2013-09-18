/*
 * Copyright (C) 2007 STMicroelectronics Limited
 * Author: Stuart Menefy <stuart.menefy@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#include <linux/kernel.h>
#include <linux/sysdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/stm/platform.h>
#include <linux/stm/emi.h>
#include <linux/stm/device.h>
#include <linux/stm/clk.h>
#include <linux/stm/pm_sys.h>

#define EMISS_CONFIG				0x0000
#define EMISS_CONFIG_PCI_CLOCK_MASTER 		(0x1 << 0)
#define EMISS_CONFIG_CLOCK_SELECT_MASK 		(0x3 << 1)
#define EMISS_CONFIG_CLOCK_SELECT_PCI		(0x1 << 2)
#define EMISS_CONFIG_PCI_HOST_NOT_DEVICE	(0x1 << 5)
#define EMISS_CONFIG_HAMMING_NOT_BCH		(0x1 << 6)

#define EMISS_ARBITER_CONFIG			0x0004
#define EMISS_ARBITER_CONFIG_SLAVE_NOTMASTER	(1 << 0)
#define EMISS_ARBITER_CONFIG_GRANT_RETRACTION	(1 << 1)
#define EMISS_ARBITER_CONFIG_BYPASS_ARBITER 	(1 << 2)
#define EMISS_ARBITER_CONFIG_MASK_BUS_REQ0 	(1 << 4)
/* Can use req0 with following macro */
#define EMISS_ARBITER_CONFIG_MASK_BUS_REQ(n)	(1 << (4 + (n)))
#define EMISS_ARBITER_CONFIG_BUS_REQ_ALL_MASKED (0xf << 4)
#define EMISS_ARBITER_CONFIG_PCI_NOT_EMI	(1 << 8)
#define EMISS_ARBITER_CONFIG_BUS_FREE		(1 << 9)
#define EMISS_ARBITER_CONFIG_STATIC_NOT_DYNAMIC	(1 << 12)

#define EMISS_INTERFACE_EMI			0
#define EMISS_INTERFACE_NAND			1
#define EMISS_INTERFACE_PCI			2
#define EMISS_INTERFACE_REQ0			3
#define EMISS_INTERFACE_REQ1			4
#define EMISS_INTERFACE_REQ2			5
#define EMISS_INTERFACE_REQ3			6

#define EMISS_FRAME_LENGTH(n)			(0x010 + ((n)*0x10))
#define EMISS_HOLDOFF(n)			(0x014 + ((n)*0x10))
#define EMISS_PRIORITY(n)			(0x018 + ((n)*0x10))
#define EMISS_BANDWIDTH_LIMIT(n)		(0x01c + ((n)*0x10))


#define EMI_GEN_CFG			0x0028
#define EMI_BANKNUMBER			0x0860
#define EMI_BANK_ENABLE			0x0280
#define BANK_BASEADDRESS(b)		(0x800 + (0x10 * b))
#define BANK_EMICONFIGDATA(b, r)	(0x100 + (0x40 * b) + (8 * r))
#define EMI_COMMON_CFG(reg)		(0x10 + (0x8 * (reg)))

#define EMI_CFG_DATA0_WRITE_CS		(1 << 10)

static struct platform_device *emi;
static struct clk *emi_clk;
#define emi_initialised			(emi != NULL)
static unsigned long emi_memory_base;
static void __iomem *emi_control;
static void __iomem *emiss_config;
static struct stm_device_state *emi_device_state;

static inline void emi_clk_xxable(int enable)
{
	if (!emi_clk || IS_ERR(emi_clk))
		return;

	if (enable)
		clk_enable(emi_clk);
	else
		clk_disable(emi_clk);
}

static inline void emi_clk_enable(void)
{
	emi_clk_xxable(1);
}

static inline void emi_clk_disable(void)
{
	emi_clk_xxable(0);
}


unsigned long emi_bank_base(int bank)
{
	unsigned long reg;

	BUG_ON(bank < 0 || bank >= EMI_BANKS);
	BUG_ON(!emi_initialised);

	reg = readl(emi_control + BANK_BASEADDRESS(bank));

	return emi_memory_base + (reg << 22);
}
EXPORT_SYMBOL_GPL(emi_bank_base);

void emi_bank_configure(int bank, unsigned long data[4])
{
	int i;

	BUG_ON(bank < 0 || bank >= EMI_BANKS);
	BUG_ON(!emi_initialised);

	for (i = 0; i < 4; i++)
		writel(data[i], emi_control + BANK_EMICONFIGDATA(bank, i));
}
EXPORT_SYMBOL_GPL(emi_bank_configure);

void emi_bank_write_cs_enable(int bank, int enable)
{
	unsigned long reg;

	BUG_ON(bank < 0 || bank >= EMI_BANKS);
	BUG_ON(!emi_initialised);

	reg = readl(emi_control + BANK_EMICONFIGDATA(bank, 0));

	if (enable)
		reg |= EMI_CFG_DATA0_WRITE_CS;
	else
		reg &= ~EMI_CFG_DATA0_WRITE_CS;

	writel(reg, emi_control + BANK_EMICONFIGDATA(bank, 0));
}
EXPORT_SYMBOL_GPL(emi_bank_write_cs_enable);

void emi_config_pcmode(int bank, int pc_mode)
{
	int mask;

	BUG_ON(!emi_initialised);

	switch (bank) {
	case 2:	/* Bank C */
		mask = 1<<3;
		break;
	case 3:	/* Bank D */
		mask = 1<<4;
		break;
	default:
		mask = 0;
		break;
	}

	if (mask) {
		u32 val = readl(emi_control + EMI_GEN_CFG);
		if (pc_mode)
			val |= mask;
		else
			val &= (~mask);
		writel(val, emi_control + EMI_GEN_CFG);
	}
}
EXPORT_SYMBOL_GPL(emi_config_pcmode);

/*
 *                ______________________________
 * EMIADDR    ___/                              \________
 *               \______________________________/
 *
 * (The cycle time specified in nano seconds)
 *
 *                |-----------------------------| cycle_time
 *                 ______________                ___________
 * CYCLE_TIME     /              \______________/
 *
 *
 * (IORD_start the number of nano seconds after the start of the cycle the
 * RD strobe is asserted IORD_end the number of nano seconds before the
 * end of the cycle the RD strobe is de-asserted.)
 *                   _______________________
 * IORD       ______/                       \________
 *
 *               |--|                       |---|
 *                 ^--- IORD_start            ^----- IORD_end
 *
 * (RD_latch the number of nano seconds at the end of the cycle the read
 * data is latched)
 *                                  __
 * RD_LATCH  ______________________/__\________
 *
 *                                 |------------|
 *                                      ^---------- RD_latch
 *
 * (IOWR_start the number of nano seconds after the start of the cycle the
 * WR strobe is asserted IOWR_end the number of nano seconds before the
 * end of the cycle the WR strobe is de-asserted.)
 *                   _______________________
 * IOWR       ______/                       \________
 *
 *               |--|                       |---|
 *                 ^--- IOWR_start            ^----- IOWR_end
 */



/* NOTE: these calculations assume a 100MHZ clock */



static void __init set_pata_read_timings(int bank, int cycle_time,
		int IORD_start, int IORD_end, int RD_latch)
{
	cycle_time = cycle_time / 10;
	IORD_start = IORD_start / 5;
	IORD_end = IORD_end / 5;
	RD_latch = RD_latch / 10;

	writel((cycle_time << 24) | (IORD_start << 8) | (IORD_end << 12),
			emi_control + BANK_EMICONFIGDATA(bank, 1));
	writel(0x791 | (RD_latch << 20),
			emi_control + BANK_EMICONFIGDATA(bank, 0));
}

static void __init set_pata_write_timings(int bank, int cycle_time,
		int IOWR_start, int IOWR_end)
{
	cycle_time = cycle_time / 10;
	IOWR_start = IOWR_start / 5;
	IOWR_end = IOWR_end / 5;

	writel((cycle_time << 24) | (IOWR_start << 8) | (IOWR_end << 12),
			emi_control + BANK_EMICONFIGDATA(bank, 2));
}

void __init emi_config_pata(int bank, int pc_mode)
{
	BUG_ON(bank < 0 || bank >= EMI_BANKS);
	BUG_ON(!emi_initialised);

	/* Set timings for PIO4 */
	set_pata_read_timings(bank, 120, 35, 30, 20);
	set_pata_write_timings(bank, 120, 35, 30);

	emi_config_pcmode(bank, pc_mode);

}

static void set_nand_read_timings(int bank, int cycle_time,
		int IORD_start, int IORD_end,
		int RD_latch, int busreleasetime,
		int wait_active_low )
{
	cycle_time = cycle_time / 10;		/* cycles */
	IORD_start = IORD_start / 5;		/* phases */
	IORD_end = IORD_end / 5;		/* phases */
	RD_latch = RD_latch / 10;		/* cycles */
	busreleasetime = busreleasetime / 10;   /* cycles */

	writel(0x04000699 | (busreleasetime << 11) | (RD_latch << 20) | (wait_active_low << 25),
			emi_control + BANK_EMICONFIGDATA(bank, 0));

	writel((cycle_time << 24) | (IORD_start << 12) | (IORD_end << 8),
			emi_control + BANK_EMICONFIGDATA(bank, 1));
}

static void set_nand_write_timings(int bank, int cycle_time,
		int IOWR_start, int IOWR_end)
{
	cycle_time = cycle_time / 10;		/* cycles */
	IOWR_start = IOWR_start / 5;		/* phases */
	IOWR_end   = IOWR_end / 5;		/* phases */

	writel((cycle_time << 24) | (IOWR_start << 12) | (IOWR_end << 8),
			emi_control + BANK_EMICONFIGDATA(bank, 2));
}

void emi_config_nand(int bank, struct emi_timing_data *timing_data)
{
	BUG_ON(bank < 0 || bank >= EMI_BANKS);
	BUG_ON(!emi_initialised);

	set_nand_read_timings(bank,
			timing_data->rd_cycle_time,
			timing_data->rd_oee_start,
			timing_data->rd_oee_end,
			timing_data->rd_latchpoint,
			timing_data->busreleasetime,
			timing_data->wait_active_low);

	set_nand_write_timings(bank,
			timing_data->wr_cycle_time,
			timing_data->wr_oee_start,
			timing_data->wr_oee_end);

	/* Disable PC mode */
	emi_config_pcmode(bank, 0);
}
EXPORT_SYMBOL_GPL(emi_config_nand);

void emi_config_pci(struct stm_plat_pci_config *pci_config)
{
	unsigned long v;
	unsigned long req_gnt_mask;
	int i, req;

	BUG_ON(!emi_initialised);

	v = readl(emi_control + EMI_GEN_CFG);
	/* bit 16 is undocumented but enables extra pullups on the bus which
	 * is needed for correction operation if the EMI is accessed
	 * simultaneously with PCI
	 */
	writel(v | (1 << 16), emi_control + EMI_GEN_CFG);

	v = readl(emiss_config + EMISS_CONFIG);

	writel((v & ~EMISS_CONFIG_CLOCK_SELECT_MASK) |
	       EMISS_CONFIG_PCI_CLOCK_MASTER |
	       EMISS_CONFIG_CLOCK_SELECT_PCI |
	       EMISS_CONFIG_PCI_HOST_NOT_DEVICE, emiss_config + EMISS_CONFIG);

	/* It doesn't make any sense to try to use req/gnt3 when the chip has
	 * the req0_to_req3 workaround. Effectively req3 is disconnected, so
	 * it only supports 3 external masters
	 */
	BUG_ON(pci_config->req0_to_req3 &&
	       pci_config->req_gnt[3] != PCI_PIN_UNUSED);

	req_gnt_mask = EMISS_ARBITER_CONFIG_BUS_REQ_ALL_MASKED;
	/* Figure out what req/gnt lines we are using */
	for (i = 0; i < 4; i++) {
		if (pci_config->req_gnt[i] != PCI_PIN_UNUSED) {
			req = ((i == 0) && pci_config->req0_to_req3) ? 3 : i;
			req_gnt_mask &= ~EMISS_ARBITER_CONFIG_MASK_BUS_REQ(req);
		}
	}
	/* The PCI_NOT_EMI bit really controls MPX or PCI. It also must be set
	 * to allow the req0_to_req3 logic to be enabled. GRANT_RETRACTION is
	 * not available on MPX, so should be set for PCI
	 */
	writel(EMISS_ARBITER_CONFIG_PCI_NOT_EMI |
	       EMISS_ARBITER_CONFIG_GRANT_RETRACTION |
	       req_gnt_mask, emiss_config + EMISS_ARBITER_CONFIG);
}
EXPORT_SYMBOL_GPL(emi_config_pci);

void emiss_nandi_select(enum nandi_controllers controller)
{
	unsigned v;

	BUG_ON(!emi_initialised);

	v = readl(emiss_config + EMISS_CONFIG);

	if (controller == STM_NANDI_HAMMING) {
		if (v & EMISS_CONFIG_HAMMING_NOT_BCH)
			return;
		v |= EMISS_CONFIG_HAMMING_NOT_BCH;
	} else {
		if (!(v & EMISS_CONFIG_HAMMING_NOT_BCH))
			return;
		v &= ~EMISS_CONFIG_HAMMING_NOT_BCH;
	}

	writel(v, emiss_config + EMISS_CONFIG);
	readl(emiss_config + EMISS_CONFIG);
}
EXPORT_SYMBOL_GPL(emiss_nandi_select);


#ifdef CONFIG_PM
/*
 * Note on Power Management of EMI device
 * ======================================
 * The EMI is registered twice on different view:
 * 1. as platform_device to acquire the platform specific
 *    capability (via sysconf)
 * 2. as sysdev_device to really manage the suspend/resume
 *    operation on standby and hibernation
 */

/*
 * emi_num_common_cfg = 12 common config	+
 * 			emi_bank_enable(0x280)	+
 *			emi_bank_number(0x860)	+
 *			emiss_config		+
 *			emiss_arbiter_config
 */
#define emi_num_common_cfg	(12 + 4)
#define emi_num_bank		5
#define emi_num_bank_cfg	4

struct emi_pm_bank {
	unsigned long cfg[emi_num_bank_cfg];
	unsigned long base_address;
};

struct emi_pm {
	unsigned long common_cfg[emi_num_common_cfg];
	struct emi_pm_bank bank[emi_num_bank];
};

#ifdef CONFIG_HIBERNATION
static int emi_hibernation(int resuming)
{
	int idx;
	int bank, data;
	static struct emi_pm *emi_saved_data;

	if (resuming) {
		if (emi_saved_data) {
			/* restore the previous common value */
			for (idx = 0; idx < emi_num_common_cfg-4; ++idx)
				writel(emi_saved_data->common_cfg[idx],
				       emi_control+EMI_COMMON_CFG(idx));
			writel(emi_saved_data->common_cfg[12], emi_control
					+ EMI_BANK_ENABLE);
			writel(emi_saved_data->common_cfg[13], emi_control
					+ EMI_BANKNUMBER);
			writel(emi_saved_data->common_cfg[14], emiss_config +
			       EMISS_CONFIG);
			writel(emi_saved_data->common_cfg[15], emiss_config +
			       EMISS_ARBITER_CONFIG);
			/* restore the previous bank values */
			for (bank = 0; bank < emi_num_bank; ++bank) {
			  writel(emi_saved_data->bank[bank].base_address,
				emi_control + BANK_BASEADDRESS(bank));
			  for (data = 0; data < emi_num_bank_cfg; ++data)
				emi_bank_configure(bank, emi_saved_data->bank[bank].cfg);
			}
			kfree(emi_saved_data);
			emi_saved_data = NULL;
		}
		return 0;
	}
	emi_saved_data = kmalloc(sizeof(struct emi_pm), GFP_NOWAIT);
	if (!emi_saved_data) {
		printk(KERN_ERR "Unable to freeze the emi registers\n");
		return -ENOMEM;
	}
	/* save the emi common values */
	for (idx = 0; idx < emi_num_common_cfg-4; ++idx)
		emi_saved_data->common_cfg[idx] =
			readl(emi_control + EMI_COMMON_CFG(idx));
	emi_saved_data->common_cfg[12] = readl(emi_control + EMI_BANK_ENABLE);
	emi_saved_data->common_cfg[13] = readl(emi_control + EMI_BANKNUMBER);
	emi_saved_data->common_cfg[14] = readl(emiss_config + EMISS_CONFIG);
	emi_saved_data->common_cfg[15] =
		readl(emiss_config + EMISS_ARBITER_CONFIG);
	/* save the emi bank value */
	for (bank  = 0; bank < emi_num_bank; ++bank) {
		emi_saved_data->bank[bank].base_address =
			readl(emi_control + BANK_BASEADDRESS(bank));
		for (data = 0; data < emi_num_bank_cfg; ++data)
			emi_saved_data->bank[bank].cfg[data] =
			   readl(emi_control + BANK_EMICONFIGDATA(bank, data));
	}
	return 0;
}

static int emi_freeze(void)
{
	return emi_hibernation(0);
}

static int emi_restore(void)
{
	return emi_hibernation(1);
}
#else
#define emi_freeze	NULL
#define emi_restore	NULL
#endif

static int emi_suspend(void)
{
	stm_device_power(emi_device_state, stm_device_power_off);
	emi_clk_disable();
	return 0;
}

static int emi_resume(void)
{
	emi_clk_enable();
	stm_device_power(emi_device_state, stm_device_power_on);
	return 0;
}


static struct stm_system emi_subsys = {
	.name = "emi",
	.priority = stm_emi_pr,
	.suspend = emi_suspend,
	.resume = emi_resume,
	.freeze = emi_freeze,
	.restore = emi_restore,
};

static int __init emi_subsystem_register(void)
{
	return stm_register_system(&emi_subsys);
}

module_init(emi_subsystem_register);
#endif

static int __init remap_named_resource(struct platform_device *pdev,
				       char *name,
				       void __iomem **io_ptr)
{
	struct resource *res;
	resource_size_t size;
	void __iomem *p;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	if (!res) {
		dev_err(&pdev->dev, "failed to find resource [%s]\n", name);
		return -ENXIO;
	}

	size = resource_size(res);

	if (!devm_request_mem_region(&pdev->dev,
				     res->start, size, name)) {
		dev_err(&pdev->dev, "failed to request memory region "
			"[%s:0x%08x-0x%08x]\n", name, res->start, res->end);
		return -EBUSY;
	}

	p = devm_ioremap_nocache(&pdev->dev, res->start, size);
	if (!p) {
		dev_err(&pdev->dev, "failed to remap memory region "
			"[%s:0x%08x-0x%08x]\n", name, res->start, res->end);
		return -ENOMEM;
	}

	*io_ptr = p;

	return 0;
}

static int __init emi_driver_probe(struct platform_device *pdev)
{
	struct resource	*res;
	int err;

	BUG_ON(emi_initialised);

	emi_device_state = devm_stm_device_init(&pdev->dev,
		(struct stm_device_config *)pdev->dev.platform_data);

	if (!emi_device_state)
		return -EBUSY;

	err = remap_named_resource(pdev, "emiss config", &emiss_config);
	if (err)
		return err;

	err = remap_named_resource(pdev, "emi4 config", &emi_control);
	if (err)
		return err;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "emi memory");
	if (!res)
		return -ENXIO;
	emi_memory_base = res->start;

	emi_clk = clk_get(&pdev->dev, "emi_clk");
	if (!emi_clk)
		pr_warning("emi_clk not found!\n");

	emi_clk_enable();
	emi = pdev; /* to say the EMI is initialised */

	return 0;
}

static struct platform_driver emi_driver = {
	.driver.name = "emi",
	.driver.owner = THIS_MODULE,
	.probe = emi_driver_probe,
};

static int __init stm_emi_driver_init(void)
{
	return platform_driver_register(&emi_driver);
}

postcore_initcall(stm_emi_driver_init);
