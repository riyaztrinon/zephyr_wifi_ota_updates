cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esp32_wifi_ota)

target_sources(app PRIVATE
    src/main.c
    src/wifi_manager.c
    src/web_server.c
    src/ota_manager.c
    src/storage.c
)

target_include_directories(app PRIVATE
    src/
)