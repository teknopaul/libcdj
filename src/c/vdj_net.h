#ifndef _VDJ_NET_H_INCLUDED_
#define _VDJ_NET_H_INCLUDED_

#define VDJ_OK          0
#define VDJ_ERROR       1

/**
 * Network functions for VirtualCDJ startup, find the right interface and ip
 */


/**
 * return true if this PC has on network interface and IP address
 */
int vdj_has_single_ip();

/**
 * If the host only has one network card and one IP address we can assume this is the CDJ LAN.
 */
int vdj_get_single_ip(char* iface, struct sockaddr_in* addr, struct sockaddr_in* netmask);

/**
 * find the ip address of a given interface
 */
int vdj_find_ip(const char* iface, struct sockaddr_in* addr, struct sockaddr_in* netmask);

/**
 * mac - pointer to a char[6] to return the mac address as a number
 * mac_string - if not NULL should be apointer to char[18] to return mac address as a string as well
 */
int vdj_get_mac_addr(const char* iface, uint8_t* mac);

/**
 * convert char[6] to : separated string
 *
 * mac - pointer to a char[6] to return the mac address as a number
 * mac_string - if not NULL should be apointer to char[18] to return mac address as a string as well
 */
void vdj_mac_addr_to_string(uint8_t* mac, char* mac_string);

/**
 * list interface names
 */
void vdj_print_iface();

#endif // _VDJ_NET_H_INCLUDED_
