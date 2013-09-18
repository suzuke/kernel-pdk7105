/*
 * stm_spi_fsm.c	Support for STM SPI Serial Flash Controller
 *
 * Author: Angus Clark <angus.clark@st.com>
 *
 * Copyright (C) 2010-2011 STMicroelectronics Limited
 *
 * JEDEC probe based on drivers/mtd/devices/m25p80.c
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/stm/platform.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include "stm_spi_fsm.h"

#define NAME		"stm-spi-fsm"

/* #define DEBUG_SPI_FSM_SEQS */

#define FLASH_PROBE_FREQ	10		/* Probe freq. (MHz) */
#define FLASH_PAGESIZE		256
#define FLASH_MAX_BUSY_WAIT	(10 * HZ)	/* Maximum erase time */


/*
 * Flags to tweak operation of default read/write/erase routines
 */
#define CFG_READ_TOGGLE32BITADDR		0x00000001
#define CFG_WRITE_TOGGLE32BITADDR		0x00000002
#define CFG_WRITE_EX32BITADDR_DELAY		0x00000004
#define CFG_ERASESEC_TOGGLE32BITADDR		0x00000008


/*
 * SPI FSM Controller data
 */
struct stm_spi_fsm {
	struct mtd_info		mtd;
	struct device		*dev;
	struct resource		*region;
	struct stm_pad_state	*pad_state;
	struct stm_spifsm_caps	capabilities;
	uint32_t		configuration;
	uint32_t		fifo_dir_delay;

	void __iomem		*base;
	struct mutex		lock;
	unsigned		partitioned;
	uint8_t	page_buf[FLASH_PAGESIZE]__attribute__((aligned(4)));
};

/*
 * SPI FLASH
 */

/* Commands */
#define FLASH_CMD_WREN		0x06
#define FLASH_CMD_WRDI		0x04
#define FLASH_CMD_RDID		0x9f
#define FLASH_CMD_RDSR		0x05
#define FLASH_CMD_RDSR2		0x35
#define FLASH_CMD_WRSR		0x01
#define FLASH_CMD_SE_4K		0x20
#define FLASH_CMD_SE_32K	0x52
#define FLASH_CMD_SE		0xd8
#define FLASH_CMD_CHIPERASE	0xc7
#define FLASH_CMD_WRVCR		0x81
#define FLASH_CMD_RDVCR		0x85

#define FLASH_CMD_READ		0x03	/* READ */
#define FLASH_CMD_READ_FAST	0x0b	/* FAST READ */
#define FLASH_CMD_READ_1_1_2	0x3b	/* DUAL OUTPUT READ */
#define FLASH_CMD_READ_1_2_2	0xbb	/* DUAL I/O READ */
#define FLASH_CMD_READ_1_1_4	0x6b	/* QUAD OUTPUT READ */
#define FLASH_CMD_READ_1_4_4	0xeb	/* QUAD I/O READ */

#define FLASH_CMD_WRITE		0x02	/* PAGE PROGRAM */
#define FLASH_CMD_WRITE_1_1_2	0xa2	/* DUAL INPUT PROGRAM */
#define FLASH_CMD_WRITE_1_2_2	0xd2	/* DUAL INPUT EXT PROGRAM */
#define FLASH_CMD_WRITE_1_1_4	0x32	/* QUAD INPUT PROGRAM */
#define FLASH_CMD_WRITE_1_4_4	0x12	/* QUAD INPUT EXT PROGRAM */

#define FLASH_CMD_EN4B_ADDR	0xb7	/* Enter 4-byte address mode */
#define FLASH_CMD_EX4B_ADDR	0xe9	/* Exit 4-byte address mode */

/* N25Q Commands */
/*	- READ with 32-bit addressing */
#define N25Q_CMD_READ4			0x13
#define N25Q_CMD_READ4_FAST		0x0c
#define N25Q_CMD_READ4_1_1_2		0x3c
#define N25Q_CMD_READ4_1_2_2		0xbc
#define N25Q_CMD_READ4_1_1_4		0x6c
#define N25Q_CMD_READ4_1_4_4		0xec
/*	- READ/CLEAR Flags Status register */
#define N25Q_CMD_RFSR			0x70
#define N25Q_CMD_CLFSR			0x50

/* MX25 Commands */
/*	- Read Security Register (home of '4BYTE' status bit!) */
#define MX25_CMD_RDSCUR			0x2B


/* Status register */
#define FLASH_STATUS_BUSY	0x01
#define FLASH_STATUS_WEL	0x02
#define FLASH_STATUS_BP0	0x04
#define FLASH_STATUS_BP1	0x08
#define FLASH_STATUS_BP2	0x10
#define FLASH_STATUS_TB		0x20
#define FLASH_STATUS_SP		0x40
#define FLASH_STATUS_SRWP0	0x80

/* Capabilities */
#define FLASH_CAPS_SINGLE	0x000000ff
#define FLASH_CAPS_READ_WRITE	0x00000001
#define FLASH_CAPS_READ_FAST	0x00000002
#define FLASH_CAPS_SE_4K	0x00000004
#define FLASH_CAPS_SE_32K	0x00000008
#define FLASH_CAPS_CE		0x00000010
#define FLASH_CAPS_32BITADDR	0x00000020
#define FLASH_CAPS_RESET	0x00000040

#define FLASH_CAPS_DUAL		0x0000ff00
#define FLASH_CAPS_READ_1_1_2	0x00000100
#define FLASH_CAPS_READ_1_2_2	0x00000200
#define FLASH_CAPS_READ_2_2_2	0x00000400
#define FLASH_CAPS_WRITE_1_1_2	0x00001000
#define FLASH_CAPS_WRITE_1_2_2	0x00002000
#define FLASH_CAPS_WRITE_2_2_2	0x00004000

#define FLASH_CAPS_QUAD		0x00ff0000
#define FLASH_CAPS_READ_1_1_4	0x00010000
#define FLASH_CAPS_READ_1_4_4	0x00020000
#define FLASH_CAPS_READ_4_4_4	0x00040000
#define FLASH_CAPS_WRITE_1_1_4	0x00100000
#define FLASH_CAPS_WRITE_1_4_4	0x00200000
#define FLASH_CAPS_WRITE_4_4_4	0x00400000

/*
 * FSM template sequences (Note, various fields are modified on-the-fly)
 */
struct fsm_seq {
	uint32_t data_size;
	uint32_t addr1;
	uint32_t addr2;
	uint32_t addr_cfg;
	uint32_t seq_opc[5];
	uint32_t mode;
	uint32_t dummy;
	uint32_t status;
	uint8_t  seq[16];
	uint32_t seq_cfg;
} __attribute__((__packed__, aligned(4)));
#define FSM_SEQ_SIZE			sizeof(struct fsm_seq)

