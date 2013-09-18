/*
 *  drivers/mtd/nandids.c
 *
 *  Copyright (C) 2002 Thomas Gleixner (tglx@linutronix.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/mtd/nand.h>
/*
*	Chip ID list
*
*	Name. ID code, pagesize, chipsize in MegaByte, eraseblock size,
*	options
*
*	Pagesize; 0, 256, 512
*	0	get this information from the extended chip ID
+	256	256 Byte page size
*	512	512 Byte page size
*/
struct nand_flash_dev nand_flash_ids[] = {

#ifdef CONFIG_MTD_NAND_MUSEUM_IDS
	{"NAND 1MiB 5V 8-bit",		0x6e, 256, 1, 0x1000, 0},
	{"NAND 2MiB 5V 8-bit",		0x64, 256, 2, 0x1000, 0},
	{"NAND 4MiB 5V 8-bit",		0x6b, 512, 4, 0x2000, 0},
	{"NAND 1MiB 3,3V 8-bit",	0xe8, 256, 1, 0x1000, 0},
	{"NAND 1MiB 3,3V 8-bit",	0xec, 256, 1, 0x1000, 0},
	{"NAND 2MiB 3,3V 8-bit",	0xea, 256, 2, 0x1000, 0},
	{"NAND 4MiB 3,3V 8-bit",	0xd5, 512, 4, 0x2000, 0},
	{"NAND 4MiB 3,3V 8-bit",	0xe3, 512, 4, 0x2000, 0},
	{"NAND 4MiB 3,3V 8-bit",	0xe5, 512, 4, 0x2000, 0},
	{"NAND 8MiB 3,3V 8-bit",	0xd6, 512, 8, 0x2000, 0},

	{"NAND 8MiB 1,8V 8-bit",	0x39, 512, 8, 0x2000, 0},
	{"NAND 8MiB 3,3V 8-bit",	0xe6, 512, 8, 0x2000, 0},
	{"NAND 8MiB 1,8V 16-bit",	0x49, 512, 8, 0x2000, NAND_BUSWIDTH_16},
	{"NAND 8MiB 3,3V 16-bit",	0x59, 512, 8, 0x2000, NAND_BUSWIDTH_16},
#endif

	{"NAND 16MiB 1,8V 8-bit",	0x33, 512, 16, 0x4000, 0},
	{"NAND 16MiB 3,3V 8-bit",	0x73, 512, 16, 0x4000, 0},
	{"NAND 16MiB 1,8V 16-bit",	0x43, 512, 16, 0x4000, NAND_BUSWIDTH_16},
	{"NAND 16MiB 3,3V 16-bit",	0x53, 512, 16, 0x4000, NAND_BUSWIDTH_16},

	{"NAND 32MiB 1,8V 8-bit",	0x35, 512, 32, 0x4000, 0},
	{"NAND 32MiB 3,3V 8-bit",	0x75, 512, 32, 0x4000, 0},
	{"NAND 32MiB 1,8V 16-bit",	0x45, 512, 32, 0x4000, NAND_BUSWIDTH_16},
	{"NAND 32MiB 3,3V 16-bit",	0x55, 512, 32, 0x4000, NAND_BUSWIDTH_16},

	{"NAND 64MiB 1,8V 8-bit",	0x36, 512, 64, 0x4000, 0},
	{"NAND 64MiB 3,3V 8-bit",	0x76, 512, 64, 0x4000, 0},
	{"NAND 64MiB 1,8V 16-bit",	0x46, 512, 64, 0x4000, NAND_BUSWIDTH_16},
	{"NAND 64MiB 3,3V 16-bit",	0x56, 512, 64, 0x4000, NAND_BUSWIDTH_16},

