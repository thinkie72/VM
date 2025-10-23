# Virtual Memory Manager

A sophisticated usermode virtual memory management system implemented in C for Windows that demonstrates core operating system memory management concepts including page tables, physical frame management, page trimming, and disk-backed virtual memory.

> **Note**: This is a markdown (.md) file. Save as `README.md` in your project directory.

## Overview

This project implements a complete virtual memory system in userspace using Windows AWE (Address Windowing Extensions) APIs. The system manages virtual-to-physical address translations, handles page faults, performs page trimming, and implements disk-based backing store for pages that don't fit in physical memory.

## Key Features

- **Virtual Memory Management**: Full virtual address space management with configurable size (default: 16MB virtual, ~2% physical pages)
- **Page Table Management**: Custom page table entries (PTEs) with valid, transition, and disk states
- **Physical Frame Management**: PFN (Page Frame Number) database tracking physical page states
- **Multi-threaded Architecture**: Separate threads for user operations, page trimming, and disk I/O
- **Page States**: Implements Active, Modified, Standby, and Free page lists
- **Disk Backing Store**: Pages can be written to and read from simulated disk storage
- **Page Fault Handling**: Complete page fault handler with rescue logic for transition pages
- **Batch Processing**: Efficient batch operations for trimming and disk writes

## Architecture

### Core Components

#### Page Table Entry (PTE) States
- **Valid PTE**: Page is currently mapped to physical memory
- **Transition PTE**: Page is in memory but unmapped (Modified or Standby state)
- **Disk PTE**: Page has been written to backing store

#### Physical Frame Number (PFN) States
- **FREE**: Available for allocation
- **ACTIVE**: Currently mapped to a virtual address
- **MODIFIED**: Unmapped but contains modified data (needs disk write)
- **STANDBY**: Unmapped, clean, available for reuse

#### Thread Architecture

1. **Main/User Thread** (`threadUser.c`)
   - Performs random memory accesses to virtual addresses
   - Triggers page faults for unmapped pages
   - Coordinates with other threads via events

2. **Page Trimmer Thread** (`threadPageTrimmer.c`)
   - Scans active pages and unmaps them in batches
   - Moves unmapped pages to the Modified list
   - Helps maintain free page availability

3. **Disk Writer Thread** (`threadWriteToDisk.c`)
   - Takes pages from Modified list
   - Writes page contents to simulated disk storage
   - Moves pages to Standby list after write

### Key Data Structures

```c
// Page Table Entry
typedef struct {
    union {
        validPTE valid;          // Active mapping
        transitionPTE transition; // In memory, unmapped
        diskPTE disk;            // On disk
        ULONG64 zero;            // Zero page
    };
} pte;

// Page Frame Number
typedef struct {
    LIST_ENTRY entry;    // List linkage
    pte* pte;           // Back pointer to PTE
    ULONG64 diskIndex;  // Disk location if written
    ULONG64 status;     // FREE/ACTIVE/MODIFIED/STANDBY
} pfn;
```

## Building

### Requirements
- CMake 3.31 or higher
- C11 compatible compiler
- Windows OS (uses Windows-specific AWE APIs)
- Administrator privileges (for SE_LOCK_MEMORY_NAME privilege)

### Build Instructions

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Running

The program requires administrator privileges to allocate physical pages:

```bash
# Run as Administrator
.\VM.exe
```

The program will:
1. Allocate physical pages from Windows
2. Create a large virtual address space (64x physical memory size)
3. Initialize page tables, PFN database, and disk storage
4. Spawn worker threads (trimmer, disk writer)
5. Perform 10 million random memory accesses
6. Print completion statistics

## Configuration

Key constants in `vm.h`:

- `VIRTUAL_ADDRESS_SIZE`: Size of virtual address space (default: 16MB)
- `NUMBER_OF_PHYSICAL_PAGES`: Physical page pool size (default: ~2% of virtual)
- `BATCH_SIZE`: Number of pages to process per batch (default: 10)
- `PAGE_SIZE`: System page size (default: 4096 bytes)

## Thread Synchronization

The system uses several synchronization primitives:

### Critical Sections
- `lockFreeList`: Protects free page list
- `lockModifiedList`: Protects modified page list
- `lockStandbyList`: Protects standby page list
- `lockPTE`: Protects page table entries

### Events
- `eventStartTrim`: Signals trimmer to start work
- `eventStartDiskWrite`: Signals disk writer to start
- `eventRedoFault`: Signals user thread to retry page fault
- `eventSystemStart`: Initial synchronization point
- `eventSystemShutdown`: Clean shutdown signal

## Page Fault Handling Flow

1. User thread accesses unmapped virtual address → Page fault
2. Check if page is in transition state (rescue path)
   - If yes: Reactivate page from Modified/Standby list
3. If not in transition:
   - Check free list → Use free page if available
   - Check standby list → Reuse standby page if available
   - If no pages available → Wake trimmer thread and wait
4. If page was on disk → Read from disk to physical page
5. Map physical page to virtual address
6. Retry access (no fault this time)

## Memory Management Policies

- **Page Replacement**: Clock-like algorithm scanning active pages
- **Write Policy**: Copy-on-write with deferred disk writes
- **Page Reuse**: Standby pages can be rescued before reallocation
- **Disk Management**: First-fit allocation with wraparound

## Project Structure

```
VM/
├── CMakeLists.txt          # Build configuration
├── main.c                  # Entry point
├── vm.c/h                  # Core VM initialization
├── pt.c/h                  # Page table management
├── list.c/h                # List management utilities
├── disk.c/h                # Disk backing store
├── threadUser.c            # User thread implementation
├── threadPageTrimmer.c     # Page trimming thread
├── threadWriteToDisk.c     # Disk write thread
├── user.h                  # User thread interface
├── trim.h                  # Trimmer interface
├── diskWrite.h             # Disk writer interface
└── util.h                  # Utility macros (ASSERT)
```

## Performance Considerations

- Batch processing reduces lock contention
- Sparse PFN array minimizes memory overhead
- Event-driven architecture avoids busy waiting
- Critical sections protect shared data structures
- Transition pages enable fast rescue operations

## Limitations

- Windows-only (uses AWE APIs)
- Single user thread in default configuration
- Simulated disk (malloc'd memory, not actual file I/O)
- No support for shared memory between processes
- Fixed page size (4KB)

## Future Enhancements

- Multiple concurrent user threads
- Real file-backed disk storage
- Page compression
- NUMA awareness
- Memory-mapped file support
- Enhanced statistics and monitoring

## License

Educational project - use for learning purposes.

## Authors

Tyler Hinkie - Initial implementation and architecture

## Acknowledgments

Based on operating system memory management principles, particularly Windows NT memory manager design patterns.
