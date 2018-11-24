#pragma once
#include "CommonIncludes.h"

// Sapera-related
#define BUFFER_MEMORY_MB 16
#define NUM_BUFFERS()  ((BUFFER_MEMORY_MB*1024*1024) / (desiredHeight*desiredWidth) )

#ifdef UNICODE
#   define tcharify(s) (std::wstring(s.begin(), s.end()).c_str())
#   define STRCMP(s1,s2) (wcscmp(s1,s2))
#else
#   define tcharify(s) (s.c_str())
#   define STRCMP(s1,s2) (strcmp(s1,s2))
#endif

// Grrr... windows headers are broken with respect strict C++ compiler warnings
#undef INVALID_HANDLE_VALUE // was: ((HANDLE)(LONG_PTR)-1)
#define INVALID_HANDLE_VALUE (HANDLE(LONG_PTR(-1)))
