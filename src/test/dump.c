#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dump.h"

// libdump - load and save dump files used for unit testing binary data

static int
parse(unsigned char* buffer, char* line)
{
    if (strlen(line) == 0) return 0;
    if (*line == '#') return 0;
    int i = 0;

    char* tok = strtok(line, " ");
    while (tok) {

        if (*tok == '#' ) break;
        *buffer++ = (unsigned char) strtol(tok, NULL, 16);
        i++;
        tok = strtok(NULL, " ");
    }
    return i;
}

unsigned char*
dump_load(const char* name, unsigned char* buffer, ssize_t* d_len)
{
    char line[2048];
    if (buffer == NULL) buffer = (unsigned char* ) calloc(1, 2048);

    unsigned char* pos = buffer;
    int len;
    *d_len = 0;

    FILE* p;
    if ( (p = fopen(name, "r")) ) {
        while (fgets(line, 2047, p)) {
            len = parse(pos, line);
            pos += len;
            *d_len += len;
        }
    } else {
        fprintf(stderr, "[\x1B[31merror\x1B[0m] failed to open '%s'", name);
    }

    return buffer;
}

void
dump_save(const char* name, unsigned char* buffer, ssize_t len, int width)
{
    int i;
    FILE* p;
    if ( (p = fopen(name, "w")) ) {
        for ( i = 0 ; i < len ; i++) {
            if (i && i % width == 0) fprintf(p, "\n");
            fprintf(p, "0x%02x ", buffer[i] );
        }
    }
}

void
dump_out(unsigned char* buffer, ssize_t len, int width)
{
    int i;
    for ( i = 0 ; i < len ; i++) {
        if (i && i % width == 0) printf("\n");
        printf("0x%02x ", buffer[i] );
    }
    printf("\n");
}

int
dump_cmp(const char* file_name, unsigned char* buffer_act, ssize_t len_act)
{
    int rv;
    ssize_t d_len;
    unsigned char* dump;
    if ( (dump = dump_load(file_name, NULL, &d_len)) ) {
        rv = dump_eq(file_name, dump, d_len, buffer_act, len_act);
        free(dump);
        return rv;
    }
    fprintf(stderr, "[\x1B[31merror\x1B[0m] loading %s", file_name);
    return 0;
}

int
dump_eq(const char* name, unsigned char* buffer_exp, ssize_t len_exp, unsigned char* buffer_act, ssize_t len_act)
{
    int i;
    if (len_exp != len_act) {
        fprintf(stderr, "[\x1B[31merror\x1B[0m] %s differ: expected len=%li actual len=%li\n", name, len_exp, len_act);
        return 0;
    }
    for ( i = 0 ; i < len_exp ; i++) {
        if (buffer_exp[i] != buffer_act[i]) {
            fprintf(stderr, "[\x1B[31merror\x1B[0m] %s differ @ 0x%02x,  0x%02x 0x%02x\n", name, i, buffer_exp[i], buffer_act[i] );
            return 0;
        }
    }
    return 1;
}
