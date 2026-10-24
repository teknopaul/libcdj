/*
 * xdj-anlz: standalone byte-level scanner for rekordbox ANLZ analysis files.
 *
 * Deliberately does NOT use the libcdj ANLZ parser, so tests can cross-check the two.
 *
 * Usage:
 *   xdj-anlz <file>            print the file header and one line per tag, exit 1 if the
 *                              structure is invalid (bad magic, tag overruns, tags do not tile the file)
 *   xdj-anlz --survey <dir>    walk <dir>/USBANLZ (or <dir> itself) for ANLZ*.{DAT,EXT,2EX}
 *                              and print per-extension tag counts
 *
 * Output line per tag:  fourcc offset len_header len_tag
 */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>
#include <ftw.h>

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint8_t *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)sz + 1);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *len = (size_t)sz;
    return buf;
}

typedef void (*tag_fn)(const char *fourcc, uint32_t off, uint32_t hdr, uint32_t len, void *ud);

/* Returns 0 if valid, else a message. hdr_out/filelen_out are filled when the magic matched. */
static const char *scan(const uint8_t *d, size_t n, uint32_t *hdr_out, uint32_t *filelen_out,
                        tag_fn fn, void *ud)
{
    if (n < 28) return "file shorter than the 28 byte PMAI header";
    if (memcmp(d, "PMAI", 4) != 0) return "bad magic (not PMAI)";
    uint32_t hl = be32(d + 4), fl = be32(d + 8);
    if (hdr_out) *hdr_out = hl;
    if (filelen_out) *filelen_out = fl;
    if (hl < 28 || hl > n) return "bad file header length";
    size_t p = hl;
    while (p < n) {
        if (p + 12 > n) return "trailing bytes shorter than a tag header";
        uint32_t th = be32(d + p + 4), tl = be32(d + p + 8);
        char cc[5];
        memcpy(cc, d + p, 4);
        cc[4] = 0;
        if (fn) fn(cc, (uint32_t)p, th, tl, ud);
        if (tl < 12) return "tag length below 12";
        if (p + tl > n) return "tag runs past the end of the file";
        p += tl;
    }
    return NULL;
}

static void print_tag(const char *cc, uint32_t off, uint32_t hdr, uint32_t len, void *ud)
{
    (void)ud;
    printf("%s %u 0x%x %u\n", cc, off, hdr, len);
}

static int scan_file(const char *path)
{
    size_t n;
    uint8_t *d = read_all(path, &n);
    if (!d) { fprintf(stderr, "%s: cannot read\n", path); return 1; }
    uint32_t hl = 0, fl = 0;
    const char *err = scan(d, n, &hl, &fl, NULL, NULL);
    if (err && hl == 0) { fprintf(stderr, "%s: %s\n", path, err); free(d); return 1; }
    printf("PMAI len_header=0x%x len_file=%u size=%zu%s\n", hl, fl, n, fl == n ? "" : " (len_file != size)");
    err = scan(d, n, NULL, NULL, print_tag, NULL);
    free(d);
    if (err) { fprintf(stderr, "%s: %s\n", path, err); return 1; }
    return 0;
}

/* ---- survey ---- */
#define MAXKEYS 256
static struct { char ext[4]; char cc[5]; uint32_t hdr; unsigned count; } keys[MAXKEYS];
static int nkeys;
static unsigned files[3], size_min[3], size_max[3], bad, mismatch;
static const char *EXTS[3] = { "DAT", "EXT", "2EX" };
static int cur_ext;

static void count_tag(const char *cc, uint32_t off, uint32_t hdr, uint32_t len, void *ud)
{
    (void)off; (void)len; (void)ud;
    for (int i = 0; i < nkeys; i++)
        if (!strcmp(keys[i].ext, EXTS[cur_ext]) && !strcmp(keys[i].cc, cc) && keys[i].hdr == hdr) { keys[i].count++; return; }
    if (nkeys < MAXKEYS) {
        strcpy(keys[nkeys].ext, EXTS[cur_ext]);
        strcpy(keys[nkeys].cc, cc);
        keys[nkeys].hdr = hdr;
        keys[nkeys].count = 1;
        nkeys++;
    }
}

static int visit(const char *path, const struct stat *sb, int type, struct FTW *ftw)
{
    (void)sb; (void)ftw;
    if (type != FTW_F) return 0;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strncmp(base, "ANLZ", 4) != 0) return 0;      /* skips USBMNG.DAT */
    const char *dot = strrchr(base, '.');
    if (!dot) return 0;
    cur_ext = -1;
    for (int i = 0; i < 3; i++) if (!strcasecmp(dot + 1, EXTS[i])) cur_ext = i;
    if (cur_ext < 0) return 0;
    size_t n;
    uint8_t *d = read_all(path, &n);
    if (!d) { bad++; return 0; }
    uint32_t fl = 0;
    const char *err = scan(d, n, NULL, &fl, count_tag, NULL);
    if (err) { bad++; fprintf(stderr, "%s: %s\n", path, err); }
    else if (fl != n) mismatch++;
    files[cur_ext]++;
    if (!size_min[cur_ext] || n < size_min[cur_ext]) size_min[cur_ext] = (unsigned)n;
    if (n > size_max[cur_ext]) size_max[cur_ext] = (unsigned)n;
    free(d);
    return 0;
}

static int cmp(const void *a, const void *b)
{
    const typeof(keys[0]) *x = a, *y = b;
    int c = strcmp(x->ext, y->ext);
    if (c) return c;
    c = strcmp(x->cc, y->cc);
    return c ? c : (x->hdr > y->hdr) - (x->hdr < y->hdr);
}

static int survey(const char *dir)
{
    if (nftw(dir, visit, 16, FTW_PHYS) != 0) { fprintf(stderr, "cannot walk %s\n", dir); return 1; }
    qsort(keys, (size_t)nkeys, sizeof keys[0], cmp);
    for (int i = 0; i < 3; i++)
        if (files[i]) printf("%s files=%u size=%u..%u\n", EXTS[i], files[i], size_min[i], size_max[i]);
    for (int i = 0; i < nkeys; i++)
        printf("  %s %s header=0x%x count=%u\n", keys[i].ext, keys[i].cc, keys[i].hdr, keys[i].count);
    printf("invalid=%u len_file_mismatch=%u\n", bad, mismatch);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--survey")) return survey(argv[2]);
    if (argc == 2 && argv[1][0] != '-') return scan_file(argv[1]);
    fprintf(stderr, "usage: xdj-anlz <file> | xdj-anlz --survey <dir>\n");
    return 2;
}
