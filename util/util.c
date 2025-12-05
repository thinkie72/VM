//
// Created by tyler on 12/5/2025.
//
#include "util.h"
#include <time.h>
#include <winbase.h>

void log_lock_event(lock_event_type_t type, void* lock_addr, int typeOfThread) {
#if DBG
    int idx = g_lock_debug_buffer.head;
    lock_event_t* event = &g_lock_debug_buffer.events[idx];

    event->type = type;
    event->typeOfThread = typeOfThread;
    event->timestamp = GetTickCount();
    event->lock_addr = lock_addr;

    // Capture stack trace (Windows)
    event->stack_size = CaptureStackBackTrace(0, MAX_STACK_DEPTH, event->stack, NULL);

    // Update circular buffer indices
    g_lock_debug_buffer.head = (g_lock_debug_buffer.head + 1) % BUFFER_SIZE;
    if (g_lock_debug_buffer.count < BUFFER_SIZE) {
        g_lock_debug_buffer.count++;
    }
#endif
}
