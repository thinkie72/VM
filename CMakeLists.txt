cmake_minimum_required(VERSION 3.31)
project(VM C)

set(CMAKE_C_STANDARD 11)

add_executable(VM vm.c
        macros.h
        util.h
        vm.h
        pt.h
        pt.c
        list.c
        list.h
        disk.c
        disk.h)
