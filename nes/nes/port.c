#include "rnse.h"
#include <bitmap.h>
#include <nes.h>
#include <nes_apu.h>
#include <nes_mmc.h>
#include <nes_rom.h>
#include <nesinput.h>
#include <noftypes.h>
#include <osd.h>
#include <stdarg.h>
extern u8 *rom_image;
extern u32 rom_bytes;
extern int cartridge_valid(const u8 *, u32);
static bitmap_t *video;
bitmap_t *vid_getbuffer(void) { return video; }
void vid_setpalette(rgb_t *p) {
  int i;
  for (i = 0; i < 256; i++)
    screen_palette(i, p[i].r, p[i].g, p[i].b);
}
void osd_getsoundinfo(sndinfo_t *p) {
  p->sample_rate = 44100;
  p->bps = 16;
}
int log_printf(const char *s, ...) {
  (void)s;
  return 0;
}
void gui_sendmsg(int c, char *s, ...) {
  (void)c;
  (void)s;
}
void gui_setfps(int f) { (void)f; }
int printf(const char *s, ...) {
  (void)s;
  return 0;
}
int memcmp(const void *a, const void *b, unsigned long n) {
  const u8 *x = a, *y = b;
  while (n--) {
    if (*x != *y)
      return *x - *y;
    x++;
    y++;
  }
  return 0;
}
int rand(void) {
  static u32 s = 1;
  s = s * 1664525 + 1013904223;
  return s >> 1;
}
rominfo_t *rom_load(const char *name) {
  rominfo_t *r;
  u8 *h = rom_image;
  (void)name;
  if (!cartridge_valid(h, rom_bytes))
    return 0;
  r = malloc(sizeof(*r));
  if (!r)
    return 0;
  memset(r, 0, sizeof(*r));
  r->rom_banks = h[4];
  r->vrom_banks = h[5];
  r->mapper_number = (h[6] >> 4) | (h[7] & 240);
  r->mirror = (h[6] & 1) ? MIRROR_VERT : MIRROR_HORIZ;
  r->flags = (h[6] & 8) ? ROM_FLAG_FOURSCREEN : 0;
  r->rom = h + 16 + ((h[6] & 4) ? 512 : 0);
  r->vrom = h[5] ? r->rom + h[4] * 16384 : 0;
  r->sram_banks = 8;
  r->vram_banks = 1;
  r->sram = malloc(8192);
  r->vram = h[5] ? 0 : malloc(8192);
  if (!r->sram || (!h[5] && !r->vram))
    return 0;
  memset(r->sram, 0, 8192);
  if (r->vram)
    memset(r->vram, 0, 8192);
  if (h[6] & 4)
    memcpy(r->sram + 4096, h + 16, 512);
  video = bmp_create(256, 240, 8);
  return video ? r : 0;
}
void rom_free(rominfo_t **r) { *r = 0; }
int rom_checkmagic(const char *s) {
  (void)s;
  return 1;
}
extern mapintf_t map0_intf, map1_intf, map2_intf, map3_intf, map4_intf,
    map7_intf;
mapintf_t *mappers[] = {&map0_intf, &map1_intf, &map2_intf, &map3_intf,
                        &map4_intf, &map7_intf, 0};

#undef malloc
#undef free
void *_my_malloc(int n) { return malloc(n); }
void _my_free(void **p) { *p = 0; }
