/* This file contains macros for manipulating doubly linked lists.
 * The code for these macros was written by Microsoft. */
#ifndef VM_MACROS_H
#define VM_MACROS_H

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

FORCEINLINE
        VOID
InitializeListHead(
        __out PLIST_ENTRY ListHead
)
{
ListHead->Flink = ListHead->Blink = ListHead;
}

__checkReturn
        BOOLEAN
FORCEINLINE
isEmpty(
        __in const LIST_ENTRY* ListHead
)
{
return (BOOLEAN)(ListHead->Flink == ListHead);
}

FORCEINLINE
        BOOLEAN
RemoveEntryList(
        __in PLIST_ENTRY Entry
)
{
PLIST_ENTRY Blink;
PLIST_ENTRY Flink;

Flink = Entry->Flink;
Blink = Entry->Blink;
Blink->Flink = Flink;
Flink->Blink = Blink;
return (BOOLEAN)(Flink == Blink);
}

FORCEINLINE
        PLIST_ENTRY
RemoveHeadList(
        __inout PLIST_ENTRY ListHead
)
{
PLIST_ENTRY Flink;
PLIST_ENTRY Entry;

Entry = ListHead->Flink;
Flink = Entry->Flink;
ListHead->Flink = Flink;
Flink->Blink = ListHead;
return Entry;
}

FORCEINLINE
        PLIST_ENTRY
RemoveTailList(
        __inout PLIST_ENTRY ListHead
)
{
PLIST_ENTRY Blink;
PLIST_ENTRY Entry;

Entry = ListHead->Blink;
Blink = Entry->Blink;
ListHead->Blink = Blink;
Blink->Flink = ListHead;
return Entry;
}

FORCEINLINE
        VOID
InsertTailList(
        __inout PLIST_ENTRY ListHead,
        __inout __drv_aliasesMem PLIST_ENTRY Entry
)
{
PLIST_ENTRY Blink;

Blink = ListHead->Blink;
Entry->Flink = ListHead;
Entry->Blink = Blink;
Blink->Flink = Entry;
ListHead->Blink = Entry;
}


FORCEINLINE
        VOID
InsertHeadList(
        __inout PLIST_ENTRY ListHead,
        __inout __drv_aliasesMem PLIST_ENTRY Entry
)
{
PLIST_ENTRY Flink;

Flink = ListHead->Flink;
Entry->Flink = Flink;
Entry->Blink = ListHead;
Flink->Blink = Entry;
ListHead->Flink = Entry;
}

FORCEINLINE
        VOID
AppendTailList(
        __inout PLIST_ENTRY ListHead,
        __inout PLIST_ENTRY ListToAppend
)
{
PLIST_ENTRY ListEnd = ListHead->Blink;

ListHead->Blink->Flink = ListToAppend;
ListHead->Blink = ListToAppend->Blink;
ListToAppend->Blink->Flink = ListHead;
ListToAppend->Blink = ListEnd;
}

FORCEINLINE
        PSINGLE_LIST_ENTRY
PopEntryList(
        __inout PSINGLE_LIST_ENTRY ListHead
)
{
PSINGLE_LIST_ENTRY FirstEntry;
FirstEntry = ListHead->Next;
if (FirstEntry != NULL) {
ListHead->Next = FirstEntry->Next;
}

return FirstEntry;
}


FORCEINLINE
        VOID
PushEntryList(
        __inout PSINGLE_LIST_ENTRY ListHead,
        __inout __drv_aliasesMem PSINGLE_LIST_ENTRY Entry
)
{
Entry->Next = ListHead->Next;
ListHead->Next = Entry;
}
#endif //VM_MACROS_H//
