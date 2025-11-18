//
// pt.c
// Page table management implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../util.h"
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
    EnterCriticalSection(&lockPTE) ;
    page->pte = new;
    page->status = ACTIVE;
    new->valid.valid = VALID;
    new->valid.frameNumber = frameNumber;
    LeaveCriticalSection(&lockPTE);
    InterlockedIncrement64(&activeCount);
    InterlockedIncrement64(&pagesActivated);
}

pfn* standbyFree(threadInfo* info) {
    EnterCriticalSection(&lockStandbyList);
    // TODO: add case to restart thread at beginning of page fault handler
    // Set Trim Again
    ASSERT(!isEmpty(&headStandbyList));
    pfn* page = linkRemoveHead(&headStandbyList);
    // if (isEmpty(&headStandbyList)) {
    //     ResetEvent(eventPagesReady);
    //     LeaveCriticalSection(&lockPTE);
    //     LeaveCriticalSection(&lockStandbyList);
    //     SetEvent(eventStartTrim);
    //     WaitForSingleObject(eventPagesReady, INFINITE);
    //     EnterCriticalSection(&lockPTE);
    //     EnterCriticalSection(&lockStandbyList);
    // }
    LeaveCriticalSection(&lockStandbyList);

    // We already have the page table lock
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
    EnterCriticalSection(&lockPTE);
    pte* x = va2pte(arbitrary_va);
    pfn* page;
    boolean rescue = x->transition.transition == TRANSITION;
    if (rescue) {
        page = frameNumber2pfn(x->transition.frameNumber);
        // Add NULL check here
        ASSERT(page);

        LeaveCriticalSection(&lockPTE);

        // TODO: redo

        if (page->status == STANDBY) {
            ASSERT(isFull[page->diskIndex]);
            isFull[page->diskIndex] = FALSE;
        }
        linkRemovePFN(page);
    } else {
        LeaveCriticalSection(&lockPTE);
        // Now we know the pte is in zero or disk format (can't be active b/c it won't be faulted on)
        // Either way, we need a free page
        EnterCriticalSection(&lockFreeList);
        boolean free = !isEmpty(&headFreeList);
        LeaveCriticalSection(&lockFreeList);

        EnterCriticalSection(&lockStandbyList);
        boolean standby = !isEmpty(&headStandbyList);
        LeaveCriticalSection(&lockStandbyList);

        if (free) {
            EnterCriticalSection(&lockFreeList);
            page = linkRemoveHead(&headFreeList);
            ASSERT(page);
            LeaveCriticalSection(&lockFreeList);
        } else {
            if (standby) {
                page = standbyFree(info);
                ASSERT(page);
            }
            else {
                ResetEvent(eventRedoFault);
                SetEvent(eventStartTrim);
                WaitForSingleObject(eventRedoFault, INFINITE);
                return REDO;
            }

            EnterCriticalSection(&lockPTE);
            if (x->disk.diskIndex != 0) {
                readFromDisk(x->disk.diskIndex, pfn2frameNumber(page), info);
            }
            LeaveCriticalSection(&lockPTE);
        }
    }
    activatePage(page, x);
    return SUCCESS;
}