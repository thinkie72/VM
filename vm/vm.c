//
// vm.c
// Core memory management implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "../util.h"
#include "../user/user.h"
#include "../pt/pt.h"
#include "../disk/disk.h"
#include "../list/list.h"
#include "../trim/trim.h"
#include "../diskWrite/diskWrite.h"
#include "vm.h"

#pragma comment(lib, "advapi32.lib")

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

// Global variables
pte* ptes;
pfn* pfnStart;
PULONG_PTR vaStart;
PVOID transferVa;
PVOID diskTransferVa;

LONG64 activeCount;
LONG64 pagesActivated;

HANDLE physical_page_handle;
BOOL privilege;

// Threads
HANDLE threadTrim;
HANDLE threadDiskWrite;
HANDLE threadsUser[THREADS];

// Thread info
threadInfo info[THREADS];

// Events
HANDLE eventStartTrim;
HANDLE eventStartDiskWrite;
HANDLE eventRedoFault; // User threads have something to wait on while worker threads, well, work
HANDLE eventSystemShutdown;
HANDLE eventStartUser;

VOID initializeThreads() {
    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section
    // created above.
    //

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;
    for (int i = 0; i < THREADS; i++) {
        info[i].index = i;
        info[i].transferVa = VirtualAlloc2 (NULL,
                       NULL,
                       PAGE_SIZE,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);

        threadsUser[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) threadUser, &info[i], 0, NULL);
    }
    threadTrim = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) threadPageTrimmer, NULL, 0, NULL);
    threadDiskWrite = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) threadWriteToDisk, NULL, 0, NULL);
}

VOID initializeEvents() {
    eventStartUser = CreateEvent(NULL, AUTO, FALSE, NULL);
    eventStartTrim = CreateEvent(NULL, AUTO, FALSE, NULL);
    eventStartDiskWrite = CreateEvent(NULL, AUTO, FALSE, NULL);
    eventRedoFault = CreateEvent(NULL, MANUAL, FALSE, NULL);
    eventSystemStart = CreateEvent(NULL, MANUAL, FALSE, NULL);
    eventSystemShutdown = CreateEvent(NULL, MANUAL, FALSE, NULL);
}

BOOL
GetPrivilege  (
    VOID
    )
{
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege [1];
    } Info;

    //
    // This is Windows-specific code to acquire a privilege.
    // Understanding each line of it is not so important for
    // our efforts.
    //

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    //
    // Open the token.
    //

    hProcess = GetCurrentProcess ();

    Result = OpenProcessToken (hProcess,
                               TOKEN_ADJUST_PRIVILEGES,
                               &Token);

    if (Result == FALSE) {
        printf ("Cannot open process token.\n");
        return FALSE;
    }

    //
    // Enable the privilege.
    //

    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    //
    // Get the LUID.
    //

    Result = LookupPrivilegeValue (NULL,
                                   SE_LOCK_MEMORY_NAME,
                                   &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        printf ("Cannot get privilege\n");
        return FALSE;
    }

    //
    // Adjust the privilege.
    //

    Result = AdjustTokenPrivileges (Token,
                                    FALSE,
                                    (PTOKEN_PRIVILEGES) &Info,
                                    0,
                                    NULL,
                                    NULL);

    //
    // Check the result.
    //

    if (Result == FALSE) {
        printf ("Cannot adjust token privileges %u\n", GetLastError ());
        return FALSE;
    }

    if (GetLastError () != ERROR_SUCCESS) {
        printf ("Cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy\n");
        return FALSE;
    }

    CloseHandle (Token);

    return TRUE;
}

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

HANDLE
CreateSharedMemorySection (
    VOID
    )
{
    HANDLE section;
    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Create an AWE section.  Later we deposit pages into it and/or
    // return them.
    //

    parameter.Type = MemSectionExtendedParameterUserPhysicalFlags;
    parameter.ULong = 0;

    section = CreateFileMapping2 (INVALID_HANDLE_VALUE,
                                  NULL,
                                  SECTION_MAP_READ | SECTION_MAP_WRITE,
                                  PAGE_READWRITE,
                                  SEC_RESERVE,
                                  0,
                                  NULL,
                                  &parameter,
                                  1);

    return section;
}

#endif

PVOID initialize(ULONG64 numBytes) {
    PVOID new;
    new = malloc(numBytes);
    ASSERT(new);
    memset(new, 0, numBytes);
    return new;
}

ULONG64 getMaxFrameNumber(PULONG_PTR pages) {
    ULONG64 maxFrameNumber = 0;

    for (int i = 0; i < NUMBER_OF_PHYSICAL_PAGES; ++i) {
        maxFrameNumber = max(maxFrameNumber, pages[i]);
    }
    return maxFrameNumber;
}

