cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cellsim-card)

target_sources(app PRIVATE
    src/main.c
    src/card_id.c
    src/cell.c
    src/self_test.c
    src/watchdog.c
    src/health.c
    src/network.c
    src/led.c
)
