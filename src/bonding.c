/*
 * Routines for handling bonding devices
 *
 * Copyright (C) 2009-2012 Olaf Kirch <okir@suse.de>
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <net/if_arp.h>
#include <arpa/inet.h>

#include <wicked/netinfo.h>
#include <wicked/bridge.h>
#include <wicked/bonding.h>
#include "netinfo_priv.h"
#include "sysfs.h"

/*
 * Create a bonding config
 */
ni_bonding_t *
ni_bonding_new(void)
{
	ni_bonding_t *bonding;

	bonding = calloc(1, sizeof(ni_bonding_t));
	bonding->mode = NI_BOND_MODE_BALANCE_RR;
	bonding->monitoring = NI_BOND_MONITOR_ARP;
	bonding->arpmon.interval = 2000;
	bonding->arpmon.validate = NI_BOND_VALIDATE_ACTIVE;

	return bonding;
}

/*
 * Add a slave device to the bond
 */
void
ni_bonding_add_slave(ni_bonding_t *bonding, const char *ifname)
{
	ni_string_array_append(&bonding->slave_names, ifname);
}

/*
 * Reinitialize the bonding configuration
 *
 * - The default bonding mode is balance-rr
 * - The default monitoring mode is ARP (unless miimon=... is set)
 */
static void
ni_bonding_clear(ni_bonding_t *bonding)
{
	bonding->mode = NI_BOND_MODE_BALANCE_RR;
	bonding->monitoring = NI_BOND_MONITOR_ARP;

	ni_string_free(&bonding->primary);

	ni_string_array_destroy(&bonding->slave_names);
	ni_string_array_destroy(&bonding->arpmon.targets);
}

/*
 * Check whether the given bonding settings are valid
 */
const char *
ni_bonding_validate(const ni_bonding_t *bonding)
{
	switch (bonding->mode) {
	case NI_BOND_MODE_BALANCE_RR:
	case NI_BOND_MODE_ACTIVE_BACKUP:
	case NI_BOND_MODE_BALANCE_XOR:
	case NI_BOND_MODE_BROADCAST:
	case NI_BOND_MODE_802_3AD:
	case NI_BOND_MODE_BALANCE_TLB:
	case NI_BOND_MODE_BALANCE_ALB:
		break;

	default:
		return "unsupported bonding mode";
	}

	switch (bonding->monitoring) {
	case NI_BOND_MONITOR_ARP:
		if (bonding->arpmon.interval == 0)
			return "invalid arpmon interval";

		switch (bonding->arpmon.validate) {
		case NI_BOND_VALIDATE_NONE:
		case NI_BOND_VALIDATE_ACTIVE:
		case NI_BOND_VALIDATE_BACKUP:
		case NI_BOND_VALIDATE_ALL:
			break;

		default:
			return "invalid arpmon validate setting";
		}

		if (bonding->arpmon.targets.count == 0)
			return "no targets for arp monitoring";
		break;

	case NI_BOND_MONITOR_MII:
		/* FIXME: validate frequency, updelay, downdelay */
		switch (bonding->miimon.carrier_detect) {
		case NI_BOND_CARRIER_DETECT_IOCTL:
		case NI_BOND_CARRIER_DETECT_NETIF:
			break;

		default:
			return "invalid miimon carrier detect setting";

		}
		break;

	default:
		return "unsupported monitoring mode";
	}

	return NULL;
}

/*
 * Free bonding configuration
 */
void
ni_bonding_free(ni_bonding_t *bonding)
{
	ni_bonding_clear(bonding);
	free(bonding->module_opts);
	free(bonding);
}

/*
 * Set the bonding mode, using the strings supported by the
 * module options
 */
static ni_intmap_t	__kernel_bonding_mode_names[] = {
	{ "balance-rr",		NI_BOND_MODE_BALANCE_RR },
	{ "active-backup",	NI_BOND_MODE_ACTIVE_BACKUP },
	{ "balance-xor",	NI_BOND_MODE_BALANCE_XOR },
	{ "broadcast",		NI_BOND_MODE_BROADCAST },
	{ "802.3ad",		NI_BOND_MODE_802_3AD },
	{ "balance-tlb",	NI_BOND_MODE_BALANCE_TLB },
	{ "balance-alb",	NI_BOND_MODE_BALANCE_ALB },
	{ NULL }
};

int
__ni_bonding_set_module_option_mode(ni_bonding_t *bonding, char *value)
{
	/* When we parse /sys/net/class/<ifname>/bonding/mode, we end up
	 * with "balance-rr 0" or similar; strip off the int value */
	value[strcspn(value, " \t\n")] = '\0';
	return ni_parse_int_mapped(value, __kernel_bonding_mode_names, &bonding->mode);
}

const char *
__ni_bonding_get_module_option_mode(const ni_bonding_t *bonding)
{
	return ni_format_int_mapped(bonding->mode, __kernel_bonding_mode_names);
}