VOID commitSparseArray(PULONG_PTR pages) {
    ULONG64 max = getMaxFrameNumber(pages);
    max += 1;
    pfnStart = VirtualAlloc(NULL,sizeof(pfn) * max, MEM_RESERVE,PAGE_READWRITE);


    for (int i = 0; i < NUMBER_OF_PHYSICAL_PAGES; i++) {
        pfn* newpfn = (pfn*) (pfnStart + pages[i]);
        // newpfn for address
        if (PAGE_SIZE % sizeof(pfn) != 0) {
            ULONG64 x = (ULONG64) newpfn / PAGE_SIZE;
            ULONG64 y = ((ULONG64) newpfn + sizeof(pfn)) / PAGE_SIZE;
            if (x != y) {
                VirtualAlloc((PVOID) newpfn,sizeof(pfn),MEM_COMMIT,PAGE_READWRITE);
                VirtualAlloc((PVOID) ((ULONG64) newpfn + PAGE_SIZE),sizeof(pfn),MEM_COMMIT,PAGE_READWRITE);
            }
        }
        else {
            PVOID z = VirtualAlloc((PVOID) newpfn,sizeof(pfn),MEM_COMMIT,PAGE_READWRITE);
            ASSERT(z);
            ULONG64 frameNumberCheck = pfn2frameNumber(newpfn);
            ASSERT(frameNumberCheck == pages[i]);
            pfn* pfnCheck = frameNumber2pfn(pages[i]);
            ASSERT(pfnCheck == newpfn);
        }
    }
}

VOID
full_virtual_memory_test (
    VOID
    )
{
    BOOL allocated;
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    //
    // Allocate the physical pages that we will be managing.
    //
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    //

    privilege = GetPrivilege();

    if (privilege == FALSE) {
        printf ("full_virtual_memory_test : could not get privilege\n");
        return;
    }

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    physical_page_handle = CreateSharedMemorySection();

    if (physical_page_handle == NULL) {
        printf ("CreateFileMapping2 failed, error %#x\n", GetLastError ());
        return;
    }

#else

    physical_page_handle = GetCurrentProcess ();

#endif

    physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

    physical_page_numbers = malloc(physical_page_count * sizeof (ULONG_PTR));
    memset(physical_page_numbers, 0, physical_page_count * sizeof (ULONG_PTR));

    if (physical_page_numbers == NULL) {
        printf ("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return;
    }

    allocated = AllocateUserPhysicalPages (physical_page_handle,
                                           &physical_page_count,
                                           physical_page_numbers);

    if (allocated == FALSE) {
        printf ("full_virtual_memory_test : could not allocate physical pages\n");
        return;
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {

        printf ("full_virtual_memory_test : allocated only %llu pages out of %u pages requested\n",
                physical_page_count,
                NUMBER_OF_PHYSICAL_PAGES);
    }

    //
    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    //


#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section
    // created above.
    //

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    activeCount = 0;

    diskTransferVa = VirtualAlloc2 (NULL,
                       NULL,
                       BATCH_SIZE * PAGE_SIZE,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);

    vaStart = VirtualAlloc2 (NULL,
                       NULL,
                       VIRTUAL_ADDRESS_SIZE,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);


#else

    vaStart = VirtualAlloc (NULL,
                      virtual_address_size,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

#endif

    if (vaStart == NULL) {

        printf ("full_virtual_memory_test : could not reserve memory %x\n",
                GetLastError ());

        return;
    }

    ULONG64 numBytes = VIRTUAL_ADDRESS_SIZE / PAGE_SIZE * sizeof(pte);

    ptes = initialize(numBytes);

    initializeListHeads();
    initializeListLocks();
    commitSparseArray(physical_page_numbers);
    initializeDisk();

    initializeThreads();
    initializeEvents();

    // Initialize free list with all pages
    for (int j = 0; j < NUMBER_OF_PHYSICAL_PAGES; j++) {
        pfn* free = pfnStart + physical_page_numbers[j];
        linkAdd(free, &headFreeList);
        free->pte = 0;
        free->diskIndex = 0;
        free->status = 0;
    }

    SetEvent(eventSystemStart);

    SetEvent(eventStartUser);

    for (int j = 0; j < THREADS; j++) {
        WaitForSingleObject (threadsUser[j], INFINITE);
    }

    SetEvent(eventSystemShutdown);

    WaitForSingleObject (threadTrim, INFINITE);

    WaitForSingleObject (threadDiskWrite, INFINITE);

    printf ("full_virtual_memory_test : finished accessing %llu random virtual addresses\n", pagesActivated);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    VirtualFree (vaStart, 0, MEM_RELEASE);
    for (int i = 0; i < THREADS; i++) {
        VirtualFree (info[i].transferVa, 0, MEM_RELEASE);
    }
    VirtualFree (diskTransferVa, 0, MEM_RELEASE);
    VirtualFree (pfnStart, 0, MEM_RELEASE);

    free(disk);
    free(isFull);
    free(ptes);
    return;
}