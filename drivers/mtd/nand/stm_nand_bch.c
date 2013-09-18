/*
 *  ------------------------------------------------------------------------
 *  stm_nand_bch.c Support for STMicroelectronics NANDi BCH Controller
 *
 *  See Documentation/mtd/stm-nand-bch-notes.txt for additional information.
 *  ------------------------------------------------------------------------
 *
 *  Copyright (c) 2011 STMicroelectronics Limited
 *  Author: Angus Clark <Angus.Clark@st.com>
 *
 *  ------------------------------------------------------------------------
 *  May be copied or modified under the terms of the GNU General Public
 *  License Version 2.0 only.  See linux/COPYING for more information.
 *  ------------------------------------------------------------------------
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/stm/platform.h>
#include <linux/stm/nand.h>
#include <linux/stm/emi.h>
#include <linux/utsrelease.h>

#include "stm_nand_regs.h"

#define NAME	"stm-nand-bch"


/* NANDi BCH Controller properties */
#define NANDI_BCH_SECTOR_SIZE			1024
#define NANDI_BCH_DMA_ALIGNMENT			64
#define NANDI_BCH_MAX_BUF_LIST			8
#define NANDI_BCH_BUF_LIST_SIZE			(4 * NANDI_BCH_MAX_BUF_LIST)

/* BCH ECC sizes */
static int bch_ecc_sizes[] = {
	[BCH_18BIT_ECC] = 32,
	[BCH_30BIT_ECC] = 54,
	[BCH_NO_ECC] = 0,
};

/*
 * Inband Bad Block Table (IBBT)
 */
#define NAND_IBBT_NBLOCKS	4
#define NAND_IBBT_SIGLEN	4
#define NAND_IBBT_PRIMARY	0
#define NAND_IBBT_MIRROR	1
#define NAND_IBBT_SCHEMA	0x10
#define NAND_IBBT_BCH_SCHEMA	0x10

static uint8_t ibbt_sigs[2][NAND_IBBT_SIGLEN] = {
	{'B', 'b', 't', '0'},
	{'1', 't', 'b', 'B'},
};

static char *bbt_strs[] = {
	"primary",
	"mirror",
};

/* IBBT header */
struct nand_ibbt_header {
	uint8_t	signature[4];		/* "Bbt0" or "1tbB" signature */
	uint8_t version;		/* BBT version ("age") */
	uint8_t reserved[3];		/* padding */
	uint8_t schema[4];		/* "base" schema (x4) */
} __attribute__((__packed__));

/* Extend IBBT header with some stm-nand-bch niceties */
struct nand_ibbt_bch_header {
	struct nand_ibbt_header base;
	uint8_t schema[4];		/* "private" schema (x4) */
	uint8_t ecc_size[4];		/* ECC bytes (0, 32, 54) (x4) */
	char	author[64];		/* Arbitrary string for S/W to use */
}; __attribute__((__packed__))


/* Bad Block Table (BBT) */
struct nandi_bbt_info {
	uint32_t	bbt_size;		/* Size of bad-block table */
	uint32_t	bbt_vers[2];		/* Version (Primary/Mirror) */
	uint32_t	bbt_block[2];		/* Block No. (Primary/Mirror) */
	uint8_t		*bbt;			/* Table data */
};


/* Collection of MTD/NAND device information */
struct nandi_info {
	struct mtd_info		mtd;		/* MTD info */
	struct nand_chip	chip;		/* NAND chip info */

	struct nand_ecclayout	ecclayout;	/* MTD ECC layout */
	struct nandi_bbt_info	bbt_info;	/* Bad Block Table */
	int			nr_parts;	/* Number of MTD partitions */
	struct	mtd_partition 	*parts;		/* MTD partitions */
};

/* NANDi Controller (Hamming/BCH) */
struct nandi_controller {
	void __iomem		*base;		/* Controller base*/
	void __iomem		*dma;		/* DMA control base */

						/* IRQ-triggered Completions: */
	struct completion	seq_completed;	/*   SEQ Over */
	struct completion	rbn_completed;	/*   RBn */

	struct device		*dev;

	int			bch_ecc_mode;	/* ECC mode */

	uint32_t		page_shift;	/* Some working variables */
	uint32_t		block_shift;
	uint32_t		blocks_per_device;
	uint32_t		sectors_per_page;

	uint8_t			*buf;		/* Some buffers to use */
	uint8_t			*page_buf;
	uint8_t			*oob_buf;
	uint32_t		*buf_list;

	int			cached_page;	/* page number of page in
						 *  'page_buf' */

	struct nandi_info	info;		/* NAND device info */
};

/* BCH 'program' structure */
struct bch_prog {
	u32	multi_cs_addr[3];
	u32	multi_cs_config;
	u8	seq[16];
	u32	addr;
	u32	extra;
	u8	cmd[4];
	u32	reserved1;
	u32	gen_cfg;
	u32	delay;
	u32	reserved2;
	u32	seq_cfg;
} __attribute__((__packed__, aligned(32)));

/* BCH template programs (modified on-the-fly) */
static struct bch_prog bch_prog_read_page = {
	.cmd = {
		NAND_CMD_READ0,
		NAND_CMD_READSTART,
	},
	.seq = {
		BCH_ECC_SCORE(0),
		BCH_CMD_ADDR,
		BCH_CL_CMD_1,
		BCH_DATA_2_SECTOR,
		BCH_STOP,
	},
	.gen_cfg = (GEN_CFG_DATA_8_NOT_16 |
		    GEN_CFG_EXTRA_ADD_CYCLE |
		    GEN_CFG_LAST_SEQ_NODE),
	.seq_cfg = SEQ_CFG_GO_STOP,
};

static struct bch_prog bch_prog_write_page = {
	.cmd = {
		NAND_CMD_SEQIN,
		NAND_CMD_PAGEPROG,
		NAND_CMD_STATUS,
	},
	.seq = {
		BCH_CMD_ADDR,
		BCH_DATA_4_SECTOR,
		BCH_CL_CMD_1,
		BCH_CL_CMD_2,
		BCH_OP_ERR,
		BCH_STOP,
	},
	.gen_cfg = (GEN_CFG_DATA_8_NOT_16 |
		    GEN_CFG_EXTRA_ADD_CYCLE |
		    GEN_CFG_LAST_SEQ_NODE),
	.seq_cfg = (SEQ_CFG_GO_STOP |
		    SEQ_CFG_DATA_WRITE),
};

static struct bch_prog bch_prog_erase_block = {
	.seq = {
		BCH_CL_CMD_1,
		BCH_AL_EX_0,
		BCH_AL_EX_1,
		BCH_AL_EX_2,
		BCH_CL_CMD_2,
		BCH_CL_CMD_3,
		BCH_OP_ERR,
		BCH_STOP,
	},
	.cmd = {
		NAND_CMD_ERASE1,
		NAND_CMD_ERASE1,
		NAND_CMD_ERASE2,
		NAND_CMD_STATUS,
	},
	.gen_cfg = (GEN_CFG_DATA_8_NOT_16 |
		    GEN_CFG_EXTRA_ADD_CYCLE |
		    GEN_CFG_LAST_SEQ_NODE),
	.seq_cfg = (SEQ_CFG_GO_STOP |
		    SEQ_CFG_ERASE),
};


/* Configure BCH read/write/erase programs */
static void bch_configure_progs(struct nandi_controller *nandi)
{
	uint8_t data_opa = ffs(nandi->sectors_per_page) - 1;
	uint8_t data_instr = BCH_INSTR(BCH_OPC_DATA, data_opa);
	uint32_t gen_cfg_ecc = nandi->bch_ecc_mode << GEN_CFG_ECC_SHIFT;

	/* Set 'DATA' instruction */
	bch_prog_read_page.seq[3] = data_instr;
	bch_prog_write_page.seq[1] = data_instr;

	/* Set ECC mode */
	bch_prog_read_page.gen_cfg |= gen_cfg_ecc;
	bch_prog_write_page.gen_cfg |= gen_cfg_ecc;
	bch_prog_erase_block.gen_cfg |= gen_cfg_ecc;
}

/*
 * NANDi Interrupts (shared by Hamming and BCH controllers)
 */
static irqreturn_t nandi_irq_handler(int irq, void *dev)
{
	struct nandi_controller *nandi = dev;
	unsigned int status;

	status = readl(nandi->base + NANDBCH_INT_STA);

	if (status & NANDBCH_INT_SEQNODESOVER) {
		/* BCH */
		writel(NANDBCH_INT_CLR_SEQNODESOVER,
		       nandi->base + NANDBCH_INT_CLR);
		complete(&nandi->seq_completed);
	}
	if (status & NAND_INT_RBN) {
		/* Hamming */
		writel(NAND_INT_CLR_RBN, nandi->base + NANDHAM_INT_CLR);
		complete(&nandi->rbn_completed);
	}

	return IRQ_HANDLED;
}

static void nandi_enable_interrupts(struct nandi_controller *nandi,
				    uint32_t irqs)
{
	uint32_t val;

	val = readl(nandi->base + NANDBCH_INT_EN);
	val |= irqs;
	writel(val, nandi->base + NANDBCH_INT_EN);
}

static void nandi_disable_interrupts(struct nandi_controller *nandi,
				     uint32_t irqs)
{
	uint32_t val;

	val = readl(nandi->base + NANDBCH_INT_EN);
	val &= ~irqs;
	writel(val, nandi->base + NANDBCH_INT_EN);
}

/*
 * BCH Operations
 */
static inline void bch_load_prog_cpu(struct nandi_controller *nandi,
				     struct bch_prog *prog)
{
	memcpy_toio(nandi->base + NANDBCH_ADDRESS_REG_1, prog, 64);
}

static void bch_wait_seq(struct nandi_controller *nandi)
{
	int ret;

	ret = wait_for_completion_timeout(&nandi->seq_completed, HZ/2);
	if (!ret)
		dev_err(nandi->dev, "BCH Seq timeout\n");
}

static uint8_t bch_erase_block(struct nandi_controller *nandi,
			       loff_t offs)
{
	struct bch_prog *prog = &bch_prog_erase_block;
	uint8_t status;

	dev_dbg(nandi->dev, "%s: offs = 0x%012llx\n", __func__, offs);

	prog->extra = (uint32_t)(offs >> nandi->page_shift);

	emiss_nandi_select(STM_NANDI_BCH);

	nandi_enable_interrupts(nandi, NANDBCH_INT_SEQNODESOVER);
	INIT_COMPLETION(nandi->seq_completed);

	bch_load_prog_cpu(nandi, prog);

	bch_wait_seq(nandi);

	nandi_disable_interrupts(nandi, NANDBCH_INT_SEQNODESOVER);

	status = (uint8_t)(readl(nandi->base +
				 NANDBCH_CHECK_STATUS_REG_A) & 0xff);

	return status;
}

/* Attempt to establish if a page is empty (likely to have been erased), while
 * tolerating a number of bits stuck at, or drifted to, 0.
 */
static int is_page_empty(uint8_t *data, uint32_t page_size, int max_bit_errors)
{
	int e = 0;

	while (page_size--) {
		e += hweight8(~*data++);
		if (e > max_bit_errors)
			return 0;
	}

	return 1;
}

