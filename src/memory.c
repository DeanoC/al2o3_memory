#include "al2o3_memory/memory.h"

char const *g_lastSourceFile = NULL;
unsigned int g_lastSourceLine = 0;
char const *g_lastSourceFunc = NULL;

#if !defined(MEMORY_TRACKING)
#define MEMORY_TRACKING 1
#endif

#if MEMORY_TRACKING == 1
#if !defined(MEMORY_TRACKING_SETUP) || MEMORY_TRACKING_SETUP == 0
#error MEMORY_TRACKING requires MMEMORY_TRACKING_SETUP == 1
#endif
#endif

AL2O3_EXTERN_C bool Memory_TrackerPushNextSrcLoc(const char *sourceFile,
																								 const unsigned int sourceLine,
																								 const char *sourceFunc) {
	g_lastSourceFile = sourceFile;
	g_lastSourceLine = sourceLine;
	g_lastSourceFunc = sourceFunc;
	return true;
}

#if AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
#include "malloc.h"
// on win32 we only have 8-byte alignment guaranteed, but the CRT provides special aligned allocation fns
AL2O3_FORCE_INLINE AL2O3_EXTERN_C void *platformMalloc(size_t size) {
	return _aligned_malloc(size, 16);
}

AL2O3_FORCE_INLINE AL2O3_EXTERN_C void *platformAalloc(size_t size, size_t align) {
	return _aligned_malloc(size, align);
}

AL2O3_FORCE_INLINE AL2O3_EXTERN_C void *platformCalloc(size_t count, size_t size) {
	void *mem = _aligned_malloc(count * size, 16);
	if (mem) {
		memset(mem, 0, count * size);
	}
	return mem;
}

AL2O3_FORCE_INLINE AL2O3_EXTERN_C void *platformRealloc(void *ptr, size_t size) {
	return _aligned_realloc(ptr, size, 16);
}

AL2O3_FORCE_INLINE AL2O3_EXTERN_C void platformFree(void *ptr) {
	_aligned_free(ptr);
}

#elif AL2O3_PLATFORM == AL2O3_PLATFORM_UNIX || AL2O3_PLATFORM_OS == AL2O3_OS_OSX

AL2O3_FORCE_INLINE AL2O3_EXTERN_C void* platformMalloc(size_t size)
{
	void* mem;
	posix_memalign(&mem, 16, size);
	return mem;	
}

AL2O3_FORCE_INLINE AL2O3_EXTERN_C void* platformAalloc(size_t size, size_t align)
{
	void* mem;
	posix_memalign(&mem, align, size);
	return mem;
}

AL2O3_FORCE_INLINE AL2O3_EXTERN_C void* platformCalloc(size_t count, size_t size)
{
	void* mem;
	posix_memalign(&mem, 16, count * size);
	if(mem) {
		memset(mem, 0, count * size);
	}
	return mem;
}

AL2O3_FORCE_INLINE AL2O3_EXTERN_C void* platformRealloc(void* ptr, size_t size) {
	// technically this appears to be a bit dodgy but given
	// chromium and ffmpeg do this according to
	// https://trac.ffmpeg.org/ticket/6403
	// i'll live with it and assert it
	ptr = realloc(ptr, size);
	ASSERT(((uintptr_t) ptr & 0xFUL) == 0);
	return ptr;
}

AL2O3_FORCE_INLINE AL2O3_EXTERN_C void platformFree(void* ptr)
{
	free(ptr);
}

#else

#error Unsupported platform

#endif

#if MEMORY_TRACKING == 1

// ---------------------------------------------------------------------------------------------------------------------------------
// Originally created on 12/22/2000 by Paul Nettle
//
// Copyright 2000, Fluid Studios, Inc., all rights reserved.
// ---------------------------------------------------------------------------------------------------------------------------------
typedef struct AllocUnit {
	void *actualAddress;
	void *reportedAddress;
	char const *sourceFile;
	char const *sourceFunc;
	struct AllocUnit *next;
	struct AllocUnit *prev;

	size_t reportedSize;

	uint32_t sourceLine;
} AllocUnit;

#define hashBits 12u
#define hashSize (1u << hashBits)
static AllocUnit *hashTable[hashSize];
static AllocUnit *reservoir;
static AllocUnit **reservoirBuffer = NULL;
static uint32_t reservoirBufferSize = 0;
static const uint32_t paddingSize = 4;

// ---------------------------------------------------------------------------------------------------------------------------------
AL2O3_FORCE_INLINE size_t calculateActualSize(const size_t reportedSize) {
	return reportedSize + paddingSize * sizeof(uint32_t) * 2;
}

