// The panel's Modbus RTU server, the way the CX60 wants it. 115200 8N1, the CX60 is the client.
//
// Every register 0..MB_REGS-1 has a value and a pending coil. The CX60 polls the coils with FC1, reads each set
// one with FC3 and broadcasts it to all panels with FC 0x65, which stores the value and clears the coil. Status
// comes as FC16/FC6 broadcasts to address 0. At start-up the CX60 reads the input registers with FC4 at every
// address to find the panels. Input register 0 is FW_TYPE, 2 the patch level and 3 the version. Stock has 0, 0 and 7.x.
//
// mb_frame() runs in the receive interrupt. No hardware access in here, so it runs on the PC too.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MB_REGS 0x169
#define MB_WORDS ((MB_REGS + 31) / 32)  // of a bitmap, one bit per register
#define MB_INPUTS 5
#define MB_MAX_FRAME 256

// The main loop sets addr, the rest changes in the interrupt too
typedef struct {
  uint8_t addr;
  int16_t hold[MB_REGS];                   // holding registers, our copy of the CX60's
  uint32_t pending[MB_WORDS];              // coils, changes of ours the CX60 hasn't picked up yet
  uint32_t again[MB_WORDS];                // written again while the coil was set, keep it through the next relay
  uint16_t input[MB_INPUTS];               // input registers, read at discovery
  uint32_t written[MB_WORDS];              // registers with a value, from the CX60 or from the panel's flash
  uint32_t changed[MB_WORDS];              // registers changed since the main loop last looked
  uint32_t relayed[MB_WORDS];              // last value came by FC 0x65, a panel's write, not a block of the CX60's own
  bool any_changed;                        // some bit in changed[] is set, saves the main loop a scan
  volatile uint32_t frames, errors, ours;  // frames with a good CRC, bad frames, frames addressed to us
  volatile bool enter_bootloader;          // set by FC 0x64, see mb_bootloader_requested()
} mb_t;

extern mb_t MB;

void mb_init(uint8_t addr, uint16_t version, uint8_t patch);
int mb_frame(const uint8_t *req, int n, uint8_t *resp);  // returns the reply length, 0 = no reply
int mb_chunk(const uint8_t *buf, int n, uint8_t *resp);  // one receive, which may hold several frames

// Main loop side. On the panel, mb_write(), mb_take_changed() and mb_load() need interrupts off, the interrupt
// changes those words too. The rest, and reading one register or counter of MB, are single loads. Reads that must
// agree, like a hi/lo pair or mb_pending() before mb_load(), need interrupts off too.
void mb_write(uint16_t reg, int16_t value);  // store our change and set its coil
bool mb_take_changed(uint16_t *reg);         // next register the CX60 changed or set for the first time
bool mb_written(uint16_t reg);               // reg has a value yet
bool mb_relayed(uint16_t reg);               // its value is the relay of a panel's write, see relayed[]
bool mb_pending(uint16_t reg);               // our change the CX60 hasn't picked up yet, its coil is set
void mb_load(uint16_t reg, int16_t value);   // a value from the panel's flash, handled as if the CX60 sent it
bool mb_bootloader_requested(void);          // an updater sent FC 0x64, restart into the bootloader
