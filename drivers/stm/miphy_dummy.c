#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/stm/pio.h>
#include <linux/stm/platform.h>
#include <linux/stm/sysconf.h>
#include <linux/stm/miphy.h>
#include "miphy.h"

#define NAME		"stm-miphy-dummy"

static struct stm_miphy_device miphy_dummy_dev;

static int miphydummy_start(struct stm_miphy *miphy)
{
	return 0;
}

static struct stm_miphy_style_ops miphydummy_ops = {
	.miphy_start 		= miphydummy_start,
};

static int miphydummy_probe(struct stm_miphy *miphy)
{
	pr_info("MiPHY driver style %s probed successfully\n",
		ID_MIPHYDUMMY);
	return 0;
}

static int miphydummy_remove(void)
{
	return 0;
}

static struct stm_miphy_style miphydummy_style = {
	.style_id	= ID_MIPHYDUMMY,
	.probe		= miphydummy_probe,
	.remove		= miphydummy_remove,
	.miphy_ops	= &miphydummy_ops,
};

static u8 stm_miphy_dummy_reg_read(int port, u8 addr)
{
	return 0;
}

static void stm_miphy_dummy_reg_write(int port, u8 addr, u8 data)
{
}

static int stm_miphy_dummy_probe(struct platform_device *pdev)
{
	struct stm_plat_miphy_dummy_data *data = pdev->dev.platform_data;
	int result;
	miphy_dummy_dev.type = DUMMY_IF;
	miphy_dummy_dev.miphy_first = data->miphy_first;
	miphy_dummy_dev.miphy_count = data->miphy_count;
	miphy_dummy_dev.modes = data->miphy_modes;
	miphy_dummy_dev.parent = &pdev->dev;
	miphy_dummy_dev.reg_write = stm_miphy_dummy_reg_write,
	miphy_dummy_dev.reg_read = stm_miphy_dummy_reg_read,
	miphy_dummy_dev.style_id = ID_MIPHYDUMMY;

	result = miphy_register_device(&miphy_dummy_dev);
	if (result) {
		printk(KERN_ERR "Unable to Register DUMMY MiPHY device\n");
		return result;
	}

	result = miphy_register_style(&miphydummy_style);
	if (result)
		pr_err("MiPHY driver style %s register failed (%d)",
				ID_MIPHYDUMMY, result);

	return result;
}

static int stm_miphy_dummy_remove(struct platform_device *pdev)
{
	miphy_unregister_device(&miphy_dummy_dev);

	return 0;
}

static struct platform_driver stm_miphy_dummy_plat_driver = {
	.driver.name = NAME,
	.driver.owner = THIS_MODULE,
	.probe = stm_miphy_dummy_probe,
	.remove = stm_miphy_dummy_remove,
};

static int __init stm_miphy_dummy_init(void)
{
	return platform_driver_register(&stm_miphy_dummy_plat_driver);
}
postcore_initcall(stm_miphy_dummy_init);

MODULE_AUTHOR("STMicroelectronics @st.com");
MODULE_DESCRIPTION("STM MiPHY DUMMY driver");
MODULE_LICENSE("GPL");
