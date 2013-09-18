/*
 * include/linux/mtd/stm_emi_wp_map.h: STMicroelectronics EMI 'Write-Protect'
 * Mapping
 *
 * Copyright (C) 2011 STMicroelectronics Limited
 *
 * May be copied or modified under the terms of the GNU General Public License.
 * See linux/COPYING for more information.
 */

#ifndef STM_EMI_WP_MAP_H
#define STM_EMI_WP_MAP_H

#include <linux/mtd/map.h>

void stm_emi_map_init(struct map_info *map);

#endif /* STM_EMI_WP_MAP_H */