AL2O3_FORCE_INLINE size_t calculateReportedSize(const size_t actualSize) {
	return actualSize - paddingSize * sizeof(uint32_t) * 2;
}

AL2O3_FORCE_INLINE void *calculateActualAddress(const void *reportedAddress) {
	// We allow this...
	if (!reportedAddress) {
		return NULL;
	}

	// JUst account for the padding
	return (void *) (((uint8_t const *) (reportedAddress)) - sizeof(uint32_t) * paddingSize);
}

AL2O3_FORCE_INLINE void *calculateReportedAddress(const void *actualAddress) {
	// We allow this...
	if (!actualAddress) {
		return NULL;
	}

	// JUst account for the padding
	return (void *) (((uint8_t const *) (actualAddress)) + sizeof(uint32_t) * paddingSize);
}

static const char *sourceFileStripper(const char *sourceFile) {
	const char *ptr = strrchr(sourceFile, '\\');
	if (ptr) {
		return ptr + 1;
	}
	ptr = strrchr(sourceFile, '/');
	if (ptr) {
		return ptr + 1;
	}
	return sourceFile;
}

static AllocUnit *findAllocUnit(const void *reportedAddress) {
	// Just in case...
	ASSERT(reportedAddress != NULL);

	// Use the address to locate the hash index. Note that we shift off the lower four bits. This is because most allocated
	// addresses will be on four-, eight- or even sixteen-byte boundaries. If we didn't do this, the hash index would not have
	// very good coverage.
	uintptr_t hashIndex = (((uintptr_t) reportedAddress) >> 4) & (hashSize - 1);
	AllocUnit *ptr = hashTable[hashIndex];
	while (ptr) {
		if (ptr->reportedAddress == reportedAddress) {
			return ptr;
		}
		ptr = ptr->next;
	}

	return NULL;
}

static bool GrowReservoir() {
	// Allocate 256 reservoir elements
	reservoir = (AllocUnit *) platformMalloc(sizeof(AllocUnit) * 256);

	// If you hit this assert, then the memory manager failed to allocate internal memory for tracking the
	// allocations
	ASSERT(reservoir != NULL);

	// Danger Will Robinson!
	if (reservoir == NULL) {
		return false;
	}

	// Build a linked-list of the elements in our reservoir
	memset(reservoir, 0, sizeof(AllocUnit) * 256);
	for (unsigned int i = 0; i < 256 - 1; i++) {
		reservoir[i].next = &reservoir[i + 1];
	}

	// Add this address to our reservoirBuffer so we can free it later
	AllocUnit **temp = (AllocUnit **) platformRealloc(reservoirBuffer, (reservoirBufferSize + 1) * sizeof(AllocUnit *));
	ASSERT(temp);
	if (temp) {
		reservoirBuffer = temp;
		reservoirBuffer[reservoirBufferSize++] = reservoir;
	}
	return true;
}

void *TrackedAlloc(const char *sourceFile,
									 const unsigned int sourceLine,
									 const char *sourceFunc,
									 const size_t reportedSize,
									 void *actualSizedAllocation) {

	// If necessary, grow the reservoir of unused allocation units
	if (!reservoir) {
		if (!GrowReservoir()) {
			return NULL;
		}
	}
	if(sourceFile == NULL) {
		int a = 0;
	}

	// Logical flow says this should never happen...
	ASSERT(reservoir != NULL);

	// Grab a new allocaton unit from the front of the reservoir
	AllocUnit *au = reservoir;
	reservoir = au->next;

	// Populate it with some real data
	memset(au, 0, sizeof(AllocUnit));
	au->actualAddress = actualSizedAllocation;
	au->reportedSize = reportedSize;
	au->reportedAddress = calculateReportedAddress(au->actualAddress);
	au->sourceFile = sourceFile;
	au->sourceLine = sourceLine;
	au->sourceFunc = sourceFunc;

	// We don't want to assert with random failures, because we want the application to deal with them.
	if (au->actualAddress == NULL) {
		LOGERROR("Request for allocation failed. Out of memory.");
		return NULL;
	}

	// Insert the new allocation into the hash table
	uintptr_t hashIndex = (((uintptr_t) au->reportedAddress) >> 4) & (hashSize - 1);
	if (hashTable[hashIndex]) {
		hashTable[hashIndex]->prev = au;
	}
	au->next = hashTable[hashIndex];
	au->prev = NULL;
	hashTable[hashIndex] = au;

	g_lastSourceFile = NULL;
	g_lastSourceLine = 0;
	g_lastSourceFunc = NULL;

	return au->reportedAddress;
}

