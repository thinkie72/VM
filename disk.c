//
// disk_manager.c
// Disk (backing store) management implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
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
    diskBytes = VIRTUAL_ADDRESS_SIZE - ((NUMBER_OF_PHYSICAL_PAGES + 1) * PAGE_SIZE);
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
    if (full) return 0;

    isFull[diskIndex] = TRUE;
    ULONG64 returnIndex = diskIndex;

    diskIndex++;
    if (diskIndex >= diskBytes / PAGE_SIZE) {
        diskIndex = 1;
    }

    return returnIndex;
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