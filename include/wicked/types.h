/*
 * Type declarations for netinfo.
 *
 * Copyright (C) 2010-2012 Olaf Kirch <okir@suse.de>
 */
#ifndef __WICKED_TYPES_H__
#define __WICKED_TYPES_H__

#include <wicked/constants.h>
#include <stdint.h>

typedef unsigned char		ni_bool_t;
#ifndef TRUE
# define TRUE 1
#endif
#ifndef FALSE
# define FALSE 0
#endif

typedef struct ni_interface	ni_interface_t;		/* rename to ni_netif_t? */
typedef struct ni_vlan		ni_vlan_t;
typedef struct ni_bridge	ni_bridge_t;
typedef struct ni_bridge_port	ni_bridge_port_t;
typedef struct ni_bonding	ni_bonding_t;
typedef struct ni_wireless	ni_wireless_t;
typedef struct ni_wireless_scan	ni_wireless_scan_t;
typedef struct ni_ethernet	ni_ethernet_t;
typedef struct ni_nis_info	ni_nis_info_t;
typedef struct ni_resolver_info	ni_resolver_info_t;
typedef struct ni_addrconf_lease  ni_addrconf_lease_t;
typedef struct ni_interface_request ni_interface_request_t;

typedef struct ni_dbus_server	ni_dbus_server_t;
typedef struct ni_dbus_client	ni_dbus_client_t;

typedef struct ni_socket	ni_socket_t;
typedef struct ni_buffer	ni_buffer_t;
typedef struct ni_extension	ni_extension_t;
typedef struct ni_script_action	ni_script_action_t;

typedef struct ni_shellcmd	ni_shellcmd_t;
typedef struct ni_process	ni_process_t;

/*
 * These are used by the XML and XPATH code.
 */
typedef struct xpath_format xpath_format_t;
typedef struct xpath_enode xpath_enode_t;
typedef struct xml_document xml_document_t;
typedef struct xml_node xml_node_t;
typedef struct ni_xs_type	ni_xs_type_t;
typedef struct ni_xs_scope	ni_xs_scope_t;

typedef struct xpath_format_array {
	unsigned int		count;
	xpath_format_t **	data;
} xpath_format_array_t;

/*
 * This is the all-encompassing thingy that holds a
 * complete network config state
 */
typedef struct ni_netconfig	ni_netconfig_t;
struct ni_netconfig {
	ni_interface_t *	interfaces;
	struct ni_route *	routes;		/* should kill this */
};

typedef union ni_uuid {
	unsigned char		octets[16];
	uint32_t		words[4];
} ni_uuid_t;
#define NI_UUID_INIT		{ .words = { 0, 0, 0, 0 } }

/*
 * Link layer address
 */
#define NI_MAXHWADDRLEN		64
typedef struct ni_hwaddr {
	unsigned short		type;
	unsigned short		len;
	unsigned char		data[NI_MAXHWADDRLEN];
} ni_hwaddr_t;

/*
 * Range of unsigned values
 */
typedef struct ni_uint_range {
	unsigned int		min, max;
} ni_uint_range_t;

static inline void
ni_uint_range_update_min(ni_uint_range_t *r, unsigned int min)
{
	if (min > r->min)
		r->min = min;
}

static inline void
ni_uint_range_update_max(ni_uint_range_t *r, unsigned int max)
{
	if (max < r->max)
		r->max = max;
}

#endif /* __WICKED_TYPES_H__ */
