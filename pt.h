//
// pt.h
// Page table management declarations
//

#ifndef PT_H
#define PT_H

#include <windows.h>
#include "vm.h"

//
// Function declarations
//
pte* va2pte(PVOID va);
PVOID pte2va(pte* pte);
pfn* frameNumber2pfn(ULONG64 frameNumber);
ULONG64 pfn2frameNumber(pfn* p);

void mapandset(pfn* page, pte* new);
pfn* standbyFree(void);
VOID pageFaultHandler(PVOID arbitrary_va, PULONG_PTR pages);
VOID pageTrimmer(void);

#endif // PT_H