	{"NAND 128MiB 1,8V 8-bit",	0x78, 512, 128, 0x4000, 0},
	{"NAND 128MiB 1,8V 8-bit",	0x39, 512, 128, 0x4000, 0},
	{"NAND 128MiB 3,3V 8-bit",	0x79, 512, 128, 0x4000, 0},
	{"NAND 128MiB 1,8V 16-bit",	0x72, 512, 128, 0x4000, NAND_BUSWIDTH_16},
	{"NAND 128MiB 1,8V 16-bit",	0x49, 512, 128, 0x4000, NAND_BUSWIDTH_16},
	{"NAND 128MiB 3,3V 16-bit",	0x74, 512, 128, 0x4000, NAND_BUSWIDTH_16},
	{"NAND 128MiB 3,3V 16-bit",	0x59, 512, 128, 0x4000, NAND_BUSWIDTH_16},

	{"NAND 256MiB 3,3V 8-bit",	0x71, 512, 256, 0x4000, 0},

	/*
	 * These are the new chips with large page size. The pagesize and the
	 * erasesize is determined from the extended id bytes
	 */
#define LP_OPTIONS (NAND_SAMSUNG_LP_OPTIONS | NAND_NO_READRDY | NAND_NO_AUTOINCR)
#define LP_OPTIONS16 (LP_OPTIONS | NAND_BUSWIDTH_16)

	/* 512 Megabit */
	{"NAND 64MiB 1,8V 8-bit",	0xA2, 0,  64, 0, LP_OPTIONS},
	{"NAND 64MiB 1,8V 8-bit",	0xA0, 0,  64, 0, LP_OPTIONS},
	{"NAND 64MiB 3,3V 8-bit",	0xF2, 0,  64, 0, LP_OPTIONS},
	{"NAND 64MiB 3,3V 8-bit",	0xD0, 0,  64, 0, LP_OPTIONS},
	{"NAND 64MiB 3,3V 8-bit",	0xF0, 0,  64, 0, LP_OPTIONS},
	{"NAND 64MiB 1,8V 16-bit",	0xB2, 0,  64, 0, LP_OPTIONS16},
	{"NAND 64MiB 1,8V 16-bit",	0xB0, 0,  64, 0, LP_OPTIONS16},
	{"NAND 64MiB 3,3V 16-bit",	0xC2, 0,  64, 0, LP_OPTIONS16},
	{"NAND 64MiB 3,3V 16-bit",	0xC0, 0,  64, 0, LP_OPTIONS16},

	/* 1 Gigabit */
	{"NAND 128MiB 1,8V 8-bit",	0xA1, 0, 128, 0, LP_OPTIONS},
	{"NAND 128MiB 3,3V 8-bit",	0xF1, 0, 128, 0, LP_OPTIONS},
	{"NAND 128MiB 3,3V 8-bit",	0xD1, 0, 128, 0, LP_OPTIONS},
	{"NAND 128MiB 1,8V 16-bit",	0xB1, 0, 128, 0, LP_OPTIONS16},
	{"NAND 128MiB 3,3V 16-bit",	0xC1, 0, 128, 0, LP_OPTIONS16},
	{"NAND 128MiB 1,8V 16-bit",     0xAD, 0, 128, 0, LP_OPTIONS16},

	/* 2 Gigabit */
	{"NAND 256MiB 1,8V 8-bit",	0xAA, 0, 256, 0, LP_OPTIONS},
	{"NAND 256MiB 3,3V 8-bit",	0xDA, 0, 256, 0, LP_OPTIONS},
	{"NAND 256MiB 1,8V 16-bit",	0xBA, 0, 256, 0, LP_OPTIONS16},
	{"NAND 256MiB 3,3V 16-bit",	0xCA, 0, 256, 0, LP_OPTIONS16},

	/* 4 Gigabit */
	{"NAND 512MiB 1,8V 8-bit",	0xAC, 0, 512, 0, LP_OPTIONS},
	{"NAND 512MiB 3,3V 8-bit",	0xDC, 0, 512, 0, LP_OPTIONS},
	{"NAND 512MiB 1,8V 16-bit",	0xBC, 0, 512, 0, LP_OPTIONS16},
	{"NAND 512MiB 3,3V 16-bit",	0xCC, 0, 512, 0, LP_OPTIONS16},