/* Returns the number of ECC errors, or '-1' for uncorrectable error */
static int bch_read_page(struct nandi_controller *nandi,
			 loff_t offs,
			 uint8_t *buf)
{
	struct bch_prog *prog = &bch_prog_read_page;
	uint32_t page_size = nandi->info.mtd.writesize;
	unsigned long list_phys;
	unsigned long buf_phys;
	uint32_t ecc_err;
	int ret = 0;

	dev_dbg(nandi->dev, "%s: offs = 0x%012llx\n", __func__, offs);

	BUG_ON((unsigned long)buf & (NANDI_BCH_DMA_ALIGNMENT - 1));
	BUG_ON(offs & (NANDI_BCH_DMA_ALIGNMENT - 1));

	emiss_nandi_select(STM_NANDI_BCH);

	nandi_enable_interrupts(nandi, NANDBCH_INT_SEQNODESOVER);
	INIT_COMPLETION(nandi->seq_completed);

	/* Reset ECC stats */
	writel(CFG_RESET_ECC_ALL | CFG_ENABLE_AFM,
	       nandi->base + NANDBCH_CONTROLLER_CFG);
	writel(CFG_ENABLE_AFM, nandi->base + NANDBCH_CONTROLLER_CFG);

	prog->addr = (uint32_t)((offs >> (nandi->page_shift - 8)) & 0xffffff00);

	buf_phys = dma_map_single(NULL, buf, page_size, DMA_FROM_DEVICE);

	memset(nandi->buf_list, 0x00, NANDI_BCH_BUF_LIST_SIZE);
	nandi->buf_list[0] = buf_phys | (nandi->sectors_per_page - 1);

	list_phys = dma_map_single(NULL, nandi->buf_list,
				   NANDI_BCH_BUF_LIST_SIZE, DMA_TO_DEVICE);

	writel(list_phys, nandi->base + NANDBCH_BUFFER_LIST_PTR);

	bch_load_prog_cpu(nandi, prog);

	bch_wait_seq(nandi);

	nandi_disable_interrupts(nandi, NANDBCH_INT_SEQNODESOVER);

	dma_unmap_single(NULL, list_phys, NANDI_BCH_BUF_LIST_SIZE,
			 DMA_TO_DEVICE);
	dma_unmap_single(NULL, buf_phys, page_size, DMA_FROM_DEVICE);

	/* Use the maximum per-sector ECC count! */
	ecc_err = readl(nandi->base + NANDBCH_ECC_SCORE_REG_A) & 0xff;
	if (ecc_err == 0xff) {
		/* Do we have a genuine uncorrectable ECC error, or is it just
		 * an erased page?
		 */
		if (is_page_empty(buf, page_size, nandi->sectors_per_page)) {
			dev_dbg(nandi->dev, "%s: detected uncorrectable error, "
				"but looks like an erased page\n", __func__);
			ret = 0;
		} else {
			ret = -1;
		}
	} else {
		ret = (int)ecc_err;
	}

	return ret;
}

/* Returns the status of the NAND device following the write operation */
static uint8_t bch_write_page(struct nandi_controller *nandi,
			      loff_t offs, const uint8_t *buf)
{
	struct bch_prog *prog = &bch_prog_write_page;
	uint32_t page_size = nandi->info.mtd.writesize;
	uint8_t *p = (uint8_t *)buf;
	unsigned long list_phys;
	unsigned long buf_phys;
	uint8_t status;

	dev_dbg(nandi->dev, "%s: offs = 0x%012llx\n", __func__, offs);

	BUG_ON((unsigned int)buf & (NANDI_BCH_DMA_ALIGNMENT - 1));
	BUG_ON(offs & (page_size - 1));

	emiss_nandi_select(STM_NANDI_BCH);

	nandi_enable_interrupts(nandi, NANDBCH_INT_SEQNODESOVER);
	INIT_COMPLETION(nandi->seq_completed);

	prog->addr = (uint32_t)((offs >> (nandi->page_shift - 8)) & 0xffffff00);

	buf_phys = dma_map_single(NULL, p, page_size, DMA_TO_DEVICE);
	memset(nandi->buf_list, 0x00, NANDI_BCH_BUF_LIST_SIZE);
	nandi->buf_list[0] = buf_phys | (nandi->sectors_per_page - 1);

	list_phys = dma_map_single(NULL, nandi->buf_list,
				   NANDI_BCH_BUF_LIST_SIZE, DMA_TO_DEVICE);

	writel(list_phys, nandi->base + NANDBCH_BUFFER_LIST_PTR);

	bch_load_prog_cpu(nandi, prog);

	bch_wait_seq(nandi);

	nandi_disable_interrupts(nandi, NANDBCH_INT_SEQNODESOVER);

	dma_unmap_single(NULL, list_phys, NANDI_BCH_BUF_LIST_SIZE,
			 DMA_TO_DEVICE);
	dma_unmap_single(NULL, buf_phys, page_size, DMA_FROM_DEVICE);

	status = (uint8_t)(readl(nandi->base +
				 NANDBCH_CHECK_STATUS_REG_A) & 0xff);

	return status;
}

/* Helper function for mtd_read, to handle multi-page or non-aligned reads */
static int bch_read(struct nandi_controller *nandi,
		    loff_t from, size_t len,
		    size_t *retlen, u_char *buf)
{
	uint32_t page_size = nandi->info.mtd.writesize;
	struct mtd_ecc_stats stats;
	loff_t page_mask;
	loff_t page_offs;
	int page_num;
	uint32_t col_offs;
	int ecc_errs;
	size_t bytes;
	uint8_t *p;

	int bounce;

	dev_dbg(nandi->dev, "%s: %llu @ 0x%012llx\n", __func__,
		(unsigned long long)len, from);

	stats = nandi->info.mtd.ecc_stats;
	page_mask = (loff_t)page_size - 1;
	col_offs = (uint32_t)(from & page_mask);
	page_offs = from & ~page_mask;
	page_num = (int)(page_offs >> nandi->page_shift);

	if (retlen)
		*retlen = 0;

	while (len > 0) {
		bytes = min((page_size - col_offs), len);

		if ((bytes != page_size) ||
		    ((unsigned int)buf & (NANDI_BCH_DMA_ALIGNMENT - 1)) ||
		    (!virt_addr_valid(buf))) /* vmalloc'd buffer! */
			bounce = 1;
		else
			bounce = 0;

		if (page_num == nandi->cached_page) {
			memcpy(buf, nandi->page_buf + col_offs, bytes);
		} else {
			p = bounce ? nandi->page_buf : buf;

			ecc_errs = bch_read_page(nandi, page_offs, p);
			if (ecc_errs < 0) {
				/* Might be better to break/return here... but
				 * we follow approach in
				 * nand_base.c:do_nand_read_ops()
				 */
				dev_err(nandi->dev, "%s: uncorrectable error "
					"at 0x%012llx\n", __func__, page_offs);
				nandi->info.mtd.ecc_stats.failed++;
			} else {
				if (ecc_errs) {
					dev_info(nandi->dev, "%s: corrected %u "
						 "error(s) at 0x%012llx\n",
						 __func__, ecc_errs, page_offs);
					nandi->info.mtd.ecc_stats.corrected +=
						ecc_errs;
				}
			}

			if (bounce) {
				nandi->cached_page = page_num;
				memcpy(buf, p + col_offs, bytes);
			}
		}

		buf += bytes;
		len -= bytes;

		if (retlen)
			*retlen += bytes;

		/* We are now page-aligned */
		page_offs += page_size;
		page_num++;
		col_offs = 0;
	}

	if (nandi->info.mtd.ecc_stats.failed - stats.failed)
		return -EBADMSG;

	if (nandi->info.mtd.ecc_stats.corrected - stats.corrected)
		return -EUCLEAN;

	return 0;
}


/* Helper function for mtd_write, to handle multi-page and non-aligned writes */
static int bch_write(struct nandi_controller *nandi,
		     loff_t to, size_t len,
		     size_t *retlen, const uint8_t *buf)
{
	uint32_t page_size = nandi->info.mtd.writesize;
	int bounce;
	const uint8_t *p = NULL;

	dev_dbg(nandi->dev, "%s: %llu @ 0x%012llx\n", __func__,
		(unsigned long long)len, to);

	BUG_ON(len & (page_size - 1));
	BUG_ON(to & (page_size - 1));

	if (((unsigned long)buf & (NANDI_BCH_DMA_ALIGNMENT - 1)) ||
	    !virt_addr_valid(buf)) { /* vmalloc'd buffer! */
		bounce = 1;
	} else {
		bounce = 0;
	}

	if (retlen)
		*retlen = 0
			;
	while (len > 0) {

		if (bounce) {
			memcpy(nandi->page_buf, buf, page_size);
			p = nandi->page_buf;
			nandi->cached_page = -1;
		} else {
			p = buf;
		}

		if (bch_write_page(nandi, to, p) & NAND_STATUS_FAIL)
			return -EIO;

		to += page_size;
		buf += page_size;
		len -= page_size;

		if (retlen)
			*retlen += page_size;
	}

	return 0;
}

/*
 * Hamming-FLEX operations
 */
static int flex_wait_rbn(struct nandi_controller *nandi)
{
	int ret;

	ret = wait_for_completion_timeout(&nandi->rbn_completed, HZ/2);
	if (!ret)
		dev_err(nandi->dev, "FLEX RBn timeout\n");

	return ret;
}

static void flex_cmd(struct nandi_controller *nandi, uint8_t cmd)
{
	uint32_t val;

	val = (FLEX_CMD_CSN | FLEX_CMD_BEATS_1 | cmd);

	writel(val, nandi->base + NANDHAM_FLEX_CMD);
}

static void flex_addr(struct nandi_controller *nandi,
		      uint32_t addr, int cycles)
{
	addr &= 0x00ffffff;

	BUG_ON(cycles < 1);
	BUG_ON(cycles > 3);

	addr |= (FLEX_ADDR_CSN | FLEX_ADDR_ADD8_VALID);
	addr |= (cycles & 0x3) << 28;

	writel(addr, nandi->base + NANDHAM_FLEX_ADD);
}

/*
 * Partial implementation of MTD/NAND Interface, based on Hamming-FLEX
 * operation.
 *
 * Allows us to make use of nand_base.c functions where possible
 * (e.g. nand_scan_ident() and friends).
 */
static void flex_command_lp(struct mtd_info *mtd, unsigned int command,
			    int column, int page)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;

	emiss_nandi_select(STM_NANDI_HAMMING);

	switch (command) {
	case NAND_CMD_READOOB:
		/* Emulate NAND_CMD_READOOB */
		column += mtd->writesize;
		command = NAND_CMD_READ0;
		break;
	case NAND_CMD_READ0:
	case NAND_CMD_ERASE1:
	case NAND_CMD_SEQIN:
	case NAND_CMD_RESET:
	case NAND_CMD_PARAM:
		/* Prime RBn wait */
		nandi_enable_interrupts(nandi, NAND_INT_RBN);
		INIT_COMPLETION(nandi->rbn_completed);
		break;
	case NAND_CMD_READID:
	case NAND_CMD_STATUS:
	case NAND_CMD_ERASE2:
		break;
	default:
		/* Catch unexpected commands */
		BUG();
	}

	/*
	 * Command Cycle
	 */
	flex_cmd(nandi, command);

	/*
	 * Address Cycles
	 */
	if (column != -1)
		flex_addr(nandi, column,
			  (command == NAND_CMD_READID) ? 1 : 2);
	if (page != -1)
		flex_addr(nandi, page, (chip->chipsize > (128 << 20)) ? 3 : 2);

	/* Complete 'READ0' command */
	if (command == NAND_CMD_READ0)
		flex_cmd(nandi, NAND_CMD_READSTART);

	/* Wait for RBn, if required.  (Note, other commands may handle wait
	 * elsewhere)
	 */
	if (command == NAND_CMD_RESET ||
	    command == NAND_CMD_READ0 ||
	    command == NAND_CMD_PARAM) {
		flex_wait_rbn(nandi);
		nandi_disable_interrupts(nandi, NAND_INT_RBN);
	}

}

