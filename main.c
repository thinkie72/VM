//
// Created by tyler.hinkie on 6/16/2025.
// Main entry point and test functions
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "util.h"
#include "vm.h"
#include "pt.h"
#include "disk.h"
#include "list.h"

VOID
malloc_test (
    VOID
    )
{
    unsigned i;
    PULONG_PTR p;
    unsigned random_number;

    p = malloc (VIRTUAL_ADDRESS_SIZE);

    if (p == NULL) {
        printf ("malloc_test : could not malloc memory\n");
        return;
    }

    for (i = 0; i < MB (1); i += 1) {

        //
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        //

        random_number = (unsigned) (ReadTimeStampCounter() >> 4);

        random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        *(p + random_number) = (ULONG_PTR) p;
    }

    printf ("malloc_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    free (p);

    return;
}

VOID
commit_at_fault_time_test (
    VOID
    )
{
    unsigned i;
    PULONG_PTR p;
    PULONG_PTR committed_va;
    unsigned random_number;
    BOOL page_faulted;

    p = VirtualAlloc (NULL,
                      VIRTUAL_ADDRESS_SIZE,
                      MEM_RESERVE,
                      PAGE_NOACCESS);

    if (p == NULL) {
        printf ("commit_at_fault_time_test : could not reserve memory\n");
        return;
    }

    for (i = 0; i < MB (1); i += 1) {

        //
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        //

        random_number = (unsigned) (ReadTimeStampCounter() >> 4);

        random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        page_faulted = FALSE;

        __try {

            *(p + random_number) = (ULONG_PTR) p;

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }

        if (page_faulted) {

            //
            // Commit the virtual address now - if that succeeds then
            // we'll be able to access it from now on.
            //

            committed_va = p + random_number;

            committed_va = VirtualAlloc (committed_va,
                                         sizeof (ULONG_PTR),
                                         MEM_COMMIT,
                                         PAGE_READWRITE);

            if (committed_va == NULL) {
                printf ("commit_at_fault_time_test : could not commit memory\n");
                return;
            }

            //
            // No exception handler needed now since we are guaranteed
            // by virtue of our commit that the operating system will
            // honor our access.
            //

            *committed_va = (ULONG_PTR) committed_va;
        }
    }

    printf ("commit_at_fault_time_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    VirtualFree (p, 0, MEM_RELEASE);

    return;
}

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

    full_virtual_memory_test ();

    return;
}