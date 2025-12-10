//
// pt.c
// Page table management implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../util/util.h"
#include "../vm/vm.h"
#include "pt.h"
#include "../list/list.h"
#include "../disk/disk.h"

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

void activatePage(pfn* page, pte* new) {
    ULONG64 frameNumber = pfn2frameNumber(page);
    ASSERT(MapUserPhysicalPages(pte2va(new), 1, &frameNumber));
    page->diskIndex = 0;
    page->pte = new;
    page->status = ACTIVE;
    new->valid.valid = VALID;
    new->valid.frameNumber = frameNumber;
    InterlockedIncrement64(&activeCount);
    InterlockedIncrement64(&pagesActivated);
    printf(".");
}

pfn* standbyFree(threadInfo* info) {
    acquireLock(&lockStandbyList, USER);
    pfn* page = linkRemoveHead(&headStandbyList);
    releaseLock(&lockStandbyList, USER);
    if (page == NULL) {
        return NULL;
    }

    // We already have the page table lock
    // TODO: make separate acquire and release functions for PTE lock
    page->pte->disk.invalid = INVALID;
    page->pte->disk.disk = DISK;
    page->pte->disk.diskIndex = page->diskIndex;

    ULONG64 frameNumber = pfn2frameNumber(page);
    ASSERT(MapUserPhysicalPages(info->transferVa, 1, &frameNumber));

    // Zero the page content, not the PFN structure
    memset(info->transferVa, 0, PAGE_SIZE);  // Use transferVa, which points to the mapped page

    ASSERT(MapUserPhysicalPages(info->transferVa, 1, NULL));

    return page;
}

BOOL pageFaultHandler(PVOID arbitrary_va, threadInfo* info) {
    //
    // Connect the virtual address now - if that succeeds then
    // we'll be able to access it from now on.
    //
    // THIS IS JUST REUSING THE SAME PHYSICAL PAGE OVER AND OVER !
    //
    // IT NEEDS TO BE REPLACED WITH A TRUE MEMORY MANAGEMENT
    // STATE MACHINE !
    //
    acquireLock(&lockPTE, USER);
    pte* x = va2pte(arbitrary_va);
    if (x->valid.valid == VALID) {
        releaseLock(&lockPTE, USER);
        return SUCCESS;
    }
    pfn* page;
    boolean rescue = x->transition.transition == TRANSITION;
    if (rescue) {
        page = frameNumber2pfn(x->transition.frameNumber);
        // Add NULL check here
        ASSERT(page);
        if (page->status == STANDBY) {
            ASSERT(isFull[page->diskIndex]);
            isFull[page->diskIndex] = FALSE;
        }
        linkRemovePFN(page);
    } else {
        // Now we know the pte is in zero or disk format (can't be active b/c it won't be faulted on)
        // Either way, we need a free page
        acquireLock(&lockFreeList, USER);
        page = linkRemoveHead(&headFreeList);
        releaseLock(&lockFreeList, USER);
        if (page == NULL){
            page = standbyFree(info);
            if (page == NULL) {
                releaseLock(&lockPTE, USER);
                SetEvent(eventStartTrim);
                WaitForSingleObject(eventRedoFault, INFINITE);
                return REDO;
            }
        }

        if (x->disk.disk == DISK) {
            readFromDisk(x->disk.diskIndex, pfn2frameNumber(page), info);
        } else {
            zeroAPage(pfn2frameNumber(page), info);
        }
    }
    activatePage(page, x);
    releaseLock(&lockPTE, USER);
    return SUCCESS;
}