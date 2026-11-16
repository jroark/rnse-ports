#include "protocol.h"
#include "rnse.h"
#include <bitmap.h>
#include <nes.h>
#include <nes_apu.h>
#include <nesinput.h>
#include <noftypes.h>
u8 *rom_image = (u8 *)0x8c800000;
u32 rom_bytes;
extern bitmap_t *vid_getbuffer(void);
extern void nes_frame(int);
static struct pads pads;
static nesinput_t inputs[2] = {{INP_JOYPAD0, 0}, {INP_JOYPAD1, 0}};
static s16 mono[735], stereo[2][1470];
static int slot, volume = 8, playing, faceplate_ready;
static void stop(void) {
  playing = 0;
  pcm_stop();
}
static void service(void) {
  u16 id;
  u8 d[8];
  int n, budget = 32;
  panel_service();
  while (budget-- && (n = can_receive(&id, d)) >= 0)
    pads_receive(&pads, id, d, n, millis());
  pads_expire(&pads, millis());
  /* Do not turn the ROM-launch press into an NES Start press. */
  if (playing && !panel_key(KEY_SELECT))
    faceplate_ready = 1;
  u8 buttons[2];
  pads_buttons(&pads,
               playing && faceplate_ready ? faceplate_buttons(panel_key) : 0,
               buttons);
  inputs[0].data = buttons[0];
  inputs[1].data = buttons[1];
  int v = panel_knob(0);
  if (v) {
    volume += v;
    if (volume < 0)
      volume = 0;
    if (volume > 30)
      volume = 30;
    radio_volume(volume);
  }
}
static void message(const char *s) {
  screen_fill(0, 0, 400, 235, 0);
  screen_palette(255, 255, 255, 255);
  screen_text(8, 8, s, 255, 2);
  console_puts(s);
  console_puts("\n");
}
static int mount(void) {
  int i;
  for (i = 0; i < 2; i++)
    if (sd_init(i) == 0) {
      slot = i;
      return 1;
    }
  return 0;
}
static void play(void) {
  nes_t *m;
  int bank = 0, i;
  u32 start = micros(), frames = 0, skips = 0, underruns = 0;
  heap_init((void *)0x8cc00000, 0x800000);
  m = nes_create();
  if (!m || nes_insertcart("GAME", m)) {
    message("CORE INIT FAILED");
    return;
  }
  screen_fill(0, 0, 400, 235, 192); /* palette entry 192 is black */
  extern void input_reset(void);
  input_reset();
  input_register(&inputs[0]);
  input_register(&inputs[1]);
  if (pcm_start(slot) < 0) {
    message("PCM INIT FAILED");
    return;
  }
  radio_source(RADIO_DECODER, volume);
  faceplate_ready = 0;
  playing = 1;
  console_puts("NES PLAYING\n");
  while (1) {
    u32 due = start + (frames * 16683u + frames / 3);
    int draw = (s32)(micros() - due) < 16684 || skips >= 2;
    service();
    if (panel_return_short())
      break;
    nes_frame(draw);
    apu_process(mono, 735);
    for (i = 0; i < 735; i++)
      stereo[bank][2 * i] = stereo[bank][2 * i + 1] = mono[i];
    if (frames && !pcm_dma_busy())
      underruns++;
    while (pcm_dma_busy()) {
      service();
    }
    pcm_dma(stereo[bank], 735);
    bank ^= 1;
    if (draw) {
      bitmap_t *v = vid_getbuffer();
      int x, y;
      for (y = 0; y < 234; y++) {
        u8 *row = v->line[y * 240 / 234];
        for (x = 0; x < 312; x++)
          REG8(0xb8110000 + ((y * 448 + 44 + x) ^ 3)) = row[x * 256 / 312];
        if (!(y & 15))
          service();
      }
      skips = 0;
    } else
      skips++;
    frames++;
    if (!(frames % 300)) {
      console_puts("NES FRAMES ");
      console_dec(frames);
      console_puts(" UNDERRUNS ");
      console_dec(underruns);
      console_puts("\n");
    }
    while ((s32)(micros() - (start + (frames * 16683u + frames / 3))) < 0)
      service();
  }
  stop();
}
int main(void) {
  char names[128][12], name[12];
  u32 sizes[128], size;
  int count = 0, index = 0, i, was = 0;
  board_init();
  cache_setup(CCR_EMODE | CCR_ICE | CCR_OCE | CCR_CB);
  can_init();
  panel_init();
  sd_idle = panel_service;
  loader_leave = stop;
  if (!mount()) {
    message("NO SUPPORTED SD CARD");
    while (!panel_return_short())
      service();
    return 1;
  }
  for (i = 0; sd_list(i, name, &size) >= 0; i++) {
    if (count < 128 && name[8] == 'N' && name[9] == 'E' && name[10] == 'S' &&
        size <= 4194304) {
      memcpy(names[count], name, 12);
      sizes[count++] = size;
    }
  }
  if (!count) {
    message("NO NES ROMS IN SD ROOT");
    while (!panel_return_short())
      service();
    return 0;
  }
  while (1) {
    char text[13];
    int turn, key;
    for (i = 0; i < 11; i++)
      text[i] = names[index][i];
    text[11] = 0;
    message(text);
    do {
      service();
      turn = panel_knob(1);
      if (panel_return_short())
        return 0;
      key = panel_key(KEY_SELECT);
      if (key && !was)
        break;
      was = key;
    } while (!turn);
    if (turn) {
      index = (index + (turn > 0 ? 1 : count - 1)) % count;
      continue;
    }
    was = 1;
    message("LOADING ROM");
    if (sd_load(names[index], rom_image, 4194304, &rom_bytes)) {
      message("ROM READ FAILED");
      delay_ms(1000);
      continue;
    }
    rom_bytes = sizes[index];
    if (!cartridge_valid(rom_image, rom_bytes)) {
      message("UNSUPPORTED OR INVALID ROM");
      delay_ms(1000);
      continue;
    }
    play();
    if (sd_init(slot)) {
      message("REINSERT SD CARD");
      if (!mount())
        return 2;
    }
  }
}