	/* 8 Gigabit */
	{"NAND 1GiB 1,8V 8-bit",	0xA3, 0, 1024, 0, LP_OPTIONS},
	{"NAND 1GiB 3,3V 8-bit",	0xD3, 0, 1024, 0, LP_OPTIONS},
	{"NAND 1GiB 1,8V 16-bit",	0xB3, 0, 1024, 0, LP_OPTIONS16},
	{"NAND 1GiB 3,3V 16-bit",	0xC3, 0, 1024, 0, LP_OPTIONS16},

	/* 16 Gigabit */
	{"NAND 2GiB 1,8V 8-bit",	0xA5, 0, 2048, 0, LP_OPTIONS},
	{"NAND 2GiB 3,3V 8-bit",	0xD5, 0, 2048, 0, LP_OPTIONS},
	{"NAND 2GiB 1,8V 16-bit",	0xB5, 0, 2048, 0, LP_OPTIONS16},
	{"NAND 2GiB 3,3V 16-bit",	0xC5, 0, 2048, 0, LP_OPTIONS16},

	/* 32 Gigabit */
	{"NAND 4GiB 1,8V 8-bit",	0xA7, 0, 4096, 0, LP_OPTIONS},
	{"NAND 4GiB 3,3V 8-bit",	0xD7, 0, 4096, 0, LP_OPTIONS},
	{"NAND 4GiB 1,8V 16-bit",	0xB7, 0, 4096, 0, LP_OPTIONS16},
	{"NAND 4GiB 3,3V 16-bit",	0xC7, 0, 4096, 0, LP_OPTIONS16},

	/* 64 Gigabit */
	{"NAND 8GiB 1,8V 8-bit",	0xAE, 0, 8192, 0, LP_OPTIONS},
	{"NAND 8GiB 3,3V 8-bit",	0xDE, 0, 8192, 0, LP_OPTIONS},
	{"NAND 8GiB 1,8V 16-bit",	0xBE, 0, 8192, 0, LP_OPTIONS16},
	{"NAND 8GiB 3,3V 16-bit",	0xCE, 0, 8192, 0, LP_OPTIONS16},

	/* 128 Gigabit */
	{"NAND 16GiB 1,8V 8-bit",	0x1A, 0, 16384, 0, LP_OPTIONS},
	{"NAND 16GiB 3,3V 8-bit",	0x3A, 0, 16384, 0, LP_OPTIONS},
	{"NAND 16GiB 1,8V 16-bit",	0x2A, 0, 16384, 0, LP_OPTIONS16},
	{"NAND 16GiB 3,3V 16-bit",	0x4A, 0, 16384, 0, LP_OPTIONS16},

	/* 256 Gigabit */
	{"NAND 32GiB 1,8V 8-bit",	0x1C, 0, 32768, 0, LP_OPTIONS},
	{"NAND 32GiB 3,3V 8-bit",	0x3C, 0, 32768, 0, LP_OPTIONS},
	{"NAND 32GiB 1,8V 16-bit",	0x2C, 0, 32768, 0, LP_OPTIONS16},
	{"NAND 32GiB 3,3V 16-bit",	0x4C, 0, 32768, 0, LP_OPTIONS16},

	/* 512 Gigabit */
	{"NAND 64GiB 1,8V 8-bit",	0x1E, 0, 65536, 0, LP_OPTIONS},
	{"NAND 64GiB 3,3V 8-bit",	0x3E, 0, 65536, 0, LP_OPTIONS},
	{"NAND 64GiB 1,8V 16-bit",	0x2E, 0, 65536, 0, LP_OPTIONS16},
	{"NAND 64GiB 3,3V 16-bit",	0x4E, 0, 65536, 0, LP_OPTIONS16},

