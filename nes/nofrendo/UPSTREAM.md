Source: https://github.com/espressif/esp32-nesemu components/nofrendo
Per-file LGPL v2 notices retained; upstream README identifies GPLv2. Treat combined NES distribution as GPLv2.
Local port: endian selection, standalone frame stepping, memory-only cartridge loader, restricted mapper list.

Revision: 693e378643dd4810665e52c0ec02bd9f64559c50
Archive SHA256: b28ad6b7808a6abaed39b9310a1171d9ad9508696a34ff8808fbe308e91a0cab
Audio accumulators converted to Q8 integers; scanline timing uses integer twelfths of a CPU cycle. CPU word fetches use bytes, including zero-page wrap and bank crossings.

Controller serial reads saturate after eight buttons, returning the hardware idle bit.