static uint8_t flex_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;

	emiss_nandi_select(STM_NANDI_HAMMING);

	return (uint8_t)(readl(nandi->base + NANDHAM_FLEX_DATA) & 0xff);
}

static int flex_wait_func(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct nandi_controller *nandi = chip->priv;

	emiss_nandi_select(STM_NANDI_HAMMING);

	flex_wait_rbn(nandi);

	flex_cmd(nandi, NAND_CMD_STATUS);

	return (int)(readl(nandi->base + NANDHAM_FLEX_DATA) & 0xff);
}

/* Multi-CS devices not supported */
static void flex_select_chip(struct mtd_info *mtd, int chipnr)
{

}

static void flex_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;
	int aligned;

	emiss_nandi_select(STM_NANDI_HAMMING);

	/* Read bytes until buf is 4-byte aligned */
	while (len && ((unsigned int)buf & 0x3)) {
		*buf++ = (uint8_t)(readl(nandi->base + NANDHAM_FLEX_DATA)
				   & 0xff);
		len--;
	};

	/* Use 'BEATS_4'/readsl */
	if (len > 8) {
		aligned = len & ~0x3;
		writel(FLEX_DATA_CFG_BEATS_4 | FLEX_DATA_CFG_CSN,
		       nandi->base + NANDHAM_FLEX_DATAREAD_CONFIG);

		readsl(nandi->base + NANDHAM_FLEX_DATA, buf, aligned >> 2);

		buf += aligned;
		len -= aligned;

		writel(FLEX_DATA_CFG_BEATS_1 | FLEX_DATA_CFG_CSN,
		       nandi->base + NANDHAM_FLEX_DATAREAD_CONFIG);
	}

	/* Mop up remaining bytes */
	while (len > 0) {
		*buf++ = (uint8_t)(readl(nandi->base + NANDHAM_FLEX_DATA)
				   & 0xff);
		len--;
	}
}

static void flex_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;
	int aligned;

	/* Write bytes until buf is 4-byte aligned */
	while (len && ((unsigned int)buf & 0x3)) {
		writel(*buf++, nandi->base + NANDHAM_FLEX_DATA);
		len--;
	};

	/* USE 'BEATS_4/writesl */
	if (len > 8) {
		aligned = len & ~0x3;
		writel(FLEX_DATA_CFG_BEATS_4 | FLEX_DATA_CFG_CSN,
		       nandi->base + NANDHAM_FLEX_DATAWRITE_CONFIG);
		writesl(nandi->base + NANDHAM_FLEX_DATA, buf, aligned >> 2);
		buf += aligned;
		len -= aligned;
		writel(FLEX_DATA_CFG_BEATS_1 | FLEX_DATA_CFG_CSN,
		       nandi->base + NANDHAM_FLEX_DATAWRITE_CONFIG);
	}

	/* Mop up remaining bytes */
	while (len > 0) {
		writel(*buf++, nandi->base + NANDHAM_FLEX_DATA);
		len--;
	}
}

/* Catch calls to non-supported functions */
static uint16_t flex_read_word_BUG(struct mtd_info *mtd)
{
	BUG();

	return 0;
}

static int flex_block_bad_BUG(struct mtd_info *mtd, loff_t ofs, int getchip)
{
	BUG();

	return 1;
}

static int flex_block_markbad_BUG(struct mtd_info *mtd, loff_t ofs)
{
	BUG();

	return 1;
}

static int flex_verify_buf_BUG(struct mtd_info *mtd, const uint8_t *buf,
			       int len)
{
	BUG();

	return 1;
}

int flex_scan_bbt_BUG(struct mtd_info *mtd)
{
	BUG();

	return 1;
}


/*
 * Hamming-FLEX operations (optimised replacements for nand_base.c versions)
 */
static int flex_check_wp(struct nandi_controller *nandi)
{
	uint8_t status;

	emiss_nandi_select(STM_NANDI_HAMMING);

	flex_cmd(nandi, NAND_CMD_STATUS);

	status = (uint8_t)(readl(nandi->base + NANDHAM_FLEX_DATA) & 0xff);

	return status & NAND_STATUS_WP ? 0 : 1;
}

static int flex_read_raw(struct nandi_controller *nandi,
			 uint32_t page_addr,
			 uint32_t col_addr,
			 uint8_t *buf, uint32_t len)
{
	dev_dbg(nandi->dev, "%s %u bytes at [0x%06x,0x%04x]\n",
		__func__, len, page_addr, col_addr);

	BUG_ON(len & 0x3);
	BUG_ON((unsigned long)buf & 0x3);

	emiss_nandi_select(STM_NANDI_HAMMING);
	nandi_enable_interrupts(nandi, NAND_INT_RBN);
	INIT_COMPLETION(nandi->rbn_completed);

	writel(FLEX_DATA_CFG_BEATS_4 | FLEX_DATA_CFG_CSN,
	       nandi->base + NANDHAM_FLEX_DATAREAD_CONFIG);

	flex_cmd(nandi, NAND_CMD_READ0);
	flex_addr(nandi, col_addr, 2);
	flex_addr(nandi, page_addr, 3);
	flex_cmd(nandi, NAND_CMD_READSTART);

	flex_wait_rbn(nandi);

	readsl(nandi->base + NANDHAM_FLEX_DATA, buf, len/4);

	nandi_disable_interrupts(nandi, NAND_INT_RBN);

	writel(FLEX_DATA_CFG_BEATS_1 | FLEX_DATA_CFG_CSN,
	       nandi->base + NANDHAM_FLEX_DATAREAD_CONFIG);

	return 0;
}

static int flex_write_raw(struct nandi_controller *nandi,
			  uint32_t page_addr,
			  uint32_t col_addr,
			  uint8_t *buf, uint32_t len)
{
	uint8_t status;

	dev_dbg(nandi->dev, "%s %u bytes at [0x%06x,0x%04x]\n",
		__func__, len, page_addr, col_addr);

	BUG_ON(len & 0x3);
	BUG_ON((unsigned long)buf & 0x3);

	emiss_nandi_select(STM_NANDI_HAMMING);
	nandi_enable_interrupts(nandi, NAND_INT_RBN);
	INIT_COMPLETION(nandi->rbn_completed);

	writel(FLEX_DATA_CFG_BEATS_4 | FLEX_DATA_CFG_CSN,
	       nandi->base + NANDHAM_FLEX_DATAWRITE_CONFIG);

	flex_cmd(nandi, NAND_CMD_SEQIN);
	flex_addr(nandi, col_addr, 2);
	flex_addr(nandi, page_addr, 3);

	writesl(nandi->base + NANDHAM_FLEX_DATA, buf, len/4);

	flex_cmd(nandi, NAND_CMD_PAGEPROG);

	flex_wait_rbn(nandi);

	nandi_disable_interrupts(nandi, NAND_INT_RBN);

	writel(FLEX_DATA_CFG_BEATS_1 | FLEX_DATA_CFG_CSN,
	       nandi->base + NANDHAM_FLEX_DATAWRITE_CONFIG);

	flex_cmd(nandi, NAND_CMD_STATUS);

	status = (uint8_t)(readl(nandi->base + NANDHAM_FLEX_DATA) & 0xff);

	return status;
}

/*
 * Bad Block Tables/Bad Block Markers
 */
#define BBT_MARK_BAD_FACTORY	0x0
#define BBT_MARK_BAD_WEAR	0x1
#define BBT_MARK_GOOD		0x3
static void bbt_set_block_mark(uint8_t *bbt, uint32_t block, uint8_t mark)
{
	unsigned int byte = block >> 2;
	unsigned int shift = (block & 0x3) << 1;

	bbt[byte] &= ~(0x3 << shift);
	bbt[byte] |= ((mark & 0x3) << shift);
}

static uint8_t bbt_get_block_mark(uint8_t *bbt, uint32_t block)
{
	unsigned int byte = block >> 2;
	unsigned int shift = (block & 0x3) << 1;

	return (bbt[byte] >> shift) & 0x3;
}

static int bbt_is_block_bad(uint8_t *bbt, uint32_t block)
{
	return bbt_get_block_mark(bbt, block) == BBT_MARK_GOOD ? 0 : 1;
}

/* Scan page for BBM(s), according to specified scheme */
static int nandi_scan_bad_block_markers_page(struct nandi_controller *nandi,
					     uint32_t page,
					     uint32_t bbm_scheme)
{
	struct mtd_info *mtd = &nandi->info.mtd;
	uint8_t *oob_buf = nandi->oob_buf;
	int i;
	int ret = 0;

	/* Read the OOB area */
	flex_read_raw(nandi, page, mtd->writesize, oob_buf, mtd->oobsize);

	/* Check for markers */
	if (bbm_scheme & NAND_BBM_BYTE_OOB_ALL) {
		for (i = 0; i < mtd->oobsize; i++)
			if (oob_buf[i] != 0xff) {
				ret = 1;
				break;
			}
	} else if (((bbm_scheme & NAND_BBM_BYTE_OOB_0) &&
		    (oob_buf[0] != 0xff)) ||
		   ((bbm_scheme & NAND_BBM_BYTE_OOB_5) &&
		    (oob_buf[5] != 0xff))) {
		ret = 1;
	}

#ifdef NANDI_BBM_TOLERATE_BCH_DATA
	/* Check for valid BCH ECC data by performing a page read */
	if (ret == 1) {
		loff_t offs = (loff_t)page << nandi->page_shift;
		uint8_t *page_buf = nandi->page_buf;

		nandi->cached_page = -1;
		if (bch_read_page(nandi, offs, page_buf) >= 0) {
			/* All 0x00s leads to valid BCH ECC, but likely to be
			 * bad-block marker */
			ret = 0;
			for (i = 0; i < mtd->writesize; i++) {
				if (page_buf[i] != 0x00) {
					ret = 1;
					break;
				}
			}

			/* No uncorrectable errors, assume valid BCH ECC data */
			if (ret == 0)
				dev_info(nandi->dev, "BBM @ 0x%012llx: valid "
					 "BCH ECC data, treat as good\n", offs);
		}
	}
#endif

	return ret;
}

/* Scan block for BBM(s), according to specified scheme */
static int nandi_scan_bad_block_markers_block(struct nandi_controller *nandi,
					      uint32_t block,
					      uint32_t bbm_scheme)

{
	struct mtd_info *mtd = &nandi->info.mtd;
	uint32_t pages_per_block = mtd->erasesize >> nandi->page_shift;
	uint32_t page = block << (nandi->block_shift - nandi->page_shift);

	if (nandi_scan_bad_block_markers_page(nandi, page, bbm_scheme))
		return 1;

	if ((bbm_scheme & NAND_BBM_PAGE_1) &&
	    nandi_scan_bad_block_markers_page(nandi, page + 1, bbm_scheme))
		return 1;

	if ((bbm_scheme & NAND_BBM_PAGE_LAST) &&
	    nandi_scan_bad_block_markers_page(nandi, page + pages_per_block - 1,
					      bbm_scheme))
		return 1;

	return 0;
}