	/*
	 * Renesas AND 1 Gigabit. Those chips do not support extended id and
	 * have a strange page/block layout !  The chosen minimum erasesize is
	 * 4 * 2 * 2048 = 16384 Byte, as those chips have an array of 4 page
	 * planes 1 block = 2 pages, but due to plane arrangement the blocks
	 * 0-3 consists of page 0 + 4,1 + 5, 2 + 6, 3 + 7 Anyway JFFS2 would
	 * increase the eraseblock size so we chose a combined one which can be
	 * erased in one go There are more speed improvements for reads and
	 * writes possible, but not implemented now
	 */
	{"AND 128MiB 3,3V 8-bit",	0x01, 2048, 128, 0x4000,
	 NAND_IS_AND | NAND_NO_AUTOINCR |NAND_NO_READRDY | NAND_4PAGE_ARRAY |
	 BBT_AUTO_REFRESH
	},

	{NULL,}
};

/*
*	Manufacturer ID list
*/
struct nand_manufacturers nand_manuf_ids[] = {
	{NAND_MFR_TOSHIBA, "Toshiba"},
	{NAND_MFR_SAMSUNG, "Samsung"},
	{NAND_MFR_FUJITSU, "Fujitsu"},
	{NAND_MFR_NATIONAL, "National"},
	{NAND_MFR_RENESAS, "Renesas"},
	{NAND_MFR_STMICRO, "ST Micro"},
	{NAND_MFR_HYNIX, "Hynix"},
	{NAND_MFR_MICRON, "Micron"},
	{NAND_MFR_AMD, "AMD"},
	{NAND_MFR_MACRONIX, "Macronix"},
	{0x0, "Unknown"}
};

EXPORT_SYMBOL(nand_manuf_ids);
EXPORT_SYMBOL(nand_flash_ids);

/*
 *	Decode READID data
 */

static int nand_decode_id_2(struct mtd_info *mtd,
			    struct nand_chip *chip,
			    struct nand_flash_dev *type,
			    uint8_t *id, int id_len)
{
	mtd->writesize = type->pagesize;
	mtd->oobsize = type->pagesize / 32;
	chip->chipsize = ((uint64_t)type->chipsize) << 20;

	/* SPANSION/AMD (S30ML-P ORNAND) has non-standard block size */
	if (id[0] == NAND_MFR_AMD)
		mtd->erasesize = 512 * 1024;
	else
		mtd->erasesize = type->erasesize;

	/* Get chip options from table */
	chip->options &= ~NAND_CHIPOPTIONS_MSK;
	chip->options |= type->options & NAND_CHIPOPTIONS_MSK;
	chip->options |= NAND_NO_AUTOINCR;
	if (mtd->writesize > 512)
		chip->options |= NAND_NO_READRDY;

	/* Assume some defaults */
	chip->cellinfo = 0;
	chip->planes_per_chip = 1;
	chip->planes_per_chip = 1;
	chip->luns_per_chip = 1;

	return 0;
}

