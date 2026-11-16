/* Doom on the RNS-E 192: the WAD file, which doomboot put in RAM (and
 * libc.c lists as DOOM1.WAD).  Takes the place of doomgeneric's
 * w_file_stdc.c, under its name, since w_file.c asks for that.  Lumps
 * are copied out of it into the zone as from a file (`mapped` stays
 * unset): a lump in the WAD need not start at an address the SH-4 can
 * read a word from, and Doom reads words from lumps. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "w_file.h"
#include "z_zone.h"

extern wad_file_class_t stdc_wad_file;

typedef struct {
    wad_file_t wad;
    FILE *f;
} ram_wad_file_t;

static wad_file_t *ram_open(char *path)
{
    ram_wad_file_t *result;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    result = Z_Malloc(sizeof *result, PU_STATIC, 0);
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    fseek(f, 0, SEEK_END);
    result->wad.length = (unsigned int)ftell(f);
    result->f = f;
    return &result->wad;
}

static void ram_close(wad_file_t *wad)
{
    ram_wad_file_t *w = (ram_wad_file_t *)wad;
    fclose(w->f);
    Z_Free(w);
}

static size_t ram_read(wad_file_t *wad, unsigned int offset, void *buffer, size_t len)
{
    ram_wad_file_t *w = (ram_wad_file_t *)wad;
    fseek(w->f, (long)offset, SEEK_SET);
    return fread(buffer, 1, len, w->f);
}

wad_file_class_t stdc_wad_file = {
    ram_open,
    ram_close,
    ram_read,
};
