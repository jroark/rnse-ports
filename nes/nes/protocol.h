#ifndef NES_PROTOCOL_H
#define NES_PROTOCOL_H
#include "rnse.h"
#ifndef NES_CAN_ID
#define NES_CAN_ID 0x6e3
#endif
struct pads {
  u8 button[2], seq, seen;
  u32 at;
};
int pads_receive(struct pads *, u16, const u8 *, int, u32);
void pads_expire(struct pads *, u32);
u8 faceplate_buttons(int (*down)(int));
void pads_buttons(const struct pads *, u8 faceplate, u8 out[2]);
int cartridge_valid(const u8 *, u32);
#endif