static int nand_decode_id_ext(struct mtd_info *mtd,
			      struct nand_chip *chip,
			      struct nand_flash_dev *type,
			      uint8_t *id, int id_len) {
	uint8_t data;

	if (id_len < 3 || id_len > 5) {
		pr_err("[MTD][NAND]: %s: invalid ID length [%d]\n",
		       __func__, id_len);
		return 1;
	}

	/* Clear chip options */
	chip->options &= ~NAND_CHIPOPTIONS_MSK;

	/* ID4: Planes/Chip Size */
	if (id[0] == NAND_MFR_HYNIX && id_len == 5 && id[4] == 0 &&
	    (id[1] == 0xDA || id[1] == 0xCA)) {
		/* Non-standard decode: HY27UF082G2A, HY27UF162G2A */
		chip->planes_per_chip = 2;
		chip->chipsize = (128 * 1024 * 1024) * chip->planes_per_chip;
	} else if (id[0] == NAND_MFR_HYNIX && id_len == 5 &&
		   id[1] == 0xD5 && id[4] == 0x44) {
		/* Non-standard decode: H27UAG8T2M */
		chip->planes_per_chip = 2;
		chip->chipsize = (1024UL * 1024 * 1024) * chip->planes_per_chip;
	} else if (id_len == 5) {
		/*   - Planes per chip: ID4[3:2] */
		data = (id[4] >> 2) & 0x3;
		chip->planes_per_chip = 1 << data;

		if (id[0] != NAND_MFR_TOSHIBA) {
			/*   - Plane size: ID4[6:4], multiples of 8MiB */
			data = (id[4] >> 4) & 0x7;
			chip->chipsize = (8 * 1024 * 1024) << data;
			chip->chipsize *= chip->planes_per_chip;
		} else {
			/* Toshiba ID4 does not give plane size: get chipsize
			 * from table */
			chip->chipsize = (((uint64_t)type->chipsize) << 20);
		}
	} else {
		/* Fall-back to table */
		chip->planes_per_chip = 1;
		chip->chipsize = (((uint64_t)type->chipsize) << 20);
	}

	/* ID3: Page/OOB/Block Size */
	if (id_len >= 4) {
		/*   - Page Size: ID3[1:0] */
		data = id[3] & 0x3;
		mtd->writesize = 1024 << data; /* multiples of 1k */

		/*   - OOB Size: ID3[2] */
		data = (id[3] >> 2) & 0x1;
		mtd->oobsize = 8 << data;		/* per 512 */
		mtd->oobsize *= mtd->writesize / 512;	/* per page */

		/* TC58NVG3S0F: non-standard OOB size! */
		if (id[0] == NAND_MFR_TOSHIBA && id[1] == 0xD3 &&
		    id[2] == 0x90 && id[3] == 0x26 && id[4] == 0x76)
			mtd->oobsize = 232;

		/*   - Block Size: ID3[5:4] */
		data = (id[3] >> 4) & 0x3;
		mtd->erasesize = (64 * 1024) << data; /* multiples of 64k */

		/*   - Bus Width; ID3[6] */
		if ((id[3] >> 6) & 0x1)
			chip->options |= NAND_BUSWIDTH_16;
	} else {
		/* Fall-back to table */
		mtd->writesize = type->pagesize;
		mtd->oobsize = type->pagesize / 32;
		if (type->options & NAND_BUSWIDTH_16)
			chip->options |= NAND_BUSWIDTH_16;
	}

	/* Some default 'chip' options */
	chip->options |= NAND_NO_AUTOINCR;
	if (chip->planes_per_chip > 1)
		chip->options |= NAND_MULTIPLANE_READ;

	if (mtd->writesize > 512)
		chip->options |= NAND_NO_READRDY;

	if (id[0] == NAND_MFR_SAMSUNG && mtd->writesize > 512)
		chip->options |= NAND_SAMSUNG_LP_OPTIONS;

	/* ID2: Package/Cell/Features */
	/*   Note, ID2 invalid, or documented as "don't care" on certain devices
	 *   (assume some defaults)
	 */
	if (id_len == 4 && id[0] == NAND_MFR_HYNIX &&
	    (id[1] == 0xF1 || id[1] == 0xC1 || id[1] == 0xA1 || id[1] == 0xAD ||
	     id[1] == 0xDA || id[1] == 0xCA)) {
		/* HY27{U,S}F{08,16}1G2M;
		 * HY27UF{08,16}2G2M
		 */
		chip->luns_per_chip = 1;
		chip->cellinfo = 0;
		chip->options |= (NAND_CACHEPRG |
				  NAND_CACHERD |
				  NAND_COPYBACK);
	} else if (id_len == 4 && id[0] == NAND_MFR_MICRON &&
		   (id[1] == 0xDA || id[1] == 0xCA || id[1] == 0xDC ||
		    id[1] == 0xCC || id[1] == 0xAA || id[1] == 0xBA)) {
		/* MT29F2G{08,16}AAB;
		 * MT29F4G{08,16}BAB;
		 * MT29F2G{08,16}A{A,B}C;
		 * MT29F4G08BAC
		 */
		chip->luns_per_chip = 1;
		chip->cellinfo = 0;
		chip->options |= (NAND_CACHEPRG |
				  NAND_CACHERD |
				  NAND_COPYBACK);
	} else if (id_len == 4 && id[0] == NAND_MFR_SAMSUNG &&
		   (id[1] == 0xF1 || id[1] == 0xA1)) {
		/* K9F1G08{U,Q}A */
		chip->luns_per_chip = 1;
		chip->cellinfo = 0;
		chip->options |= (NAND_CACHEPRG |
				  NAND_CACHERD |
				  NAND_COPYBACK);
	} else {
		/*   - LUNs: ID2[1:0] */
		data = id[2] & 0x3;
		chip->luns_per_chip = 0x1 << data;

		/*   - Interleave: ID2[6] */
		if ((id[2] >> 6) & 0x1)
			chip->options |= NAND_MULTILUN;

		/*   - Cache Program: ID2[7] */
		if ((id[2] >> 7) & 0x1)
			chip->options |= NAND_CACHEPRG;

		/*   - Copy to 'cellinfo' */
		chip->cellinfo = id[2];
	}

	return 0;
}