void *TrackedAAlloc(const char *sourceFile,
										const unsigned int sourceLine,
										const char *sourceFunc,
										const size_t reportedSize,
										void *actualSizedAllocation) {

	// If necessary, grow the reservoir of unused allocation units
	if (!reservoir) {
		if (!GrowReservoir()) {
			return NULL;
		}
	}

	// Logical flow says this should never happen...
	ASSERT(reservoir != NULL);

	// Grab a new allocaton unit from the front of the reservoir
	AllocUnit *au = reservoir;
	reservoir = au->next;

	// Populate it with some real data
	memset(au, 0, sizeof(AllocUnit));
	au->actualAddress = actualSizedAllocation;
	au->reportedSize = reportedSize;
	au->reportedAddress = au->actualAddress; // alignment means reported == actual
	au->sourceFile = sourceFile;
	au->sourceLine = sourceLine;
	au->sourceFunc = sourceFunc;

	// We don't want to assert with random failures, because we want the application to deal with them.
	if (au->actualAddress == NULL) {
		LOGERROR("Request for allocation failed. Out of memory.");
		return NULL;
	}

	// Insert the new allocation into the hash table
	uintptr_t hashIndex = (((uintptr_t) au->reportedAddress) >> 4) & (hashSize - 1);
	if (hashTable[hashIndex]) {
		hashTable[hashIndex]->prev = au;
	}
	au->next = hashTable[hashIndex];
	au->prev = NULL;
	hashTable[hashIndex] = au;

	g_lastSourceFile = NULL;
	g_lastSourceLine = 0;
	g_lastSourceFunc = NULL;

	return au->reportedAddress;
}

void *TrackedRealloc(const char *sourceFile,
										 const unsigned int sourceLine,
										 const char *sourceFunc,
										 const size_t reportedSize,
										 void *reportedAddress,
										 void *actualSizedAllocation) {
	// Calling realloc with a NULL should force same operations as a malloc

	if (!reportedAddress) {
		return TrackedAlloc(sourceFile, sourceLine, sourceFunc, reportedSize, actualSizedAllocation);
	}

	// Locate the existing allocation unit
	AllocUnit *au = findAllocUnit(reportedAddress);

	// If you hit this assert, you tried to reallocate RAM that wasn't allocated by this memory manager.
	if (au == NULL) {
		LOGERROR("Request to reallocate RAM that was never allocated");
		return NULL;
	}


	// Keep track of the original size
	size_t originalReportedSize = au->reportedSize;

	// Do the reallocation
	void *oldReportedAddress = reportedAddress;
	size_t newActualSize = calculateActualSize(reportedSize);

	if (!actualSizedAllocation) {
		LOGERROR("Request for reallocation failed. Out of memory.");
		return NULL;
	}

	// Update the allocation with the new information
	au->actualAddress = actualSizedAllocation;
	au->reportedSize = calculateReportedSize(newActualSize);
	au->reportedAddress = calculateReportedAddress(actualSizedAllocation);
	au->sourceFile = sourceFile;
	au->sourceLine = sourceLine;
	au->sourceFunc = sourceFunc;

	// The reallocation may cause the address to change, so we should relocate our allocation unit within the hash table
	unsigned int hashIndex = ~0;
	if (oldReportedAddress != au->reportedAddress) {
		// Remove this allocation unit from the hash table
		{
			uintptr_t hashIndex = (((uintptr_t) reportedAddress) >> 4) & (hashSize - 1);
			if (hashTable[hashIndex] == au) {
				hashTable[hashIndex] = hashTable[hashIndex]->next;
			} else {
				if (au->prev) {
					au->prev->next = au->next;
				}
				if (au->next) {
					au->next->prev = au->prev;
				}
			}
		}

		// Re-insert it back into the hash table
		hashIndex = (((uintptr_t) au->reportedAddress) >> 4) & (hashSize - 1);
		if (hashTable[hashIndex]) {
			hashTable[hashIndex]->prev = au;
		}
		au->next = hashTable[hashIndex];
		au->prev = NULL;
		hashTable[hashIndex] = au;
	}


	// Prepare the allocation unit for use (wipe it with recognizable garbage)
	//	wipeWithPattern(au, unusedPattern, originalReportedSize);

	g_lastSourceFile = NULL;
	g_lastSourceLine = 0;
	g_lastSourceFunc = NULL;

	// Return the (reported) address of the new allocation unit
	return au->reportedAddress;
}

