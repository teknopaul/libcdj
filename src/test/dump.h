

#ifndef _LIBDUMP_H_INCLUDED_
#define _LIBDUMP_H_INCLUDED_

// libdump - load and save dump files used for unit testing binary data

unsigned char* dump_load(const char* name, unsigned char* buffer, ssize_t* dlen);

void dump_save(const char* name, unsigned char* buffer, ssize_t len, int width);

void dump_out(unsigned char* buffer, ssize_t len, int width);

int dump_cmp(const char* file_name, unsigned char* buffer_act, ssize_t len_act);

int dump_eq(const char* name, unsigned char* buffer_exp, ssize_t len_exp, unsigned char* buffer_act, ssize_t len_act);

#endif // _LIBDUMP_H_INCLUDED_
