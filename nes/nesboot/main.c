#include "rnse.h"
int main(void) {
  int s;
  u32 size = 0;
  char name[12];
  int i;
  u32 *image = (u32 *)0xac100080;
  board_init();
  panel_init();
  sd_idle = panel_service;
  for (s = 0; s < 2; s++)
    if (!sd_init(s))
      break;
  if (s == 2)
    return 1;
  for (i = 0; sd_list(i, name, &size) >= 0; i++) {
    int j;
    for (j = 0; j < 11 && name[j] == "NESCORE BIN"[j]; j++)
      ;
    if (j == 11)
      break;
  }
  if (sd_list(i, name, &size) < 0 || size < 32 || size > 0x100000)
    return 2;
  if (sd_load("NESCORE BIN", image, 0x100000, &size))
    return 3;
  if (image[4] != 0x524e5345 || image[5] != 0x200000 || image[0] < 0x8c1000a0 ||
      image[0] >= 0x8c100080 + size || (image[0] & 1))
    return 4;
  cache_setup(CCR_EMODE | CCR_OCE | CCR_ICE | CCR_CB);
  __asm__ volatile("ldc %0,sr" ::"r"(0x400000f0) : "memory");
  ((void (*)(u32, u32, u32))image[0])(start_args[0], start_args[1],
                                      start_args[2]);
  return 0;
}