/*
 * For now, the enum names in the xml schema use almost the same mode names as
 * the kernel. 802.3ad being the notable exception, as starts with a digit, which
 * is illegal in xml element names.
 */
static ni_intmap_t	__user_bonding_mode_names[] = {
	{ "balance-rr",		NI_BOND_MODE_BALANCE_RR },
	{ "active-backup",	NI_BOND_MODE_ACTIVE_BACKUP },
	{ "balance-xor",	NI_BOND_MODE_BALANCE_XOR },
	{ "broadcast",		NI_BOND_MODE_BROADCAST },
	{ "ieee802-3ad",	NI_BOND_MODE_802_3AD },
	{ "balance-tlb",	NI_BOND_MODE_BALANCE_TLB },
	{ "balance-alb",	NI_BOND_MODE_BALANCE_ALB },
	{ NULL }
};

const char *
ni_bonding_mode_type_to_name(unsigned int mode)
{
	return ni_format_int_mapped(mode, __user_bonding_mode_names);
}

int
ni_bonding_mode_name_to_type(const char *name)
{
	unsigned int value;

	if (ni_parse_int_mapped(name, __user_bonding_mode_names, &value) < 0)
		return -1;
	return value;
}

/*
 * Set the validation mode of ARP probes.
 */
static ni_intmap_t	__arp_validate[] = {
	{ "none",		NI_BOND_VALIDATE_NONE },
	{ "active",		NI_BOND_VALIDATE_ACTIVE },
	{ "backup",		NI_BOND_VALIDATE_BACKUP },
	{ "all",		NI_BOND_VALIDATE_ALL },
	{ NULL }
};

int
__ni_bonding_set_module_option_arp_validate(ni_bonding_t *bonding, char *value)
{
	/* When we parse /sys/net/class/<ifname>/bonding/arp_validate, we end up
	 * with "none 0" or similar; strip off the int value */
	value[strcspn(value, " \t\n")] = '\0';
	return ni_parse_int_mapped(value, __arp_validate, &bonding->arpmon.validate);
}

const char *
__ni_bonding_get_module_option_arp_validate(const ni_bonding_t *bonding)
{
	return ni_format_int_mapped(bonding->arpmon.validate, __arp_validate);
}

/*
 * For now, the enum names in the xml schema use the same arp-valiate names as
 * the kernel.
 */
const char *
ni_bonding_validate_type_to_name(unsigned int value)
{
	return ni_format_int_mapped(value, __arp_validate);
}

int
ni_bonding_validate_name_to_type(const char *name)
{
	unsigned int value;

	if (ni_parse_int_mapped(name, __arp_validate, &value) < 0)
		return -1;
	return value;
}

/*
 * Set one bonding module option/attribute
 */
static int
ni_bonding_parse_module_attribute(ni_bonding_t *bonding, const char *attr, char *value)
{
	if (!strcmp(attr, "mode")) {
		if (__ni_bonding_set_module_option_mode(bonding, value) < 0)
			return -1;
	} else if (!strcmp(attr, "miimon")) {
		if (ni_parse_int(value, &bonding->miimon.frequency) < 0)
			return -1;
		if (bonding->miimon.frequency != 0)
			bonding->monitoring = NI_BOND_MONITOR_MII;
		else
			bonding->monitoring = NI_BOND_MONITOR_ARP;
	} else if (!strcmp(attr, "updelay")) {
		if (ni_parse_int(value, &bonding->miimon.updelay) < 0)
			return -1;
	} else if (!strcmp(attr, "downdelay")) {
		if (ni_parse_int(value, &bonding->miimon.downdelay) < 0)
			return -1;
	} else if (!strcmp(attr, "use_carrier")) {
		if (ni_parse_int(value, &bonding->miimon.carrier_detect) < 0)
			return -1;
	} else if (!strcmp(attr, "arp_validate")) {
		if (__ni_bonding_set_module_option_arp_validate(bonding, value) < 0)
			return -1;
	} else if (!strcmp(attr, "arp_interval")) {
		if (ni_parse_int(value, &bonding->arpmon.interval) < 0)
			return -1;
	} else if (!strcmp(attr, "arp_ip_target")) {
		char *s, *saveptr = NULL;

		for (s = strtok_r(value, ",", &saveptr); s; s = strtok_r(NULL, ",", &saveptr)) {
			struct in_addr dummy;

			if (inet_aton(value, &dummy) == 0)
				return -1;
			ni_string_array_append(&bonding->arpmon.targets, s);
		}
	} else if (!strcmp(attr, "primary")) {
		ni_string_dup(&bonding->primary, value);
	} else {
		return -2;
	}

	/* FIXME: Support xmit_hash_policy
	   	"layer2"
		"layer3+4"
		"layer2+3"
	 */

	return 0;
}

