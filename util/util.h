//
// Created by tyler.hinkie on 7/24/2025.
//

#ifndef UTIL_H
#define UTIL_H

#include <winbase.h>

#define USER 1
#define WRITER 2
#define TRIMMER 3

#define DBG 1
#if DBG
#define ASSERT(x) if (!(x)) DebugBreak();
#else
#define ASSERT(x)
#endif

#define BUFFER_SIZE 100
#define MAX_STACK_DEPTH 10

typedef enum {
    LOCK_ACQUIRE,
    LOCK_RELEASE
} lock_event_type_t;

typedef struct {
    lock_event_type_t type;
    int typeOfThread;
    DWORD timestamp;
    void* lock_addr;
    void* stack[MAX_STACK_DEPTH];
    int stack_size;
} lock_event_t;

typedef struct {
    lock_event_t events[BUFFER_SIZE];
    int head;
    int count;
} lock_debug_buffer_t;

// Global debug buffer
extern lock_debug_buffer_t g_lock_debug_buffer;

// Log a lock event
void log_lock_event(lock_event_type_t type, void* lock_addr, int typeOfThread);


#endif //UTIL_H