static struct fsm_seq fsm_seq_dummy = {
	.data_size = TRANSFER_SIZE(0),
	.seq = {
		FSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct fsm_seq fsm_seq_read_jedec = {
	.data_size = TRANSFER_SIZE(8),
	.seq_opc[0] = (SEQ_OPC_PADS_1 |
		       SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(FLASH_CMD_RDID)),
	.seq = {
		FSM_INST_CMD1,
		FSM_INST_DATA_READ,
		FSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct fsm_seq fsm_seq_read_status_fifo = {
	.data_size = TRANSFER_SIZE(4),
	.seq_opc[0] = (SEQ_OPC_PADS_1 |
		       SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(FLASH_CMD_RDSR)),
	.seq = {
		FSM_INST_CMD1,
		FSM_INST_DATA_READ,
		FSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct fsm_seq fsm_seq_write_status = {
	.seq_opc[0] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(FLASH_CMD_WREN) | SEQ_OPC_CSDEASSERT),
	.seq_opc[1] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(FLASH_CMD_WRSR)),
	.seq = {
		FSM_INST_CMD1,
		FSM_INST_CMD2,
		FSM_INST_STA_WR1,
		FSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct fsm_seq fsm_seq_wrvcr = {
	.seq_opc[0] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(FLASH_CMD_WREN) | SEQ_OPC_CSDEASSERT),
	.seq_opc[1] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		       SEQ_OPC_OPCODE(FLASH_CMD_WRVCR)),
	.seq = {
		FSM_INST_CMD1,
		FSM_INST_CMD2,
		FSM_INST_STA_WR1,
		FSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct fsm_seq fsm_seq_erase_sector = {
	/* 'addr_cfg' configured during initialisation */
	.seq_opc = {
		(SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		 SEQ_OPC_OPCODE(FLASH_CMD_WREN) | SEQ_OPC_CSDEASSERT),

		(SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		 SEQ_OPC_OPCODE(FLASH_CMD_SE)),
	},
	.seq = {
		FSM_INST_CMD1,
		FSM_INST_CMD2,
		FSM_INST_ADD1,
		FSM_INST_ADD2,
		FSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

static struct fsm_seq fsm_seq_erase_chip = {
	.seq_opc = {
		(SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		 SEQ_OPC_OPCODE(FLASH_CMD_WREN) | SEQ_OPC_CSDEASSERT),

		(SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
		 SEQ_OPC_OPCODE(FLASH_CMD_CHIPERASE)),
	},
	.seq = {
		FSM_INST_CMD1,
		FSM_INST_CMD2,
		FSM_INST_STOP,
	},
	.seq_cfg = (SEQ_CFG_PADS_1 |
		    SEQ_CFG_READNOTWRITE |
		    SEQ_CFG_CSDEASSERT |
		    SEQ_CFG_STARTSEQ),
};

/* Configure 'addr_cfg' according to addressing mode */
static int configure_erasesec_seq(struct fsm_seq *seq, int use_32bit_addr)
{
	int addr1_cycles = use_32bit_addr ? 16 : 8;

	seq->addr_cfg = (ADR_CFG_CYCLES_ADD1(addr1_cycles) |
			 ADR_CFG_PADS_1_ADD1 |
			 ADR_CFG_CYCLES_ADD2(16) |
			 ADR_CFG_PADS_1_ADD2 |
			 ADR_CFG_CSDEASSERT_ADD2);
	return 0;
}

/* The following FSM sequences are configured during initialisation according to
 * the platform and device capabilities.
 */
static struct fsm_seq fsm_seq_read;
static struct fsm_seq fsm_seq_write;
static struct fsm_seq fsm_seq_en32bitaddr;

/*
 * Debug code for examining FSM sequences
 */
#ifdef DEBUG_SPI_FSM_SEQS
char *flash_cmd_strs[256] = {
	[FLASH_CMD_WREN]	= "WREN",
	[FLASH_CMD_WRDI]	= "WRDI",
	[FLASH_CMD_RDID]	= "RDID",
	[FLASH_CMD_RDSR]	= "RDSR",
	[FLASH_CMD_RDSR2]	= "RDSR2",
	[FLASH_CMD_WRSR]	= "WRSR",
	[FLASH_CMD_SE]		= "SE",
	[FLASH_CMD_SE_4K]	= "SE_4K",
	[FLASH_CMD_SE_32K]	= "SE_32K",
	[FLASH_CMD_CHIPERASE]	= "CHIPERASE",
	[FLASH_CMD_WRVCR]	= "WRVCR",
	[FLASH_CMD_READ]	= "READ",
	[FLASH_CMD_READ_FAST]	= "READ_FAST",
	[FLASH_CMD_READ_1_1_2]	= "READ_1_1_2",
	[FLASH_CMD_READ_1_2_2]	= "READ_1_2_2",
	[FLASH_CMD_READ_1_1_4]	= "READ_1_1_4",
	[FLASH_CMD_READ_1_4_4]	= "READ_1_4_4",
	[FLASH_CMD_WRITE]	= "WRITE",
	[FLASH_CMD_WRITE_1_1_2]	= "WRITE_1_1_2",
	[FLASH_CMD_WRITE_1_2_2]	= "WRITE_1_2_2",
	[FLASH_CMD_WRITE_1_1_4]	= "WRITE_1_1_4",
	[FLASH_CMD_WRITE_1_4_4] = "WRITE_1_4_4",
	[FLASH_CMD_EN4B_ADDR]	= "EN4B_ADDR",
	[FLASH_CMD_EX4B_ADDR]	= "EX4B_ADDR",
	[N25Q_CMD_READ4]	= "READ4",
	[N25Q_CMD_READ4_FAST]	= "READ4_FAST",
	[N25Q_CMD_READ4_1_1_2]	= "READ4_1_1_2",
	[N25Q_CMD_READ4_1_2_2]	= "READ4_1_2_2",
	[N25Q_CMD_READ4_1_1_4]	= "READ4_1_1_4",
	[N25Q_CMD_READ4_1_4_4]	= "READ4_1_4_4",
};

char *fsm_inst_strs[256] = {
	[FSM_INST_CMD1]		= "CMD1",
	[FSM_INST_CMD2]		= "CMD2",
	[FSM_INST_CMD3]		= "CMD3",
	[FSM_INST_CMD4]		= "CMD4",
	[FSM_INST_CMD5]		= "CMD5",
	[FSM_INST_ADD1]		= "ADD1",
	[FSM_INST_ADD2]		= "ADD2",
	[FSM_INST_DATA_WRITE]	= "D_WR",
	[FSM_INST_DATA_READ]	= "D_RD",
	[FSM_INST_STA_RD1]	= "S_RW1",
	[FSM_INST_STA_RD2]	= "S_R2",
	[FSM_INST_STA_WR1_2]	= "S_W12",
	[FSM_INST_MODE]		= "MODE",
	[FSM_INST_DUMMY]	= "DUMMY",
	[FSM_INST_WAIT]		= "WAIT",
	[FSM_INST_STOP]		= "STOP",
};

static void fsm_dump_seq(char *tag, struct fsm_seq *seq)
{
	int i, j, k;
	uint8_t cmd;
	uint32_t *seq_reg;
	char *str;
	char cmd_str[64], *c;

	printk(KERN_INFO "%s:\n", tag ? tag : "FSM Sequence");
	printk(KERN_INFO "\t-------------------------------------------------"
	       "--------------\n");
	printk(KERN_INFO "\tTRANS_SIZE : 0x%08x  [ %d cycles/%d bytes ]\n",
	       seq->data_size, seq->data_size, seq->data_size/8);
	printk(KERN_INFO "\tADD1       : 0x%08x\n", seq->addr1);
	printk(KERN_INFO "\tADD2       : 0x%08x\n", seq->addr2);
	printk(KERN_INFO "\tADD_CFG    : 0x%08x  [ ADD1 %d(x%d)%s%s ]\n",
	       seq->addr_cfg, seq->addr_cfg & 0x1f,
	       ((seq->addr_cfg >> 6) & 0x3) + 1,
	       ((seq->addr_cfg >> 8) & 0x1) ? " CSDEASSERT" : "",
	       ((seq->addr_cfg >> 9) & 0x1) ? " 32BIT" : " 24BIT");
	printk(KERN_INFO "\t                         [ ADD2 %d(x%d)%s%s ]\n",
	       (seq->addr_cfg >> 16) & 0x1f,
	       ((seq->addr_cfg >> 22) & 0x3) + 1,
	       ((seq->addr_cfg >> 24) & 0x1) ? " CSDEASSERT" : "",
	       ((seq->addr_cfg >> 25) & 0x1) ? " 32BIT" : " 24BIT");

	for (i = 0; i < 5; i++) {
		if (seq->seq_opc[i] == 0x00000000) {
			printk(KERN_INFO "\tSEQ_OPC%d   : 0x%08x\n",
			       i, seq->seq_opc[i]);
		} else {
			cmd = (uint8_t)(seq->seq_opc[i] & 0xff);
			str = flash_cmd_strs[cmd];
			if (!str)
				str = "UNKNOWN";
			printk(KERN_INFO "\tSEQ_OPC%d   : 0x%08x  "
			       "[ 0x%02x/%-12s %d(x%d)%11s ]\n",
			       i, seq->seq_opc[i], cmd, str,
			       (seq->seq_opc[i] >> 8) & 0x3f,
			       ((seq->seq_opc[i] >> 14) & 0x3) + 1,
			       ((seq->seq_opc[i] >> 16) & 0x1) ?
			       " CSDEASSERT" : "");
		}
	}

	printk(KERN_INFO "\tMODE       : 0x%08x  [ 0x%02x %d(x%d)%s ]\n",
	       seq->mode,
	       seq->mode & 0xff,
	       (seq->mode >> 16) & 0x3f,
	       ((seq->mode >> 22) & 0x3) + 1,
	       ((seq->mode >> 24) & 0x1) ? " CSDEASSERT" : "");

	printk(KERN_INFO "\tDUMMY      : 0x%08x  [ 0x%02x %d(x%d)%s ]\n",
	       seq->dummy,
	       seq->dummy & 0xff,
	       (seq->dummy >> 16) & 0x3f,
	       ((seq->dummy >> 22) & 0x3) + 1,
	       ((seq->dummy >> 24) & 0x1) ? " CSDEASSERT" : "");

	if ((seq->status >> 21) & 0x1)
		printk(KERN_INFO "\tFLASH_STA  : 0x%08x  [ READ x%d%s ]\n",
		       seq->status, ((seq->status >> 16) & 0x3) + 1,
		       ((seq->status >> 20) & 0x1) ? " CSDEASSERT" : "");
	else
		printk(KERN_INFO "\tFLASH_STA  : 0x%08x  "
		       "[ WRITE {0x%02x, 0x%02x} x%d%s ]\n",
		       seq->status, seq->status & 0xff,
		       (seq->status >> 8) & 0xff,
		       ((seq->status >> 16) & 0x3) + 1,
		       ((seq->status >> 20) & 0x1) ? " CSDEASSERT" : "");

	seq_reg = (uint32_t *)seq->seq;
	for (i = 0, k = 0; i < 4; i++) {
		c = cmd_str;
		*c = '\0';
		for (j = 0; j < 4; j++, k++) {
			cmd = (uint8_t)(seq->seq[k] & 0xff);
			str = fsm_inst_strs[cmd];
			if (j != 0)
				c += sprintf(c, "%4s", str ? " -> " : "");
			c += sprintf(c, "%5s", str ? str : "");
		}
		printk(KERN_INFO "\tFAST_SEQ%d  : 0x%08x  [ %s ]\n",
		       i, seq_reg[i], cmd_str);
	}

	printk(KERN_INFO "\tSEQ_CONFIG : 0x%08x  [ START_SEQ   = %d ]\n",
	       seq->seq_cfg, seq->seq_cfg & 0x1);
	printk(KERN_INFO "\t                         [ SW_RESET    = %d ]\n",
	       (seq->seq_cfg >> 5) & 0x1);
	printk(KERN_INFO "\t                         [ CSDEASSERT  = %d ]\n",
	       (seq->seq_cfg >> 6) & 0x1);
	printk(KERN_INFO "\t                         [ RD_NOT_WR   = %d ]\n",
	       (seq->seq_cfg >> 7) & 0x1);
	printk(KERN_INFO "\t                         [ ERASE_BIT   = %d ]\n",
	       (seq->seq_cfg >> 8) & 0x1);
	printk(KERN_INFO "\t                         [ DATA_PADS   = %d ]\n",
	       ((seq->seq_cfg >> 16) & 0x3) + 1);
	printk(KERN_INFO "\t                         [ DREQ_HALF   = %d ]\n",
	       (seq->seq_cfg >> 18) & 0x1);
	printk(KERN_INFO "\t-------------------------------------------------"
	       "--------------\n");
}
#endif /* DEBUG_SPI_FSM_SEQS */

/*
 * SPI Flash Device Table
 */
struct flash_info {
	char		*name;

	/* JEDEC id zero means "no ID" (most older chips); otherwise it has
	 * a high byte of zero plus three data bytes: the manufacturer id,
	 * then a two byte device id.
	 */
	u32		jedec_id;
	u16             ext_id;

	/* The size listed here is what works with FLASH_CMD_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned	sector_size;
	u16		n_sectors;

	/* FLASH device capabilities */
	u32		capabilities;

	/* Maximum operating frequency.  Note, where FAST_READ is supported,
	 * freq_max specifies the FAST_READ frequency, not the READ frequency.
	 */
	u32		max_freq;

	int		(*config)(struct stm_spi_fsm *, struct flash_info *);
};

static int w25q_config(struct stm_spi_fsm *fsm, struct flash_info *info);
static int n25q_config(struct stm_spi_fsm *fsm, struct flash_info *info);
static int mx25_config(struct stm_spi_fsm *fsm, struct flash_info *info);

static struct flash_info __devinitdata flash_types[] = {

	/* ST Microelectronics/Numonyx --
	 * (newer production versions may have feature updates (eg faster
	 * operating frequency) */
#define M25P_CAPS (FLASH_CAPS_READ_WRITE | FLASH_CAPS_READ_FAST)
	{ "m25p40",  0x202013, 0,  64 * 1024,   8, M25P_CAPS, 25, NULL},
	{ "m25p80",  0x202014, 0,  64 * 1024,  16, M25P_CAPS, 25, NULL},
	{ "m25p16",  0x202015, 0,  64 * 1024,  32, M25P_CAPS, 25, NULL},
	{ "m25p32",  0x202016, 0,  64 * 1024,  64, M25P_CAPS, 50, NULL},
	{ "m25p64",  0x202017, 0,  64 * 1024, 128, M25P_CAPS, 50, NULL},
	{ "m25p128", 0x202018, 0, 256 * 1024,  64, M25P_CAPS, 50, NULL},

#define M25PX_CAPS (FLASH_CAPS_READ_WRITE	| \
		    FLASH_CAPS_READ_FAST	| \
		    FLASH_CAPS_READ_1_1_2	| \
		    FLASH_CAPS_WRITE_1_1_2)
	{ "m25px32", 0x207116, 0,  64 * 1024,  64, M25PX_CAPS, 75, NULL},
	{ "m25px64", 0x207117, 0,  64 * 1024, 128, M25PX_CAPS, 75, NULL},

#define MX25_CAPS (FLASH_CAPS_READ_WRITE	| \
		   FLASH_CAPS_READ_FAST		| \
		   FLASH_CAPS_READ_1_1_2	| \
		   FLASH_CAPS_READ_1_2_2	| \
		   FLASH_CAPS_READ_1_1_4	| \
		   FLASH_CAPS_READ_1_4_4	| \
		   FLASH_CAPS_WRITE_1_4_4	| \
		   FLASH_CAPS_SE_4K		| \
		   FLASH_CAPS_SE_32K)
	{ "mx25l25635e", 0xc22019, 0, 64*1024, 512,
	  (MX25_CAPS | FLASH_CAPS_32BITADDR | FLASH_CAPS_RESET),
	  70, mx25_config},

#define N25Q_CAPS (FLASH_CAPS_READ_WRITE	| \
		   FLASH_CAPS_READ_FAST		| \
		   FLASH_CAPS_READ_1_1_2	| \
		   FLASH_CAPS_READ_1_2_2	| \
		   FLASH_CAPS_READ_1_1_4	| \
		   FLASH_CAPS_READ_1_4_4	| \
		   FLASH_CAPS_WRITE_1_1_2	| \
		   FLASH_CAPS_WRITE_1_2_2	| \
		   FLASH_CAPS_WRITE_1_1_4	| \
		   FLASH_CAPS_WRITE_1_4_4)
	{ "n25q128", 0x20ba18, 0, 64 * 1024,  256, N25Q_CAPS, 108, n25q_config},
	{ "n25q256", 0x20ba19, 0, 64 * 1024,  512,
	  N25Q_CAPS | FLASH_CAPS_32BITADDR, 108, n25q_config},

	/* Winbond -- w25x "blocks" are 64K, "sectors" are 4KiB */
#define W25X_CAPS (FLASH_CAPS_READ_WRITE	| \
		   FLASH_CAPS_READ_FAST		| \
		   FLASH_CAPS_READ_1_1_2	| \
		   FLASH_CAPS_WRITE_1_1_2)
	{ "w25x40",  0xef3013, 0,  64 * 1024,   8, W25X_CAPS, 75, NULL},
	{ "w25x80",  0xef3014, 0,  64 * 1024,  16, W25X_CAPS, 75, NULL},
	{ "w25x16",  0xef3015, 0,  64 * 1024,  32, W25X_CAPS, 75, NULL},
	{ "w25x32",  0xef3016, 0,  64 * 1024,  64, W25X_CAPS, 75, NULL},
	{ "w25x64",  0xef3017, 0,  64 * 1024, 128, W25X_CAPS, 75, NULL},

	/* Winbond -- w25q "blocks" are 64K, "sectors" are 4KiB */
#define W25Q_CAPS (FLASH_CAPS_READ_WRITE	| \
		   FLASH_CAPS_READ_FAST		| \
		   FLASH_CAPS_READ_1_1_2	| \
		   FLASH_CAPS_READ_1_2_2	| \
		   FLASH_CAPS_READ_1_1_4	| \
		   FLASH_CAPS_READ_1_4_4	| \
		   FLASH_CAPS_WRITE_1_1_4)
	{ "w25q80",  0xef4014, 0,  64 * 1024,  16, W25Q_CAPS, 80, w25q_config},
	{ "w25q16",  0xef4015, 0,  64 * 1024,  32, W25Q_CAPS, 80, w25q_config},
	{ "w25q32",  0xef4016, 0,  64 * 1024,  64, W25Q_CAPS, 80, w25q_config},
	{ "w25q64",  0xef4017, 0,  64 * 1024, 128, W25Q_CAPS, 80, w25q_config},

	{     NULL,  0x000000, 0,         0,   0,       0, },
};

/*
 * SoC reset on 'boot-from-spi' systems
 *
 * Certain modes of operation cause the Flash device to enter a particular state
 * for a period of time (e.g. 'Erase Sector', 'Quad Enable', and 'Enter 32-bit
 * Addr' commands).  On boot-from-spi systems, it is important to consider what
 * happens if a warm reset occurs during this period.  The SPIBoot controller
 * assumes that Flash device is in its default reset state, 24-bit address mode,
 * and ready to accept commands.  This can be achieved using some form of
 * on-board logic/controller to force a device POR in response to a SoC-level
 * reset or by making use of the device reset signal if available (limited
 * number of devices only).
 *
 * Failure to take such precautions can cause problems following a warm reset.
 * For some operations (e.g. ERASE), there is little that can be done.  For
 * other modes of operation (e.g. 32-bit addressing), options are often
 * available that can help minimise the window in which a reset could cause a
 * problem.
 *
 */
static int can_handle_soc_reset(struct stm_spi_fsm *fsm,
				      struct flash_info *info)
{
	/* Reset signal is available on the board and supported by the device */
	if (fsm->capabilities.reset_signal &&
	    (fsm->capabilities.reset_signal &&
	     (info->capabilities & FLASH_CAPS_RESET)))
		return 1;

	/* Board-level logic forces a power-on-reset */
	if (fsm->capabilities.reset_por)
		return 1;

	/* Reset is not properly handled and may result in failure to reboot. */
	return 0;
}

/* Parameters to configure a READ or WRITE FSM sequence */
struct seq_rw_config {
	uint32_t	capabilities;	/* capabilities to support config */
	uint8_t		cmd;		/* FLASH command */
	int		write;		/* Write Sequence */
	uint8_t		addr_pads;	/* No. of addr pads (MODE & DUMMY) */
	uint8_t		data_pads;	/* No. of data pads */
	uint8_t		mode_data;	/* MODE data */
	uint8_t		mode_cycles;	/* No. of MODE cycles */
	uint8_t		dummy_cycles;	/* No. of DUMMY cycles */
};

/* Default READ configurations, in order of preference */
static struct seq_rw_config default_read_configs[] = {
	{FLASH_CAPS_READ_1_4_4, FLASH_CMD_READ_1_4_4,	0, 4, 4, 0x00, 2, 4},
	{FLASH_CAPS_READ_1_1_4, FLASH_CMD_READ_1_1_4,	0, 1, 4, 0x00, 0, 8},
	{FLASH_CAPS_READ_1_2_2, FLASH_CMD_READ_1_2_2,	0, 2, 2, 0x00, 4, 0},
	{FLASH_CAPS_READ_1_1_2, FLASH_CMD_READ_1_1_2,	0, 1, 2, 0x00, 0, 8},
	{FLASH_CAPS_READ_FAST,	FLASH_CMD_READ_FAST,	0, 1, 1, 0x00, 0, 8},
	{FLASH_CAPS_READ_WRITE, FLASH_CMD_READ,	        0, 1, 1, 0x00, 0, 0},

	/* terminating entry */
	{0x00,			 0,			0, 0, 0, 0x00, 0, 0},
};

/* Default WRITE configurations, in order of preference */
static struct seq_rw_config default_write_configs[] = {
	{FLASH_CAPS_WRITE_1_4_4, FLASH_CMD_WRITE_1_4_4, 1, 4, 4, 0x00, 0, 0},
	{FLASH_CAPS_WRITE_1_1_4, FLASH_CMD_WRITE_1_1_4, 1, 1, 4, 0x00, 0, 0},
	{FLASH_CAPS_WRITE_1_2_2, FLASH_CMD_WRITE_1_2_2, 1, 2, 2, 0x00, 0, 0},
	{FLASH_CAPS_WRITE_1_1_2, FLASH_CMD_WRITE_1_1_2, 1, 1, 2, 0x00, 0, 0},
	{FLASH_CAPS_READ_WRITE,  FLASH_CMD_WRITE,       1, 1, 1, 0x00, 0, 0},

	/* terminating entry */
	{0x00,			  0,			 0, 0, 0, 0x00, 0, 0},
};

/* Search for preferred configuration based on available capabilities */
static struct seq_rw_config
*search_seq_rw_configs(struct seq_rw_config configs[], uint32_t capabilities)
{
	struct seq_rw_config *config;

	for (config = configs; configs->cmd != 0; config++)
		if ((config->capabilities & capabilities) ==
		    config->capabilities)
			return config;

	return NULL;
}

/* Configure a READ/WRITE sequence according to configuration parameters */
static int configure_rw_seq(struct fsm_seq *seq, struct seq_rw_config *cfg,
			    int use_32bit_addr)
{
	int i;
	int addr1_cycles, addr2_cycles;

	memset(seq, 0x00, FSM_SEQ_SIZE);

	i = 0;
	/* Add READ/WRITE OPC  */
	seq->seq_opc[i++] = (SEQ_OPC_PADS_1 |
			     SEQ_OPC_CYCLES(8) |
			     SEQ_OPC_OPCODE(cfg->cmd));

	/* Add WREN OPC for a WRITE sequence */
	if (cfg->write)
		seq->seq_opc[i++] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
				     SEQ_OPC_OPCODE(FLASH_CMD_WREN) |
				     SEQ_OPC_CSDEASSERT);

	/* Address configuration (24 or 32-bit addresses) */
	addr1_cycles = use_32bit_addr ? 16 : 8;
	addr1_cycles /= cfg->addr_pads;
	addr2_cycles = 16 / cfg->addr_pads;
	seq->addr_cfg = ((addr1_cycles & 0x3f) << 0 |	/* ADD1 cycles */
			 (cfg->addr_pads - 1) << 6 |	/* ADD1 pads */
			 (addr2_cycles & 0x3f) << 16 |	/* ADD2 cycles */
			 ((cfg->addr_pads - 1) << 22));	/* ADD2 pads */

	/* Data/Sequence configuration */
	seq->seq_cfg = ((cfg->data_pads - 1) << 16 |
			SEQ_CFG_STARTSEQ |
			SEQ_CFG_CSDEASSERT);
	if (!cfg->write)
		seq->seq_cfg |= SEQ_CFG_READNOTWRITE;

	/* Mode configuration (no. of pads taken from addr cfg) */
	seq->mode = ((cfg->mode_data & 0xff) << 0 |	/* data */
		     (cfg->mode_cycles & 0x3f) << 16 |	/* cycles */
		     (cfg->addr_pads - 1) << 22);	/* pads */

	/* Dummy configuration (no. of pads taken from addr cfg) */
	seq->dummy = ((cfg->dummy_cycles & 0x3f) << 16 |	/* cycles */
		      (cfg->addr_pads - 1) << 22);		/* pads */


	/* Instruction sequence */
	i = 0;
	if (cfg->write)
		seq->seq[i++] = FSM_INST_CMD2;

	seq->seq[i++] = FSM_INST_CMD1;

	seq->seq[i++] = FSM_INST_ADD1;
	seq->seq[i++] = FSM_INST_ADD2;

	if (cfg->mode_cycles)
		seq->seq[i++] = FSM_INST_MODE;

	if (cfg->dummy_cycles)
		seq->seq[i++] = FSM_INST_DUMMY;

	seq->seq[i++] = cfg->write ? FSM_INST_DATA_WRITE : FSM_INST_DATA_READ;
	seq->seq[i++] = FSM_INST_STOP;

	return 0;
}


/* Configure preferred READ/WRITE sequence according to available
 * capabilities
 */
static int fsm_search_configure_rw_seq(struct stm_spi_fsm *fsm,
				       struct fsm_seq *seq,
				       struct seq_rw_config *configs,
				       uint32_t capabilities)
{
	struct seq_rw_config *config;

	config = search_seq_rw_configs(configs, capabilities);
	if (!config) {
		dev_err(fsm->dev, "failed to find suitable config\n");
		return 1;
	}

	if (configure_rw_seq(seq, config,
			     capabilities & FLASH_CAPS_32BITADDR) != 0) {
		dev_err(fsm->dev, "failed to configure READ/WRITE sequence\n");
		return 1;
	}

	return 0;
}

static int fsm_read_status(struct stm_spi_fsm *fsm, uint8_t cmd,
			   uint8_t *status);
static int fsm_write_status(struct stm_spi_fsm *fsm, uint16_t status,
			    int sta_bytes);
static int fsm_wrvcr(struct stm_spi_fsm *fsm, uint8_t data);
static int fsm_enter_32bitaddr(struct stm_spi_fsm *fsm, int enter);

/* [DEFAULT] Configure READ/WRITE/ERASE sequences */
static int fsm_config_rwe_seqs_default(struct stm_spi_fsm *fsm,
				      struct flash_info *info)
{
	uint32_t capabilities = info->capabilities;

	/* Mask-out capabilities not supported by platform */
	if (fsm->capabilities.quad_mode == 0)
		capabilities &= ~FLASH_CAPS_QUAD;
	if (fsm->capabilities.dual_mode == 0)
		capabilities &= ~FLASH_CAPS_DUAL;

	/* Configure 'READ' sequence */
	if (fsm_search_configure_rw_seq(fsm, &fsm_seq_read,
					default_read_configs,
					capabilities) != 0) {
		dev_err(fsm->dev, "failed to configure READ sequence "
			"according to capabilities [0x%08x]\n", capabilities);
		return 1;
	}

	/* Configure 'WRITE' sequence */
	if (fsm_search_configure_rw_seq(fsm, &fsm_seq_write,
					default_write_configs,
					capabilities) != 0) {
		dev_err(fsm->dev, "failed to configure WRITE sequence "
			"according to capabilities [0x%08x]\n", capabilities);
		return 1;
	}

	/* Configure 'ERASE_SECTOR' sequence */
	configure_erasesec_seq(&fsm_seq_erase_sector,
			       capabilities & FLASH_CAPS_32BITADDR);

	return 0;
}

/* [W25Qxxx] Configure READ/WRITE sequences */
#define W25Q_STATUS_QE			(0x1 << 9)
static int w25q_config(struct stm_spi_fsm *fsm, struct flash_info *info)
{
	uint32_t data_pads;
	uint8_t sta1, sta2;
	uint16_t sta_wr;

	if (fsm_config_rwe_seqs_default(fsm, info) != 0)
		return 1;

	/* If using QUAD mode, set QE STATUS bit */
	data_pads = ((fsm_seq_read.seq_cfg >> 16) & 0x3) + 1;
	if (data_pads == 4) {
		fsm_read_status(fsm, FLASH_CMD_RDSR, &sta1);
		fsm_read_status(fsm, FLASH_CMD_RDSR2, &sta2);

		sta_wr = ((uint16_t)sta2 << 8) | sta1;

		sta_wr |= W25Q_STATUS_QE;

		fsm_write_status(fsm, sta_wr, 2);
	}

	return 0;
}

/* [MX25xxx] Configure READ/Write sequences */
#define MX25_STATUS_QE			(0x1 << 6)

static int mx25_configure_en32bitaddr_seq(struct fsm_seq *seq)
{
	seq->seq_opc[0] = (SEQ_OPC_PADS_1 |
			   SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(FLASH_CMD_EN4B_ADDR) |
			   SEQ_OPC_CSDEASSERT);

	seq->seq[0] = FSM_INST_CMD1;
	seq->seq[1] = FSM_INST_WAIT;
	seq->seq[2] = FSM_INST_STOP;

	seq->seq_cfg = (SEQ_CFG_PADS_1 |
			SEQ_CFG_ERASE |
			SEQ_CFG_READNOTWRITE |
			SEQ_CFG_CSDEASSERT |
			SEQ_CFG_STARTSEQ);

	return 0;
}

static int mx25_config(struct stm_spi_fsm *fsm, struct flash_info *info)
{
	uint32_t data_pads;
	uint8_t sta;

	/* Disable support for 'WRITE_1_4_4' (limited to 20MHz which is of
	 * marginal benefit on our hardware and doesn't justify implementing
	 * different READ/WRITE frequencies).
	 */
	info->capabilities &= ~FLASH_CAPS_WRITE_1_4_4;

	/*
	 * Use default READ/WRITE sequences
	 */
	if (fsm_config_rwe_seqs_default(fsm, info) != 0)
		return 1;

	/*
	 * Configure 32-bit Address Support
	 */
	if (info->capabilities & FLASH_CAPS_32BITADDR) {
		/* Configure 'enter_32bitaddr' FSM sequence */
		mx25_configure_en32bitaddr_seq(&fsm_seq_en32bitaddr);

		if (!fsm->capabilities.boot_from_spi ||
		    can_handle_soc_reset(fsm, info)) {
			/* If we can handle SoC resets, we enable 32-bit address
			 * mode pervasively */
			fsm_enter_32bitaddr(fsm, 1);

		} else {
			/* Else, enable/disable 32-bit addressing before/after
			 * each operation */
			fsm->configuration = (CFG_READ_TOGGLE32BITADDR |
					      CFG_WRITE_TOGGLE32BITADDR |
					      CFG_ERASESEC_TOGGLE32BITADDR);
			/* It seems a small delay is required after exiting
			 * 32-bit mode following a write operation.  The issue
			 * is under investigation.
			 */
			fsm->configuration |= CFG_WRITE_EX32BITADDR_DELAY;
		}
	}

	/* For QUAD mode, set 'QE' STATUS bit */
	data_pads = ((fsm_seq_read.seq_cfg >> 16) & 0x3) + 1;
	if (data_pads == 4) {
		fsm_read_status(fsm, FLASH_CMD_RDSR, &sta);
		sta |= MX25_STATUS_QE;
		fsm_write_status(fsm, sta, 1);
	}

	return 0;
}

/*
 * [N25Qxxx] Configuration
 */
#define N25Q_VCR_DUMMY_CYCLES(x)	(((x) & 0xf) << 4)
#define N25Q_VCR_XIP_DISABLED		((uint8_t)0x1 << 3)
#define N25Q_VCR_WRAP_CONT		0x3

/* N25Q 3-byte Address READ configurations
 *	- 'FAST' variants configured for 8 dummy cycles.
 *
 * Note, the number of dummy cycles used for 'FAST' READ operations is
 * configurable and would normally be tuned according to the READ command and
 * operating frequency.  However, this applies universally to all 'FAST' READ
 * commands, including those used by the SPIBoot controller, and remains in
 * force until the device is power-cycled.  Since the SPIBoot controller is
 * hard-wired to use 8 dummy cycles, we must configure the device to also use 8
 * cycles.
 */
static struct seq_rw_config n25q_read3_configs[] = {
	{FLASH_CAPS_READ_1_4_4, FLASH_CMD_READ_1_4_4,	0, 4, 4, 0x00, 0, 8},
	{FLASH_CAPS_READ_1_1_4, FLASH_CMD_READ_1_1_4,	0, 1, 4, 0x00, 0, 8},
	{FLASH_CAPS_READ_1_2_2, FLASH_CMD_READ_1_2_2,	0, 2, 2, 0x00, 0, 8},
	{FLASH_CAPS_READ_1_1_2, FLASH_CMD_READ_1_1_2,	0, 1, 2, 0x00, 0, 8},
	{FLASH_CAPS_READ_FAST,	FLASH_CMD_READ_FAST,	0, 1, 1, 0x00, 0, 8},
	{FLASH_CAPS_READ_WRITE, FLASH_CMD_READ,	        0, 1, 1, 0x00, 0, 0},
	{0x00,			 0,			0, 0, 0, 0x00, 0, 0},
};

/* N25Q 4-byte Address READ configurations
 *	- use special 4-byte address READ commands (reduces overheads, and
 *        reduces risk of hitting watchdog resets issues).
 *	- 'FAST' variants configured for 8 dummy cycles (see note above.)
 */
static struct seq_rw_config n25q_read4_configs[] = {
	{FLASH_CAPS_READ_1_4_4, N25Q_CMD_READ4_1_4_4,	0, 4, 4, 0x00, 0, 8},
	{FLASH_CAPS_READ_1_1_4, N25Q_CMD_READ4_1_1_4,	0, 1, 4, 0x00, 0, 8},
	{FLASH_CAPS_READ_1_2_2, N25Q_CMD_READ4_1_2_2,	0, 2, 2, 0x00, 0, 8},
	{FLASH_CAPS_READ_1_1_2, N25Q_CMD_READ4_1_1_2,	0, 1, 2, 0x00, 0, 8},
	{FLASH_CAPS_READ_FAST,	N25Q_CMD_READ4_FAST,	0, 1, 1, 0x00, 0, 8},
	{FLASH_CAPS_READ_WRITE, N25Q_CMD_READ4,		0, 1, 1, 0x00, 0, 0},
	{0x00,			 0,			0, 0, 0, 0x00, 0, 0},
};

static int n25q_configure_en32bitaddr_seq(struct fsm_seq *seq)
{
	seq->seq_opc[0] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(FLASH_CMD_EN4B_ADDR));
	seq->seq_opc[1] = (SEQ_OPC_PADS_1 | SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(FLASH_CMD_WREN) |
			   SEQ_OPC_CSDEASSERT);

	seq->seq[0] = FSM_INST_CMD2;
	seq->seq[1] = FSM_INST_CMD1;
	seq->seq[2] = FSM_INST_WAIT;
	seq->seq[3] = FSM_INST_STOP;

	seq->seq_cfg = (SEQ_CFG_PADS_1 |
			SEQ_CFG_ERASE |
			SEQ_CFG_READNOTWRITE |
			SEQ_CFG_CSDEASSERT |
			SEQ_CFG_STARTSEQ);

	return 0;
}

static int n25q_config(struct stm_spi_fsm *fsm, struct flash_info *info)
{
	uint8_t vcr;
	int ret = 0;
	uint32_t capabilities = info->capabilities;

	/* Mask out-capabilities not supported by platform */
	if (fsm->capabilities.quad_mode == 0)
		capabilities &= ~FLASH_CAPS_QUAD;
	if (fsm->capabilities.dual_mode == 0)
		capabilities &= ~FLASH_CAPS_DUAL;

	/*
	 * Configure 'READ' sequence
	 */
	if (capabilities & FLASH_CAPS_32BITADDR)
		/* 32-bit addressing supported by N25Q 'READ4' commands */
		ret = fsm_search_configure_rw_seq(fsm, &fsm_seq_read,
						  n25q_read4_configs,
						  capabilities);
	else
		/* 24-bit addressing with 8 dummy cycles */
		ret = fsm_search_configure_rw_seq(fsm, &fsm_seq_read,
						  n25q_read3_configs,
						  capabilities);

	if (ret != 0) {
		dev_err(fsm->dev, "failed to configure READ sequence "
			"according to capabilities [0x%08x]\n", capabilities);
		return 1;
	}


	/*
	 * Configure 'WRITE' sequence (use default configs)
	 */
	ret = fsm_search_configure_rw_seq(fsm, &fsm_seq_write,
					  default_write_configs,
					  capabilities);
	if (ret != 0) {
		dev_err(fsm->dev, "failed to configure WRITE sequence "
			"according to capabilities [0x%08x]\n", capabilities);
		return 1;
	}

	/*
	 * Configure 'ERASE_SECTOR' sequence
	 */
	configure_erasesec_seq(&fsm_seq_erase_sector,
			       capabilities & FLASH_CAPS_32BITADDR);

	/*
	 * Configure 32-bit address support
	 */
	if (capabilities & FLASH_CAPS_32BITADDR) {
		/* Configure 'enter_32bitaddr' FSM sequence */
		n25q_configure_en32bitaddr_seq(&fsm_seq_en32bitaddr);

		if (!fsm->capabilities.boot_from_spi ||
		    can_handle_soc_reset(fsm, info)) {
			/* If we can handle SoC resets, we enable 32-bit address
			 * mode pervasively */
			fsm_enter_32bitaddr(fsm, 1);
		} else {
			/* Else, enable/disable for WRITE and ERASE operations
			 * (READ uses special commands) */
			fsm->configuration = (CFG_WRITE_TOGGLE32BITADDR |
					      CFG_ERASESEC_TOGGLE32BITADDR);
		}
	}

	/*
	 * Configure device to use 8 dummy cycles
	 */
	vcr = (N25Q_VCR_DUMMY_CYCLES(8) | N25Q_VCR_XIP_DISABLED |
	       N25Q_VCR_WRAP_CONT);
	fsm_wrvcr(fsm, vcr);

	return ret;
}

/*
 * FSM interface
 */
static inline int fsm_is_idle(struct stm_spi_fsm *fsm)
{
	return readl(fsm->base + SPI_FAST_SEQ_STA) & 0x10;
}

static inline uint32_t fsm_fifo_available(struct stm_spi_fsm *fsm)
{
	return (readl(fsm->base + SPI_FAST_SEQ_STA) >> 5) & 0x7f;
}

static inline void fsm_load_seq(struct stm_spi_fsm *fsm,
				const struct fsm_seq *const seq)
{
	BUG_ON(!fsm_is_idle(fsm));
	memcpy_toio(fsm->base + SPI_FAST_SEQ_TRANSFER_SIZE,
		    seq, FSM_SEQ_SIZE);
}

static int fsm_wait_seq(struct stm_spi_fsm *fsm)
{
	unsigned long timeo = jiffies + HZ;

	while (time_before(jiffies, timeo)) {
		if (fsm_is_idle(fsm))
			return 0;

		cond_resched();
	}

	dev_err(fsm->dev, "timeout on sequence completion\n");

	return 1;
}

static void fsm_clear_fifo(struct stm_spi_fsm *fsm)
{
	uint32_t avail;

	while ((avail = fsm_fifo_available(fsm)) > 0) {

		dev_dbg(fsm->dev, "clearing %d bytes from FIFO\n", avail*4);

		while (avail) {
			readl(fsm->base + SPI_FAST_SEQ_DATA_REG);
			avail--;
		}
	}
}

static int fsm_read_fifo(struct stm_spi_fsm *fsm,
			 uint32_t *buf, const uint32_t size)
{
	uint32_t avail;
	uint32_t remaining = size >> 2;
	uint32_t words;

	dev_dbg(fsm->dev, "reading %d bytes from FIFO\n", size);

	BUG_ON((((uint32_t)buf) & 0x3) || (size & 0x3));

	while (remaining) {
		while (!(avail = fsm_fifo_available(fsm)))
			;
		words = min(avail, remaining);
		remaining -= words;

		readsl(fsm->base + SPI_FAST_SEQ_DATA_REG, buf, words);
		buf += words;

	};

	return size;
}

static int fsm_write_fifo(struct stm_spi_fsm *fsm,
			  const uint32_t *buf, const uint32_t size)
{
	uint32_t words = size >> 2;

	dev_dbg(fsm->dev, "writing %d bytes to FIFO\n", size);

	BUG_ON((((uint32_t)buf) & 0x3) || (size & 0x3));

	writesl(fsm->base + SPI_FAST_SEQ_DATA_REG, buf, words);

	return size;
}

/*
 * Serial Flash operations
 */
static int fsm_wait_busy(struct stm_spi_fsm *fsm)
{
	struct fsm_seq *seq = &fsm_seq_read_status_fifo;
	unsigned long deadline;
	uint8_t status[4] = {0x00, 0x00, 0x00, 0x00};

	/* Use RDRS1 */
	seq->seq_opc[0] = (SEQ_OPC_PADS_1 |
			   SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(FLASH_CMD_RDSR));

	/* Load read_status sequence */
	fsm_load_seq(fsm, seq);

	/* Repeat until busy bit is deasserted, or timeout */
	deadline = jiffies + FLASH_MAX_BUSY_WAIT;
	do {
		cond_resched();

		fsm_wait_seq(fsm);

		fsm_read_fifo(fsm, (uint32_t *)status, 4);

		if ((status[3] & FLASH_STATUS_BUSY) == 0)
			return 0;

		/* Restart */
		writel(seq->seq_cfg, fsm->base + SPI_FAST_SEQ_CFG);

	} while (!time_after_eq(jiffies, deadline));

	dev_err(fsm->dev, "timeout on wait_busy\n");

	return 1;
}

static int fsm_read_jedec(struct stm_spi_fsm *fsm, uint8_t *const jedec)
{
	const struct fsm_seq *seq = &fsm_seq_read_jedec;
	uint32_t tmp[2];

	fsm_load_seq(fsm, seq);

	fsm_read_fifo(fsm, tmp, 8);

	memcpy(jedec, tmp, 5);

	return 0;
}

static int fsm_read_status(struct stm_spi_fsm *fsm, uint8_t cmd,
			   uint8_t *status)
{
	struct fsm_seq *seq = &fsm_seq_read_status_fifo;
	uint32_t tmp;

	dev_dbg(fsm->dev, "reading STA[%s]\n",
		(cmd == FLASH_CMD_RDSR) ? "1" : "2");

	seq->seq_opc[0] = (SEQ_OPC_PADS_1 |
			   SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(cmd)),

	fsm_load_seq(fsm, seq);

	fsm_read_fifo(fsm, &tmp, 4);

	*status = (uint8_t)(tmp >> 24);

	fsm_wait_seq(fsm);

	return 0;
}

static int fsm_write_status(struct stm_spi_fsm *fsm, uint16_t status,
			    int sta_bytes)
{
	struct fsm_seq *seq = &fsm_seq_write_status;

	dev_dbg(fsm->dev, "writing STA[%s] 0x%04x\n",
		(sta_bytes == 1) ? "1" : "1+2", status);

	seq->status = (uint32_t)status | STA_PADS_1 | STA_CSDEASSERT;
	seq->seq[2] = (sta_bytes == 1) ? FSM_INST_STA_WR1 : FSM_INST_STA_WR1_2;

	fsm_load_seq(fsm, seq);

	fsm_wait_seq(fsm);

	return 0;
};

static int fsm_wrvcr(struct stm_spi_fsm *fsm, uint8_t data)
{
	struct fsm_seq *seq = &fsm_seq_wrvcr;

	dev_dbg(fsm->dev, "writing VCR 0x%02x\n", data);

	seq->status = (STA_DATA_BYTE1(data) |
		       STA_PADS_1 | STA_CSDEASSERT);

	fsm_load_seq(fsm, seq);

	fsm_wait_seq(fsm);

	return 0;
}

static int fsm_enter_32bitaddr(struct stm_spi_fsm *fsm, int enter)
{
	struct fsm_seq *seq = &fsm_seq_en32bitaddr;
	uint32_t cmd = enter ? FLASH_CMD_EN4B_ADDR : FLASH_CMD_EX4B_ADDR;

	seq->seq_opc[0] = (SEQ_OPC_PADS_1 |
			   SEQ_OPC_CYCLES(8) |
			   SEQ_OPC_OPCODE(cmd) |
			   SEQ_OPC_CSDEASSERT);

	fsm_load_seq(fsm, seq);

	fsm_wait_seq(fsm);

	return 0;
}


static int fsm_erase_sector(struct stm_spi_fsm *fsm, const uint32_t offset)
{
	struct fsm_seq *seq = &fsm_seq_erase_sector;

	dev_dbg(fsm->dev, "erasing sector at 0x%08x\n", offset);

	/* Enter 32-bit address mode, if required */
	if (fsm->configuration & CFG_ERASESEC_TOGGLE32BITADDR)
		fsm_enter_32bitaddr(fsm, 1);

	seq->addr1 = (offset >> 16) & 0xffff;
	seq->addr2 = offset & 0xffff;

	fsm_load_seq(fsm, seq);

	fsm_wait_seq(fsm);

	fsm_wait_busy(fsm);

	/* Exit 32-bit address mode, if required */
	if (fsm->configuration & CFG_ERASESEC_TOGGLE32BITADDR)
		fsm_enter_32bitaddr(fsm, 0);

	return 0;
}

static int fsm_erase_chip(struct stm_spi_fsm *fsm)
{
	const struct fsm_seq *seq = &fsm_seq_erase_chip;

	dev_dbg(fsm->dev, "erasing chip\n");

	fsm_load_seq(fsm, seq);

	fsm_wait_seq(fsm);

	fsm_wait_busy(fsm);

	return 0;
}

static int fsm_read(struct stm_spi_fsm *fsm, uint8_t *const buf,
		    const uint32_t size, const uint32_t offset)
{
	struct fsm_seq *seq = &fsm_seq_read;
	uint32_t data_pads;
	uint32_t read_mask;
	uint8_t *page_buf = fsm->page_buf;
	uint32_t size_ub;
	uint32_t size_lb;
	uint32_t size_mop;
	uint32_t tmp[4];
	uint8_t *p;

	dev_dbg(fsm->dev, "reading %d bytes from 0x%08x\n", size, offset);

	/* Enter 32-bit address mode, if required */
	if (fsm->configuration & CFG_READ_TOGGLE32BITADDR)
		fsm_enter_32bitaddr(fsm, 1);

	/* Must read in multiples of 32 cycles (or 32*pads/8 bytes) */
	data_pads = ((seq->seq_cfg >> 16) & 0x3) + 1;
	read_mask = (data_pads << 2) - 1;

	/* Handle non-aligned buf */
	p = ((uint32_t)buf & 0x3) ? page_buf : buf;

	/* Handle non-aligned size */
	size_ub = (size + read_mask) & ~read_mask;
	size_lb = size & ~read_mask;
	size_mop = size & read_mask;

	seq->data_size = TRANSFER_SIZE(size_ub);
	seq->addr1 = (offset >> 16) & 0xffff;
	seq->addr2 = offset & 0xffff;

	fsm_load_seq(fsm, seq);

	if (size_lb)
		fsm_read_fifo(fsm, (uint32_t *)p, size_lb);

	if (size_mop) {
		fsm_read_fifo(fsm, tmp, read_mask + 1);
		memcpy(p + size_lb, &tmp, size_mop);
	}

	/* Handle non-aligned buf */
	if ((uint32_t)buf & 0x3)
		memcpy(buf, page_buf, size);

	/* Wait for sequence to finish */
	fsm_wait_seq(fsm);

	fsm_clear_fifo(fsm);

	/* Exit 32-bit address mode, if required */
	if (fsm->configuration & CFG_READ_TOGGLE32BITADDR)
		fsm_enter_32bitaddr(fsm, 0);

	return 0;
}

static int fsm_write(struct stm_spi_fsm *fsm, const uint8_t *const buf,
		     const uint32_t size, const uint32_t offset)
{
	struct fsm_seq *seq = &fsm_seq_write;
	uint32_t data_pads;
	uint32_t write_mask;
	uint8_t *page_buf = fsm->page_buf;
	uint32_t size_ub;
	uint32_t size_lb;
	uint32_t size_mop;
	uint32_t tmp[4];
	uint8_t *t = (uint8_t *)&tmp;
	int i;
	const uint8_t *p;

	dev_dbg(fsm->dev, "writing %d bytes to 0x%08x\n", size, offset);

	/* Enter 32-bit address mode, if required */
	if (fsm->configuration & CFG_WRITE_TOGGLE32BITADDR)
		fsm_enter_32bitaddr(fsm, 1);

	/* Must write in multiples of 32 cycles (or 32*pads/8 bytes) */
	data_pads = ((seq->seq_cfg >> 16) & 0x3) + 1;
	write_mask = (data_pads << 2) - 1;

	/* Handle non-aligned buf */
	if ((uint32_t)buf & 0x3) {
		memcpy(page_buf, buf, size);
		p = page_buf;
	} else {
		p = buf;
	}

	/* Handle non-aligned size */
	size_ub = (size + write_mask) & ~write_mask;
	size_lb = size & ~write_mask;
	size_mop = size & write_mask;

	seq->data_size = TRANSFER_SIZE(size_ub);
	seq->addr1 = (offset >> 16) & 0xffff;
	seq->addr2 = offset & 0xffff;

	if (fsm->capabilities.dummy_on_write) {
		fsm_load_seq(fsm, &fsm_seq_dummy);
		readl(fsm->base + SPI_FAST_SEQ_CFG);
	}

	/* Need to set FIFO to write mode, before writing data to FIFO (see
	 * GNBvb79594)
	 */
	writel(0x00040000, fsm->base + SPI_FAST_SEQ_CFG);

	/*
	 * Before writing data to the FIFO, apply a small delay to allow a
	 * potential change of FIFO direction to complete.
	 */
	if (fsm->fifo_dir_delay == 0)
		readl(fsm->base + SPI_FAST_SEQ_CFG);
	else
		udelay(fsm->fifo_dir_delay);

	/* Write data to FIFO, before starting sequence (see GNBvd79593) */
	if (size_lb) {
		fsm_write_fifo(fsm, (uint32_t *)p, size_lb);
		p += size_lb;
	}

	/* Handle non-aligned size */
	if (size_mop) {
		memset(t, 0xff, write_mask + 1);	/* fill with 0xff's */
		for (i = 0; i < size_mop; i++)
			t[i] = *p++;

		fsm_write_fifo(fsm, tmp, write_mask + 1);
	}

	/* Start sequence */
	fsm_load_seq(fsm, seq);

	/* Wait for sequence to finish */
	fsm_wait_seq(fsm);

	/* Wait for completion */
	fsm_wait_busy(fsm);

	/* Exit 32-bit address mode, if required */
	if (fsm->configuration & CFG_WRITE_TOGGLE32BITADDR) {
		fsm_enter_32bitaddr(fsm, 0);
		if (fsm->configuration & CFG_WRITE_EX32BITADDR_DELAY)
			udelay(1);
	}

	return 0;
}


/*
 * FSM Configuration
 */
static int fsm_set_mode(struct stm_spi_fsm *fsm, uint32_t mode)
{
	/* Wait for controller to accept mode change */
	if (!fsm->capabilities.no_poll_mode_change) {
		while (!(readl(fsm->base + SPI_STA_MODE_CHANGE) & 0x1))
			;
	}
	writel(mode, fsm->base + SPI_MODESELECT);

	return 0;
}

static int fsm_set_freq(struct stm_spi_fsm *fsm, uint32_t freq)
{
	struct clk *emi_clk;
	uint32_t emi_freq;
	uint32_t clk_div;

	/* Timings set in terms of EMI clock... */
	emi_clk = clk_get(NULL, "emi_clk");

	if (IS_ERR(emi_clk)) {
		dev_warn(fsm->dev, "Failed to find EMI clock. "
			 "Using default 100MHz.\n");
		emi_freq = 100000000UL;
	} else {
		emi_freq = clk_get_rate(emi_clk);
	}

	freq *= 1000000;

	/* Calculate clk_div: multiple of 2, round up, 2 -> 128. Note, clk_div =
	 * 4 is not supported on some SoCs, use 6 instead */
	clk_div = 2*((emi_freq + (2*freq - 1))/(2*freq));
	if (clk_div < 2)
		clk_div = 2;
	else if (clk_div == 4 && fsm->capabilities.no_clk_div_4)
		clk_div = 6;
	else if (clk_div > 128)
		clk_div = 128;

	/*
	 * Determine a suitable delay for the IP to complete a change of
	 * direction of the FIFO.  The required delay is related to the clock
	 * divider used.  The following heuristics are based on empirical tests,
	 * using a 100MHz EMI clock.
	 */
	if (clk_div <= 4)
		fsm->fifo_dir_delay = 0;
	else if (clk_div <= 10)
		fsm->fifo_dir_delay = 1;
	else
		fsm->fifo_dir_delay = (clk_div + 9) / 10;

	dev_dbg(fsm->dev, "emi_clk = %uHZ, spi_freq = %uHZ, clock_div = %u\n",
		emi_freq, freq, clk_div);

	/* Set SPI_CLOCKDIV */
	writel(clk_div, fsm->base + SPI_CLOCKDIV);

	return 0;
}

static int fsm_init(struct stm_spi_fsm *fsm)
{
	/* Perform a soft reset of the FSM controller */
	if (!fsm->capabilities.no_sw_reset) {
		writel(SEQ_CFG_SWRESET, fsm->base + SPI_FAST_SEQ_CFG);
		udelay(1);
		writel(0, fsm->base + SPI_FAST_SEQ_CFG);
	}

	/* Set clock to 'safe' frequency for device probe */
	fsm_set_freq(fsm, FLASH_PROBE_FREQ);

	/* Switch to FSM */
	fsm_set_mode(fsm, SPI_MODESELECT_FSM);

	/* Set timing parameters (use reset values for now (awaiting information
	 * from Validation Team)
	 */
	writel(SPI_CFG_DEVICE_ST |
	       SPI_CFG_MIN_CS_HIGH(0x0AA) |
	       SPI_CFG_CS_SETUPHOLD(0xa0) |
	       SPI_CFG_DATA_HOLD(0x00), fsm->base + SPI_CONFIGDATA);
	writel(0x0016e360, fsm->base + SPI_STATUS_WR_TIME_REG);

	/* Clear FIFO, just in case */
	fsm_clear_fifo(fsm);

	return 0;
}

static void fsm_exit(struct stm_spi_fsm *fsm)
{

}


/*
 * MTD interface
 */

/*
 * Read an address range from the flash chip.  The address range
 * may be any size provided it is within the physical boundaries.
 */
static int mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
		    size_t *retlen, u_char *buf)
{
	struct stm_spi_fsm *fsm = mtd->priv;
	uint32_t bytes;

	dev_dbg(fsm->dev, "%s %s 0x%08x, len %zd\n",  __func__,
		"from", (u32)from, len);

	/* Byte count starts at zero. */
	if (retlen)
		*retlen = 0;

	if (!len)
		return 0;

	if (from + len > mtd->size)
		return -EINVAL;

	mutex_lock(&fsm->lock);

	while (len > 0) {
		bytes = min(len, (size_t)FLASH_PAGESIZE);

		fsm_read(fsm, buf, bytes, from);

		buf += bytes;
		from += bytes;
		len -= bytes;

		if (retlen)
			*retlen += bytes;
	}

	mutex_unlock(&fsm->lock);

	return 0;
}

