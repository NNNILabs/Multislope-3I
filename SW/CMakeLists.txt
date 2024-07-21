# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.12)

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_SDK_PATH}/tools/CMakeLists.txt)

# Set name of project (as PROJECT_NAME) and C/C   standards
project(Multislope C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

# include_directories(${CMAKE_SOURCE_DIR}/inc)
# link_directories(${CMAKE_SOURCE_DIR}/lib)

# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/main.c)

# Create C header file with the name <pio program>.pio.h
# pico_generate_pio_header(${PROJECT_NAME}  
#         ${CMAKE_CURRENT_LIST_DIR}/.pio
# )

# Link to pico_stdlib (gpio, time, etc. functions)
target_link_libraries(${PROJECT_NAME} 
    pico_stdlib
    hardware_pio
    hardware_gpio
    hardware_irq
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

pico_generate_pio_header(${PROJECT_NAME}  
        ${CMAKE_CURRENT_LIST_DIR}/ms.pio
)

# Enable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)