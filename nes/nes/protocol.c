#include "protocol.h"
void pads_expire(struct pads *p, u32 now) {
  if (now - p->at >= 200) {
    p->button[0] = p->button[1] = 0;
    p->seen = 0;
  }
}
int pads_receive(struct pads *p, u16 id, const u8 *d, int n, u32 now) {
  if (id != NES_CAN_ID || n != 8 || d[0] != 1 || (d[4] & ~1) || d[5] || d[6] ||
      d[7])
    return 0;
  if (d[4] & 1) {
    p->button[0] = p->button[1] = 0;
    p->seen = 0;
  }
  if (p->seen && d[1] == p->seq)
    return 0;
  if (p->seen && (u8)(p->seq + 1) != d[1])
    p->button[0] = p->button[1] = 0;
  p->button[0] = d[2];
  p->button[1] = d[3];
  p->seq = d[1];
  p->at = now;
  p->seen = 1;
  return 1;
}
/* Physical display/eject/RETURN and volume retain their SDK functions. */
u8 faceplate_buttons(int (*down)(int)) {
  static const int keys[8] = {KEY_SOFT_TR, KEY_SOFT_BR, KEY_INFO, KEY_SELECT,
                              KEY_SOFT_TL, KEY_SOFT_BL, KEY_PREV, KEY_NEXT};
  u8 buttons = 0;
  int i;
  for (i = 0; i < 8; i++)
    if (down(keys[i]))
      buttons |= 1u << i;
  return buttons;
}
void pads_buttons(const struct pads *p, u8 faceplate, u8 out[2]) {
  out[0] = p->seen ? p->button[0] : faceplate;
  out[1] = p->seen ? p->button[1] : 0;
}
int cartridge_valid(const u8 *h, u32 n) {
  u32 size, m;
  if (n < 16 || n > 4194304 || h[0] != 'N' || h[1] != 'E' || h[2] != 'S' ||
      h[3] != 26 || !h[4])
    return 0;
  if ((h[7] & 15) || h[9] & 1)
    return 0; /* NES2, VS, PlayChoice, PAL */
  m = (h[6] >> 4) | (h[7] & 240);
  if (m != 0 && m != 1 && m != 2 && m != 3 && m != 4 && m != 7)
    return 0;
  if (h[8] > 1 || (m == 7 && (h[4] < 2 || (h[4] & 1))))
    return 0; /* core has 8 KiB SRAM; AxROM banks are 32 KiB */
  size = 16 + ((h[6] & 4) ? 512 : 0) + h[4] * 16384u + h[5] * 8192u;
  return size <= n;
}
