Прототип embedded-частини системи налаштування embedded пристроїв.
====================

Детальніше про систему https://github.com/den951682/ConfB

## Development Environment

- **OS**: Windows 10 x64
- **IDE**: Espressif IDE (based on Eclipse CDT)
- **Framework**: [ESP-IDF v5.3.1](https://github.com/espressif/esp-idf)
- **Toolchain**: Xtensa GCC (ESP32), bundled with ESP-IDF
- **Build system**: CMake + Ninja
- **Programming language**: C / C++
- **RTOS**: FreeRTOS (вбудований у ESP-IDF)
- **Board**: ESP32

## How to Build

1. Встановіть [ESP-IDF v5.3.1](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html#installation).
2. Імпортуйте проєкт у Espressif IDE або використовуйте CLI:
   ```bash
   idf.py set-target esp32
   idf.py build
   idf.py flash -p COM3 -b 921600
   idf.py monitor