/*
 * Write an address range to the flash chip.  Data must be written in
 * FLASH_PAGESIZE chunks.  The address range may be any size provided
 * it is within the physical boundaries.
 */
static int mtd_write(struct mtd_info *mtd, loff_t to, size_t len,
	size_t *retlen, const u_char *buf)
{
	struct stm_spi_fsm *fsm = mtd->priv;

	u32 page_offs;
	u32 bytes;
	uint8_t *b = (uint8_t *)buf;

	dev_dbg(fsm->dev, "%s %s 0x%08x, len %zd\n", __func__,
		"to", (u32)to, len);

	if (retlen)
		*retlen = 0;

	if (!len)
		return 0;

	if (to + len > mtd->size)
		return -EINVAL;

	/* offset within page */
	page_offs = to % FLASH_PAGESIZE;

	mutex_lock(&fsm->lock);

	while (len) {

		/* write up to page boundary */
		bytes = min(FLASH_PAGESIZE - page_offs, len);

		fsm_write(fsm, b, bytes, to);

		b += bytes;
		len -= bytes;
		to += bytes;

		/* We are now page-aligned */
		page_offs = 0;

		if (retlen)
			*retlen += bytes;

	}

	mutex_unlock(&fsm->lock);

	return 0;
}

/*
 * Erase an address range on the flash chip.  The address range may extend
 * one or more erase sectors.  Return an error is there is a problem erasing.
 */