static int nand_decode_id_6(struct mtd_info *mtd,
			    struct nand_chip *chip,
			    struct nand_flash_dev *type,
			    uint8_t *id, int id_len) {
	uint8_t data;

	if (id_len != 6) {
		pr_err("[MTD][NAND]: %s: invalid ID length [%d]\n",
		       __func__, id_len);
		return 1;
	}

	chip->chipsize = (((uint64_t)type->chipsize) << 20);

	/* ID4: Planes */
	/*   - Number: ID4[3:2] */
	data = (id[4] >> 2) & 0x3;
	chip->planes_per_chip = 1 << data;

	/* ID3: Page/OOB/Block Size */
	/*   - Page Size:  ID3[1:0] */
	data = id[3] & 0x3;
	mtd->writesize = 2048 << data; /* multiples of 2k */

	/*   - OOB Size: ID3[6,3:2] */
	data = ((id[3] >> 4) & 0x4) | ((id[3] >> 2) & 0x3);
	if (id[0] == NAND_MFR_SAMSUNG) {
		switch (data) {
		case 1:
			mtd->oobsize = 128;
			break;
		case 2:
			mtd->oobsize = 218;
			break;
		case 3:
			mtd->oobsize = 400;
			break;
		case 4:
			mtd->oobsize = 436;
			break;
		case 5:
			mtd->oobsize = 640;
			break;
		default:
			pr_err("[MTD][NAND]: %s: unknown OOB size\n",
			       __func__);
			return 1;
			break;
		}
	} else {
		switch (data) {
		case 0:
			mtd->oobsize = 128;
			break;
		case 1:
			mtd->oobsize = 224;
			break;
		case 2:
			mtd->oobsize = 448;
			break;
		default:
			pr_err("[MTD][NAND]: %s: unknown OOB size\n",
			       __func__);
			break;
		}
	}

	/*   - Block Size: ID3[7,5:4] */
	data = ((id[3] >> 5) & 0x4) | ((id[3] >> 4) & 0x3);
	switch (data) {
	case 0:
	case 1:
	case 2:
		mtd->erasesize = (128 * 1024) << data;
		break;
	case 3:
		if (id[0] == NAND_MFR_SAMSUNG)
			mtd->erasesize = (1024 * 1024);
		else
			mtd->erasesize = (768 * 1024);
		break;
	case 4:
	case 5:
		mtd->erasesize = (1024 * 1024) << (data - 4);
		break;
	default:
		pr_err("[MTD][NAND]: %s: unknown block size\n",
		       __func__);
		return 1;
		break;
	}

	/* Some default 'chip' options */
	chip->options &= ~NAND_CHIPOPTIONS_MSK;
	chip->options |= NAND_NO_AUTOINCR;
	if (chip->planes_per_chip > 1)
		chip->options |= NAND_MULTIPLANE_READ;

	if (mtd->writesize > 512)
		chip->options |= NAND_NO_READRDY;

	if (id[0] == NAND_MFR_SAMSUNG && mtd->writesize > 512)
		chip->options |= NAND_SAMSUNG_LP_OPTIONS;

	/* ID2: Package/Cell/Features */
	/*   - LUNs: ID2[1:0] */
	data = id[2] & 0x3;
	chip->luns_per_chip = 0x1 << data;

	/*   - Interleave: ID2[6] */
	if ((id[2] >> 6) & 0x1)
		chip->options |= NAND_MULTILUN;

	/*   - Cache Program: ID2[7] */
	if ((id[2] >> 7) & 0x1)
		chip->options |= NAND_CACHEPRG;

	/*   - Copy to 'cellinfo' */
	chip->cellinfo = id[2];

	/* Bus Width, from table */
	chip->options |= (type->options & NAND_BUSWIDTH_16);

	return 0;
}


