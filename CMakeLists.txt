cmake_minimum_required(VERSION 3.22)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(snes C CXX ASM)
SET(BUILD_NAME "${PROJECT_NAME}")

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

# =========================
# OUTPUT DIRECTORIES
# =========================
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${PICO_PLATFORM}/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

# INCLUDE FILES THAT SHOULD BE COMPILED:
file(GLOB_RECURSE SRC "src/snes9x/*.*")

message(STATUS "Add source files:")
foreach (SRC_FILE IN LISTS SRC)
    message(STATUS "${SRC_FILE}")
endforeach ()
message(STATUS "")

#add_compile_options(-funroll-loops -fms-extensions -O3)
#add_link_options(-flto -fwhole-program)
# -frename-registers -fno-tree-vectorize
add_compile_options(
#        -flto=auto
#        -fwhole-program
        -ffast-math
        -ffunction-sections
        -fdata-sections
        -fms-extensions
        -O3
)

if (PICO_PLATFORM STREQUAL "host")
    add_executable(${PROJECT_NAME} ${SRC} src/main.c)
    message(WARNING "Host build!")
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
else ()


    # the following enables the system malloc/free to be wrapped during compilation
    add_definitions(-DSFE_PICO_ALLOC_WRAP)
    set(SKIP_PICO_MALLOC 1)
    set(SFE_PICO_ALLOC_WRAP 1)
    pico_sdk_init()

    add_subdirectory(sparkfun_pico)
    add_subdirectory(drivers/audio)
    add_subdirectory(drivers/hdmi)
#    add_subdirectory(drivers/vga-nextgen)
    add_subdirectory(drivers/graphics)
#    add_subdirectory(drivers/ntsc-tv)

    add_executable(${PROJECT_NAME} ${SRC} src/main.c)

    target_link_libraries(${PROJECT_NAME} PRIVATE
            pico_runtime
            pico_multicore

            sparkfun_pico

            audio
            graphics
#            vga-nextgen
            hdmi

            -Wl,--wrap=atexit,--wrap=abort,--strip-all,--gc-sections # size optimizations
    )
#    pico_set_printf_implementation(${PROJECT_NAME} none)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            PICO_FLASH_SIZE_BYTES=16777216
            HDMI
            #            VGA
#            AUDIO_PWM_PIN=26
            AUDIO_DATA_PIN=26
            AUDIO_CLOCK_PIN=27

            # Core definitions
            PICO_PRINTF_ALWAYS_INCLUDED=0
            PICO_PANIC_FUNCTION=

            SFE_RP2350_XIP_CSI_PIN=19
    )
    pico_set_linker_script(${PROJECT_NAME} "${CMAKE_SOURCE_DIR}/memmap.ld")
    pico_add_extra_outputs(${PROJECT_NAME})
#    pico_enable_stdio_usb(${PROJECT_NAME} 1)
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${BUILD_NAME}")
target_link_options(${PROJECT_NAME} PRIVATE -Xlinker --print-memory-usage --data-sections --function-sections)