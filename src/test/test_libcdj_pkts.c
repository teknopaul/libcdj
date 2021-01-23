
#include <net/if.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "cdj.h"



static unsigned char*
load_packet(char* name)
{
    if ( (p = fopen(name, "r")) ) {
        char str[1500 * 3];
        if ( fgets(str, 3, p) ) {
            unsigned char pid = atoi(str);
            if (pid > 0 && pid < 5) v->player_id = pid;
        }
        fclose(p);
    }
}


int main(int argc, const char *argv[])
{
    int n = 99;
    char master = 0;

    unsigned char model = 'X';
    unsigned char bar_pos = 3;
    unsigned char active = 1;
    unsigned int sync_counter = 99;
    unsigned char member_count = 3;
    unsigned char mac[6] = { 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, };
    unsigned char player_id = 5;

    // test cdj_ip_format()

    const char* TEST_IP = "253.100.99.1";
    unsigned char ip[4];
    int ret = cdj_ip_format(TEST_IP, ip);

    int len;
    unsigned char* packet = cdj_create_initial_discovery_packet(&len, model);
    printf("len=%x initial ", len);
    cdj_print_packet(packet, len, 50000);

    packet = cdj_create_stage1_discovery_packet(&len, model, mac, 1);
    printf("len=%x stage1 ", len);
    cdj_print_packet(packet, len, 50000);

    packet = cdj_create_id_use_req_packet(&len, model, ip, mac, player_id, 1);
    printf("len=%x id use req ", len);
    cdj_print_packet(packet, len, 50000);

    packet = cdj_create_id_set_req_packet(&len, model, player_id, 1);
    printf("len=%x id set req ", len);
    cdj_print_packet(packet, len, 50000);

    packet = cdj_create_keepalive_packet(&len, model, ip, mac, player_id, member_count);
    printf("len=%x keepalive ", len);
    cdj_print_packet(packet, len, 50000);

    packet = cdj_create_beat_packet(&len, model, player_id, 120.00, 3);
    printf("len=%x beat ", len);
    cdj_print_packet(packet, len, 50001);


    packet = cdj_create_status_packet(&len, model, player_id,
        120.00, bar_pos, active, master, sync_counter, 
        n);
    printf("len=%x status ", len);
    cdj_print_packet(packet, len, 50001);


    // BPM stuff

    float c_bpm = cdj_calculated_bpm(12000, 0x00000000);
    printf("calculated bpm = %f\n", c_bpm);

    c_bpm = cdj_calculated_bpm(12000, 0x00100000);
    printf("calculated bpm = %f\n", c_bpm);

    c_bpm = cdj_calculated_bpm(12000, 0x00200000);
    printf("calculated bpm = %f\n", c_bpm);

    fprintf(stderr,"ok\n");
    exit(0);
}
