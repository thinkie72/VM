cmake_minimum_required(VERSION 3.31)
project(VM C)

set(CMAKE_C_STANDARD 11)

add_executable(VM threadUser.c
        util.h
        user.h
        pt.h
        pt.c
        list.c
        list.h
        disk.c
        disk.h
        threadPageTrimmer.c
        trim.h
        threadWriteToDisk.c
        diskWrite.h
        main.c
        vm.c
        vm.h)