static int mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct stm_spi_fsm *fsm = mtd->priv;
	u32 addr, len;

	dev_dbg(fsm->dev, "%s %s 0x%llx, len %lld\n", __func__,
		"at", (long long)instr->addr, (long long)instr->len);

	if (instr->addr + instr->len > mtd->size)
		return -EINVAL;

	if (instr->len & (mtd->erasesize - 1))
		return -EINVAL;

	addr = instr->addr;
	len = instr->len;

	mutex_lock(&fsm->lock);

	/* whole-chip erase? */
	if (len == mtd->size) {
		if (fsm_erase_chip(fsm)) {
			instr->state = MTD_ERASE_FAILED;
			mutex_unlock(&fsm->lock);
			return -EIO;
		}
	} else {
		while (len) {
			if (fsm_erase_sector(fsm, addr)) {
				instr->state = MTD_ERASE_FAILED;
				mutex_unlock(&fsm->lock);
				return -EIO;
			}

			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	}

	mutex_unlock(&fsm->lock);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}


static struct flash_info *__devinit fsm_jedec_probe(struct stm_spi_fsm *fsm)
{
	u8			id[5];
	u32			jedec;
	u16                     ext_jedec;
	struct flash_info	*info;

	/* JEDEC also defines an optional "extended device information"
	 * string for after vendor-specific data, after the three bytes
	 * we use here.  Supporting some chips might require using it.
	 */

