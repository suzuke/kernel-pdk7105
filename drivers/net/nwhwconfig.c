/*
 * Configuration of network device hardware from the kernel command line.
 *
 * Official documentation available at:
 *   http://www.stlinux.com/howto/network/ethernet-MAC
 *
 * Copyright (c) STMicroelectronics Limited
 *
 * Author: Stuart Menefy <stuart.menefy@st.com>
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 */

#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <net/ip.h>

static struct eth_dev {
	char dev_name[IFNAMSIZ];
	char hw_addr[18];
	int speed;
	int duplex;
} nwhwdev[NETDEV_BOOT_SETUP_MAX];

static int parse_addr(char *str, struct sockaddr *addr)
{
	char *s;
	char *mac = addr->sa_data;

	while ((s = strsep(&str, ":")) != NULL) {
		unsigned byte;
		if (sscanf(s, "%x", &byte) != 1 || byte > 0xff)
			return -1;
		*mac++ = byte;
	}
	addr->sa_family = ARPHRD_ETHER;
	return 0;
}

/**
 * nwhw_config
 * @dev : net device pointer
 * Description: it sets the MAC address.
 * Note that if the network device driver already uses a right
 * address this function doesn't replace any value.
 */
static int __init nwhw_config(void)
{
	struct net_device *dev;
	struct sockaddr s_addr;
	int ndev = 0;

	while ((ndev < NETDEV_BOOT_SETUP_MAX) &&
	       (dev = __dev_get_by_name(&init_net, nwhwdev[ndev].dev_name))) {

		if (!dev)
			break;

		if (!is_valid_ether_addr(dev->dev_addr)) {

			if (nwhwdev[ndev].hw_addr[0]) {
				int valid_ether =
				    parse_addr(nwhwdev[ndev].hw_addr, &s_addr);
				if (!valid_ether) {
					rtnl_lock();
					if (dev_set_mac_address(dev, &s_addr))
						pr_err("%s: Error: not set MAC"
						       " addr", __func__);
					rtnl_unlock();
					goto hw_mac_done;
				} else
					pr_err("%s: Error: Invalid MAC addr",
					       __func__);
			}
			/* Although many drivers do that in case of
			 * problems, we assume the nwhw_config always
			 * has to exit with a good MAC address (even if
			 * generated randomly). */
			random_ether_addr(dev->dev_addr);
			pr_warning("%s: generating random addr...", __func__);
		}
hw_mac_done:
		pr_info("%s: (%s) setting mac address: %pM", __func__,
			dev->name, dev->dev_addr);

		if ((nwhwdev[ndev].speed != -1) ||
		    (nwhwdev[ndev].duplex != -1)) {
			struct ethtool_cmd cmd = { ETHTOOL_GSET };

			if (!dev->ethtool_ops->get_settings ||
			    (dev->ethtool_ops->get_settings(dev, &cmd) < 0))
				pr_err("%s: cannot read ether device settings",
				       __func__);
			else {
				cmd.cmd = ETHTOOL_SSET;
				cmd.autoneg = AUTONEG_DISABLE;
				if (nwhwdev[ndev].speed != -1)
					cmd.speed = nwhwdev[ndev].speed;
				if (nwhwdev[ndev].duplex != -1)
					cmd.duplex = nwhwdev[ndev].duplex;
				if (!dev->ethtool_ops->set_settings ||
				    (dev->ethtool_ops->set_settings(dev, &cmd) <
				     0))
					pr_err("%s: cannot setting the eth dev",
					       __func__);
			}
		}
		ndev++;
	}
	return 0;
}

device_initcall(nwhw_config);

/**
 * nwhw_config_setup - parse the nwhwconfig parameters
 * @str : pointer to the nwhwconfig parameter
 * Description:
 * This function parses the nwhwconfig command line argumets.
 * Command line syntax:
 * nwhwconf=device:eth0,hwaddr:<mac0>[,speed:<speed0>][,duplex:<duplex0>];
 *	    device:eth1,hwaddr:<mac1>[,speed:<speed1>][,duplex:<duplex1>];
 *	...
 */
static int __init nwhw_config_setup(char *str)
{
	char *opt;
	int j = 0;

	if (!str || !*str)
		return 0;

	while (((opt = strsep(&str, ";")) != NULL)
	       && (j < NETDEV_BOOT_SETUP_MAX)) {
		char *p;

		nwhwdev[j].speed = -1;
		nwhwdev[j].duplex = -1;

		while ((p = strsep(&opt, ",")) != NULL) {
			if (!strncmp(p, "device:", 7))
				strlcpy(nwhwdev[j].dev_name, p + 7,
					sizeof(nwhwdev[j].dev_name));
			else if (!strncmp(p, "hwaddr:", 7))
				strlcpy(nwhwdev[j].hw_addr, p + 7,
					sizeof(nwhwdev[j].hw_addr));
			else if (!strcmp(p, "duplex:full"))
				nwhwdev[j].duplex = DUPLEX_FULL;

			else if (!strcmp(p, "duplex:half"))
				nwhwdev[j].duplex = DUPLEX_HALF;

			else if (!strncmp(p, "speed:", 6)) {
				int speed;

				if (!(strict_strtoul(p + 6, 0,
						     (unsigned long *)&speed)))
					if ((speed == SPEED_10) ||
					    (speed == SPEED_100) ||
					    (speed == SPEED_1000) ||
					    (speed == SPEED_10000))
						nwhwdev[j].speed = speed;
			}

		}
		j++;
	}
	return 1;
}

__setup("nwhwconf=", nwhw_config_setup);
