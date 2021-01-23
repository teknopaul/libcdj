#ifndef _SNIP_CORE_H_INCLUDED_
#define _SNIP_CORE_H_INCLUDED_

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h> 

/**
 * Groundhog day Monday, April 3, 2017 8:00:14 PM
 */
#define GROUND_HOG_DAY   1491249614

static int errors = 0;

static int snip_assert(char* msg, int thing) 
{
    if ( ! thing) {
        errors++;
        fprintf(stdout, "[\x1B[31merror\x1B[0m] %s\n", msg);
        return 0;
    }
    return 1;
}

static void snip_error(char* msg) {
    fprintf(stdout, "[\x1B[31merror\x1B[0m] %s\n", msg);
}

static int snip_equals(char* msg, int expected, int actual)
{
    if ( expected != actual) {
        errors++;
        fprintf(stdout, "[\x1B[31merror\x1B[0m] %s expected=%i actual=%i\n", msg, expected, actual);
        return 0;
    }
    return 1;
}

static int snip_lequals(char* msg, long expected, long actual)
{
    if ( expected != actual) {
        errors++;
        fprintf(stdout, "[\x1B[31merror\x1B[0m] %s expected=%li actual=%li\n", msg, expected, actual);
        return 0;
    }
    return 1;
}


#endif // SNIP_CORE_H_INCLUDED_
