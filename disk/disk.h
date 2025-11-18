//
// disk_manager.h
// Disk (backing store) management declarations
//

#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include <windows.h>
#include "../vm/vm.h"

//
// Global disk variables
//
extern ULONG64 diskBytes;
extern PVOID disk;
extern boolean* isFull;
extern ULONG64 diskIndex;
extern volatile LONG64 numFreeDiskSlots;

//
// Function declarations
//
VOID initializeDisk(void);
ULONG64 findFreeDiskSlot(void);
void readFromDisk(ULONG64 diskIndex, ULONG64 frameNumber, threadInfo* info);

#endif // DISK_MANAGER_H