bool TrackedFree(const char *sourceFile,
								 const unsigned int sourceLine,
								 const char *sourceFunc,
								 const void *reportedAddress) {

	// We should only ever get here with a null pointer if they try to do so with a call to free() (delete[] and delete will
	// both bail before they get here.) So, since ANSI allows free(NULL), we'll not bother trying to actually free the allocated
	// memory or track it any further.
	if (!reportedAddress) {
		return false;
	}

	// Go get the allocation unit
	AllocUnit *au = findAllocUnit(reportedAddress);
	if (au == NULL) {
		LOGERROR("Request to deallocate RAM that was never allocated");
		return false;
	}
	bool const adjustPtr = (au->actualAddress != au->reportedAddress);

	// Wipe the deallocated RAM with a new pattern. This doen't actually do us much good in debug mode under WIN32,
	// because Microsoft's memory debugging & tracking utilities will wipe it right after we do. Oh well.

	//	wipeWithPattern(au, releasedPattern);

	// Remove this allocation unit from the hash table
	uintptr_t hashIndex = (((uintptr_t) au->reportedAddress) >> 4) & (hashSize - 1);
	if (hashTable[hashIndex] == au) {
		hashTable[hashIndex] = au->next;
	} else {
		if (au->prev) {
			au->prev->next = au->next;
		}
		if (au->next) {
			au->next->prev = au->prev;
		}
	}

	// Add this allocation unit to the front of our reservoir of unused allocation units
	memset(au, 0, sizeof(AllocUnit));
	au->next = reservoir;
	reservoir = au;
	return adjustPtr;
}

AL2O3_EXTERN_C void *trackedMalloc(size_t size) {
	void *mem = platformMalloc(calculateActualSize(size));
	return TrackedAlloc(g_lastSourceFile, g_lastSourceLine, g_lastSourceFunc, size, mem);
}

AL2O3_EXTERN_C void *trackedAalloc(size_t size, size_t align) {
	void *mem = platformAalloc(calculateActualSize(size), align);
	return TrackedAAlloc(g_lastSourceFile, g_lastSourceLine, g_lastSourceFunc, size, mem);
}

AL2O3_EXTERN_C void *trackedCalloc(size_t count, size_t size) {
	void *mem = platformMalloc(calculateActualSize(count * size));
	if (mem) {
		memset(mem, 0, calculateActualSize(count * size));
	}
	return TrackedAlloc(g_lastSourceFile, g_lastSourceLine, g_lastSourceFunc, count * size, mem);
}

AL2O3_EXTERN_C void *trackedRealloc(void *ptr, size_t size) {
	void *mem = platformRealloc(calculateActualAddress(ptr), calculateActualSize(size));
	return TrackedRealloc(g_lastSourceFile, g_lastSourceLine, g_lastSourceFunc, size, ptr, mem);
}

AL2O3_EXTERN_C void trackedFree(void *ptr) {
	bool const adjustPtr = TrackedFree(g_lastSourceFile, g_lastSourceLine, g_lastSourceFunc, ptr);
	if (adjustPtr) {
		platformFree(calculateActualAddress(ptr));
	} else {
		platformFree(ptr);
	}
}

AL2O3_EXTERN_C Memory_Allocator Memory_GlobalAllocator = {
		&trackedMalloc,
		&trackedAalloc,
		&trackedCalloc,
		&trackedRealloc,
		&trackedFree
};

AL2O3_EXTERN_C void Memory_TrackerDestroyAndLogLeaks() {
	bool loggedHeader = 0;
	for (int i = 0; i < hashSize; ++i) {
		AllocUnit *au = hashTable[i];
		while (au != NULL) {
			if (loggedHeader == false) {
				loggedHeader = true;
				LOGINFO("-=-=-=-=-=-=- Memory Leak Report -=-=-=-=-=-=-");
			}
			if(au->sourceFile) {
				char const *fileNameOnly = sourceFileStripper(au->sourceFile);
				LOGINFO("%u bytes from %s(%u): %s", au->reportedSize, fileNameOnly, au->sourceLine, au->sourceFunc);
			} else {
				LOGINFO("%u bytes from an unknown caller", au->reportedSize);
			}
			au = au->next;
		}
	}

	// free the reservoirs
	for(uint32_t i = 0;i < reservoirBufferSize;++i) {
		platformFree(reservoirBuffer[i]);
	}
	reservoirBuffer = NULL;
}

#else

AL2O3_EXTERN_C Memory_Allocator Memory_GlobalAllocator = {
		&platformMalloc,
		&platformAalloc,
		&platformCalloc,
		&platformRealloc,
		&platformFree
};
AL2O3_EXTERN_C void Memory_TrackerLogLeaks() {}

#endif
