//
// Created by tyler.hinkie on 6/16/2025.
//

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "macros.h"
#include "util.h"

//
// This define enables code that lets us create multiple virtual address
// mappings to a single physical page.  We only/need want this if/when we
// start using reference counts to avoid holding locks while performing
// pagefile I/Os - because otherwise disallowing this makes it easier to
// detect and fix unintended failures to unmap virtual addresses properly.
//

#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 0

#pragma comment(lib, "advapi32.lib")

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

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

#define NUMBER_OF_PHYSICAL_PAGES   ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)

#define DISK_DIVISIONS              8

// For 8 disk divisions, each division is 504
#define DISK_SIZE_IN_BYTES          (VIRTUAL_ADDRESS_SIZE - PAGE_SIZE * NUMBER_OF_PHYSICAL_PAGES + PAGE_SIZE)
#define DISK_SIZE_IN_PAGES          (DISK_SIZE_IN_BYTES / PAGE_SIZE)
#define DISK_DIVISION_SIZE_IN_PAGES (DISK_SIZE_IN_PAGES / DISK_DIVISIONS)

#define BATCH_SIZE                  10

#define MODIFIED                    0
#define STANDBY                     1

#define TRANSITION                  1
#define DISK                        0

#define INVALID                     0
#define VALID                       1

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

VOID
malloc_test (
    VOID
    )
{
    unsigned i;
    PULONG_PTR p;
    unsigned random_number;

    p = malloc (VIRTUAL_ADDRESS_SIZE);

    if (p == NULL) {
        printf ("malloc_test : could not malloc memory\n");
        return;
    }

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

        random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        *(p + random_number) = (ULONG_PTR) p;
    }

    printf ("malloc_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    free (p);

    return;
}

VOID
commit_at_fault_time_test (
    VOID
    )
{
    unsigned i;
    PULONG_PTR p;
    PULONG_PTR committed_va;
    unsigned random_number;
    BOOL page_faulted;

    p = VirtualAlloc (NULL,
                      VIRTUAL_ADDRESS_SIZE,
                      MEM_RESERVE,
                      PAGE_NOACCESS);

    if (p == NULL) {
        printf ("commit_at_fault_time_test : could not reserve memory\n");
        return;
    }

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

        random_number %= VIRTUAL_ADDRESS_SIZE_IN_UNSIGNED_CHUNKS;

        //
        // Write the virtual address into each page.  If we need to
        // debug anything, we'll be able to see these in the pages.
        //

        page_faulted = FALSE;

        __try {

            *(p + random_number) = (ULONG_PTR) p;

        } __except (EXCEPTION_EXECUTE_HANDLER) {

            page_faulted = TRUE;
        }

        if (page_faulted) {

            //
            // Commit the virtual address now - if that succeeds then
            // we'll be able to access it from now on.
            //

            committed_va = p + random_number;

            committed_va = VirtualAlloc (committed_va,
                                         sizeof (ULONG_PTR),
                                         MEM_COMMIT,
                                         PAGE_READWRITE);

            if (committed_va == NULL) {
                printf ("commit_at_fault_time_test : could not commit memory\n");
                return;
            }

            //
            // No exception handler needed now since we are guaranteed
            // by virtue of our commit that the operating system will
            // honor our access.
            //

            *committed_va = (ULONG_PTR) committed_va;
        }
    }

    printf ("commit_at_fault_time_test : finished accessing %u random virtual addresses\n", i);

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //

    VirtualFree (p, 0, MEM_RELEASE);

    return;
}

// WHY?? if no need for frame Number than how should i edit my PTE formats?
typedef struct {
    ULONG64 valid: 1; // Will always be 1 because otherwise it'd be invalid
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

typedef struct {
    LIST_ENTRY entry;
    pte* pte;
    ULONG64 diskIndex: FRAME_NUMBER_SIZE;
    ULONG64 MODIFIED_OR_STANDBY: 1; // Modified is 0; Standby is 1
} pfn;

PVOID initialize(ULONG64 numBytes) {
    PVOID new;
    new = malloc(numBytes);
    memset(new, 0, numBytes);
    return new;
}

LIST_ENTRY headFreeList;
LIST_ENTRY headActiveList;
LIST_ENTRY headModifiedList;
LIST_ENTRY headStandbyList;

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

pte* ptes;
pfn* pfnStart;
PULONG_PTR vaStart;

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
    return p - pfnStart;
}

