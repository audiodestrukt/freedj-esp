# s3/deck changelog

## 2026-11-14: the S3 deck starts, with a round UI

- New project beside p4/deck for S3 boards. First board: Waveshare ESP32-S3-Touch-LCD-1.46
  (ESP32-S3R8, 16 MB flash; SPD2010 412×412 round QSPI LCD with touch; PCM5101 into a
  speaker, no line out; TCA9554 expander for the LCD/touch resets and SD D3; power latch on
  GPIO7). Pins in `main/board_ws146.h`, from Waveshare's wiki and demo.
- Bring-up: power latch, I2C (touch 0x53, RTC 0x51, IMU 0x6B answer), LCD, backlight PWM,
  I2S at 44.1 kHz (a tone played).
- Round UI (`main/ui/`), plain C++ shared with a PC preview (`make -C host`). One layout for
  412 and 240 px (the puck).
- Speed, 412 px: a full frame sends in 10.3 ms. First draw 84 ms (11 fps): per-pixel float maths
  is slow on the S3 (multiply-add ~11 cycles, sqrtf ~80, floorf ~60). Overview cached per
  track (5.2 -> 0.6 ms), rings filled per row with AA only on edge pixels, strobe dots instead
  of 48 AA lines (jog ring 22.6 -> 7.0 ms), title fit cached (top 7.7 -> 2.7 ms), integer
  floor/ceil, -O2, QIO flash: **23.4 ms drawing, 38.8 fps**.