/*
 * Get one bonding module option/attribute
 */
static int
ni_bonding_format_module_attribute(const ni_bonding_t *bonding, const char *attr, char *buffer, size_t bufsize)
{
	memset(buffer, 0, bufsize);
	if (!strcmp(attr, "mode")) {
		strncpy(buffer, __ni_bonding_get_module_option_mode(bonding), bufsize - 1);
	} else if (!strcmp(attr, "miimon")) {
		unsigned int freq = 0;

		if (bonding->monitoring == NI_BOND_MONITOR_MII)
			freq = bonding->miimon.frequency;
		snprintf(buffer, bufsize, "%u", freq);
	} else if (!strcmp(attr, "updelay")) {
		if (bonding->monitoring != NI_BOND_MONITOR_MII)
			return 0;
		snprintf(buffer, bufsize, "%u", bonding->miimon.updelay);
	} else if (!strcmp(attr, "downdelay")) {
		if (bonding->monitoring != NI_BOND_MONITOR_MII)
			return 0;
		snprintf(buffer, bufsize, "%u", bonding->miimon.downdelay);
	} else if (!strcmp(attr, "use_carrier")) {
		if (bonding->monitoring != NI_BOND_MONITOR_MII)
			return 0;
		snprintf(buffer, bufsize, "%u", bonding->miimon.carrier_detect);
	} else if (!strcmp(attr, "arp_validate")) {
		if (bonding->monitoring != NI_BOND_MONITOR_ARP)
			return 0;
		strncpy(buffer, __ni_bonding_get_module_option_arp_validate(bonding), bufsize - 1);
	} else if (!strcmp(attr, "arp_interval")) {
		if (bonding->monitoring != NI_BOND_MONITOR_ARP)
			return 0;
		snprintf(buffer, bufsize, "%u", bonding->arpmon.interval);
	} else if (!strcmp(attr, "primary")) {
		if (!bonding->primary)
			return 0;
		strncpy(buffer, bonding->primary, bufsize - 1);
	} else {
		return -1;
	}

	/* FIXME: Support xmit_hash_policy
	   	"layer2"
		"layer3+4"
		"layer2+3"
	 */

	return 0;
}

/*
 * Parse the module options specified for a bonding device.
 *
 *  max_bonds:Max number of bonded devices (int)
 *  num_grat_arp:Number of gratuitous ARP packets to send on failover event (int)
 *  miimon:Link check interval in milliseconds (int)
 *  updelay:Delay before considering link up, in milliseconds (int)
 *  downdelay:Delay before considering link down, in milliseconds (int)
 *  use_carrier:Use netif_carrier_ok (vs MII ioctls) in miimon; 0 for off, 1 for on (default) (int)
 *  mode:Mode of operation : 0 for balance-rr, 1 for active-backup, 2 for balance-xor, 3 for broadcast, 4 for 802.3ad, 5 for balance-tlb, 6 for balance-alb (charp)
 *  primary:Primary network device to use (charp)
 *  lacp_rate:LACPDU tx rate to request from 802.3ad partner (slow/fast) (charp)
 *  xmit_hash_policy:XOR hashing method: 0 for layer 2 (default), 1 for layer 3+4 (charp)
 *  arp_interval:arp interval in milliseconds (int)
 *  arp_ip_target:arp targets in n.n.n.n form (array of charp)
 *  arp_validate:validate src/dst of ARP probes: none (default), active, backup or all (charp)
 *  fail_over_mac:For active-backup, do not set all slaves to the same MAC.  none (default), active or follow (charp)
 */
void
ni_bonding_parse_module_options(ni_bonding_t *bonding)
{
	char *temp, *s, *t, *saveptr = NULL;

	ni_bonding_clear(bonding);
	if (!bonding->module_opts)
		return;

	temp = xstrdup(bonding->module_opts);
	for (s = strtok_r(temp, " \t", &saveptr); s; s = strtok_r(NULL, " \t", &saveptr)) {
		int rv;

		if ((t = strchr(s, '=')) == NULL) {
			ni_error("ignoring unknown bonding module option %s", s);
			continue;
		}

		*t++ = '\0';

		rv = ni_bonding_parse_module_attribute(bonding, s, t);
		if (rv == -2) {
			ni_error("ignoring unknown bonding module option %s=%s", s, t);
		} else if (rv < 0) {
			ni_error("unable to parse bonding module option %s=%s", s, t);
			/* we should really return an error here */
		}
	}

	free(temp);
}