/* Scan for BBMs and build memory-resident BBT */
static int nandi_scan_build_bbt(struct nandi_controller *nandi,
				struct nandi_bbt_info *bbt_info,
				uint32_t bbm_scheme)
{
	uint32_t page_size = nandi->info.mtd.writesize;
	uint8_t *bbt = bbt_info->bbt;
	uint32_t block;

	dev_dbg(nandi->dev, "scan device for bad-block markers "
		"[scheme = 0x%02x]\n", bbm_scheme);

	memset(bbt, 0xff, page_size);
	bbt_info->bbt_vers[0] = 0;
	bbt_info->bbt_vers[1] = 0;
	bbt_info->bbt_block[0] = nandi->blocks_per_device - 1;
	bbt_info->bbt_block[1] = nandi->blocks_per_device - 2;

	for (block = 0; block < nandi->blocks_per_device; block++)
		if (nandi_scan_bad_block_markers_block(nandi, block,
						       bbm_scheme))
			bbt_set_block_mark(bbt, block, BBT_MARK_BAD_FACTORY);

	return 0;
}

/* Populate IBBT BCH Header */
static void bch_fill_ibbt_header(struct nandi_controller *nandi,
				 struct nand_ibbt_bch_header *ibbt_header,
				 int bak, uint8_t vers)
{
	const char author[] = "STLinux " UTS_RELEASE " (stm-nand-bch)";

	memcpy(ibbt_header->base.signature, ibbt_sigs[bak], NAND_IBBT_SIGLEN);
	ibbt_header->base.version = vers;
	memset(ibbt_header->base.schema, NAND_IBBT_SCHEMA, 4);

	memset(ibbt_header->schema, NAND_IBBT_SCHEMA, 4);
	memset(ibbt_header->ecc_size, bch_ecc_sizes[nandi->bch_ecc_mode], 4);
	memcpy(ibbt_header->author, author, sizeof(author));
}

/* Write IBBT to Flash */
static int bch_write_bbt_data(struct nandi_controller *nandi,
			      struct nandi_bbt_info *bbt_info,
			      uint32_t block, int bak, uint8_t vers)
{
	uint32_t page_size = nandi->info.mtd.writesize;
	uint32_t block_size = nandi->info.mtd.erasesize;
	struct nand_ibbt_bch_header *ibbt_header =
		(struct nand_ibbt_bch_header *)nandi->page_buf;
	loff_t offs;

	nandi->cached_page = -1;

	/* Write BBT contents to first page of block*/
	offs = (loff_t)block << nandi->block_shift;
	if (bch_write_page(nandi, offs, bbt_info->bbt) & NAND_STATUS_FAIL)
		return 1;

	/* Update IBBT header and write to last page of block */
	memset(ibbt_header, 0xff, nandi->info.mtd.writesize);
	bch_fill_ibbt_header(nandi, ibbt_header, bak, vers);
	offs += block_size - page_size;
	if (bch_write_page(nandi, offs, (uint8_t *)ibbt_header) &
	    NAND_STATUS_FAIL)
		return 1;

	return 0;
}

/* Update Flash-resident BBT: erase/search suitable block, and write table
 * data to Flash */
static int bch_update_bbt(struct nandi_controller *nandi,
			 struct nandi_bbt_info *bbt_info,
			 int bak, uint8_t vers)
{
	loff_t offs;
	uint32_t block;
	uint32_t block_lower;
	uint32_t block_other;

	block_other = bbt_info->bbt_block[(bak+1)%2];
	block_lower = nandi->blocks_per_device - NAND_IBBT_NBLOCKS;

	for (block = bbt_info->bbt_block[bak]; block >= block_lower;  block--) {
		offs = (loff_t)block << nandi->block_shift;

		/* Skip if block used by other table */
		if (block == block_other)
			continue;

		/* Skip if block is marked bad */
		if (bbt_is_block_bad(bbt_info->bbt, block))
			continue;

		/* Erase block, mark bad and skip on failure */
		if (bch_erase_block(nandi, offs) & NAND_STATUS_FAIL) {
			dev_info(nandi->dev, "failed to erase block "
				 "[%u:0x%012llx] while updating BBT\n",
				 block, offs);
			vers++;
			bbt_set_block_mark(bbt_info->bbt, block,
					   BBT_MARK_BAD_WEAR);
			continue;
		}

		/* Write BBT, mark bad and skip on failure */
		if (bch_write_bbt_data(nandi, bbt_info, block, bak, vers)) {
			dev_info(nandi->dev, "failed to write BBT to block"
				 "[%u:0x%012llx]\n", block, offs);
			vers++;
			bbt_set_block_mark(bbt_info->bbt, block,
					   BBT_MARK_BAD_WEAR);
			continue;
		}

		/* Success */
		bbt_info->bbt_block[bak] = block;
		bbt_info->bbt_vers[bak] = vers;
		break;
	}

	/* No space in BBT area */
	if (block < block_lower) {
		dev_err(nandi->dev, "no space left in BBT area\n");
		dev_err(nandi->dev, "failed to update %s BBT\n", bbt_strs[bak]);
		return -ENOSPC;
	}

	dev_info(nandi->dev, "wrote BBT [%s:%u] at 0x%012llx [%u]\n",
		 bbt_strs[bak], vers, offs, block);

	return 0;
}

#define NAND_IBBT_UPDATE_PRIMARY	0x1
#define NAND_IBBT_UPDATE_MIRROR		0x2
#define NAND_IBBT_UPDATE_BOTH		(NAND_IBBT_UPDATE_PRIMARY | \
					 NAND_IBBT_UPDATE_MIRROR)
static char *bbt_update_strs[] = {
	"",
	"primary",
	"mirror",
	"both",
};

/* Update Flash-resident BBT(s), incrementing 'vers' number if required, and
 * ensuring Primary and Mirror are kept in sync */
static int bch_update_bbts(struct nandi_controller *nandi,
			   struct nandi_bbt_info *bbt_info,
			   unsigned int update, uint8_t vers)
{
	int err;

	dev_info(nandi->dev, "updating %s BBT(s)\n", bbt_update_strs[update]);

	do {
		/* Update Primary if specified */
		if (update & NAND_IBBT_UPDATE_PRIMARY) {
			err = bch_update_bbt(nandi, bbt_info, NAND_IBBT_PRIMARY,
					     vers);
			/* Bail out on error (e.g. no space left in BBT area) */
			if (err)
				return err;

			/* If update resulted in a new BBT version
			 * (e.g. Erase/Write fail on BBT block) update version
			 * here, and force update of other table.
			 */
			if (bbt_info->bbt_vers[NAND_IBBT_PRIMARY] != vers) {
				vers = bbt_info->bbt_vers[NAND_IBBT_PRIMARY];
				update = NAND_IBBT_UPDATE_MIRROR;
			}
		}

		/* Update Mirror if specified */
		if (update & NAND_IBBT_UPDATE_MIRROR) {
			err = bch_update_bbt(nandi, bbt_info, NAND_IBBT_MIRROR,
					     vers);
			/* Bail out on error (e.g. no space left in BBT area) */
			if (err)
				return err;

			/* If update resulted in a new BBT version
			 * (e.g. Erase/Write fail on BBT block) update version
			 * here, and force update of other table.
			 */
			if (bbt_info->bbt_vers[NAND_IBBT_MIRROR] != vers) {
				vers = bbt_info->bbt_vers[NAND_IBBT_MIRROR];
				update = NAND_IBBT_UPDATE_PRIMARY;
			}
		}

		/* Continue, until Primary and Mirror versions are in sync */
	} while (bbt_info->bbt_vers[NAND_IBBT_PRIMARY] !=
		 bbt_info->bbt_vers[NAND_IBBT_MIRROR]);

	return 0;
}

/* Scan block for IBBT signature */
static int bch_find_ibbt_sig(struct nandi_controller *nandi,
			     uint32_t block, int *bak, uint8_t *vers,
			     char *author)
{
	struct mtd_info *mtd = &nandi->info.mtd;
	struct nand_ibbt_bch_header *ibbt_header;
	loff_t offs;
	uint8_t *buf = nandi->page_buf;
	int match_sig;
	unsigned int b;
	unsigned int i;

	nandi->cached_page = -1;

	/* Load last page of block */
	offs = 	(loff_t)block << nandi->block_shift;
	offs += mtd->erasesize - mtd->writesize;
	if (bch_read_page(nandi, offs, buf) < 0) {
		dev_info(nandi->dev, "Uncorrectable ECC error while scanning "
			 "BBT signature at block %u [0x%012llx]\n",
			 block, offs);
		return 0;
	}
	ibbt_header = (struct nand_ibbt_bch_header *)buf;

	/* Test IBBT signature */
	match_sig = 0;
	for (b = 0; b < 2 && !match_sig; b++) {
		match_sig = 1;
		for (i = 0; i < NAND_IBBT_SIGLEN; i++) {
			if (ibbt_header->base.signature[i] != ibbt_sigs[b][i]) {
				match_sig = 0;
				break;
			}
		}

	}

	if (!match_sig)
		return 0; /* Failed to match IBBT signature */

	/* Test IBBT schema */
	for (i = 0; i < 4; i++)
		if (ibbt_header->base.schema[i] != NAND_IBBT_SCHEMA)
			return 0;

	/* Test IBBT BCH schema */
	for (i = 0; i < 4; i++)
		if (ibbt_header->schema[i] != NAND_IBBT_BCH_SCHEMA)
			return 0;

	/* We have a match */
	*vers = ibbt_header->base.version;
	*bak = b - 1;
	strncpy(author, ibbt_header->author, 64);

	return 1;
}

/* Search for, and load Flash-resident BBT, updating Primary/Mirror if
 * required
 */
static int bch_load_bbt(struct nandi_controller *nandi,
			struct nandi_bbt_info *bbt_info)
{
	unsigned int update = 0;
	uint32_t block;
	loff_t offs;
	uint8_t vers;
	char author[64];
	int bak;

	dev_dbg(nandi->dev, "looking for Flash-resident BBTs\n");

	bbt_info->bbt_block[0] = 0;
	bbt_info->bbt_block[1] = 0;
	bbt_info->bbt_vers[0] = 0;
	bbt_info->bbt_vers[1] = 0;

	/* Look for IBBT signatures */
	for (block = nandi->blocks_per_device - NAND_IBBT_NBLOCKS;
	     block < nandi->blocks_per_device;
	     block++) {
		offs = (loff_t)block << nandi->block_shift;

		if (bch_find_ibbt_sig(nandi, block, &bak, &vers, author)) {
			dev_dbg(nandi->dev, "found BBT [%s:%u] at 0x%012llx "
				"[%u] (%s)\n", bbt_strs[bak], vers, offs, block,
				author);

			if (bbt_info->bbt_block[bak] == 0 ||
			    ((int8_t)(bbt_info->bbt_vers[bak] - vers)) < 0) {
				bbt_info->bbt_block[bak] = block;
				bbt_info->bbt_vers[bak] = vers;
			}
		}
	}

