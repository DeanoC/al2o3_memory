// License Summary: MIT see LICENSE file
#pragma once
#include "al2o3_platform/platform.h"

// by default we enable memory tracking setup, which adds as small cost to every
// alloc (cost of a function and a few copies) however it also causes the exe
// to be bloated by file/line info at every alloc call site.
// so for final master set MEMORY_TRACKING_SETUP to 0 to remove this overhead
// as well
// Whether memory tracking is actually done (not just the setup) is decided
// inside memory.c
#ifndef MEMORY_TRACKING_SETUP
#define MEMORY_TRACKING_SETUP 1
#endif

typedef void* (*Memory_MallocFunc)(size_t size);
typedef void* (*Memory_AallocFunc)(size_t size, size_t align);
typedef void* (*Memory_CallocFunc)(size_t count, size_t size);
typedef void* (*Memory_ReallocFunc)(void* memory, size_t size);
typedef void (*Memory_Free)(void* memory);

typedef struct Memory_Allocator {
	Memory_MallocFunc malloc;
	Memory_AallocFunc aalloc;
	Memory_CallocFunc calloc;
	Memory_ReallocFunc realloc;
	Memory_Free free;
} Memory_Allocator;

// always returns true
AL2O3_EXTERN_C bool Memory_TrackerPushNextSrcLoc(const char *sourceFile, const unsigned int sourceLine, const char *sourceFunc);

// call this at exit, when tracking is on will log all non freed items, if no tracking does nothing
AL2O3_EXTERN_C void Memory_TrackerDestroyAndLogLeaks();
AL2O3_EXTERN_C uint64_t Memory_TrackerBreakOnAllocNumber; // set before the allocation occurs to break in memory tracking (0 disables)

AL2O3_EXTERN_C Memory_Allocator Memory_GlobalAllocator;

#if MEMORY_TRACKING_SETUP == 1

#define Memory_TrackingPaddingSize 4
#define MEMORY_ALLOCATOR_MALLOC(allocator, size) ((Memory_TrackerPushNextSrcLoc(__FILE__, __LINE__, __FUNCTION__)) ? (allocator)->malloc(size) : NULL)
#define MEMORY_ALLOCATOR_AALLOC(allocator, size, align) ((Memory_TrackerPushNextSrcLoc(__FILE__, __LINE__, __FUNCTION__)) ? (allocator)->aalloc(size, align) : NULL)
#define MEMORY_ALLOCATOR_CALLOC(allocator, count, size) ((Memory_TrackerPushNextSrcLoc(__FILE__, __LINE__, __FUNCTION__)) ? (allocator)->calloc(count, size) : NULL)
#define MEMORY_ALLOCATOR_REALLOC(allocator, orig, size) ((Memory_TrackerPushNextSrcLoc(__FILE__, __LINE__, __FUNCTION__)) ? (allocator)->realloc(orig, size) : NULL)
#define MEMORY_ALLOCATOR_FREE(allocator, ptr) (allocator)->free(ptr)

// to use tracking on custom allocated, add these in the same way trackedMalloc etc in memory.c does for the
// default platform allocator (e.g. adjust size of alloc except aligned alloc and call tracked after ur custom alloc)
AL2O3_FORCE_INLINE size_t Memory_TrackerCalculateActualSize(const size_t reportedSize) {
	return reportedSize + Memory_TrackingPaddingSize * sizeof(uint32_t) * 2;
}

AL2O3_EXTERN_C void *Memory_TrackedAlloc(const char *sourceFile,
																				 const unsigned int sourceLine,
																				 const char *sourceFunc,
																				 const size_t reportedSize,
																				 void *actualSizedAllocation);
AL2O3_EXTERN_C void *Memory_TrackedAAlloc(const char *sourceFile,
																					const unsigned int sourceLine,
																					const char *sourceFunc,
																					const size_t reportedSize,
																					void *actualSizedAllocation);
AL2O3_EXTERN_C void *Memory_TrackedRealloc(const char *sourceFile,
																					 const unsigned int sourceLine,
																					 const char *sourceFunc,
																					 const size_t reportedSize,
																					 void *reportedAddress,
																					 void *actualSizedAllocation);
AL2O3_EXTERN_C bool Memory_TrackedFree(const void *reportedAddress);

#else

#define MEMORY_ALLOCATOR_MALLOC(allocator, size) (allocator)->malloc(size)
#define MEMORY_ALLOCATOR_AALLOC(allocator, size, align) (allocator)->aalloc(size, align)
#define MEMORY_ALLOCATOR_CALLOC(allocator, count, size) (allocator)->calloc(count, size)
#define MEMORY_ALLOCATOR_REALLOC(allocator, orig, size) (allocator)->realloc(orig, size)
#define MEMORY_ALLOCATOR_FREE(allocator, ptr) (allocator)->free(ptr)

#define Memory_TrackerCalculateActualSize(reportedSize) (reportedSize)
AL2O3_EXTERN_C void *Memory_TrackedAlloc(const char * a,const unsigned int b, const char * c, const size_t d, void * e);
AL2O3_EXTERN_C void *Memory_TrackedAAlloc(const char * a, const unsigned int b, const char * c, const size_t d, void * e);
AL2O3_EXTERN_C void *Memory_TrackedRealloc(const char *a ,const unsigned int b, const char * c,const size_t d,void * e,void *f);

#endif

#define MEMORY_MALLOC(size) MEMORY_ALLOCATOR_MALLOC(&Memory_GlobalAllocator, size)
#define MEMORY_AALLOC(size, align) MEMORY_ALLOCATOR_AALLOC(&Memory_GlobalAllocator, size, align)
#define MEMORY_CALLOC(count, size) MEMORY_ALLOCATOR_CALLOC(&Memory_GlobalAllocator, count, size)
#define MEMORY_REALLOC(orig, size) MEMORY_ALLOCATOR_REALLOC(&Memory_GlobalAllocator, orig, size)
#define MEMORY_FREE(ptr) MEMORY_ALLOCATOR_FREE(&Memory_GlobalAllocator, ptr)

// TODO temp pool
#define MEMORY_TEMP_MALLOC(size) MEMORY_ALLOCATOR_MALLOC(&Memory_GlobalAllocator, size)
#define MEMORY_TEMP_AALLOC(size, align) MEMORY_ALLOCATOR_AALLOC(&Memory_GlobalAllocator, size, align)
#define MEMORY_TEMP_CALLOC(count, size) MEMORY_ALLOCATOR_CALLOC(&Memory_GlobalAllocator, count, size)
#define MEMORY_TEMP_REALLOC(orig, size) MEMORY_ALLOCATOR_REALLOC(&Memory_GlobalAllocator,orig, size)
#define MEMORY_TEMP_FREE(ptr) MEMORY_ALLOCATOR_FREE(&Memory_GlobalAllocator, ptr)

#if AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
AL2O3_EXTERN_C void* _alloca(size_t size);
#define STACK_ALLOC(size) _alloca(size)

#elif AL2O3_PLATFORM == AL2O3_PLATFORM_UNKNOWN

#define STACK_ALLOC(size) NOT_SUPPORTED
#else

#include "alloca.h"
#define STACK_ALLOC(size) alloca(size)

#endif

#if __cplusplus
#include <new>
#define MEMORY_NEW(clas, ...) new( (clas*) MEMORY_MALLOC(sizeof(clas))) clas(__VA_ARGS__)
#define MEMORY_DELETE(clas, ptr) ptr->~clas(); MEMORY_FREE(ptr);
#endif