/*
 * Heuristics for manufacturer-programmed bad-block marker (BBM) schemes
 */
void nand_derive_bbm(struct mtd_info *mtd, struct nand_chip *chip, uint8_t *id)
{
	int bits_per_cell = ((chip->cellinfo >> 2) & 0x3) + 1;

	/*
	 * Some special cases first...
	 */

	/* Hynix HY27US1612{1,2}B: 3rd word for x16 device! */
	if (id[0] == NAND_MFR_HYNIX && id[1] == 0x56) {
		chip->bbm = (NAND_BBM_PAGE_0 |
			     NAND_BBM_PAGE_1 |
			     NAND_BBM_BYTE_OOB_5);
		goto set_bbt_options;
	}

	/* Hynix MLC VLP: last and last-2 pages, byte 0 */
	if (id[0] == NAND_MFR_HYNIX && bits_per_cell == 2 &&
	    mtd->writesize == 4096) {
		chip->bbm = (NAND_BBM_PAGE_LAST |
			     NAND_BBM_PAGE_LMIN2 |
			     NAND_BBM_BYTE_OOB_0);
		goto set_bbt_options;
	}

	/* Numonyx/ST 2K/4K pages, x8 bus use BOTH byte 0 and 5 (drivers may
	 * need to disable 'byte 5' depending on ECC layout)
	 */
	if (!(chip->options & NAND_BUSWIDTH_16) &&
	    mtd->writesize >= 2048 && id[0] == NAND_MFR_STMICRO) {
		chip->bbm =  (NAND_BBM_PAGE_0 |
			      NAND_BBM_BYTE_OOB_0 |
			      NAND_BBM_BYTE_OOB_5);
		goto set_bbt_options;
	}

	/* Samsung and Hynix MLC NAND: last page, byte 0; and 1st page for 8KiB
	 * page devices */
	if ((id[0] == NAND_MFR_SAMSUNG || id[0] == NAND_MFR_HYNIX) &&
	    bits_per_cell == 2) {
		chip->bbm = NAND_BBM_PAGE_LAST | NAND_BBM_BYTE_OOB_0;
		if (mtd->writesize == 8192)
			chip->bbm |= NAND_BBM_PAGE_0;
		goto set_bbt_options;
	}

	/* Micron 2KiB page devices use 1st and 2nd page, byte 0 */
	if (id[0] == NAND_MFR_MICRON && mtd->writesize == 2048) {
		chip->bbm = NAND_BBM_PAGE_0 | NAND_BBM_PAGE_1 |
			NAND_BBM_BYTE_OOB_0;
		goto set_bbt_options;
	}


	/*
	 * For the rest...
	 */

	/* Scan at least the first page */
	chip->bbm = NAND_BBM_PAGE_0;
	/* Also 2nd page for SLC Samsung, Hynix, Toshiba (LP), AMD/Spansion */
	if (bits_per_cell == 1 &&
	    (id[0] == NAND_MFR_SAMSUNG ||
	     id[0] == NAND_MFR_HYNIX ||
	     id[0] == NAND_MFR_AMD ||
	     (id[0] == NAND_MFR_TOSHIBA && mtd->writesize > 512)))
		chip->bbm |= NAND_BBM_PAGE_1;

	/* SP x8 devices use 6th byte OOB; everything else uses 1st byte OOB */
	if (mtd->writesize == 512 && !(chip->options & NAND_BUSWIDTH_16))
		chip->bbm |= NAND_BBM_BYTE_OOB_5;
	else
		chip->bbm |= NAND_BBM_BYTE_OOB_0;

 set_bbt_options:
	/* Set BBT chip->options, for backwards compatibility */
	if (chip->bbm & NAND_BBM_PAGE_ALL)
		chip->options |= NAND_BBT_SCANALLPAGES;

	if (chip->bbm & NAND_BBM_PAGE_1)
		chip->options |= NAND_BBT_SCAN2NDPAGE;

	/* Set the bad block position */
	if (mtd->writesize > 512 || (chip->options & NAND_BUSWIDTH_16))
		chip->badblockpos = NAND_LARGE_BADBLOCK_POS;
	else
		chip->badblockpos = NAND_SMALL_BADBLOCK_POS;

	return;
}
EXPORT_SYMBOL(nand_derive_bbm);

