//
// Created by tyler on 7/30/2025.
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "util.h"
#include "vm.h"
#include "disk.h"
#include "diskWrite.h"
#include "list.h"
#include "pt.h"

//
// threadWriteToDisk.c
//

HANDLE eventStartDiskWrite; // these need to be initialized already
HANDLE eventSystemStart;
HANDLE eventSystemShutdown;

// user thread sets trim event, wait on WaitingForPagesEvent--> wakes up trimmer, trimmer does work, trimmer sets mod write event
// --> mod writer wakes up, does work, sets waiting for pages event --> user thread wakes up


void threadWriteToDisk(void* params) {

    // initialize whatever datastructures the thread needs

    pfn* pages[BATCH_SIZE];
    ULONG_PTR frameNumbers[BATCH_SIZE];
    ULONG64 diskAddresses[BATCH_SIZE];

    int i;

    WaitForSingleObject(eventSystemStart, INFINITE);

    // allows for clean shutdown of thread at the end of simulation
    HANDLE events[2];
    events[0] = eventStartDiskWrite;
    events[1] = eventSystemShutdown;

    while (TRUE) {

        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == 1) {
            // shutdown, free datastructures, return (killing the thread)
        }


        // do your work
        EnterCriticalSection(&lockPTE);
        EnterCriticalSection(&lockModifiedList);
        for (i = 0; i < BATCH_SIZE; i++) {
            if (isEmpty(&headModifiedList)) break;

            ULONG64 diskIndex = findFreeDiskSlot();
            if (diskIndex == 0) {
                break;
            }

            diskAddresses[i] = (ULONG64) disk + diskIndex * PAGE_SIZE;
            pages[i] = linkRemoveHead(&headModifiedList);
            frameNumbers[i] = pfn2frameNumber(pages[i]);
            pages[i]->diskIndex = diskIndex;
        }
        LeaveCriticalSection(&lockModifiedList);

        if (i != 0) {
            ASSERT(MapUserPhysicalPages(diskTransferVa, i, frameNumbers));
        }

        EnterCriticalSection(&lockStandbyList);
        for (int j = 0; j < i; j++) {
            PVOID sourceAddr = (PVOID)((ULONG64)diskTransferVa + j * PAGE_SIZE);
            PVOID destAddr = (PVOID)diskAddresses[j];

            // Check if addresses look reasonable
            ASSERT(sourceAddr != NULL && destAddr != NULL);

            ASSERT(memcpy(destAddr, sourceAddr, PAGE_SIZE));

            // something here for rescue before write or between steps
            pages[j]->status = STANDBY;
            linkAdd(pages[j], &headStandbyList);
        }
        LeaveCriticalSection(&lockPTE);
        LeaveCriticalSection(&lockStandbyList);

        // Unmap the pages
        ASSERT(MapUserPhysicalPages(diskTransferVa, i, NULL));

        // signal whoever is waiting on your work, if applicable
        SetEvent(eventPagesReady); // might be the trimmer setting the mod writer event, or the mod writer setting the waiting-for-pages event for the users
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