PVOID transferVa;
ULONG64 diskBytes;
PVOID disk;
boolean* isFull;
ULONG64 diskIndex;

VOID initializeDisk() {
    diskBytes = VIRTUAL_ADDRESS_SIZE - (NUMBER_OF_PHYSICAL_PAGES + 1) * PAGE_SIZE;
    disk = initialize(diskBytes);
    isFull = initialize(diskBytes / PAGE_SIZE);
    diskIndex = 1;
}

ULONG64 findFreeDiskSlot() {
    boolean full = TRUE;
    ULONG64 startIndex = diskIndex;
    ULONG64 currentIndex = ++diskIndex;

    // Look for a free slot - search through all slots once
    do {
        // Wrap around logic
        if (currentIndex >= diskBytes / PAGE_SIZE) {
            currentIndex = 1;  // Reset to 1, not 0
        }

        full = isFull[currentIndex];
        if (!full) {
            diskIndex = currentIndex;  // Update global diskIndex
            break;
        }

        currentIndex++;

    } while (currentIndex != startIndex);  // Stop when we've made a full circle

    // If we've checked all slots and they're all full, return FALSE
    if (full) {
        MapUserPhysicalPages(transferVa, 1, NULL);
        return 0;
    }

    isFull[diskIndex] = TRUE;
    diskIndex++;
    if (diskIndex >= diskBytes / PAGE_SIZE) {
        diskIndex = 1;
    }

    return diskIndex;
}

// Also need to modify writeToDisk to return the actual disk index used
VOID writeToDisk() {
    pfn* pages[BATCH_SIZE];
    ULONG64 diskAddresses[BATCH_SIZE];

    int i;

    for (i = 0; i < BATCH_SIZE; i++) {
        if (isEmpty(&headModifiedList)) break;

        ULONG64 diskIndex = findFreeDiskSlot();
        if (diskIndex == 0) {
            break;
        }

        diskAddresses[i] = (ULONG64) disk + diskIndex * PAGE_SIZE;

        // grab a page from the active list and move to modified
        pages[i] = linkRemoveHead(&headModifiedList);
        pages[i]->diskIndex = diskIndex;
    }

    if (i != 0) {
        MapUserPhysicalPagesScatter(transferVa, i, (PULONG_PTR) pages);
    }

    for (int j = 0; j < i; j++) {
        ULONG64 address = BATCH_SIZE * 64 * NUMBER_OF_PHYSICAL_PAGES * PAGE_SIZE + j * PAGE_SIZE;
        memcpy(&diskAddresses[j], &address, PAGE_SIZE);
        pages[j]->MODIFIED_OR_STANDBY = STANDBY;
        linkAdd(pages[j], &headStandbyList);
    }

    // Unmap the transfer VA
    MapUserPhysicalPages(transferVa, i, NULL);
}

void readFromDisk(ULONG64 diskIndex, ULONG64 frameNumber) {
    // reverse write to disk
    PVOID diskAddress = (PVOID) ((ULONG64) disk + diskIndex * PAGE_SIZE);

    if (!MapUserPhysicalPages(transferVa, 1, &frameNumber)) {
        DebugBreak();
        perror("MapUserPhysicalPages failed");
        exit(1);
    }

    // Copy from mapped page to malloced disk
    ASSERT(memcpy(transferVa, diskAddress, PAGE_SIZE));

    MapUserPhysicalPages(transferVa, 1, NULL);

    isFull[diskIndex] = FALSE;
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
        ULONG64 address = (ULONG64) (pfnStart + pages[i]);
        if (PAGE_SIZE % sizeof(pfn) != 0) {
            ULONG64 x = address / PAGE_SIZE;
            ULONG64 y = (address + sizeof(pfn)) / PAGE_SIZE;
            if (x != y) {
                address &= ~(PAGE_SIZE - 1);
                VirtualAlloc((PVOID) address,sizeof(pfn),MEM_COMMIT,PAGE_READWRITE);
                VirtualAlloc((PVOID) ((PULONG_PTR) address + PAGE_SIZE),sizeof(pfn),MEM_COMMIT,PAGE_READWRITE);
            }
        }
        else {
            PVOID z = VirtualAlloc((PVOID) address,sizeof(pfn),MEM_COMMIT,PAGE_READWRITE);
            ASSERT(z);
        }
    }
}