	/* What have we found? */
	if (bbt_info->bbt_block[0] == 0 && bbt_info->bbt_block[1] == 0) {
		/* no primary, no mirror: return error*/
		return 1;
	} else if (bbt_info->bbt_block[0] == 0) {
		/* no primary: use mirror, update primary */
		bak = 1;
		update = NAND_IBBT_UPDATE_PRIMARY;
		bbt_info->bbt_block[0] = nandi->blocks_per_device - 1;
	} else if (bbt_info->bbt_block[1] == 0) {
		/* no mirror: use primary, update mirror */
		bak = 0;
		update = NAND_IBBT_UPDATE_MIRROR;
		bbt_info->bbt_block[1] = nandi->blocks_per_device - 1;
	} else if (bbt_info->bbt_vers[0] == bbt_info->bbt_vers[1]) {
		/* primary == mirror: use primary, no update required */
		bak = 0;
	} else if ((int8_t)(bbt_info->bbt_vers[1] -
			    bbt_info->bbt_vers[0]) < 0) {
		/* primary > mirror: use primary, update mirror */
		bak = 0;
		update = NAND_IBBT_UPDATE_MIRROR;
	} else {
		/* mirror > primary: use mirror, update primary */
		bak = 1;
		update = NAND_IBBT_UPDATE_PRIMARY;
	}

	vers = bbt_info->bbt_vers[bak];
	block = bbt_info->bbt_block[bak];
	offs = block << nandi->block_shift;
	dev_info(nandi->dev, "using BBT [%s:%u] at 0x%012llx [%u]\n",
		 bbt_strs[bak], vers, offs, block);

	/* Read BBT data */
	if (bch_read_page(nandi, offs, bbt_info->bbt) < 0) {
		dev_err(nandi->dev, "error while reading BBT %s:%u] at "
			"0x%012llx [%u]\n", bbt_strs[bak], vers, offs, block);
		return 1;
	}

	/* Update other BBT if required */
	if (update)
		bch_update_bbts(nandi, bbt_info, update, vers);

	return 0;
}


/*
 * MTD Interface: Standard set of callbacks for MTD functionality
 */
static int mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
		    size_t *retlen, uint8_t *buf)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;
	int ret;

	dev_dbg(nandi->dev, "%s: %llu @ 0x%012llx\n", __func__,
		(unsigned long long)len, from);

	if (retlen)
		*retlen = 0;

	if ((from + len) > mtd->size)
		return -EINVAL;
	if (!len)
		return 0;

	nand_get_device(chip, mtd, FL_READING);

	ret = bch_read(nandi, from, len, retlen, buf);

	nand_release_device(mtd);

	return ret;
}

static int mtd_write(struct mtd_info *mtd, loff_t to, size_t len,
			  size_t *retlen, const uint8_t *buf)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;
	uint32_t page_mask = mtd->writesize - 1;

	int ret;

	dev_dbg(nandi->dev, "%s: %llu @ 0x%012llx\n", __func__,
		(unsigned long long)len, to);

	if (retlen)
		*retlen = 0;

	if ((to + len) > mtd->size)
		return -EINVAL;
	if (!len)
		return 0;
	if ((to & page_mask) || (len & page_mask)) {
		dev_err(nandi->dev, "attempt to write non-page-aligned "
			"data\n");
		return -EINVAL;
	}

	nand_get_device(chip, mtd, FL_WRITING);

	if (flex_check_wp(nandi)) {
		dev_dbg(nandi->dev, "device is write-protected\n");
		return -EIO;
	}

	ret = bch_write(nandi, to, len, retlen, buf);

	nand_release_device(mtd);

	return ret;

}

/* Helper function for mtd_read_oob(): handles multi-page transfers and mapping
 * between BCH sectors and MTD page+OOB data. */
static int flex_do_read_ops(struct nandi_controller *nandi,
				loff_t from,
				struct mtd_oob_ops *ops)
{
	struct mtd_info *mtd = &nandi->info.mtd;
	uint32_t page_addr = from >> nandi->page_shift;
	int pages;
	uint32_t oob_bytes_per_sector = mtd->oobsize / nandi->sectors_per_page;
	uint32_t oob_pad_per_page = mtd->oobsize % nandi->sectors_per_page;
	int ecc_size = bch_ecc_sizes[nandi->bch_ecc_mode];
	uint8_t *o = ops->oobbuf;
	uint8_t *p = ops->datbuf;
	uint8_t *t;
	int s;

	nandi->cached_page = -1;

	pages = ops->datbuf ?
		(ops->len >> nandi->page_shift) :
		(ops->ooblen / mtd->oobsize);

	while (pages) {
		t = nandi->page_buf;

		flex_read_raw(nandi, page_addr, 0, t,
			      mtd->writesize + mtd->oobsize);

		for (s = 0; s < nandi->sectors_per_page; s++) {
			if (p) {
				memcpy(p, t, NANDI_BCH_SECTOR_SIZE);
				p += NANDI_BCH_SECTOR_SIZE;
				ops->retlen += NANDI_BCH_SECTOR_SIZE;
			}
			t += NANDI_BCH_SECTOR_SIZE;

			if (o) {
				memcpy(o, t, ecc_size);
				memset(o + ecc_size, 0xff,
				       oob_bytes_per_sector - ecc_size);

				ops->oobretlen += oob_bytes_per_sector;
				o += oob_bytes_per_sector;
			}
			t += ecc_size;
		}

		if (oob_pad_per_page && o) {
			memset(o, 0xff, oob_pad_per_page);
			o += oob_pad_per_page;
		}

		page_addr++;
		pages--;
	}

	return 0;
}

/* Helper function for mtd_write_oob(): handles multi-page transfers and mapping
 * between BCH sectors and MTD page+OOB data. */
static int flex_do_write_ops(struct nandi_controller *nandi,
			     loff_t to,
			     struct mtd_oob_ops *ops)
{
	struct mtd_info *mtd = &nandi->info.mtd;
	uint32_t page_addr = to >> nandi->page_shift;
	int pages;
	uint32_t oob_bytes_per_sector = mtd->oobsize / nandi->sectors_per_page;
	uint32_t oob_pad_per_page = mtd->oobsize % nandi->sectors_per_page;
	int ecc_size = bch_ecc_sizes[nandi->bch_ecc_mode];
	uint8_t *o = ops->oobbuf;
	uint8_t *p = ops->datbuf;
	uint8_t *t;
	uint8_t status;
	int s;

	nandi->cached_page = -1;

	pages = ops->datbuf ?
		(ops->len >> nandi->page_shift) :
		(ops->ooblen / mtd->oobsize);

	while (pages) {
		t = nandi->page_buf;

		for (s = 0; s < nandi->sectors_per_page; s++) {
			if (p) {
				memcpy(t, p, NANDI_BCH_SECTOR_SIZE);
				p += NANDI_BCH_SECTOR_SIZE;
				ops->retlen += NANDI_BCH_SECTOR_SIZE;
			} else {
				memset(t, 0xff, NANDI_BCH_SECTOR_SIZE);
			}
			t += NANDI_BCH_SECTOR_SIZE;

			if (o) {
				memcpy(t, o, ecc_size);
				ops->oobretlen += oob_bytes_per_sector;
				o += oob_bytes_per_sector;
			} else {
				memset(t, 0xff, ecc_size);
			}
			t += ecc_size;
		}

		if (oob_pad_per_page) {
			memset(t, 0xff, oob_pad_per_page);
			if (o)
				o += oob_pad_per_page;
		}

		status = flex_write_raw(nandi, page_addr, 0, nandi->page_buf,
					mtd->writesize + mtd->oobsize);

		if (status & NAND_STATUS_FAIL)
			return -EIO;

		page_addr++;
		pages--;
	}

	return 0;
}

static char *mtd_oob_mode_strs[] = {"PLACE", "AUTO", "RAW"};
static int mtd_read_oob(struct mtd_info *mtd, loff_t from,
			struct mtd_oob_ops *ops)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;
	uint32_t page_mask = mtd->writesize - 1;
	int ret;

	dev_dbg(nandi->dev, "%s: 0x%012llx [page = %u, oob = %u mode = %s]\n",
		__func__, from,
		(ops->datbuf ? ops->len : 0),
		(ops->oobbuf ? ops->ooblen : 0),
		mtd_oob_mode_strs[ops->mode]);

	ops->oobretlen = 0;
	ops->retlen = 0;

	/* We report OOB as unavailable (i.e. oobavail = 0), therefore nothing
	 * should call this */
	if (ops->mode == MTD_OOB_AUTO)
		return -ENOTSUPP;

	/* Not currently supported by MTD.  Note, will have to fake support if
	 * backporting 'in-band' nand_bbt.c... */
	if (ops->datbuf && ops->mode == MTD_OOB_PLACE)
		return -ENOTSUPP;

	/* Do not allow oob reads with ooboffs */
	if (ops->oobbuf && ops->ooboffs)
		return -ENOTSUPP;

	/* Do not allow reads past end of device */
	if (ops->datbuf && (from + ops->len) > mtd->size) {
		dev_err(nandi->dev, "attempt read beyond end of device\n");
		return -EINVAL;
	}
	if (ops->oobbuf &&
	    (from + mtd->writesize * (ops->ooblen / mtd->oobsize))
	    > mtd->size) {
		dev_err(nandi->dev, "attempt read beyond end of device\n");
		return -EINVAL;
	}

	/* Do not allow non-aligned reads.  Note, might be sensible to support
	 * oob-only or data-only non-aligned reads, but have seen no use-cases
	 * so far. */
	if ((from & page_mask) ||
	    (ops->datbuf && (ops->len & page_mask)) ||
	    (ops->oobbuf && (ops->ooblen % mtd->oobsize))) {
		dev_err(nandi->dev, "attempt to read non-aligned data\n");
		return -ENOTSUPP;
	}

	/* Do not allow inconsistent data and oob lengths */
	if (ops->datbuf && ops->oobbuf &&
	    (ops->len / mtd->writesize != ops->ooblen / mtd->oobsize)) {
		dev_err(nandi->dev, "data length inconsistent with oob "
			"length\n");
		return -EINVAL;
	}

	nand_get_device(chip, mtd, FL_READING);

	ret = flex_do_read_ops(nandi, from, ops);

	nand_release_device(mtd);

	return ret;
}

static int mtd_write_oob(struct mtd_info *mtd, loff_t to,
			  struct mtd_oob_ops *ops)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;
	uint32_t page_mask = mtd->writesize - 1;
	int ret;

	dev_dbg(nandi->dev, "%s: 0x%012llx [page = %u, oob = %u mode = %s]\n",
		__func__, to,
		(ops->datbuf ? ops->len : 0),
		(ops->oobbuf ? ops->ooblen : 0),
		mtd_oob_mode_strs[ops->mode]);

	ops->oobretlen = 0;
	ops->retlen = 0;

	/* We report OOB as unavailable (i.e. oobavail = 0), therefore nothing
	 * should call this */
	if (ops->mode == MTD_OOB_AUTO)
		return -ENOTSUPP;

	/* Not currently supported by MTD.  Note, will have to fake support if
	 * backporting wavefront nand_bbt.c... */
	if (ops->datbuf && ops->mode == MTD_OOB_PLACE)
		return -ENOTSUPP;

	/* Do not allow oob writes with ooboffs */
	if (ops->oobbuf && ops->ooboffs)
		return -ENOTSUPP;

	/* Do not allow writes past end of device */
	if (ops->datbuf && (to + ops->len) > mtd->size) {
		dev_err(nandi->dev, "attempt write beyond end of device\n");
		return -EINVAL;
	}
	if (ops->oobbuf &&
	    (to + mtd->writesize * (ops->ooblen / mtd->oobsize)) > mtd->size) {
		dev_err(nandi->dev, "attempt write beyond end of device\n");
		return -EINVAL;
	}

	/* Do not allow non-aligned writes */
	if ((to & page_mask) ||
	    (ops->datbuf && (ops->len & page_mask)) ||
	    (ops->oobbuf && (ops->ooblen % mtd->oobsize))) {
		dev_err(nandi->dev, "attempt to write non-aligned data\n");
		return -EINVAL;
	}

	/* Do not allow inconsistent data and oob lengths */
	if (ops->datbuf && ops->oobbuf &&
	    (ops->len / mtd->writesize != ops->ooblen / mtd->oobsize)) {
		dev_err(nandi->dev, "data length inconsistent with oob "
			"length\n");
		return -EINVAL;
	}

	nand_get_device(chip, mtd, FL_WRITING);

	if (flex_check_wp(nandi)) {
		dev_dbg(nandi->dev, "device is write-protected\n");
		return -EIO;
	}

	ret = flex_do_write_ops(nandi, to, ops);

	nand_release_device(mtd);

	return ret;
}

