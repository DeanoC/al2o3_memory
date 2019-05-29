// Full license at end of file
// Summary: Apache 2.0

#pragma once
#ifndef AL2O3_MEMORY_MEMORY_H
#define AL2O3_MEMORY_MEMORY_H

#include "al2o3_platform/platform.h"

typedef void* (*Memory_MallocFunc)(size_t size);
typedef void* (*Memory_CallocFunc)(size_t count, size_t size);
typedef void* (*Memory_ReallocFunc)(void* memory, size_t size);
typedef void (*Memory_Free)(void* memory);

typedef struct Memory_Allocator {
	Memory_MallocFunc malloc;
	Memory_CallocFunc calloc;
	Memory_ReallocFunc realloc;
	Memory_Free free;
} Memory_Allocator;

AL2O3_EXTERN_C Memory_Allocator Memory_GlobalAllocator;

AL2O3_EXTERN_C void* Memory_DefaultMalloc(size_t size);
AL2O3_EXTERN_C void* Memory_DefaultCalloc(size_t count, size_t size);
AL2O3_EXTERN_C void* Memory_DefaultRealloc(void* memory, size_t size);
AL2O3_EXTERN_C void Memory_DefaultFree(void* memory);

#define MEMORY_ALLOCATOR_MALLOC(size, allocator) (allocator)->malloc(size)
#define MEMORY_ALLOCATOR_CALLOC(count, size, allocator) (allocator)->calloc(count, size)
#define MEMORY_ALLOCATOR_REALLOC(orig, size, allocator) (allocator)->realloc(orig, size)
#define MEMORY_ALLOCATOR_FREE(ptr, allocator) (allocator)->free(ptr)

#define MEMORY_MALLOC(size) Memory_DefaultMalloc(size)
#define MEMORY_CALLOC(count, size) Memory_DefaultCalloc(count, size)
#define MEMORY_REALLOC(orig, size) Memory_DefaultRealloc(orig, size)
#define MEMORY_FREE(ptr) Memory_DefaultFree(ptr)

#endif // AL2O3_MEMORY_MEMORY_H