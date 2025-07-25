/**
 * Emulate the audio processing unit (APU) of the Game Boy.
 * Based on MiniGBS: https://github.com/baines/MiniGBS
 */

 #pragma once

 #include <stdint.h>
 
 #define AUDIO_SAMPLE_RATE 48000.0
 
 #define DMG_CLOCK_FREQ 4194304.0
 #define TURBO_FREQ 8388608.0
 //#define DMG_CLOCK_FREQ 10000.0
 #define SCREEN_REFRESH_CYCLES 70224.0
 #define VERTICAL_SYNC (DMG_CLOCK_FREQ / SCREEN_REFRESH_CYCLES)
 
 #define AUDIO_SAMPLES ((unsigned) (AUDIO_SAMPLE_RATE / VERTICAL_SYNC))
 
 /**
  * Fill allocated buffer "data" with "len" number of 32-bit floating point
  * samples (native endian order) in stereo interleaved format.
  */
 void audio_callback(void *ptr, uint8_t *data, int len);
 
 /**
  * Read audio register at given address "addr".
  */
 uint8_t audio_read(const uint16_t addr);
 
 /**
  * Write "val" to audio register at given address "addr".
  */
 void audio_write(const uint16_t addr, const uint8_t val);
 
 /**
  * Initialize audio driver.
  */
 void audio_init(void);