//
// pt.c
// Page table management implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "util.h"
#include "vm.h"
#include "pt.h"
#include "list.h"
#include "disk.h"

pte* va2pte(PVOID va) {
    ULONG64 index = ((ULONG_PTR)va - (ULONG_PTR) vaStart) / PAGE_SIZE;
    pte* pte = ptes + index;
    return pte;
}

PVOID pte2va (pte* pte) {
    ULONG64 index = pte - ptes;
    return (PVOID) (index * PAGE_SIZE + (ULONG_PTR) vaStart);
}

pfn* frameNumber2pfn (ULONG64 frameNumber) {
    return pfnStart + frameNumber;
}

ULONG64 pfn2frameNumber (pfn* p) {
    return (ULONG64) (p - pfnStart);
}

void mapandset(pfn* page, pte* new) {
    ULONG64 frameNumber = pfn2frameNumber(page);
    ASSERT(MapUserPhysicalPages(pte2va(new), 1, &frameNumber));
    page->pte = new;
    page->status = ACTIVE;
    new->valid.valid = VALID;
    new->valid.frameNumber = frameNumber;
}

pfn* standbyFree() {
    pfn* page;

    EnterCriticalSection(&lockStandbyList);
    page = linkRemoveHead(&headStandbyList);
    LeaveCriticalSection(&lockStandbyList);

    // TODO: page table lock
    page->pte->disk.invalid = INVALID;
    page->pte->disk.disk = DISK;
    page->pte->disk.diskIndex = page->diskIndex;

    ULONG64 frameNumber = pfn2frameNumber(page);
    ASSERT(MapUserPhysicalPages(transferVa, 1, &frameNumber));

    // Zero the page content, not the PFN structure
    memset(transferVa, 0, PAGE_SIZE);  // Use transferVa, which points to the mapped page

    ASSERT(MapUserPhysicalPages(transferVa, 1, NULL));
    return page;
}

VOID pageTrimmer() {
    pfn* pages[BATCH_SIZE];
    PVOID batch[BATCH_SIZE];

    int i = 0;
    static ULONG64 scanIndex = 0;  // Remember where we left off
    ULONG64 pagesScanned = 0;
    ULONG64 totalPages = VIRTUAL_ADDRESS_SIZE / PAGE_SIZE;

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

                // Remove from active list since we found it via page table
                linkRemovePFN(page);
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
    LeaveCriticalSection(&lockModifiedList);
}

VOID pageFaultHandler(PVOID arbitrary_va, PULONG_PTR pages) {

    // Maybe change to less than 10% or so
    if (isEmpty(&headFreeList) && isEmpty(&headStandbyList)) {
        pageTrimmer();
        writeToDisk();
    }
    //
    // Connect the virtual address now - if that succeeds then
    // we'll be able to access it from now on.
    //
    // THIS IS JUST REUSING THE SAME PHYSICAL PAGE OVER AND OVER !
    //
    // IT NEEDS TO BE REPLACED WITH A TRUE MEMORY MANAGEMENT
    // STATE MACHINE !
    //
    pte* x = va2pte(arbitrary_va);
    pfn* page;
    if (x->transition.transition == TRANSITION) {
        page = frameNumber2pfn(x->transition.frameNumber);

        // Add NULL check here
        if (page == NULL) {
            DebugBreak();
            return;
        }

        // Add bounds check for the PFN pointer
        if ((ULONG64)page < (ULONG64)pfnStart ||
            (ULONG64)page >= (ULONG64)pfnStart + (getMaxFrameNumber(pages) + 1) * sizeof(pfn)) {
            DebugBreak();
            return;
            }


        if (page->status == STANDBY) {
            isFull[page->diskIndex] = FALSE;
        }
        linkRemovePFN(page);
    } else {
        EnterCriticalSection(&lockFreeList);
        if (isEmpty(&headFreeList)) {
            LeaveCriticalSection(&lockFreeList);
            page = standbyFree();
        } else {
            page = linkRemoveHead(&headFreeList);
            LeaveCriticalSection(&lockFreeList);
        }
        if (x->disk.disk == DISK) {
            readFromDisk(x->disk.diskIndex, pfn2frameNumber(page));
        }
    }
    mapandset(page, x);
}