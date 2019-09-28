// Full license at end of file
// Summary: Apache 2.0

#pragma once
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

// TODO optional memory tracking
AL2O3_EXTERN_C Memory_Allocator Memory_GlobalAllocator;
AL2O3_EXTERN_C Memory_Allocator Memory_GlobalTempAllocator;

AL2O3_EXTERN_C void* Memory_DefaultMalloc(size_t size);
AL2O3_EXTERN_C void* Memory_DefaultCalloc(size_t count, size_t size);
AL2O3_EXTERN_C void* Memory_DefaultRealloc(void* memory, size_t size);
AL2O3_EXTERN_C void Memory_DefaultFree(void* memory);

// temp allocs shouldn't be expected to live that long, they may come out of faster
// smaller pool
AL2O3_EXTERN_C void* Memory_DefaultTempMalloc(size_t size);
AL2O3_EXTERN_C void* Memory_DefaultTempCalloc(size_t count, size_t size);
AL2O3_EXTERN_C void* Memory_DefaultTempRealloc(void* memory, size_t size);
AL2O3_EXTERN_C void Memory_DefaultTempFree(void* memory);

#define MEMORY_ALLOCATOR_MALLOC(allocator, size) (allocator)->malloc(size)
#define MEMORY_ALLOCATOR_CALLOC(allocator, count, size) (allocator)->calloc(count, size)
#define MEMORY_ALLOCATOR_REALLOC(allocator, orig, size) (allocator)->realloc(orig, size)
#define MEMORY_ALLOCATOR_FREE(allocator, ptr) (allocator)->free(ptr)

#define MEMORY_MALLOC(size) Memory_DefaultMalloc(size)
#define MEMORY_CALLOC(count, size) Memory_DefaultCalloc(count, size)
#define MEMORY_REALLOC(orig, size) Memory_DefaultRealloc(orig, size)
#define MEMORY_FREE(ptr) Memory_DefaultFree(ptr)

#define MEMORY_TEMP_MALLOC(size) Memory_DefaultTempMalloc(size)
#define MEMORY_TEMP_CALLOC(count, size) Memory_DefaultTempCalloc(count, size)
#define MEMORY_TEMP_REALLOC(orig, size) Memory_DefaultTempRealloc(orig, size)
#define MEMORY_TEMP_FREE(ptr) Memory_DefaultTempFree(ptr)

#if AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
AL2O3_EXTERN_C void* _alloca(size_t size);
#define STACK_ALLOC(size) _alloca(size)

#else

#include "alloca.h"
#define STACK_ALLOC(size) alloca(size)

#endif