static int mtd_block_isbad(struct mtd_info *mtd, loff_t offs)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;

	uint32_t block;

	/* Check for invalid offset */
	if (offs > mtd->size)
		return -EINVAL;

	block = offs >> nandi->block_shift;

	/* Protect blocks reserved for BBTs */
	if (block >= (nandi->blocks_per_device - NAND_IBBT_NBLOCKS))
		return 1;

	return bbt_is_block_bad(nandi->info.bbt_info.bbt, block);
}

static int mtd_block_markbad(struct mtd_info *mtd, loff_t offs)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;

	uint32_t block;
	int ret;

	/* Is block already considered bad? (will also catch invalid offsets) */
	ret = mtd_block_isbad(mtd, offs);
	if (ret < 0)
		return ret;
	if (ret == 1)
		return 0;

	/* Mark bad */
	block = offs >> nandi->block_shift;
	bbt_set_block_mark(nandi->info.bbt_info.bbt, block, BBT_MARK_BAD_WEAR);

	/* Update BBTs, incrementing bbt_vers */
	nand_get_device(chip, mtd, FL_WRITING);
	ret = bch_update_bbts(nandi, &nandi->info.bbt_info,
			      NAND_IBBT_UPDATE_BOTH,
			      nandi->info.bbt_info.bbt_vers[0] + 1);
	nand_release_device(mtd);

	return ret;
}

static int mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct nand_chip *chip = mtd->priv;
	struct nandi_controller *nandi = chip->priv;

	uint32_t block_mask = mtd->erasesize - 1;
	loff_t offs = instr->addr;
	size_t len = instr->len;
	uint8_t status;
	int ret;

	dev_dbg(nandi->dev, "%s: 0x%012llx @ 0x%012llx\n", __func__,
		(unsigned long long)len, offs);

	if (offs & block_mask) {
		dev_err(nandi->dev, "attempt to erase from non-block-aligned "
			"offset\n");
		return -EINVAL;
	}

	if (len & block_mask) {
		dev_err(nandi->dev, "attempt to erase non-block-aligned "
			"length\n");
		return -EINVAL;
	}

	if ((offs + len) > mtd->size) {
		dev_err(nandi->dev, "attempt to erase past end of device\n");
		return -EINVAL;
	}

	nand_get_device(chip, mtd, FL_ERASING);
	instr->fail_addr = MTD_FAIL_ADDR_UNKNOWN;

	if (flex_check_wp(nandi)) {
		dev_dbg(nandi->dev, "device is write-protected\n");
		instr->state = MTD_ERASE_FAILED;
		goto erase_exit;
	}

	instr->state = MTD_ERASING;
	while (len) {
		if (!nand_erasebb && mtd_block_isbad(mtd, offs)) {
			dev_err(nandi->dev, "attempt to erase a bad block "
				"at 0x%012llx\n", offs);
			instr->state = MTD_ERASE_FAILED;
			instr->fail_addr = offs;
			goto erase_exit;
		}

		status = bch_erase_block(nandi, offs);

		if (status & NAND_STATUS_FAIL) {
			dev_err(nandi->dev, "failed to erase block at "
				"0x%012llx\n", offs);
			instr->state = MTD_ERASE_FAILED;
			instr->fail_addr = offs;
			goto erase_exit;
		}

		len -= mtd->erasesize;
		offs += mtd->erasesize;
	}
	instr->state = MTD_ERASE_DONE;

 erase_exit:
	ret = instr->state == MTD_ERASE_DONE ? 0 : -EIO;

	nand_release_device(mtd);

	if (ret == 0)
		mtd_erase_callback(instr);

	return ret;
}

static void nandi_dump_bad_blocks(struct nandi_controller *nandi,
				  uint8_t *bbt)
{
	uint32_t block;
	int bad_count = 0;
	uint8_t mark;

	for (block = 0; block < nandi->blocks_per_device; block++) {
		mark = bbt_get_block_mark(bbt, block);
		if (mark != BBT_MARK_GOOD) {
			pr_info("\t\tBlock 0x%08x [%05u] marked bad [%s]\n",
				block << nandi->block_shift, block,
				(mark == BBT_MARK_BAD_FACTORY) ?
				"Factory" : "Wear");
			bad_count++;
		}
	}
	if (bad_count == 0)
		pr_info("\t\tNo bad blocks listed in BBT\n");
}

#ifdef CONFIG_STM_NAND_BCH_DEBUG
/*
 * Debug code (adds considerable bloat, so enable only when necessary)
 */
static char *nand_cmd_strs[256] = {
	[NAND_CMD_READ0]	= "READ0",
	[NAND_CMD_READ1]	= "READ1",
	[NAND_CMD_RNDOUT]	= "RNDOUT",
	[NAND_CMD_PAGEPROG]	= "PAGEPROG",
	[NAND_CMD_READOOB]	= "READOOB",
	[NAND_CMD_ERASE1]	= "ERASE1",
	[NAND_CMD_ERASE2]	= "ERASE2",
	[NAND_CMD_STATUS]	= "STATUS",
	[NAND_CMD_STATUS_MULTI]	= "STATUS_MULTI",
	[NAND_CMD_SEQIN]	= "SEQIN",
	[NAND_CMD_RNDIN]	= "RNDIN",
	[NAND_CMD_PARAM]	= "PARAM",
	[NAND_CMD_RESET]	= "RESET",
	[NAND_CMD_SETFEATURES]	= "SETFEATURES",
	[NAND_CMD_GETFEATURES]	= "GETFEATURES",
	[NAND_CMD_READSTART]	= "READSTART",
	[NAND_CMD_RNDOUTSTART]	= "RNDOUTSTART",
	[NAND_CMD_CACHEDPROG]	= "CACHEDPROG",
};

int bch_print_instr(char *str, uint8_t instr)
{
	uint8_t opc = instr & 0xf;
	uint8_t opa = (instr >> 4) & 0xf;

	switch (opc) {
	case BCH_OPC_CMD:
		if (opa == 0)
			return sprintf(str, "CMD_ADDR");
		else if (opa < 4)
			return sprintf(str, "CL_CMD_%d", opa);
		else if (opa < 8)
			return sprintf(str, "CL_EX_%d", opa - 4);
		break;
	case BCH_OPC_INC:
		return sprintf(str, "INC_%02d", opa);
		break;
	case BCH_OPC_DEC_JUMP:
		return sprintf(str, "DEC_JUMP_%02d", opa);
		break;
	case BCH_OPC_DATA:
		if (opa < 6)
			return sprintf(str, "DATA_%d_SECTOR", 0x1 << opa);
		break;
	case BCH_OPC_DELAY:
		if (opa < 2)
			return sprintf(str, "DELAY_%d", opa);
		break;
	case BCH_OPC_CHECK:
		if (opa == 0)
			return sprintf(str, "OP_ERR");
		else if (opa == 1)
			return sprintf(str, "CACHE_ERR");
		else if (opa == 2)
			return sprintf(str, "ERROR");
		break;
	case BCH_OPC_ADDR:
		if (opa < 4)
			return sprintf(str, "AL_EX_%d", opa);
		else
			return sprintf(str, "AL_AD_%d", opa - 4);
		break;
	case BCH_OPC_NEXT_CHIP_ON:
		if (opa == 0)
			return sprintf(str, "NEXT_CHIP_ON");
		break;
	case BCH_OPC_DEC_JMP_MCS:
		return sprintf(str, "DEC_JMP_MCS_%02d", opa);
		break;
	case BCH_OPC_ECC_SCORE:
		if (opa < 8)
			return sprintf(str, "ECC_SCORE_%d", opa);
		break;
	case BCH_OPC_STOP:
		if (opa == 0)
			return sprintf(str, "STOP");
		break;
	}

	return sprintf(str, "INVALID");
}

static char *bch_ecc_strs[] = {
	[BCH_18BIT_ECC] = "18-bit ECC ",
	[BCH_30BIT_ECC] = "30-bit ECC ",
	[BCH_NO_ECC]	= "No ECC ",
	[BCH_ECC_RSRV]	= "RSRV "
};

static void nandi_dump_bch_prog(struct nandi_controller *nandi,
				char *name, struct bch_prog *prog)
{
	char instr_str[32];
	int i;

	pr_info("BCH PROG %s:\n", name);
	for (i = 0; i < 3; i++)
		pr_info("\tmult_cs_addr[%d] = 0x%08x\n", i+1,
			prog->multi_cs_addr[i]);
	pr_info("\tmuli_cs_config  = 0x%08x [rep = %d, num = %d, "
		"start = %d, %s]\n",
		prog->multi_cs_config,
		prog->multi_cs_config & 0x3,
		(prog->multi_cs_config >> 8) & 0x3,
		(prog->multi_cs_config >> 10) & 0x3,
		(prog->multi_cs_config >> 12) & 0x1 ?
		"NO_WAIT_RBN" : "WAIT_RBN");
	for (i = 0; i < 16; i++) {
		bch_print_instr(instr_str, prog->seq[i]);
		pr_info("\tseq[%02d]         = 0x%02x [%s]\n",
			i, prog->seq[i], instr_str);
		if (prog->seq[i] == BCH_STOP)
			break;
	}
	pr_info("\taddr            = 0x%08x\n", prog->addr);
	pr_info("\textra           = 0x%08x\n", prog->extra);
	for (i = 0; i < 4; i++)
		pr_info("\tcmd[%02d]         = 0x%02x [%s]\n", i,
			prog->cmd[i],
			nand_cmd_strs[prog->cmd[i]] ?
			nand_cmd_strs[prog->cmd[i]] : "UNKNOWN");

	pr_info("\tgen_cfg         = 0x%08x [%s%s%s%s,%s]\n",
		prog->gen_cfg,
		(prog->gen_cfg >> 16) & 0x1 ? "x8, " : "x16, ",
		(prog->gen_cfg >> 18) & 0x1 ? "+AL, " : "",
		(prog->gen_cfg >> 19) & 0x1 ? "2x8, " : "",
		bch_ecc_strs[(prog->gen_cfg >> GEN_CFG_ECC_SHIFT) & 0x3],
		(prog->gen_cfg >> 22) & 0x1 ? "LAST_SEQ" : "");
	pr_info("\tdelay           = 0x%08x [DELAY_0 = %d, "
		"DELAY_1 = %d]\n",
		prog->delay,
		prog->delay & 0xffff, (prog->delay >> 16) & 0xffff);
	pr_info("\tseq_cfg         = 0x%08x [RPT = %d, ID = %d, "
		"%s%s%s]\n",
		prog->seq_cfg,
		prog->seq_cfg & 0xffff,
		(prog->seq_cfg >> 16) & 0xff,
		(prog->seq_cfg >> 24) & 0x1 ? "WRITE, " : "READ, ",
		(prog->seq_cfg >> 25) & 0x1 ? "ERASE, " : "",
		(prog->seq_cfg >> 26) & 0x1 ? "GO" : "STOP");
	pr_info("\n");
}