void
ni_bonding_build_module_options(ni_bonding_t *bonding)
{
	ni_stringbuf_t outbuf = NI_STRINGBUF_INIT_DYNAMIC;
	const char *attrs[] = {
		"mode",
		"miimon",
		"primary",

		/* ignored for ARP monitoring: */
		"updelay",
		"downdelay",
		"use_carrier",

		/* ignored for MII monitoring: */
		"arp_interval",
		"arp_validate",
		NULL,
	};
	unsigned int i;

	for (i = 0; attrs[i]; ++i) {
		char value[128];

		if (ni_bonding_format_module_attribute(bonding, attrs[i], value, sizeof(value)) < 0)
			continue;

		if (!ni_stringbuf_empty(&outbuf))
			ni_stringbuf_putc(&outbuf, ' ');
		ni_stringbuf_puts(&outbuf, value);
	}

#if 0
	/* FIXME: do arp_ip_target */
#endif

	ni_string_free(&bonding->module_opts);
	bonding->module_opts = outbuf.string;
	return;
}

/*
 * Load bonding configuration from sysfs
 */
int
ni_bonding_parse_sysfs_attrs(const char *ifname, ni_bonding_t *bonding)
{
	const char *attrs[] = {
		"mode",
		"miimon",
		"arp_validate",
		"arp_interval",
		"updelay",
		"downdelay",
		"use_carrier",
		"primary",
		NULL,
	};
	char *attrval = NULL;
	unsigned int i;

	ni_bonding_clear(bonding);
	ni_sysfs_bonding_get_slaves(ifname, &bonding->slave_names);

	for (i = 0; attrs[i]; ++i) {
		const char *attrname = attrs[i];
		int rv;

		if (ni_sysfs_bonding_get_attr(ifname, attrname, &attrval) < 0) {
			ni_error("%s: cannot get bonding attribute %s", ifname, attrname);
			goto failed;
		}

		if (attrval == NULL)
			continue;

		rv = ni_bonding_parse_module_attribute(bonding, attrname, attrval);
		if (rv == -2) {
			ni_error("ignoring unknown bonding module option %s=%s", attrname, attrval);
		} else if (rv < 0) {
			ni_error("unable to parse bonding module option %s=%s", attrname, attrval);
			goto failed;
		}
	}

	ni_sysfs_bonding_get_arp_targets(ifname, &bonding->arpmon.targets);

	ni_string_free(&attrval);
	return 0;

failed:
	ni_string_free(&attrval);
	return -1;
}

/*
 * Write one sysfs attribute
 */
static int
ni_bonding_write_one_sysfs_attr(const char *ifname, const ni_bonding_t *bonding,
			const ni_bonding_t *current, const char *attrname)
{
	char current_value[128], config_value[128];

	if (ni_bonding_format_module_attribute(current, attrname, current_value, sizeof(current_value)) < 0
	 || ni_bonding_format_module_attribute(bonding, attrname, config_value, sizeof(config_value)) < 0) {
		ni_error("%s: cannot represent attribute %s", ifname, attrname);
		return -1;
	}

	if (config_value[0] == '\0') {
		ni_debug_ifconfig("%s: attr %s ignored", ifname, attrname);
		return 0;
	}

	if (!strcmp(current_value, config_value)) {
		ni_debug_ifconfig("%s: attr %s unchanged", ifname, attrname);
		return 0;
	}

	/* FIXME: for stage 0 attributes, we should verify that the device is down.
	 * For stage 1 attribures, we should verify that it is up */

	ni_debug_ifconfig("%s: setting attr %s=%s", ifname, attrname, config_value);
	if (ni_sysfs_bonding_set_attr(ifname, attrname, config_value) < 0) {
		ni_error("%s: cannot set bonding attribute %s=%s", ifname, attrname, config_value);
		return -1;
	}

	return 0;
}

/*
 * Write bonding configuration to sysfs.
 * This happens in two stages; the first stage happens prior to enslaving interfaces,
 * the other happens afterwards.
 */
int
ni_bonding_write_sysfs_attrs(const char *ifname, const ni_bonding_t *bonding, const ni_bonding_t *current, int stage)
{
	const char *stage0_attrs[] = {
		"mode",
		"miimon",

		/* ignored for ARP monitoring: */
		"updelay",
		"downdelay",
		"use_carrier",

		/* ignored for MII monitoring: */
		"arp_interval",
		"arp_validate",
		NULL,
	};
	const char *stage1_attrs[] = {
		"primary",
		NULL,
	};
	const char **attrs;
	unsigned int i;


	attrs = (stage == 0)? stage0_attrs : stage1_attrs;
	for (i = 0; attrs[i]; ++i) {
		if (ni_bonding_write_one_sysfs_attr(ifname, bonding, current, attrs[i]) < 0)
			return -1;
	}

	/* arp_ip_target is special, since it's a list of addrs */
	if (stage == 0 && bonding->monitoring == NI_BOND_MONITOR_ARP
	 && ni_sysfs_bonding_set_list_attr(ifname, "arp_ip_target", &bonding->arpmon.targets) < 0)
		return -1;

	return 0;
}
