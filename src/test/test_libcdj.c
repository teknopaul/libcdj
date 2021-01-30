
#include <inttypes.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "cdj.h"

// TODO snip test

int main(int argc, const char *argv[])
{
    uint32_t n = 99;
    char master = 0;

    unsigned char model = 'X';
    unsigned char bar_pos = 3;
    unsigned char active = 1;
    unsigned int sync_counter = 99;
    unsigned char member_count = 3;

    printf("%s\n", cdj_flags_to_emoji(CDJ_STAT_FLAG_PLAY | CDJ_STAT_FLAG_MASTER | CDJ_STAT_FLAG_SYNC | CDJ_STAT_FLAG_ONAIR));
    printf("%s\n", cdj_flags_to_chars(CDJ_STAT_FLAG_PLAY | CDJ_STAT_FLAG_MASTER | CDJ_STAT_FLAG_SYNC | CDJ_STAT_FLAG_ONAIR));
    printf("%s\n", cdj_flags_to_term(CDJ_STAT_FLAG_PLAY | CDJ_STAT_FLAG_MASTER | CDJ_STAT_FLAG_SYNC | CDJ_STAT_FLAG_ONAIR));
    printf("%s\n", cdj_bpm_to_string(120.38));
    printf("%s\n", cdj_bpm_to_string(120.3));
    printf("%s\n", cdj_bpm_to_string(95.35));
    printf("%s\n", cdj_bpm_to_string(1000.389));


    unsigned char mac[6] = { 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, };
    unsigned char player_id = 5;

    // test cdj_ip_format()

    const char* TEST_IP = "253.100.99.1";
    unsigned char ip[4];
    int ret = cdj_ip_format(TEST_IP, ip);

    if (ret == CDJ_OK) {
        char res[23];
        sprintf(res, "%d.%d.%d.%d", ip[0], ip[1],ip[2],ip[3]);
        if (strcmp(TEST_IP, res) != 0 ) {
            fprintf(stderr,"failed\n");
            exit(1);
        }
    }

    unsigned int ip_cdj = 0xc0a8010a; // 192.168.1.10
    printf("ip_cdj=%08x\n", ip_cdj);
    struct sockaddr_in* ip_lin = cdj_ip_decode(ip_cdj);
    printf("s_addr=%08x\n", ip_lin->sin_addr.s_addr);
    char ip_s[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip_lin->sin_addr.s_addr, ip_s, INET_ADDRSTRLEN);
    printf("lin ip='%s'\n", ip_s);

    char my_ip_s[INET6_ADDRSTRLEN];
    struct sockaddr_in my_ip;
    u_int32_t my_int1 = 0xc0a8010a;
    my_ip.sin_addr.s_addr = htonl(my_int1);
    inet_ntop(AF_INET, &my_ip.sin_addr.s_addr, my_ip_s, INET_ADDRSTRLEN);
    printf("ip='%s' %x\n", my_ip_s, my_ip.sin_addr.s_addr);

    u_int32_t my_int2 = 0xc0a8010b;
    my_ip.sin_addr.s_addr = htonl(my_int2);
    inet_ntop(AF_INET, &my_ip.sin_addr.s_addr, my_ip_s, INET_ADDRSTRLEN);
    printf("ip='%s' %x\n", my_ip_s, my_ip.sin_addr.s_addr);

    u_int32_t my_int3 = 0xc0a8010c;
    my_ip.sin_addr.s_addr = htonl(my_int3);
    inet_ntop(AF_INET, &my_ip.sin_addr.s_addr, my_ip_s, INET_ADDRSTRLEN);
    printf("ip='%s' %x\n", my_ip_s, my_ip.sin_addr.s_addr);


    unsigned int ip_bak = cdj_ip_encode(ip_lin);
    if (ip_cdj != ip_bak) {
        fprintf(stderr,"ip conversions failed\n");
        exit(1);
    }
    if (strcmp(ip_s, "192.168.1.10") != 0) {
        fprintf(stderr,"ip conversions failed: '%s'\n", ip_s);
        exit(1);
    }

    // BPM stuff

    float c_bpm = cdj_calculated_bpm(12000, 0x00000000);
    printf("calculated bpm = %f\n", c_bpm);

    c_bpm = cdj_calculated_bpm(12000, 0x00100000);
    printf("calculated bpm = %f\n", c_bpm);

    c_bpm = cdj_calculated_bpm(12000, 0x00200000);
    printf("calculated bpm = %f\n", c_bpm);

    fprintf(stderr,"ok\n");

    uint8_t byte = 0xff;
    int8_t sbyte = (int8_t) byte;
    printf("signed byte %i\n", sbyte);

    exit(0);
}