static void nandi_dump_bch_progs(struct nandi_controller *nandi)
{
	nandi_dump_bch_prog(nandi, "Read Page", &bch_prog_read_page);
	nandi_dump_bch_prog(nandi, "Write Page", &bch_prog_write_page);
	nandi_dump_bch_prog(nandi, "Erase Block", &bch_prog_erase_block);
}

static void nandi_dump_device(struct nandi_controller *nandi,
			      struct mtd_info *mtd, struct nand_chip *chip)
{
	pr_info("Device Parameters:\n");
	pr_info("\t%-20s: 0x%08x [%u]\n", "Page Size",
		mtd->writesize, mtd->writesize);
	pr_info("\t%-20s: 0x%08x [%u]\n", "OOB Size",
		mtd->oobsize, mtd->oobsize);
	pr_info("\t%-20s: 0x%08x [%u]\n", "Block Size",
		mtd->erasesize, mtd->erasesize);
	pr_info("\t%-20s: 0x%012llx [%uMiB]\n", "Chip Size",
		chip->chipsize, (unsigned int)(chip->chipsize >> 20));
	pr_info("\t%-20s: %u\n", "Planes per Chip",
		chip->planes_per_chip);
	pr_info("\t%-20s: %u\n", "LUNs per Chip",
		chip->luns_per_chip);
	pr_info("\t%-20s: %u\n", "Num Chips", chip->numchips);
	pr_info("\t%-20s: 0x%012llx [%uMiB]\n", "Device Size",
		mtd->size, (unsigned int)(mtd->size >> 20));
	pr_info("\t%-20s: 0x%08x (%s)\n", "Chip Options",
		chip->options & NAND_CHIPOPTIONS_MSK,
		chip->options & NAND_BUSWIDTH_16 ? "x16" : "x8");
	pr_info("\t%-20s: 0x%08x\n", "BBM Scheme", chip->bbm);
	pr_info("\t%-20s: %u (bits per cell = %d)\n", "cellinfo",
		chip->cellinfo,
		((chip->cellinfo >> 2) & 0x3) + 1);
	pr_info("\n");
}

static void nandi_dump_bbt_info(struct nandi_controller *nandi,
				struct nandi_bbt_info *bbt_info)
{
	pr_info("BBT Info:\n");
	pr_info("\t%-20s: 0x%08x [%u]\n", "BBT Size",
		bbt_info->bbt_size, bbt_info->bbt_size);
	pr_info("\t%-20s: V%u at block %u\n", "Primary",
		bbt_info->bbt_vers[0], bbt_info->bbt_block[0]);
	pr_info("\t%-20s: V%u at block %u\n", "Mirror",
		bbt_info->bbt_vers[1], bbt_info->bbt_block[1]);
	pr_info("BBT:\n");
	nandi_dump_bad_blocks(nandi, bbt_info->bbt);
	pr_info("\n");
}

static void nandi_dump_info(struct nandi_controller *nandi)
{
	pr_info("\n");
	pr_info("--------------------------------------------------"
		"----------------------------\n");
	pr_info("%s Debug Info\n", NAME);
	pr_info("--------------------------------------------------"
		"----------------------------\n");
	nandi_dump_device(nandi, &nandi->info.mtd, &nandi->info.chip);
	nandi_dump_bbt_info(nandi, &nandi->info.bbt_info);
	pr_info("Controller data:\n");
	pr_info("\t%-20s: %u\n", "Page Shift", nandi->page_shift);
	pr_info("\t%-20s: %u\n", "Block Shift", nandi->block_shift);
	pr_info("\t%-20s: %u\n", "Blocks per device",
		nandi->blocks_per_device);
	pr_info("\t%-20s: %u\n", "Sectors per Page",
		nandi->sectors_per_page);
	pr_info("\t%-20s: %s\n", "BCH ECC mode",
		bch_ecc_strs[nandi->bch_ecc_mode]);
	pr_info("\n");
	nandi_dump_bch_progs(nandi);
	pr_info("--------------------------------------------------"
		"----------------------------\n\n");
}
#else
static void nandi_dump_info(struct nandi_controller *nandi)
{
	pr_info("%s BBT:\n", NAME);
	nandi_dump_bad_blocks(nandi, nandi->info.bbt_info.bbt);
}

#endif /* CONFIG_STM_NAND_BCH_DEBUG */


/*
 * Initialisation
 */
static int bch_check_compatibility(struct nandi_controller *nandi,
				   struct mtd_info *mtd,
				   struct nand_chip *chip)
{
	int bits_per_cell = ((chip->cellinfo >> 2) & 0x3) + 1;

	if (bits_per_cell > 1)
		dev_warn(nandi->dev, "MLC NAND not fully supported\n");

	if (chip->options & NAND_BUSWIDTH_16) {
		dev_err(nandi->dev, "x16 NAND not supported\n");
		return 1;
	}

	if (nandi->blocks_per_device/4 > mtd->writesize) {
		/* Need to implement multi-page BBT support... */
		dev_err(nandi->dev, "BBT too big to fit in single page\n");
		return 1;
	}

	if (bch_ecc_sizes[nandi->bch_ecc_mode] * nandi->sectors_per_page >
	    mtd->oobsize) {
		dev_err(nandi->dev, "insufficient OOB for selected ECC\n");
		return 1;
	}

	return 0;
}

/* Select strongest ECC scheme compatible with OOB size */
static int bch_set_ecc_auto(struct nandi_controller *nandi,
			    struct mtd_info *mtd,
			    struct nand_chip *chip)
{
	int try_ecc_modes[] = {BCH_30BIT_ECC, BCH_18BIT_ECC, -1};
	int m, ecc_mode;
	int oob_bytes_per_sector = mtd->oobsize / nandi->sectors_per_page;

	for (m = 0; try_ecc_modes[m] >= 0; m++) {
		ecc_mode = try_ecc_modes[m];
		if (oob_bytes_per_sector >= bch_ecc_sizes[ecc_mode]) {
			nandi->bch_ecc_mode = ecc_mode;
			return 0;
		}
	}

	return 1;
}

/* Configure MTD/NAND interface */
static void nandi_set_mtd_defaults(struct nandi_controller *nandi,
				   struct mtd_info *mtd, struct nand_chip *chip)
{
	struct nandi_info *info = &nandi->info;
	int i;

	/* ecclayout */
	info->ecclayout.eccbytes = mtd->oobsize;
	for (i = 0; i < 64; i++)
		info->ecclayout.eccpos[i] = i;
	info->ecclayout.oobfree[0].offset = 0;
	info->ecclayout.oobfree[0].length = 0;
	info->ecclayout.oobavail = 0;

	/* nand_chip */
	chip->controller = &chip->hwcontrol;
	spin_lock_init(&chip->controller->lock);
	init_waitqueue_head(&chip->controller->wq);
	chip->priv = nandi;
	chip->ecc.layout = &info->ecclayout;

	chip->cmdfunc = flex_command_lp;
	chip->read_byte = flex_read_byte;
	chip->select_chip = flex_select_chip;
	chip->waitfunc = flex_wait_func;
	chip->read_buf = flex_read_buf;
	chip->write_buf = flex_write_buf;

	chip->read_word = flex_read_word_BUG;
	chip->block_bad = flex_block_bad_BUG; /**/
	chip->block_markbad = flex_block_markbad_BUG; /**/
	chip->verify_buf = flex_verify_buf_BUG; /**/
	chip->scan_bbt = flex_scan_bbt_BUG; /**/

	/* mtd_info */
	mtd->owner = THIS_MODULE;
	mtd->type = MTD_NANDFLASH;
	mtd->flags = MTD_CAP_NANDFLASH;
	mtd->ecclayout = &info->ecclayout;
	mtd->oobavail = 0;

	mtd->read = mtd_read;
	mtd->write = mtd_write;
	mtd->erase = mtd_erase;
	mtd->read_oob = mtd_read_oob;
	mtd->write_oob = mtd_write_oob;
	mtd->block_isbad = mtd_block_isbad;
	mtd->block_markbad = mtd_block_markbad;

	mtd->point = NULL;
	mtd->unpoint = NULL;
	mtd->lock = NULL;
	mtd->unlock = NULL;

	mtd->sync = nand_sync;
	mtd->suspend = nand_suspend;
	mtd->resume = nand_resume;
}

static void nandi_init_hamming(struct nandi_controller *nandi, int emi_bank)
{
	dev_dbg(nandi->dev, "%s\n", __func__);

	emiss_nandi_select(STM_NANDI_HAMMING);

	/* Reset and disable boot-mode controller */
	writel(BOOT_CFG_RESET, nandi->base + NANDHAM_BOOTBANK_CFG);
	udelay(1);
	writel(0x00000000, nandi->base + NANDHAM_BOOTBANK_CFG);

	/* Reset controller */
	writel(CFG_RESET, nandi->base + NANDHAM_FLEXMODE_CFG);
	udelay(1);
	writel(0x00000000, nandi->base + NANDHAM_FLEXMODE_CFG);

	/* Set EMI Bank */
	writel(0x1 << emi_bank, nandi->base + NANDHAM_FLEX_MUXCTRL);

	/* Enable FLEX mode */
	writel(CFG_ENABLE_FLEX, nandi->base + NANDHAM_FLEXMODE_CFG);

	/* Configure FLEX_DATA_READ/WRITE for 1-byte access */
	writel(FLEX_DATA_CFG_BEATS_1 | FLEX_DATA_CFG_CSN,
	       nandi->base + NANDHAM_FLEX_DATAREAD_CONFIG);
	writel(FLEX_DATA_CFG_BEATS_1 | FLEX_DATA_CFG_CSN,
	       nandi->base + NANDHAM_FLEX_DATAREAD_CONFIG);

	/* RBn interrupt on rising edge */
	writel(NAND_EDGE_CFG_RBN_RISING, nandi->base + NANDHAM_INT_EDGE_CFG);

	/* Enable interrupts */
	nandi_enable_interrupts(nandi, NAND_INT_ENABLE);
}