VOID initializeListHeads(PULONG_PTR pages) {
    headFreeList.Flink = &headFreeList;
    headFreeList.Blink = &headFreeList;
    headActiveList.Flink = &headActiveList;
    headActiveList.Blink = &headActiveList;
}
VOID pageTrimmer() {
    pfn* pages[BATCH_SIZE];
    PVOID batch[BATCH_SIZE];

    int i;

    for (i = 0; i < 10; i++) {
        if (isEmpty(&headActiveList)) break;
        // grab a page from the active list and move to modified
        pages[i] = linkRemoveHead(&headActiveList);
        batch[i] = pte2va(pages[i]->pte);
    }

    if (i != 0) {
        MapUserPhysicalPagesScatter(batch, i, NULL);
    }

    for (int j = 0; j < i; j++) {
        pages[i]->MODIFIED_OR_STANDBY = MODIFIED;
        pages[i]->pte->transition.invalid = 0;
        linkAdd(pages[i], &headModifiedList);
    }
    // // Remove a page from the active list
    // pfn* page = linkRemove(&headActiveList);
    //
    // // Unmap (destroy) the page's virtual memory
    // PVOID va = pte2va(page->pte);
    // MapUserPhysicalPages(va, 1, NULL);
    //
    // ULONG64 actualDiskIndex;
    // if (writeToDisk(page, &actualDiskIndex)) {
    //     // Set up the invalid PTE with the correct disk index
    //     page->pte->invalid.invalid = 0;
    //     page->pte->invalid.diskIndex = actualDiskIndex;
    // } else {
    //     // Disk is full - just mark as invalid with no backing store
    //     page->pte->full = 0;  // Zero the entire PTE
    // }
    //
    // // Put the page on the modified list
    // linkAdd(page, &headModifiedList);

    return;
}
//
// VOID rescue(pfn* pfn, PVOID va) {
//     // add page to active
//     if (pfn->MODIFIED_OR_STANDBY == MODIFIED) {
//         linkRemove(pfn);
//         ULONG64 frameNumber = pfn->pte->transition.frameNumber;
//         MapUserPhysicalPages(va, 1, &frameNumber);
//         pfn->pte->valid.valid = 1;
//         pfn->pte->valid.frameNumber = frameNumber;
//         linkAdd(pfn, &headActiveList);
//     } else {
//         // do we have to evict previous disk write
//         ULONG64 frameNumber = pfn->pte->transition.frameNumber;
//         isFull[pfn->diskIndex] = FALSE;
//         // do i need this
//         pfn->diskIndex = 0;
//         MapUserPhysicalPages(va, 1, &frameNumber);
//         pfn->pte->valid.valid = 1;
//         pfn->pte->valid.frameNumber = frameNumber;
//         linkAdd(pfn, &headActiveList);
//     }
// }
//
// VOID repurpose() {
//     // Remove a page from the standby list
//     pfn* page = linkRemove(&headStandbyList);
//
//     // Unmap (destroy) the page's virtual memory
//     PVOID va = pte2va(page->pte);
//     MapUserPhysicalPages(va, 1, NULL);
//
//     page->pte->disk.invalid = INVALID;
//     page->pte->disk.disk = DISK;
//     page->pte->disk.diskIndex = page->diskIndex;
//
//
//
//     // Put the page on the active list
//     linkAdd(page, &headActiveList);
// }
//
// LIST_ENTRY* getFreePage() {
//     LIST_ENTRY* head = &headFreeList;
//     if (isEmpty(&headFreeList)) {
//         head = &headStandbyList;
//     }
//     return head->Flink;
// }