	if (fsm_read_jedec(fsm, id) != 0) {
		dev_info(fsm->dev, "error reading JEDEC ID\n");
		return NULL;
	}

	jedec = id[0];
	jedec = jedec << 8;
	jedec |= id[1];
	jedec = jedec << 8;
	jedec |= id[2];

	dev_dbg(fsm->dev, "JEDEC =  0x%08x [%02x %02x %02x %02x %02x]\n",
		jedec, id[0], id[1], id[2], id[3], id[4]);

	ext_jedec = id[3] << 8 | id[4];
	for (info = flash_types; info->name; info++) {
		if (info->jedec_id == jedec) {
			if (info->ext_id != 0 && info->ext_id != ext_jedec)
				continue;
			return info;
		}
	}

	dev_err(fsm->dev, "unrecognized JEDEC id %06x\n", jedec);

	return NULL;
}


/*
 * STM SPI FSM driver setup
 */
static int __init stm_spi_fsm_probe(struct platform_device *pdev)
{
	struct stm_plat_spifsm_data *data = pdev->dev.platform_data;
	struct stm_spi_fsm *fsm;
	struct resource *resource;
	int ret = 0;
	struct flash_info *info;
	unsigned i;

	/* Allocate memory for the driver structure (and zero it) */
	fsm = kzalloc(sizeof(struct stm_spi_fsm), GFP_KERNEL);
	if (!fsm) {
		dev_err(&pdev->dev, "failed to allocate fsm controller data\n");
		return -ENOMEM;
	}

	fsm->dev = &pdev->dev;

	/* Platform capabilities (Board/SoC/IP) */
	fsm->capabilities = data->capabilities;

	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!resource) {
		dev_err(&pdev->dev, "failed to find IORESOURCE_MEM\n");
		ret = -ENODEV;
		goto out1;
	}

	fsm->region = request_mem_region(resource->start,
					 resource_size(resource), pdev->name);
	if (!fsm->region) {
		dev_err(&pdev->dev, "failed to reserve memory region "
			"[0x%08x-0x%08x]\n", resource->start, resource->end);
		ret = -EBUSY;
		goto out1;
	}

	fsm->base = ioremap_nocache(resource->start, resource_size(resource));

	if (!fsm->base) {
		dev_err(&pdev->dev, "failed to ioremap [0x%08x]\n",
			resource->start);
		ret = -EINVAL;
		goto out2;
	}

	if (data->pads) {
		fsm->pad_state = stm_pad_claim(data->pads, pdev->name);
		if (!fsm->pad_state) {
			dev_err(&pdev->dev, "failed to request pads\n");
			ret = -EBUSY;
			goto out3;
		}
	}

	mutex_init(&fsm->lock);

	/* Initialise FSM */

	if (fsm_init(fsm) != 0) {
		dev_err(&pdev->dev, "failed to initialise SPI FSM "
			"Controller\n");
		ret = -EINVAL;
		goto out4;
	}

	/* Detect SPI FLASH device */
	info = fsm_jedec_probe(fsm);
	if (!info) {
		ret = -ENODEV;
		goto out5;
	}

	/*
	 * Issue warning if device found does not match that specified in the
	 * platform data.
	 */
	if (data->name && strcmp(data->name, info->name) != 0)
		dev_warn(&pdev->dev, "WARNING: expecting '%s', found '%s'\n",
			 data->name, info->name);

	/* Configure READ/WRITE/ERASE sequences according to platform and device
	 * capabilities.
	 */
	if (info->config) {
		if (info->config(fsm, info) != 0) {
			ret = -EINVAL;
			goto out5;
		}
	} else {
		if (fsm_config_rwe_seqs_default(fsm, info) != 0) {
			ret = -EINVAL;
			goto out5;
		}
	}

	if (fsm->capabilities.boot_from_spi &&
	    !can_handle_soc_reset(fsm, info))
		dev_warn(&pdev->dev, "WARNING: no provision for SPI reset"
			 "on boot-from-spi system\n");

