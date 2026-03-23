cmake_minimum_required(VERSION 3.16)
project(dualsense VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Debug sanitizers
option(DUALSENSE_SANITIZERS "Enable ASan + UBSan in debug builds" OFF)
if(DUALSENSE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Library sources
set(LIB_SOURCES
    src/dualsense.c
    src/crc32.c
    src/triggers.c
    src/hidraw.c
    src/haptics.c
    src/input.c
)

# Shared library
add_library(dualsense SHARED ${LIB_SOURCES})
target_include_directories(dualsense PUBLIC include)
target_include_directories(dualsense PRIVATE src)
target_link_libraries(dualsense PRIVATE m pthread)
set_target_properties(dualsense PROPERTIES
    VERSION 0.1.0
    SOVERSION 0
    OUTPUT_NAME dualsense
)

# Static library
add_library(dualsense_static STATIC ${LIB_SOURCES})
target_include_directories(dualsense_static PUBLIC include)
target_include_directories(dualsense_static PRIVATE src)
target_link_libraries(dualsense_static PRIVATE m pthread)
set_target_properties(dualsense_static PROPERTIES
    OUTPUT_NAME dualsense
)

# CLI tool
add_executable(dsctl cli/dsctl.c)
target_link_libraries(dsctl PRIVATE dualsense)

# Daemon
add_executable(dualsensed
    daemon/dualsensed.c
    daemon/json_parse.c
    daemon/native_protocol.c
    daemon/dsx_protocol.c
)
target_include_directories(dualsensed PRIVATE daemon)
target_link_libraries(dualsensed PRIVATE dualsense)

# Haptics test tool
add_executable(ds-haptics-test cli/ds-haptics-test.c)
target_include_directories(ds-haptics-test PRIVATE src)
target_link_libraries(ds-haptics-test PRIVATE dualsense m)

# Tests
enable_testing()

add_executable(test_crc32 test/test_crc32.c src/crc32.c)
target_include_directories(test_crc32 PRIVATE src)
add_test(NAME crc32 COMMAND test_crc32)

add_executable(test_triggers test/test_triggers.c src/triggers.c)
target_include_directories(test_triggers PRIVATE src)
target_link_libraries(test_triggers PRIVATE m)
add_test(NAME triggers COMMAND test_triggers)

# Install targets
include(GNUInstallDirs)

install(TARGETS dualsense dualsense_static dsctl dualsensed
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES include/dualsense.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES udev/99-dualsense.rules
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d
)

install(FILES daemon/dualsensed.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user
)

# pkg-config
configure_file(dualsense.pc.in dualsense.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/dualsense.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Haptics test tool install
install(TARGETS ds-haptics-test
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# GUI script install
install(PROGRAMS gui/dualsense-gui.py
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME dualsense-gui
)
