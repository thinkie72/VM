//
// vm.c
// Core memory management implementation
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "util.h"
#include "vm.h"
#include "pt.h"
#include "disk.h"
#include "list.h"
#include "trim.h"
#include "diskWrite.h"

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

// Threads
HANDLE threadTrim;
HANDLE threadDiskWrite;

// Events
HANDLE eventStartTrim;
HANDLE eventStartDiskWrite;
HANDLE eventPagesReady; // User threads have something to wait on while worker threads, well, work
HANDLE eventSystemShutdown;

VOID initializeThreads() {
    threadTrim = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) threadPageTrimmer, NULL, 0, NULL);
    threadDiskWrite = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) threadWriteToDisk, NULL, 0, NULL);
}

VOID initializeEvents() {
    eventStartTrim = CreateEvent(NULL, AUTO, FALSE, NULL);
    eventStartDiskWrite = CreateEvent(NULL, AUTO, FALSE, NULL);
    eventPagesReady = CreateEvent(NULL, MANUAL, FALSE, NULL);
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
    unsigned i;
    PULONG_PTR p;
    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL allocated;
    BOOL page_faulted;
    BOOL privilege;
    BOOL obtained_pages;
    ULONG_PTR physical_page_count;
    PULONG_PTR physical_page_numbers;
    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;
    ULONG_PTR virtual_address_size_in_unsigned_chunks;

    //
    // Allocate the physical pages that we will be managing.
    //
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    //

    privilege = GetPrivilege ();

    if (privilege == FALSE) {
        printf ("full_virtual_memory_test : could not get privilege\n");
        return;
    }

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    physical_page_handle = CreateSharedMemorySection ();

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

    virtual_address_size = 64 * physical_page_count * PAGE_SIZE;

    virtual_address_size_in_unsigned_chunks =
                        virtual_address_size / sizeof (ULONG_PTR);

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section
    // created above.
    //

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    diskTransferVa = VirtualAlloc2 (NULL,
                       NULL,
                       BATCH_SIZE * PAGE_SIZE,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);

    transferVa = VirtualAlloc2 (NULL,
                       NULL,
                       PAGE_SIZE,
                       MEM_RESERVE | MEM_PHYSICAL,
                       PAGE_READWRITE,
                       &parameter,
                       1);

    vaStart = VirtualAlloc2 (NULL,
                       NULL,
                       virtual_address_size,
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

    ULONG64 numBytes = virtual_address_size / PAGE_SIZE * sizeof(pte);

    ptes = initialize(numBytes);

    initializeListHeads();
    initializeListLocks();
    commitSparseArray(physical_page_numbers);
    initializeDisk();

    initializeThreads();
    initializeEvents();

    // Initialize free list with all pages
    for (int i = 0; i < NUMBER_OF_PHYSICAL_PAGES; i++) {
        pfn* p = pfnStart + physical_page_numbers[i];
        linkAdd(p, &headFreeList);
        p->pte = 0;
        p->diskIndex = 0;
        p->status = 0;
    }

    SetEvent(eventSystemStart);

    //
    // Now perform random accesses.
    //

    for (i = 0; i < MB (1); i += 1) {

        //
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        //

        random_number = (unsigned) (ReadTimeStampCounter() >> 4);

        random_number %= virtual_address_size_in_unsigned_chunks;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        page_faulted = FALSE;

        //
        // Ensure the write to the arbitrary virtual address doesn't
        // straddle a PAGE_SIZE boundary just to keep things simple for
        // now.
        //

        random_number &= ~0x7;

        arbitrary_va = vaStart + random_number;

        __try {

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }

        if (page_faulted) {
            pageFaultHandler(arbitrary_va, physical_page_numbers);

            //
            // No exception handler needed now since we have connected
            // the virtual address above to one of our physical pages
            // so no subsequent fault can occur.
            //

            *arbitrary_va = (ULONG_PTR) arbitrary_va;

            //
            // Unmap the virtual address translation we installed above
            // now that we're done writing our value into it.
            //

#if 0
            if (MapUserPhysicalPages (arbitrary_va, 1, NULL) == FALSE) {

                printf ("full_virtual_memory_test : could not unmap VA %p\n", arbitrary_va);

                return;
            }
#endif
            // committed_va = p + random_number;
            // committed_va = VirtualAlloc(committed_va, sizeof(ULONG
        }
    }

    printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    VirtualFree (vaStart, 0, MEM_RELEASE);

    return;
}