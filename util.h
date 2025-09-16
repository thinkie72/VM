//
// Created by tyler.hinkie on 7/24/2025.
//

#ifndef UTIL_H
#define UTIL_H


#define DBG 1
#if DBG
#define ASSERT(x) if (!(x)) DebugBreak();
#else
#define ASSERT(x)
#endif


#endif //UTIL_H