void MAS(pfn* page, pte* new) {
    ULONG64 frameNumber = pfn2frameNumber(page);
    MapUserPhysicalPages(pte2va(new), 1, &frameNumber);
    linkAdd(page, &headActiveList);
    new->valid.valid = VALID;
    new->valid.frameNumber = frameNumber;
}

pfn* standbyFree() {
    pfn* page = linkRemoveHead(&headStandbyList);
    page->pte->disk.invalid = INVALID;
    page->pte->disk.disk = DISK;
    page->pte->disk.diskIndex = page->diskIndex;
    ULONG64 frameNumber = pfn2frameNumber(page);
    MapUserPhysicalPages(transferVa, 1, &frameNumber);
    memset(page, 0, PAGE_SIZE);
    MapUserPhysicalPages(transferVa, 1, NULL);
    return page;
}

VOID pageFaultHandler(PVOID arbitrary_va) {

    if (isEmpty(&headFreeList) && isEmpty(&headStandbyList)) {
        pageTrimmer();
        writeToDisk();
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
    pte* x = va2pte(arbitrary_va);
    pfn* page;
    if (x->transition.transition == TRANSITION) {
        page = frameNumber2pfn(x->transition.frameNumber);
        if (page->MODIFIED_OR_STANDBY == STANDBY) {
            isFull[page->diskIndex] = FALSE;
        }
        linkRemovePFN(page);
    } else {
        if (isEmpty(&headFreeList)) {
            page = standbyFree();
        } else {
            page = linkRemoveHead(&headFreeList);
        }
        if (x->disk.disk == DISK) {
            readFromDisk(x->disk.diskIndex, pfn2frameNumber(page));
        }
    }
    MAS(page, x);
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

    transferVa = VirtualAlloc (NULL,
                      BATCH_SIZE * virtual_address_size,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);


    initializeListHeads(physical_page_numbers);
    commitSparseArray(physical_page_numbers);

    initializeDisk();

    // WHY??
    for (int i = 0; i < NUMBER_OF_PHYSICAL_PAGES; i++) {
        pfn* p = pfnStart + physical_page_numbers[i];
        linkAdd(p, &headFreeList);
        p->pte = 0;
        p->diskIndex = 0;

    }
    //

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
            pageFaultHandler(arbitrary_va);

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

VOID
main (
    int argc,
    char** argv
    )
{
    //
    // Test a simple malloc implementation - we call the operating
    // system to pay the up front cost to reserve and commit everything.
    //
    // Page faults will occur but the operating system will silently
    // handle them under the covers invisibly to us.
    //

    // malloc_test ();

    //
    // Test a slightly more complicated implementation - where we reserve
    // a big virtual address range up front, and only commit virtual
    // addresses as they get accessed.  This saves us from paying
    // commit costs for any portions we don't actually access.  But
    // the downside is what if we cannot commit it at the time of the
    // fault !
    //

    // commit_at_fault_time_test ();

    //
    // Test our very complicated usermode virtual implementation.
    //
    // We will control the virtual and physical address space management
    // ourselves with the only two exceptions being that we will :
    //
    // 1. Ask the operating system for the physical pages we'll use to
    //    form our pool.
    //
    // 2. Ask the operating system to connect one of our virtual addresses
    //    to one of our physical pages (from our pool).
    //
    // We would do both of those operations ourselves but the operating
    // system (for security reasons) does not allow us to.
    //
    // But we will do all the heavy lifting of maintaining translation
    // tables, PFN data structures, management of physical pages,
    // virtual memory operations like handling page faults, materializing
    // mappings, freeing them, trimming them, writing them out to backing
    // store, bringing them back from backing store, protecting them, etc.
    //
    // This is where we can be as creative as we like, the sky's the limit !
    //

    full_virtual_memory_test ();

    return;
}