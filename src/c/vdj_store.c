
#include <stdio.h>

#include "vdj_store.h"

void
vdj_save_player_id(vdj_t* v)
{
    if (v->auto_id) {
        FILE* p;
        if ( (p = fopen("/var/tmp/vdj-player-id", "w")) ) {
            fprintf(p, "%i\n", v->player_id);
            fflush(p);
            fclose(p);
        }
    }
}

void
vdj_load_player_id(vdj_t* v)
{
    if (v->auto_id) {
        // read last used player_id from /var/tmp/vdj-player-id
        FILE* p;
        if ( (p = fopen("/var/tmp/vdj-player-id", "r")) ) {
            char str[4];
            if ( fgets(str, 3, p) ) {
                unsigned char pid = atoi(str);
                if (pid > 0 && pid < 5) v->player_id = pid;
            }
            fclose(p);
        }
    }
}