//
// pt.c
// Page table management implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
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

void activatePage(pfn* page, pte* new) {
    ULONG64 frameNumber = pfn2frameNumber(page);
    ASSERT(MapUserPhysicalPages(pte2va(new), 1, &frameNumber));
    page->pte = new;
    page->status = ACTIVE;
    new->valid.valid = VALID;
    new->valid.frameNumber = frameNumber;
}

pfn* standbyFree() {
    EnterCriticalSection(&lockStandbyList);
    pfn* page = linkRemoveHead(&headStandbyList);
    if (isEmpty(&headStandbyList)) {
        ResetEvent(eventPagesReady);
        LeaveCriticalSection(&lockPTE);
        LeaveCriticalSection(&lockStandbyList);
        SetEvent(eventStartTrim);
        WaitForSingleObject(eventPagesReady, INFINITE);
        EnterCriticalSection(&lockPTE);
        EnterCriticalSection(&lockStandbyList);
    }
    LeaveCriticalSection(&lockStandbyList);

    // We already have the page table lock
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

VOID pageFaultHandler(PVOID arbitrary_va, PULONG_PTR pages) {

    // Maybe change to less than 10% or so
    if (isEmpty(&headFreeList) && isEmpty(&headStandbyList)) {
        ResetEvent(eventPagesReady);
        SetEvent(eventStartTrim);
        WaitForSingleObject(eventPagesReady, INFINITE);
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
    EnterCriticalSection(&lockPTE);
    pte* x = va2pte(arbitrary_va);
    pfn* page;
    if (x->transition.transition == TRANSITION) {
        page = frameNumber2pfn(x->transition.frameNumber);

        // Add NULL check here
        ASSERT(page != NULL);

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
            // TODO: add case and wake up trimmer thread
            ASSERT(page != NULL);
        } else {
            page = linkRemoveHead(&headFreeList);
            ASSERT(page != NULL);
            LeaveCriticalSection(&lockFreeList);
        }
        if (x->disk.disk == DISK) {
            readFromDisk(x->disk.diskIndex, pfn2frameNumber(page));
        }
    }
    printf(".");
    activatePage(page, x);
    LeaveCriticalSection(&lockPTE);
}