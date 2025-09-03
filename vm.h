//
// Created by tyler on 7/30/2025.
//

//
// vm.h
// Core memory management definitions and structures
//

#ifndef VM_H
#define VM_H

#include <windows.h>

//
// This define enables code that lets us create multiple virtual address
// mappings to a single physical page.  We only/need want this if/when we
// start using reference counts to avoid holding locks while performing
// pagefile I/Os - because otherwise disallowing this makes it easier to
// detect and fix unintended failures to unmap virtual addresses properly.
//

#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 1

#define PAGE_SIZE                   4096

// 40 bit because the highest physical address is 52 bits and 2^52 / 4096 (PAGE_SIZE)
// is on the order of 2^12 so then that leaves 40 bits left
#define FRAME_NUMBER_SIZE           40

#define MB(x)                       ((x) * 1024 * 1024)

//
// This is intentionally a power of two so we can use masking to stay
// within bounds.
//

#define VIRTUAL_ADDRESS_SIZE        MB(16)

#define VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS        (VIRTUAL_ADDRESS_SIZE / sizeof (ULONG_PTR))

//
// Deliberately use a physical page pool that is approximately 1% of the
// virtual address space !
//

#define NUMBER_OF_PHYSICAL_PAGES   (VIRTUAL_ADDRESS_SIZE / (2 * PAGE_SIZE))

#define DISK_DIVISIONS              8

// For 8 disk divisions, each division is 504
#define DISK_SIZE_IN_BYTES          (VIRTUAL_ADDRESS_SIZE - PAGE_SIZE * NUMBER_OF_PHYSICAL_PAGES + PAGE_SIZE)
#define DISK_SIZE_IN_PAGES          (DISK_SIZE_IN_BYTES / PAGE_SIZE)
#define DISK_DIVISION_SIZE_IN_PAGES (DISK_SIZE_IN_PAGES / DISK_DIVISIONS)

#define BATCH_SIZE                  10

#define FREE                        1
#define ACTIVE                      2
#define MODIFIED                    3
#define STANDBY                     4

#define TRANSITION                  1
#define DISK                        0

#define INVALID                     0
#define VALID                       1

#define AUTO                        0
#define MANUAL                      1

#define REDO                        0
#define SUCCESS                     1

//
// PTE structures
//
typedef struct {
    ULONG64 valid: 1; // Will always be 1 because otherwise it'd be invalid
    ULONG64 zero: 1;
    ULONG64 frameNumber: FRAME_NUMBER_SIZE;
} validPTE;

typedef struct {
    ULONG64 invalid: 1; // Will always be 0 because otherwise it'd be valid
    ULONG64 transition: 1; // Will always be 1
    ULONG64 frameNumber: FRAME_NUMBER_SIZE;
} transitionPTE;

typedef struct {
    ULONG64 invalid: 1; // Will always be 0 because otherwise it'd be valid
    ULONG64 disk: 1; // Will always be 0
    ULONG64 diskIndex: FRAME_NUMBER_SIZE;
} diskPTE;

typedef struct {
    union {
        validPTE valid;
        transitionPTE transition;
        diskPTE disk;
        ULONG64 zero;
    };
} pte;

//
// PFN structure
//
typedef struct {
    LIST_ENTRY entry;
    pte* pte;
    ULONG64 diskIndex: FRAME_NUMBER_SIZE;
    ULONG64 status: 3; // Modified is 0; Standby is 1
} pfn;

//
// Global variables (extern declarations)
//
extern pte* ptes;
extern pfn* pfnStart;
extern PULONG_PTR vaStart;
extern PVOID transferVa;
extern PVOID diskTransferVa;

extern ULONG64 activeCount;

//
// Threads
//
extern HANDLE threadTrim;
extern HANDLE threadDiskWrite;

//
// Events
//
extern HANDLE eventStartTrim;
extern HANDLE eventStartDiskWrite;
extern HANDLE eventRedoFault;
extern HANDLE eventPagesReady;
extern HANDLE eventSystemStart;
extern HANDLE eventSystemShutdown;

//
// Function declarations
//
BOOL GetPrivilege(VOID);
PVOID initialize(ULONG64 numBytes);
VOID full_virtual_memory_test(VOID);

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
HANDLE CreateSharedMemorySection(VOID);
#endif

#endif // VM_H