#ifdef DEBUG_SPI_FSM_SEQS
	fsm_dump_seq("FSM READ SEQ", &fsm_seq_read);
	fsm_dump_seq("FSM WRITE_SEQ", &fsm_seq_write);
	fsm_dump_seq("FSM ERASE_SECT_SEQ", &fsm_seq_erase_sector);
	fsm_dump_seq("FSM EN32BITADDR_SEQ", &fsm_seq_en32bitaddr);
#endif

	platform_set_drvdata(pdev, fsm);

	/* Set operating frequency, from table or overridden by platform data */
	if (data->max_freq)
		fsm_set_freq(fsm, data->max_freq);
	else if (info->max_freq)
		fsm_set_freq(fsm, info->max_freq);

	/* Set up MTD parameters */
	fsm->mtd.priv = fsm;
	if (data && data->name)
		fsm->mtd.name = data->name;
	else
		fsm->mtd.name = NAME;

	fsm->mtd.type = MTD_NORFLASH;
	fsm->mtd.writesize = 4;
	fsm->mtd.flags = MTD_CAP_NORFLASH;
	fsm->mtd.size = info->sector_size * info->n_sectors;
	fsm->mtd.erasesize = info->sector_size;

	fsm->mtd.read = mtd_read;
	fsm->mtd.write = mtd_write;
	fsm->mtd.erase = mtd_erase;

	dev_info(&pdev->dev, "found device: %s, size = %llx (%lldMiB) "
		 "erasesize = 0x%08x (%uKiB)\n",
		 info->name,
		 (long long)fsm->mtd.size, (long long)(fsm->mtd.size >> 20),
		 fsm->mtd.erasesize, (fsm->mtd.erasesize >> 10));

	/* Cap size to 16MiB if 32-bit addressing is not supported/implemented
	 * on specific device */
	if ((fsm->mtd.size > 16 * 1024 * 1024)  &&
	    ((info->capabilities & FLASH_CAPS_32BITADDR) == 0)) {
		dev_info(&pdev->dev, "reducing visibility to 16MiB "
			 "(32-bit address mode not supported on device)\n");
		fsm->mtd.size = 16 * 1024 * 1024;
	}

	/* Add partitions */
	if (mtd_has_partitions()) {
		struct mtd_partition	*parts = NULL;
		int			nr_parts = 0;

		if (mtd_has_cmdlinepart()) {
			static const char *part_probes[]
					= { "cmdlinepart", NULL, };

			nr_parts = parse_mtd_partitions(&fsm->mtd,
							part_probes, &parts, 0);
		}

		if (nr_parts <= 0 && data && data->parts) {
			parts = data->parts;
			nr_parts = data->nr_parts;
		}

		if (nr_parts > 0) {
			for (i = 0; i < nr_parts; i++) {
				dev_dbg(fsm->dev, "partitions[%d] = "
					"{.name = %s, .offset = 0x%llx, "
					".size = 0x%llx (%lldKiB) }\n",
					i, parts[i].name,
					(long long)parts[i].offset,
					(long long)parts[i].size,
					(long long)(parts[i].size >> 10));
			}
			fsm->partitioned = 1;

			if (add_mtd_partitions(&fsm->mtd, parts, nr_parts)) {
				ret = -ENODEV;
				goto out5;
			}

			/* Success :-) */
			return 0;
		}

	} else if (data->nr_parts) {
		dev_info(&pdev->dev, " ignoring %d default partitions on %s\n",
			 data->nr_parts, data->name);
	}

	if (add_mtd_device(&fsm->mtd)) {
		ret = -ENODEV;
		goto out5;
	}

	/* Success :-) */
	return 0;
 out5:
	fsm_exit(fsm);
	platform_set_drvdata(pdev, NULL);
 out4:
	if (fsm->pad_state)
		stm_pad_release(fsm->pad_state);
 out3:
	iounmap(fsm->base);
 out2:
	release_resource(fsm->region);
 out1:
	kfree(fsm);

	return ret;
}

static int __devexit stm_spi_fsm_remove(struct platform_device *pdev)
{
	struct stm_spi_fsm *fsm = platform_get_drvdata(pdev);

	if (mtd_has_partitions() && fsm->partitioned)
		del_mtd_partitions(&fsm->mtd);
	else
		del_mtd_device(&fsm->mtd);

	fsm_exit(fsm);
	if (fsm->pad_state)
		stm_pad_release(fsm->pad_state);
	iounmap(fsm->base);
	release_resource(fsm->region);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver stm_spi_fsm_driver = {
	.probe		= stm_spi_fsm_probe,
	.remove		= stm_spi_fsm_remove,
	.driver		= {
		.name	= NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init stm_spi_fsm_init(void)
{
	return platform_driver_register(&stm_spi_fsm_driver);
}

static void __exit stm_spi_fsm_exit(void)
{
	platform_driver_unregister(&stm_spi_fsm_driver);
}

module_init(stm_spi_fsm_init);
module_exit(stm_spi_fsm_exit);

MODULE_AUTHOR("Angus Clark <Angus.Clark@st.com>");
MODULE_DESCRIPTION("STM SPI FSM driver");
MODULE_LICENSE("GPL");
