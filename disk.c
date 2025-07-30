//
// disk_manager.c
// Disk (backing store) management implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "util.h"
#include "vm.h"
#include "disk.h"
#include "list.h"
#include "pt.h"

// Global disk variables
ULONG64 diskBytes;
PVOID disk;
boolean* isFull;
ULONG64 diskIndex;

VOID initializeDisk() {
    diskBytes = VIRTUAL_ADDRESS_SIZE - (NUMBER_OF_PHYSICAL_PAGES + 1) * PAGE_SIZE;
    disk = initialize(diskBytes);
    isFull = initialize(diskBytes / PAGE_SIZE);
    diskIndex = 1;
}

ULONG64 findFreeDiskSlot() {
    boolean full = TRUE;
    ULONG64 startIndex = diskIndex;
    ULONG64 currentIndex = ++diskIndex;

    // Look for a free slot - search through all slots once
    do {
        // Wrap around logic
        if (currentIndex >= diskBytes / PAGE_SIZE) {
            currentIndex = 1;  // Reset to 1, not 0
        }

        full = isFull[currentIndex];
        if (!full) {
            diskIndex = currentIndex;  // Update global diskIndex
            break;
        }

        currentIndex++;

    } while (currentIndex != startIndex);  // Stop when we've made a full circle

    // If we've checked all slots and they're all full, return FALSE
    if (full) {
        ASSERT(MapUserPhysicalPages(transferVa, 1, NULL));
        return 0;
    }

    isFull[diskIndex] = TRUE;
    ULONG64 returnIndex = diskIndex;

    diskIndex++;
    if (diskIndex >= diskBytes / PAGE_SIZE) {
        diskIndex = 1;
    }

    return returnIndex;
}

VOID writeToDisk() {
    pfn* pages[BATCH_SIZE];
    ULONG_PTR frameNumbers[BATCH_SIZE];
    ULONG64 diskAddresses[BATCH_SIZE];

    int i;

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
        ASSERT(MapUserPhysicalPages(transferVa, i, frameNumbers));
    }

    EnterCriticalSection(&lockStandbyList);
    for (int j = 0; j < i; j++) {
        PVOID sourceAddr = (PVOID)((ULONG64)transferVa + j * PAGE_SIZE);
        PVOID destAddr = (PVOID)diskAddresses[j];

        // Check if addresses look reasonable
        ASSERT(sourceAddr != NULL && destAddr != NULL);

        ASSERT(memcpy(destAddr, sourceAddr, PAGE_SIZE));

        // something here for rescue before write or between steps
        pages[j]->status = STANDBY;
        linkAdd(pages[j], &headStandbyList);
    }
    LeaveCriticalSection(&lockStandbyList);

    // Unmap the pages
    ASSERT(MapUserPhysicalPages(transferVa, i, NULL));
}

void readFromDisk(ULONG64 diskIndex, ULONG64 frameNumber) {
    // reverse write to disk
    PVOID diskAddress = (PVOID) ((ULONG64) disk + diskIndex * PAGE_SIZE);

    ASSERT(MapUserPhysicalPages(transferVa, 1, &frameNumber));

    // Copy from mapped page to malloced disk
    ASSERT(memcpy(transferVa, diskAddress, PAGE_SIZE));

    ASSERT(MapUserPhysicalPages(transferVa, 1, NULL));

    isFull[diskIndex] = FALSE;
}