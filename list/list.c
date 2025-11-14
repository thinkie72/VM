//
// list_manager.c
// List management implementation
//

#include <windows.h>
#include "../vm/vm.h"
#include "list.h"

// Global list heads
LIST_ENTRY headFreeList;
LIST_ENTRY headModifiedList;
LIST_ENTRY headStandbyList;

// Global list locks
CRITICAL_SECTION lockFreeList;
CRITICAL_SECTION lockModifiedList;
CRITICAL_SECTION lockStandbyList;

CRITICAL_SECTION lockPTE;

VOID initializeListHeads() {
    headFreeList.Flink = &headFreeList;
    headFreeList.Blink = &headFreeList;
    headModifiedList.Flink = &headModifiedList;
    headModifiedList.Blink = &headModifiedList;
    headStandbyList.Flink = &headStandbyList;
    headStandbyList.Blink = &headStandbyList;
}

VOID initializeListLocks() {
    InitializeCriticalSection(&lockFreeList);
    InitializeCriticalSection(&lockModifiedList);
    InitializeCriticalSection(&lockStandbyList);
    InitializeCriticalSection(&lockPTE);
}

VOID linkAdd(pfn* pfn, LIST_ENTRY* head) {
    pfn->entry.Flink = head;
    pfn->entry.Blink = head->Blink;
    head->Blink->Flink = &pfn->entry;
    head->Blink = &pfn->entry;
}

pfn* linkRemoveHead(LIST_ENTRY* head) {
    // Handles case with removing last element in the list
    if (head->Flink->Flink == head) {
        pfn* freePage = (pfn*) head->Flink;
        head->Flink = head;
        head->Blink = head;
        return freePage;
    }

    pfn* freePage = (pfn*) head->Flink;
    head->Flink = freePage->entry.Flink;
    head->Flink->Blink = head;
    return freePage;
}

pfn* linkRemovePFN(pfn* pfn) {
    pfn->entry.Blink->Flink = pfn->entry.Flink;
    pfn->entry.Flink->Blink = pfn->entry.Blink;
    return pfn;
}

BOOL isEmpty(LIST_ENTRY* head) {
    return (head->Flink == head);
}