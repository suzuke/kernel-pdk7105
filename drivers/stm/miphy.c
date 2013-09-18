/*
 * STMicroelectronics MiPHY driver
 *
 * Copyright (C) 2012 STMicroelectronics Limited
 * Author: Srinivas Kandagatla <srinivas.kandagatla@st.com>
 * Author: Pawel Moll <pawel.moll@st.com>
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
#include <linux/stm/miphy.h>
#include "miphy.h"


#define MIPHY_VERSION			0xfb
#define MIPHY_REVISION			0xfc

static struct class *miphy_class;
static DEFINE_MUTEX(miphy_list_mutex);
static LIST_HEAD(miphy_list);
static LIST_HEAD(miphy_style_list);


/****************************************************************************
 * 	MiPHY Generic functions available for other drivers
 */

static void stm_miphy_write(struct stm_miphy *miphy, u8 addr, u8 data)
{
	struct stm_miphy_device *dev = miphy->dev;
	int port = miphy->port;

	dev->reg_write(port, addr, data);
}

static u8 stm_miphy_read(struct stm_miphy *miphy, u8 addr)
{
	struct stm_miphy_device *dev = miphy->dev;
	int port = miphy->port;

	return dev->reg_read(port, addr);
}

int stm_miphy_sata_status(struct stm_miphy *miphy)
{
	struct stm_miphy_device *dev = miphy->dev;
	struct stm_miphy_style_ops *miphy_ops = miphy->style->miphy_ops;
	int rval = 0;

	mutex_lock(&dev->mutex);
	if (miphy_ops->miphy_sata_status)
		rval = miphy_ops->miphy_sata_status(miphy);
	mutex_unlock(&dev->mutex);

	return rval;
}
EXPORT_SYMBOL(stm_miphy_sata_status);

void stm_miphy_assert_deserializer(struct stm_miphy *miphy, int assert)
{
	struct stm_miphy_device *dev = miphy->dev;
	struct stm_miphy_style_ops *miphy_ops = miphy->style->miphy_ops;

	mutex_lock(&dev->mutex);
	if (miphy_ops->miphy_assert_deserializer)
		miphy_ops->miphy_assert_deserializer(miphy, assert);
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL(stm_miphy_assert_deserializer);

int stm_miphy_start(struct stm_miphy *miphy)
{
	struct stm_miphy_device *dev = miphy->dev;
	struct stm_miphy_style_ops *miphy_ops = miphy->style->miphy_ops;
	int rval = -ENODEV;

	mutex_lock(&dev->mutex);
	if (miphy_ops->miphy_start)
		rval = miphy_ops->miphy_start(miphy);
	mutex_unlock(&dev->mutex);

	return rval;
}
EXPORT_SYMBOL(stm_miphy_start);

static struct stm_miphy_style *stm_miphy_style_find(char *style_id)
{
	struct stm_miphy_style *iterator;
	struct stm_miphy_style *style = NULL;

	list_for_each_entry(iterator, &miphy_style_list, node)
		if (strcmp(style_id, iterator->style_id) == 0) {
			style = iterator;
			break;
		}

	return style;
}

struct stm_miphy *stm_miphy_claim(int port, enum miphy_mode mode,
	struct device *owner)
{
	struct stm_miphy *iterator;
	struct stm_miphy *miphy = NULL;
	struct stm_miphy_style *style;
	u8 miphy_version, miphy_revision;

	if (!owner)
		return NULL;

	mutex_lock(&miphy_list_mutex);

	list_for_each_entry(iterator, &miphy_list, node)
		if (iterator->port == port) {
			miphy = iterator;
			break;
		}

	if (!miphy) {		/* Not found */
		pr_warning("MiPhy %d not available\n", port);
		goto _on_error;
	}
	if (miphy->owner) {	/* already claimed */
		pr_err("MiPhy %d already claimed by %s\n",
			port, dev_name(miphy->owner));
		miphy = NULL;
		goto _on_error;
	}
	if (miphy->mode != mode) {
		pr_err("MiPHY %d is not in the correct mode\n", port);
		miphy = NULL;
		goto _on_error;
	}

	style = stm_miphy_style_find(miphy->dev->style_id);
	if (!style) {
		pr_err("No style for MiPHY %d found \n", miphy->port);
		goto _on_error;
	}
	miphy->style = style;
	miphy->owner = owner;

	if (style->probe(miphy)) {
		pr_err("Unable to probe MiPHY %d style \n", miphy->port);
		goto _on_error;
	}

	miphy_version = stm_miphy_read(miphy, MIPHY_VERSION);
	miphy_revision = stm_miphy_read(miphy, MIPHY_REVISION);
	pr_info("%s, c%d.%d Claimed by %s \n",
			miphy->dev->style_id, (miphy_version & 0xf),
			miphy_revision, dev_name(miphy->owner));

	stm_miphy_start(miphy);

_on_error:
	mutex_unlock(&miphy_list_mutex);
	return miphy;
}
EXPORT_SYMBOL(stm_miphy_claim);

void stm_miphy_release(struct stm_miphy *miphy)
{
	/* FIXME */
}
EXPORT_SYMBOL(stm_miphy_release);

void stm_miphy_freeze(struct stm_miphy *miphy)
{
	/* Nothing to do */
}
EXPORT_SYMBOL(stm_miphy_freeze);

void stm_miphy_thaw(struct stm_miphy *miphy)
{
	stm_miphy_start(miphy);
}
EXPORT_SYMBOL(stm_miphy_thaw);

/****************************************************************************
 *      MiPHY style/device management
 */

int miphy_register_style(struct stm_miphy_style *style)
{
	list_add(&style->node, &miphy_style_list);

	return 0;
}
int miphy_unregister_style(struct stm_miphy_style *style)
{
	list_del(&style->node);

	return 0;
}

int miphy_register_device(struct stm_miphy_device *miphy_dev)
{
	int miphy_first = miphy_dev->miphy_first;
	int miphy_count = miphy_dev->miphy_count;
	struct stm_miphy *miphy, *new_miphys;
	int miphy_num;

	if (!miphy_class) {
		miphy_class = class_create(THIS_MODULE, "stm-miphy");
		if (IS_ERR(miphy_class)) {
			printk(KERN_ERR "stm-miphy: can't register class\n");
			return PTR_ERR(miphy_class);
		}
	}

	new_miphys = kzalloc(sizeof(*miphy) * miphy_count, GFP_KERNEL);
	if (!new_miphys)
		return -ENOMEM;

	for (miphy_num = miphy_first, miphy = new_miphys;
	     miphy_num < miphy_first + miphy_count;
	     miphy_num++, miphy++) {
		miphy->dev = miphy_dev;
		miphy->port = miphy_num;
		miphy->mode = *miphy_dev->modes++;

		mutex_lock(&miphy_list_mutex);
		list_add(&miphy->node, &miphy_list);
		mutex_unlock(&miphy_list_mutex);

		miphy->device = device_create(miphy_class, miphy_dev->parent, 0,
					miphy, "stm-miphy%d", miphy_num);
		if (IS_ERR(miphy->device))
			return PTR_ERR(miphy->device);
	}

	mutex_init(&miphy_dev->mutex);

	return 0;
}

int miphy_unregister_device(struct stm_miphy_device *miphy_dev)
{
	struct stm_miphy_style *style = stm_miphy_style_find(miphy_dev->style_id);

	if (style)
		style->remove();

	return 0;
}