static void nandi_init_bch(struct nandi_controller *nandi, int emi_bank)
{
	dev_dbg(nandi->dev, "%s\n", __func__);

	/* Initialise BCH Controller */
	emiss_nandi_select(STM_NANDI_BCH);

	/* Reset and disable boot-mode controller */
	writel(BOOT_CFG_RESET, nandi->base + NANDBCH_BOOTBANK_CFG);
	udelay(1);
	writel(0x00000000, nandi->base + NANDBCH_BOOTBANK_CFG);

	/* Reset AFM controller */
	writel(CFG_RESET, nandi->base + NANDBCH_CONTROLLER_CFG);
	udelay(1);
	writel(0x00000000, nandi->base + NANDBCH_CONTROLLER_CFG);

	/* Set EMI Bank */
	writel(0x1 << emi_bank, nandi->base + NANDBCH_FLEX_MUXCTRL);

	/* Reset ECC stats */
	writel(0x7f0, nandi->base + NANDBCH_CONTROLLER_CFG);
	udelay(1);

	/* Enable AFM */
	writel(CFG_ENABLE_AFM, nandi->base + NANDBCH_CONTROLLER_CFG);

	/* Timing parameters */
	/* Values from validation found not to work on some parts.  Awaiting
	 * clarification.  Use reset values for the time being.
	 *
	 * writel(0x14000205, nandi->base + NANDBCH_CTL_TIMING);
	 * writel(0x00020304, nandi->base + NANDBCH_WEN_TIMING);
	 * writel(0x060b0304, nandi->base + NANDBCH_REN_TIMING);
	 */

	/* Configure Read DMA Plugs (values supplied by Validation)*/
	writel(0x00000005, nandi->dma + EMISS_NAND_RD_DMA_PAGE_SIZE);
	writel(0x00000005, nandi->dma + EMISS_NAND_RD_DMA_MAX_OPCODE_SIZE);
	writel(0x00000002, nandi->dma + EMISS_NAND_RD_DMA_MIN_OPCODE_SIZE);
	writel(0x00000001, nandi->dma + EMISS_NAND_RD_DMA_MAX_CHUNK_SIZE);
	writel(0x00000000, nandi->dma + EMISS_NAND_RD_DMA_MAX_MESSAGE_SIZE);

	/* Configure Write DMA Plugs (values supplied by Validation) */
	writel(0x00000005, nandi->dma + EMISS_NAND_WR_DMA_PAGE_SIZE);
	writel(0x00000005, nandi->dma + EMISS_NAND_WR_DMA_MAX_OPCODE_SIZE);
	writel(0x00000002, nandi->dma + EMISS_NAND_WR_DMA_MIN_OPCODE_SIZE);
	writel(0x00000001, nandi->dma + EMISS_NAND_WR_DMA_MAX_CHUNK_SIZE);
	writel(0x00000000, nandi->dma + EMISS_NAND_WR_DMA_MAX_MESSAGE_SIZE);

	nandi_enable_interrupts(nandi, NAND_INT_ENABLE);
}

static int __devinit remap_named_resource(struct platform_device *pdev,
					  char *name,
					  void __iomem **io_ptr)
{
	struct resource *res;
	resource_size_t size;
	void __iomem *p;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	if (!res)
		return -ENXIO;

	size = resource_size(res);

	if (!devm_request_mem_region(&pdev->dev,
				     res->start, size, name))
		return -EBUSY;

	p = devm_ioremap_nocache(&pdev->dev, res->start, size);
	if (!p)
		return -ENOMEM;

	*io_ptr = p;

	return 0;
}

static struct nandi_controller * __init
nandi_init_resources(struct platform_device *pdev)
{
	struct nandi_controller *nandi;
	int irq;

	int err = 0;

	nandi = devm_kzalloc(&pdev->dev, sizeof(struct nandi_controller),
			     GFP_KERNEL);
	if (!nandi) {
		dev_err(&pdev->dev, "failed to allocate NANDi controller "
			"data\n");
		return ERR_PTR(-ENOMEM);
	}

	nandi->dev = &pdev->dev;

	err = remap_named_resource(pdev, "nand_mem", &nandi->base);
	if (err)
		return ERR_PTR(err);

	err = remap_named_resource(pdev, "nand_dma", &nandi->dma);
	if (err)
		return ERR_PTR(err);

	irq = platform_get_irq_byname(pdev, "nand_irq");
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to find IRQ resource\n");
		return ERR_PTR(irq);
	}

	err = devm_request_irq(&pdev->dev, irq, nandi_irq_handler,
			       IRQF_DISABLED, dev_name(&pdev->dev), nandi);
	if (err) {
		dev_err(&pdev->dev, "irq request failed\n");
		return ERR_PTR(err);
	}

	platform_set_drvdata(pdev, nandi);

	return nandi;
}

static void __devexit nandi_exit_controller(struct nandi_controller *nandi)
{

}

static void __devinit nandi_init_controller(struct nandi_controller *nandi,
					    int emi_bank)
{
	nandi_init_bch(nandi, emi_bank);
	nandi_init_hamming(nandi, emi_bank);
}


static int __devinit stm_nand_bch_probe(struct platform_device *pdev)
{
	struct stm_plat_nand_bch_data *pdata = pdev->dev.platform_data;
	struct stm_nand_bank_data *bank;

	struct nandi_controller *nandi;
	struct nandi_info *info;
	struct nandi_bbt_info *bbt_info;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	uint32_t buf_size;
	uint32_t bbt_buf_size;
	int i;
	int err;

	nandi = nandi_init_resources(pdev);
	if (IS_ERR(nandi)) {
		dev_err(&pdev->dev, "failed to initialise NANDi resources\n");
		return PTR_ERR(nandi);
	}

	init_completion(&nandi->seq_completed);
	init_completion(&nandi->rbn_completed);

	bank = pdata->bank;
	nandi_init_controller(nandi, bank->csn);

	info = &nandi->info;
	chip = &info->chip;
	bbt_info = &info->bbt_info;
	mtd = &info->mtd;
	mtd->priv = chip;
	mtd->name = dev_name(&pdev->dev);

	nandi_set_mtd_defaults(nandi, mtd, chip);

	if (nand_scan_ident(mtd, 1) != 0)
		return -ENODEV;

	/* Derive some working variables */
	nandi->sectors_per_page = mtd->writesize / NANDI_BCH_SECTOR_SIZE;
	nandi->blocks_per_device = mtd->size >> chip->phys_erase_shift;
	nandi->page_shift = chip->page_shift;
	nandi->block_shift = chip->phys_erase_shift;

	/* Set ECC mode */
	switch (pdata->bch_ecc_cfg) {
	case BCH_ECC_CFG_AUTO:
		if (bch_set_ecc_auto(nandi, mtd, chip) != 0) {
			dev_err(nandi->dev, "insufficient OOB for BCH ECC\n");
			return -EINVAL;
		}
		break;
	case BCH_ECC_CFG_NOECC:
		nandi->bch_ecc_mode = BCH_NO_ECC;
		break;
	case BCH_ECC_CFG_18BIT:
		nandi->bch_ecc_mode = BCH_18BIT_ECC;
		break;
	case BCH_ECC_CFG_30BIT:
		nandi->bch_ecc_mode = BCH_30BIT_ECC;
		break;
	}

	/* Check compatibility */
	if (bch_check_compatibility(nandi, mtd, chip) != 0) {
		dev_err(nandi->dev, "NAND device incompatible with NANDi/BCH "
			"Controller\n");
		return -EINVAL;
	}

	/* Tune BCH programs according to device found and ECC mode */
	bch_configure_progs(nandi);

	/*
	 * Initialise working buffers, accomodating DMA alignment constraints:
	 */

	/*	- Page and OOB */
	buf_size = mtd->writesize + mtd->oobsize + NANDI_BCH_DMA_ALIGNMENT;

	/*	- BBT data (page-size aligned) */
	bbt_info->bbt_size = nandi->blocks_per_device >> 2; /* 2 bits/block */
	bbt_buf_size = ALIGN(bbt_info->bbt_size, mtd->writesize);
	buf_size += bbt_buf_size + NANDI_BCH_DMA_ALIGNMENT;

	/*	- BCH BUF list */
	buf_size += NANDI_BCH_BUF_LIST_SIZE + NANDI_BCH_DMA_ALIGNMENT;

	/* Allocate bufffer */
	nandi->buf = devm_kzalloc(&pdev->dev, buf_size, GFP_KERNEL);
	if (!nandi->buf) {
		dev_err(nandi->dev, "failed to allocate working buffers\n");
		return -ENOMEM;
	}

	/* Set/Align buffer pointers */
	nandi->page_buf = PTR_ALIGN(nandi->buf, NANDI_BCH_DMA_ALIGNMENT);
	nandi->oob_buf = nandi->page_buf + mtd->writesize;
	bbt_info->bbt = PTR_ALIGN(nandi->oob_buf + mtd->oobsize,
				  NANDI_BCH_DMA_ALIGNMENT);
	nandi->buf_list = (uint32_t *) PTR_ALIGN(bbt_info->bbt + bbt_buf_size,
						 NANDI_BCH_DMA_ALIGNMENT);
	nandi->cached_page = -1;

	/* Load Flash-resident BBT */
	err = bch_load_bbt(nandi, bbt_info);

	if (err) {
		dev_err(nandi->dev, "failed to find BBTs: "
			"scan device for bad-block markers\n");
		/* scan, build, and write BBT */
		nandi_scan_build_bbt(nandi, bbt_info, chip->bbm);
		if (bch_update_bbts(nandi, bbt_info, NAND_IBBT_UPDATE_BOTH,
				    bbt_info->bbt_vers[0] + 1) != 0)
			return -ENXIO;
	}

	nandi_dump_info(nandi);

	/* Add partitions */
	if (mtd_has_partitions()) {
		if (mtd_has_cmdlinepart()) {
			static const char *part_probes[]
				= { "cmdlinepart", NULL, };
			info->nr_parts = parse_mtd_partitions(mtd,
							      part_probes,
							      &info->parts,
							      0);
		}

		if (info->nr_parts <= 0 && bank->partitions) {
			info->parts = bank->partitions;
			info->nr_parts = bank->nr_partitions;
		}

		if (info->nr_parts > 0) {
			for (i = 0; i < info->nr_parts; i++) {
				dev_dbg(nandi->dev, "partitions[%d] = "
					"{.name = %s, .offset = 0x%llx, "
					".size = 0x%llx (%lldKiB) }\n",
					i, info->parts[i].name,
					(long long)info->parts[i].offset,
					(long long)info->parts[i].size,
					(long long)(info->parts[i].size >> 10));
			}

			if (add_mtd_partitions(mtd, info->parts,
					       info->nr_parts)) {
				return -ENODEV;
			}

			return 0;
		}

	} else if (bank->nr_partitions) {
		dev_info(nandi->dev, " ignoring %d default partitions on %s\n",
			 bank->nr_partitions, "NAND");
	}

	if (add_mtd_device(mtd))
		return -ENODEV;

	return 0;
}

static int __devexit stm_nand_bch_remove(struct platform_device *pdev)
{
	struct stm_plat_nand_bch_data *pdata = pdev->dev.platform_data;
	struct stm_nand_bank_data *bank = pdata->bank;
	struct nandi_controller *nandi = platform_get_drvdata(pdev);
	struct nandi_info *info = &nandi->info;

	if (mtd_has_partitions() && info->parts) {
		del_mtd_partitions(&info->mtd);
		if (info->parts != bank->partitions)
			kfree(info->parts);
	} else {
		del_mtd_device(&info->mtd);
	}

	nandi_exit_controller(nandi);

	return 0;
}

static struct platform_driver stm_nand_bch_driver = {
	.probe		= stm_nand_bch_probe,
	.remove		= stm_nand_bch_remove,
	.driver		= {
		.name	= NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init stm_nand_bch_init(void)
{
	return platform_driver_register(&stm_nand_bch_driver);
}

static void __exit stm_nand_bch_exit(void)
{
	platform_driver_unregister(&stm_nand_bch_driver);
}

module_init(stm_nand_bch_init);
module_exit(stm_nand_bch_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Angus Clark");
MODULE_DESCRIPTION("STM NAND BCH driver");
