#include "protocol.h"
#include <assert.h>
#include <string.h>
static int keys[64];
static int down(int key) { return keys[key]; }
int main(void) {
  struct pads p = {0};
  u8 d[8] = {1, 255, 1, 128, 0};
  u8 rom[16 + 16384] = {0};
  assert(pads_receive(&p, NES_CAN_ID, d, 8, 10));
  assert(p.button[0] == 1 && p.button[1] == 128);
  d[1] = 0;
  assert(pads_receive(&p, NES_CAN_ID, d, 8, 11)); /* wrap */
  d[2] = 2;
  assert(
      !pads_receive(&p, NES_CAN_ID, d, 8, 100)); /* duplicate cannot refresh */
  pads_expire(&p, 211);
  assert(!p.seen && !p.button[0]);
  assert(!pads_receive(&p, NES_CAN_ID + 1, d, 8, 212));
  assert(!pads_receive(&p, NES_CAN_ID, d, 7, 212));
  d[7] = 1;
  assert(!pads_receive(&p, NES_CAN_ID, d, 8, 212));
  d[7] = 0;
  d[1] = 4;
  d[4] = 1;
  assert(pads_receive(&p, NES_CAN_ID, d, 8, 212));
  assert(p.button[0] == 2);
  u8 buttons[2];
  assert(faceplate_buttons(down) == 0);
  const int mapping[8] = {KEY_SOFT_TR, KEY_SOFT_BR, KEY_INFO, KEY_SELECT,
                          KEY_SOFT_TL, KEY_SOFT_BL, KEY_PREV, KEY_NEXT};
  for (int i = 0; i < 8; i++) {
    keys[mapping[i]] = 1;
    assert(faceplate_buttons(down) == (1u << i));
    keys[mapping[i]] = 0;
  }
  keys[KEY_RETURN] = keys[KEY_DISPLAY] = keys[KEY_CD_EJECT] = 1;
  assert(faceplate_buttons(down) == 0);
  pads_buttons(&p, 0xff, buttons);
  assert(buttons[0] == 2 && buttons[1] == 128); /* CAN takes priority */
  p.button[0] = p.button[1] = 0;
  pads_buttons(&p, 0xff, buttons);
  assert(!buttons[0] && !buttons[1]); /* idle adapter is still authoritative */
  pads_expire(&p, 412);
  pads_buttons(&p, 0x91, buttons);
  assert(buttons[0] == 0x91 && buttons[1] == 0); /* fallback after timeout */
  memcpy(rom, "NES\032", 4);
  rom[4] = 1;
  assert(cartridge_valid(rom, sizeof(rom)));
  assert(!cartridge_valid(rom, sizeof(rom) - 1));
  rom[7] = 8;
  assert(!cartridge_valid(rom, sizeof(rom)));
  rom[7] = 0;
  rom[6] = 0x50;
  assert(!cartridge_valid(rom, sizeof(rom)));
  rom[6] = 0;
  rom[9] = 1;
  assert(!cartridge_valid(rom, sizeof(rom)));
  return 0;
}
