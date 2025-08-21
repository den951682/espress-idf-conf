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

## Як прошити файлами з релізу
Використайте [ESP-FLASH Tool ](https://dl.espressif.com/public/flash_download_tool.zip)
Скачайте файли з відповідного релізу.
Виставте їх і відповідні їм адреси в ESP-FLASH Tool, встановіть правильний порт, до якого підключена ESP

<p align="center">
    <img src="https://github.com/den951682/misc/blob/main/espressif.png" alt="Description" width="500">
</p>

Натисніть старт

Адереси можна взяти з наступного шаблону, або напряму виконати його
```bash
 python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 2MB --flash_freq 40m 0x1000 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\app-template.bin
```

## How to Build

1. Встановіть [ESP-IDF v5.3.1](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html#installation).

2. Імпортуйте проєкт у Espressif IDE або використовуйте CLI:
   ```bash
   idf.py set-target esp32
   idf.py build
   idf.py flash -p COM3 -b 921600
   idf.py monitor

Вкажіть правильний порт.
   
## Ініціалізація прото моделі
1. виконай скрипт protogen.bat (в середовищі Windows), todo // в idf не працює, налаштувати
2. вручну зроби #include "descriptor.pb.h" в proto-model/nanopb.pb.h

## Menuconfig
idf.py menuconfig дозволяє виставити деякі параметри в секціїї Conf Configuration - протокол зв'язку, початкова пассфраза, параметри безпечного блютуз іт.п.
