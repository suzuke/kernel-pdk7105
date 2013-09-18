/*
 * drivers/mtd/maps/stm_emi_wp_map.c: STMicroelectronics EMI 'Write-Protect'
 * Mapping
 *
 * Mapping functions based on drivers/mtd/maps/map_funcs.c, modified to enable
 * the EMI 'write-CS' only when a write operation is requested by the mapping
 * driver.  This limits the window in which a rogue initiator can write to the
 * EMI bank.
 *
 * Copyright (C) 2011 STMicroelectronics Limited
 *
 * May be copied or modified under the terms of the GNU General Public License.
 * See linux/COPYING for more information.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/map.h>
#include <linux/mtd/xip.h>
#include <linux/stm/emi.h>

static map_word __xipram emi_map_read(struct map_info *map, unsigned long ofs)
{
	return inline_map_read(map, ofs);
}

static void __xipram emi_map_write(struct map_info *map, const map_word datum,
				   unsigned long ofs)
{
	emi_bank_write_cs_enable(map->map_priv_1, 1);
	inline_map_write(map, datum, ofs);
	emi_bank_write_cs_enable(map->map_priv_1, 0);
}

static void __xipram emi_map_copy_from(struct map_info *map, void *to,
				       unsigned long from, ssize_t len)
{
	inline_map_copy_from(map, to, from, len);
}

static void __xipram emi_map_copy_to(struct map_info *map, unsigned long to,
				     const void *from, ssize_t len)
{
	emi_bank_write_cs_enable(map->map_priv_1, 1);
	inline_map_copy_to(map, to, from, len);
	emi_bank_write_cs_enable(map->map_priv_1, 0);
}

void stm_emi_map_init(struct map_info *map)
{
	int bank;

	BUG_ON(!map_bankwidth_supported(map->bankwidth));

	/* Find the EMI bank number over which the map is defined (assumes a
	 * single bank per device).
	 */
	for (bank = 0; bank < EMI_BANKS; bank++) {
		if (emi_bank_base(bank) == map->phys) {
			map->map_priv_1 = bank;
			break;
		}
	}
	BUG_ON(bank == EMI_BANKS);

	emi_bank_write_cs_enable(map->map_priv_1, 0);

	map->read = emi_map_read;
	map->write = emi_map_write;
	map->copy_from = emi_map_copy_from;
	map->copy_to = emi_map_copy_to;
}
EXPORT_SYMBOL(stm_emi_map_init);

MODULE_LICENSE("GPL");
