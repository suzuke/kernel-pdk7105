/*
 * (c) 2010 STMicroelectronics Limited
 *
 * Author: Pawel Moll <pawel.moll@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct stm_miphy_style_ops {
	int (*miphy_start)(struct stm_miphy *miphy);
	void (*miphy_assert_deserializer)(struct stm_miphy *miphy, int assert);
	int (*miphy_sata_status)(struct stm_miphy *miphy);
};

struct stm_miphy_style {
	char *style_id;
	int (*probe)(struct stm_miphy *miphy);
	int (*remove)(void);
	struct list_head node;
	struct stm_miphy_style_ops *miphy_ops;
};

struct stm_miphy_device {
	struct mutex mutex;
	struct stm_miphy *dev;
	struct device *parent;
	int miphy_first;
	int miphy_count;
	int tx_pol_inv;
	int rx_pol_inv;
	char *style_id;
	enum miphy_if_type type;
	enum miphy_mode *modes;
	void (*reg_write)(int port, u8 addr, u8 data);
	u8 (*reg_read)(int port, u8 addr);
};

struct stm_miphy {
	struct stm_miphy_device *dev;
	struct stm_miphy_style *style;
	int port;
	enum miphy_mode mode;
	struct list_head node;
	struct device *owner;
	struct device *device;
};

/* MiPHY style registration for diff versions of MiPHY */
int miphy_register_style(struct stm_miphy_style *drv);

int miphy_unregister_style(struct stm_miphy_style *drv);

/* MiPHY register Device with Read/Write interfaces and device info*/
int miphy_register_device(struct stm_miphy_device *dev);

/* MiPHY unregister Device with dev info */
int miphy_unregister_device(struct stm_miphy_device *dev);
