cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include(pico_sdk_import.cmake)

add_subdirectory(lib mpr121)

project(audioSynth-project C CXX ASM)

pico_sdk_init()

add_executable(audioSynth)

pico_enable_stdio_usb(audioSynth 0)
pico_enable_stdio_uart(audioSynth 1)

# must match with pio filename and executable name from above
pico_generate_pio_header(audioSynth ${CMAKE_CURRENT_LIST_DIR}/hsync.pio)
pico_generate_pio_header(audioSynth ${CMAKE_CURRENT_LIST_DIR}/vsync.pio)
pico_generate_pio_header(audioSynth ${CMAKE_CURRENT_LIST_DIR}/rgb.pio)

pico_generate_pio_header(audioSynth ${CMAKE_CURRENT_LIST_DIR}/SPIPIO.pio)

# must match with executable name and source file names
target_sources(audioSynth PRIVATE audioSynth.c TFTMaster.c glcdfont.c)

# Add pico_multicore which is required for multicore functionality
target_link_libraries(audioSynth PRIVATE pico_time pico-mpr121 
		      pico_stdlib pico_divider pico_multicore
		      hardware_sync hardware_spi hardware_pio hardware_dma 
		      hardware_adc hardware_irq hardware_clocks hardware_pll 
		      hardware_gpio hardware_pwm hardware_exception hardware_i2c)

# create map/bin/hex file etc.
pico_add_extra_outputs(audioSynth)
