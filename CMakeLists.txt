cmake_minimum_required(VERSION 3.31)
project(VM C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Source files
set(VM_SOURCES
        main.c
        vm/vm.c
        pt/pt.c
        list/list.c
        disk/disk.c
        user/threadUser.c
        trim/threadPageTrimmer.c
        diskWrite/threadWriteToDisk.c
)

# Header files (for IDE support)
set(VM_HEADERS
        vm/vm.h
        pt/pt.h
        list/list.h
        disk/disk.h
        user/user.h
        trim/trim.h
        diskWrite/diskWrite.h
        util.h
)

# Create executable
add_executable(VM ${VM_SOURCES} ${VM_HEADERS})

# Windows-specific settings
if(WIN32)
    target_compile_definitions(VM PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(VM PRIVATE /W4)
else()
    target_compile_options(VM PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Set output directory
set_target_properties(VM PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Enable debugging symbols in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(VM PRIVATE $<$<C_COMPILER_ID:MSVC>:/Zi>)
endif()