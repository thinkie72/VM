//
// Created by tyler on 7/30/2025.
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "util.h"
#include "vm.h"
#include "pt.h"
#include "list.h"
#include "trim.h"
#include "disk.h"
#include "diskWrite.h"
#include "vm.h"

HANDLE eventStartTrim; // these need to be initialized already
HANDLE eventSystemStart;
HANDLE eventSystemShutdown;

// user thread sets trim event, wait on WaitingForPagesEvent--> wakes up trimmer, trimmer does work, trimmer sets mod write event
// --> mod writer wakes up, does work, sets waiting for pages event --> user thread wakes up


void threadPageTrimmer(void* params) {

    // initialize whatever datastructures the thread needs

    pfn* pages[BATCH_SIZE];
    PVOID batch[BATCH_SIZE];

    int i = 0;
    ULONG64 scanIndex = 0;  // Remember where we left off
    ULONG64 pagesScanned = 0;
    ULONG64 totalPages = VIRTUAL_ADDRESS_SIZE / PAGE_SIZE;

    // no shutdown waiting, most basic (EITHER have this, or the WaitForMultipleObjects, not both!)
    WaitForSingleObject(eventSystemStart, INFINITE);

    // allows for clean shutdown of thread at the end of simulation
    HANDLE events[2];
    events[0] = eventStartTrim;
    events[1] = eventSystemShutdown;

    while (TRUE) {

        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == 1) {
            // shutdown, free datastructures, return (killing the thread)
        }


        // do your work

        EnterCriticalSection(&lockPTE);

        // Scan from where we left off last time
        while (i < BATCH_SIZE && pagesScanned < totalPages) {
            pte* currentPte = &ptes[scanIndex];

            // Only process valid pages that are mapped to physical memory
            if (currentPte->valid.valid == VALID) {
                pfn* page = frameNumber2pfn(currentPte->valid.frameNumber);

                // Check if this page is active and can be trimmed
                if (page->status == ACTIVE) {
                    pages[i] = page;
                    batch[i] = pte2va(currentPte);
                    i++;
                }
            }

            // Move to next page, wrap around if needed
            scanIndex = (scanIndex + 1) % totalPages;
            pagesScanned++;
        }

        if (i != 0) {
            ASSERT(MapUserPhysicalPagesScatter(batch, i, NULL));
        }

        EnterCriticalSection(&lockModifiedList);
        for (int j = 0; j < i; j++) {
            pages[j]->pte->transition.invalid = INVALID;
            pages[j]->pte->transition.transition = TRANSITION;
            pages[j]->status = MODIFIED;
            linkAdd(pages[j], &headModifiedList);
        }
        LeaveCriticalSection(&lockPTE);
        LeaveCriticalSection(&lockModifiedList);


        // signal whoever is waiting on your work, if applicable
        SetEvent(eventStartDiskWrite); // might be the trimmer setting the mod writer event, or the mod writer setting the waiting-for-pages event for the users

    }

    // We want the thread to run forever, so we should not exit the while loop
    DebugBreak();
    return;
}

/*
// User thread needs to wait for pages!

ResetEvent(waitforpagesevent) // we want the waitforpagesevent to be manual reset, look at parameters in CreateEvent

// release locks (like pagetable lock)

SetEvent(trimEvent)

WaitForSingleObject(waitforpagesevent)

// user thread awakens!

// retry the pagefault, our information before this point might be invalid
*/