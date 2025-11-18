#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "user.h"
#include "../pt/pt.h"
#include "../vm/vm.h"

HANDLE eventSystemStart; // these need to be initialized already
HANDLE eventSystemShutdown;
// user thread sets trim event, wait on WaitingForPagesEvent--> wakes up trimmer, trimmer does work, trimmer sets mod write event
// --> mod writer wakes up, does work, sets waiting for pages event --> user thread wakes up


VOID threadUser(LPVOID lpParameter) {

    // initialize whatever datastructures the thread needs

    PULONG_PTR arbitrary_va = vaStart;

    BOOL redo = FALSE;
    BOOL trySameAddress = FALSE;

    // no shutdown waiting, most basic (EITHER have this, or the WaitForMultipleObjects, not both!)
    WaitForSingleObject(eventSystemStart, INFINITE);

    // allows for clean shutdown of thread at the end of simulation
    HANDLE events[2];
    events[0] = eventStartUser;
    events[1] = eventSystemShutdown;

    while (TRUE) {

        for (int i = 0; i < MB (10); i += 1) {

            BOOL page_faulted = FALSE;

            if (!trySameAddress) {
                unsigned random_number = (unsigned) (ReadTimeStampCounter() >> 4);
                random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

                random_number &= ~0x7;
                arbitrary_va = vaStart + random_number;
            }

            __try {

                *arbitrary_va = (ULONG_PTR) arbitrary_va;
                // printf("noah");

            } __except (EXCEPTION_EXECUTE_HANDLER) {

                page_faulted = TRUE;
            }

            if (page_faulted) {
                do {
                    redo = pageFaultHandler(arbitrary_va, (threadInfo *) lpParameter);
                    printf("fault");
                } while (redo);

                trySameAddress = TRUE;


                // No exception handler needed now since we have connected
                // the virtual address above to one of our physical pages
                // so no subsequent fault can occur.
                //

                //
                // Unmap the virtual address translation we installed above
                // now that we're done writing our value into it.
                //

#if 0
                if (MapUserPhysicalPages (arbitrary_va, 1, NULL) == FALSE) {

                    printf ("full_virtual_memory_test : could not unmap VA %p\n", arbitrary_va);

                    return;
                }
#endif
                // committed_va = p + random_number;
                // committed_va = VirtualAlloc(committed_va, sizeof(ULONG
            } else {
                trySameAddress = FALSE;
            }
        }

        return;
    }
}

/*
// User thread needs to wait for pages!

ResetEvent(waitforpagesevent) // we want the waitforpagesevent to be manual reset, look at parameters in CreateEvent

// release locks (like pagetable lock)

SetEvent(trimevent)

WaitForSingleObject(waitforpagesevent)

// user thread awakens!

// retry the pagefault, our information before this point might be invalid
*/