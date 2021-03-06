
/**
 * Tools to find network interfaces needed for VirtualCDJ.
 * 
 * @author teknopaul
 */
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <netinet/in.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "vdj_net.h"

/**
 * If the host only has one interface and one IP address we can assume this is the CDJ LAN.
 * This code discounts virtual devices (no /sys/class/net/%s/device) 
 * and it discounts wifi on grounds of the fact that CDJs use ethernet.
 */
int
vdj_has_single_ip()
{
    struct ifaddrs *addrs, *tmp;
    int found = 0;
    char path[255];

    getifaddrs(&addrs);
    tmp = addrs;

    while (tmp) {
        snprintf(path, 254, "/sys/class/net/%s/device", tmp->ifa_name);
        if (tmp->ifa_addr && 
            tmp->ifa_addr->sa_family == AF_INET &&
            (tmp->ifa_flags & IFF_LOOPBACK) == 0 &&
            access(path, F_OK) == 0) {
            
            snprintf(path, 254, "/sys/class/net/%s/wireless", tmp->ifa_name);
            if (access(path, F_OK) != 0) {
                found++;
            }
        }
        tmp = tmp->ifa_next;
    }

    freeifaddrs(addrs);

    return found == 1 ? VDJ_OK : VDJ_ERROR;
}

/**
 * If the host only has one network card and one IP address we can assume this is the CDJ LAN.
 */
int
vdj_get_single_ip(char* iface, struct sockaddr_in* addr, struct sockaddr_in* netmask)
{
    struct ifaddrs *addrs, *tmp;
    char path[255];

    getifaddrs(&addrs);
    tmp = addrs;

    while (tmp) {
        snprintf(path, 254, "/sys/class/net/%s/device", tmp->ifa_name);
        if (tmp->ifa_addr && 
            tmp->ifa_addr->sa_family == AF_INET &&
            (tmp->ifa_flags & IFF_LOOPBACK) == 0 &&
            access(path, F_OK) == 0) {

            snprintf(path, 254, "/sys/class/net/%s/wireless", tmp->ifa_name);
            if (access(path, F_OK) != 0) {

                memcpy(addr, tmp->ifa_addr, sizeof(struct sockaddr_in));
                memcpy(netmask, tmp->ifa_netmask, sizeof(struct sockaddr_in));
                strncpy(iface, tmp->ifa_name, IFNAMSIZ);
                freeifaddrs(addrs);

                return VDJ_OK;
            }
        }
        tmp = tmp->ifa_next;
    }

    freeifaddrs(addrs);
    return VDJ_ERROR;
}

/**
 * find the ip address of a given interface
 */
int
vdj_find_ip(const char* iface, struct sockaddr_in* addr, struct sockaddr_in* netmask)
{
    struct ifaddrs *addrs, *tmp;

    getifaddrs(&addrs);
    tmp = addrs;

    while (tmp) {
        if (tmp->ifa_addr && 
                tmp->ifa_addr->sa_family == AF_INET &&
                strcmp(tmp->ifa_name, iface) == 0) {

            memcpy(addr, tmp->ifa_addr, sizeof(struct sockaddr_in));
            memcpy(netmask, tmp->ifa_netmask, sizeof(struct sockaddr_in));
            freeifaddrs(addrs);

            return VDJ_OK;
        }
        tmp = tmp->ifa_next;
    }

    freeifaddrs(addrs);
    return VDJ_ERROR;
}

/**
 * get the mac addres of an interface
 */
int
vdj_get_mac_addr(const char* iface, uint8_t* mac)
{
    struct ifaddrs *addrs, *tmp;

    getifaddrs(&addrs);
    tmp = addrs;

    while (tmp) {
        if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_PACKET) {
            if ( strcmp(iface, tmp->ifa_name) == 0) {

                struct sockaddr_ll *s = (struct sockaddr_ll*)tmp->ifa_addr;
                memcpy(mac, s->sll_addr, 6);
                freeifaddrs(addrs);
                
                return VDJ_OK;
            }
        }
        tmp = tmp->ifa_next;
    }

    freeifaddrs(addrs);

    return VDJ_ERROR;
}

void
vdj_mac_addr_to_string(uint8_t* mac, char* mac_string)
{
    int i;

    for (i = 0; i < 6; i++) {
        mac_string += snprintf(mac_string, 3, "%02x", mac[i]);
        if (i < 5) {
            *mac_string++ = ':';
        }
    }
}

/**
 * list interface names
 */
void 
vdj_print_iface()
{
    struct ifaddrs *addrs, *tmp;

    getifaddrs(&addrs);
    tmp = addrs;

    while (tmp) {
        if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_PACKET) {
            fprintf(stderr, "  %s\n", tmp->ifa_name);
        }
        tmp = tmp->ifa_next;
    }

    freeifaddrs(addrs);
}