/*
 * Find the length of the 'READID' string.  It is assumed that the length can be
 * determined by looking for repeated sequences, or that the device returns
 * 0x00's after the string has been returned.
 */
static int nand_get_id_len(uint8_t *id, int max_id_len)
{
	int i, len;

	/* Determine signature length by looking for repeats */
	for (len = 2; len < max_id_len; len++) {
		for (i = len; i < max_id_len; i++)
			if (id[i] != id[i % len])
				break;

		if (i == max_id_len)
			break;
	}

	/* No repeats found, look for trailing 0x00s */
	if (len == max_id_len) {
		while (len > 2 && id[len - 1] == 0x00)
			len--;
	}

	/*
	 * Some Toshiba devices return additional, undocumented, READID bytes
	 * (e.g. TC58NVG3S0F).  Cap ID length to 5 bytes.
	 */
	if (id[0] == NAND_MFR_TOSHIBA && len > 5)
		len = 5;

	/*
	 * Some Samsung devices return 'NAND_MFR_SAMSUNG' as a 6th READID
	 * byte. (e.g. K9F4G08U0D). Use ID length of 5 bytes.
	 */
	if (id[0] == NAND_MFR_SAMSUNG && len == 6 &&
	    id[5] == NAND_MFR_SAMSUNG && id[6] == NAND_MFR_SAMSUNG)
		len = 5;

	return len;
}

/*
 * Determine device properties by decoding the 'READID' string
 */
int nand_decode_id(struct mtd_info *mtd,
		   struct nand_chip *chip,
		   struct nand_flash_dev *type,
		   uint8_t *id, int max_id_len)
{
	int id_len;
	int ret;

	id_len = nand_get_id_len(id, max_id_len);
	if (id_len == 0) {
		pr_err("[MTD][NAND]: %s: failed to read device ID\n",
		       __func__);
		return 1;
	}

	/*
	 * Decode ID string
	 */
	if (id_len == 2 || type->pagesize)
		ret = nand_decode_id_2(mtd, chip, type, id, id_len);
	else if (id_len <= 5)
		ret = nand_decode_id_ext(mtd, chip, type, id, id_len);
	else if (id_len == 6)
		ret = nand_decode_id_6(mtd, chip, type, id, id_len);
	else
		ret = 1;

	if (ret) {
		pr_err("[MTD][NAND]: %s: failed to decode NAND "
		       "device ID\n", __func__);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(nand_decode_id);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Gleixner <tglx@linutronix.de>");
MODULE_DESCRIPTION("Nand device & manufacturer IDs");
