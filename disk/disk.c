//
// disk_manager.c
// Disk (backing store) management implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../util/util.h"
#include "../vm/vm.h"
#include "disk.h"

// Global disk variables
ULONG64 diskBytes;
PVOID disk;
boolean* isFull;
ULONG64 diskIndex;
volatile LONG64 numFreeDiskSlots;

VOID initializeDisk() {
    // diskBytes = VIRTUAL_ADDRESS_SIZE - (NUMBER_OF_PHYSICAL_PAGES - 2) * PAGE_SIZE;
    diskBytes = VIRTUAL_ADDRESS_SIZE;
    numFreeDiskSlots = diskBytes;
    disk = initialize(diskBytes);
    isFull = initialize(diskBytes / PAGE_SIZE);
    isFull[0] = TRUE;
    diskIndex = 1;
}

ULONG64 findFreeDiskSlot() {
    boolean full = TRUE;
    ULONG64 startIndex = diskIndex;
    ULONG64 currentIndex = diskIndex;

    // Look for a free slot - search through all slots once
    do {
        // Wrap around logic
        if (currentIndex >= diskBytes / PAGE_SIZE) {
            currentIndex = 0;  // Reset to 0, not 1 b/c there's a ++ to current index
        }

        full = isFull[currentIndex];
        if (!full) {
            diskIndex = currentIndex;
            // Update global diskIndex
            break;
        }

        currentIndex++;

    } while (currentIndex != startIndex);  // Stop when we've made a full circle

    // If we've checked all slots and they're all full, return FALSE
    if (full) return 0;

    isFull[diskIndex] = TRUE;
    InterlockedIncrement64(&numFreeDiskSlots);
  //  ASSERT(numFreeDiskSlots < VIRTUAL_ADDRESS_SIZE);
    ULONG64 returnIndex = diskIndex;

    diskIndex++;
    if (diskIndex >= diskBytes / PAGE_SIZE) {
        diskIndex = 1;
    }

    return returnIndex;
}

void readFromDisk(ULONG64 readIndex, ULONG64 frameNumber, threadInfo* info) {
    // reverse write to disk
    PVOID diskAddress = (PVOID) ((ULONG64) disk + readIndex * PAGE_SIZE);

    ASSERT(MapUserPhysicalPages(info->transferVa, 1, &frameNumber));

    // Copy from mapped page to malloced disk
    ASSERT(memcpy(info->transferVa, diskAddress, PAGE_SIZE));

    ASSERT(MapUserPhysicalPages(info->transferVa, 1, NULL));
    
    isFull[readIndex] = FALSE;
    InterlockedDecrement64(&numFreeDiskSlots);
    ASSERT(numFreeDiskSlots >= 0);
}