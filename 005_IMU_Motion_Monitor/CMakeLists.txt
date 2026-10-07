cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(psoc_e84_imu_motion_monitor)

target_sources(app PRIVATE src/main.c)
