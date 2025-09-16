//
// Created by tyler.hinkie on 6/16/2025.
// Main entry point and test functions
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "util.h"
#include "user.h"
#include "pt.h"
#include "disk.h"
#include "list.h"

VOID
main (
    int argc,
    char** argv
    )
{
    //
    // Test a simple malloc implementation - we call the operating
    // system to pay the up front cost to reserve and commit everything.
    //
    // Page faults will occur but the operating system will silently
    // handle them under the covers invisibly to us.
    //

    // malloc_test ();

    //
    // Test a slightly more complicated implementation - where we reserve
    // a big virtual address range up front, and only commit virtual
    // addresses as they get accessed.  This saves us from paying
    // commit costs for any portions we don't actually access.  But
    // the downside is what if we cannot commit it at the time of the
    // fault !
    //

    // commit_at_fault_time_test ();

    //
    // Test our very complicated usermode virtual implementation.
    //
    // We will control the virtual and physical address space management
    // ourselves with the only two exceptions being that we will :
    //
    // 1. Ask the operating system for the physical pages we'll use to
    //    form our pool.
    //
    // 2. Ask the operating system to connect one of our virtual addresses
    //    to one of our physical pages (from our pool).
    //
    // We would do both of those operations ourselves but the operating
    // system (for security reasons) does not allow us to.
    //
    // But we will do all the heavy lifting of maintaining translation
    // tables, PFN data structures, management of physical pages,
    // virtual memory operations like handling page faults, materializing
    // mappings, freeing them, trimming them, writing them out to backing
    // store, bringing them back from backing store, protecting them, etc.
    //
    // This is where we can be as creative as we like, the sky's the limit !
    //
    // TODO: consider adding tick count to benchmark time elapsed

    ULONG64 start = GetTickCount64();
    full_virtual_memory_test ();
    ULONG64 end = GetTickCount64();
    printf("Date: %s", "08.05.2025");
    printf("%llu", end - start);
    return;
}