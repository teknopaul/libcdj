
#include <stdio.h>

#include "vdj_store.h"

#define PLAYER_ID_FILE       "/var/tmp/vdj-player-id"

void
vdj_save_player_id(vdj_t* v)
{
    if (v->auto_id) {
        FILE* p;
        if ( (p = fopen(PLAYER_ID_FILE, "w")) ) {
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
        if ( (p = fopen(PLAYER_ID_FILE, "r")) ) {
            char str[4];
            if ( fgets(str, 3, p) ) {
                uint8_t player_id = atoi(str);
                if (player_id > 0 && player_id < 5) v->player_id = player_id;
            }
            fclose(p);
        }
    }
}