//
// list_manager.h
// List management declarations
//

#ifndef LIST_H
#define LIST_H

#include <windows.h>
#include "vm.h"

//
// Global list heads
//
extern LIST_ENTRY headFreeList;
extern LIST_ENTRY headModifiedList;
extern LIST_ENTRY headStandbyList;

//
// Global list locks
//
extern CRITICAL_SECTION lockFreeList;
extern CRITICAL_SECTION lockModifiedList;
extern CRITICAL_SECTION lockStandbyList;

extern CRITICAL_SECTION lockPTE;

//
// Function declarations
//
VOID initializeListHeads(void);
VOID initializeListLocks(void);
VOID linkAdd(pfn* pfn, LIST_ENTRY* head);
pfn* linkRemoveHead(LIST_ENTRY* head);
pfn* linkRemovePFN(pfn* pfn);
BOOL isEmpty(LIST_ENTRY* head);

#endif // LIST_H