#include "al2o3_memory/memory.h"

char const *g_lastSourceFile = NULL;
unsigned int g_lastSourceLine = 0;
char const *g_lastSourceFunc = NULL;
static uint64_t g_allocCounter = 0;
static uint64_t g_breakOnAllocNumber = 0;

#if !defined(MEMORY_TRACKING)
#define MEMORY_TRACKING 1
#endif

#if MEMORY_TRACKING == 1
#if !defined(MEMORY_TRACKING_SETUP) || MEMORY_TRACKING_SETUP == 0
#error MEMORY_TRACKING requires MEMORY_TRACKING_SETUP == 1
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

#if AL2O3_PLATFORM_OS == AL2O3_OS_OSX || AL2O3_PLATFORM == AL2O3_PLATFORM_UNIX
#include <unistd.h>
#include <sys/sysctl.h>
#if defined(__linux__)
#include <sys/sysinfo.h>
#endif
#include <pthread.h>

typedef pthread_mutex_t Mini_Mutex_t;
static bool Mini_MutexCreate(Mini_Mutex_t *mutex) {
	ASSERT(mutex);
	return pthread_mutex_init(mutex, NULL) == 0;
}

static void Mini_MutexDestroy(Mini_Mutex_t *mutex) {
	ASSERT(mutex);
	pthread_mutex_destroy(mutex);
}

static void Mini_MutexAcquire(Mini_Mutex_t *mutex) {
	ASSERT(mutex);
	pthread_mutex_lock(mutex);

}

static void Mini_MutexRelease(Mini_Mutex_t *mutex) {
	ASSERT(mutex);
	pthread_mutex_unlock(mutex);
}
#elif AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
#include "al2o3_platform/windows.h"
#if AL2O3_CPU_BIT_SIZE == 32
typedef struct { char dummy[24]; } Mini_Mutex_t;
#elif AL2O3_CPU_BIT_SIZE == 64
typedef struct { char dummy[40]; } Mini_Mutex_t;
#else
#error What bit size if this CPU?!
#endif

static bool Mini_MutexCreate(Mini_Mutex_t *mutex) {
  InitializeCriticalSection((CRITICAL_SECTION *) mutex);
  return true;
}
static void Mini_MutexDestroy(Mini_Mutex_t *mutex) {
  DeleteCriticalSection((CRITICAL_SECTION *) mutex);
}

static void Mini_MutexAcquire(Mini_Mutex_t *mutex) {
  EnterCriticalSection((CRITICAL_SECTION *) mutex);
}
static void Mini_MutexRelease(Mini_Mutex_t *mutex) {
  LeaveCriticalSection((CRITICAL_SECTION *) mutex);
}
#endif

#define MUTEX_LOCK Mini_MutexAcquire(&g_allocMutex);
#define MUTEX_UNLOCK Mini_MutexRelease(&g_allocMutex);


// ---------------------------------------------------------------------------------------------------------------------------------
// Originally created on 12/22/2000 by Paul Nettle
//
// Copyright 2000, Fluid Studios, Inc., all rights reserved.
// ---------------------------------------------------------------------------------------------------------------------------------
#define REPORTED_ADDRESS_BITS_MASK(x) (((uintptr_t)(x)) & 0xF)
#define REPORTED_ADDRESS_BITES_SAME_AS_REPORTED 0x1

#define CLEAN_REPORTED_ADDRESS(x) (void*)(((uintptr_t)(x)) & ~0xF)

typedef struct AllocUnit {
	void *uncleanReportedAddress; //address is always at least 16 byte aligned so we stuff things in the bottom four bit!
	char const *sourceFile;
	char const *sourceFunc;
	struct AllocUnit *next;
	struct AllocUnit *prev;

	uint64_t allocationNumber;

	uint32_t reportedSize; // as most allocs will be less 4GiB we assume we saturate to 4GiB should be enough to spot
	uint32_t sourceLine;

} AllocUnit;

#define hashBits 12u
#define hashSize (1u << hashBits)
static AllocUnit *hashTable[hashSize];
static AllocUnit *reservoir;
static AllocUnit **reservoirBuffer = NULL;
static uint32_t reservoirBufferSize = 0;
const uint32_t paddingSize = 4;
static Mini_Mutex_t g_allocMutex;

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
	if( REPORTED_ADDRESS_BITS_MASK(reportedAddress) & REPORTED_ADDRESS_BITES_SAME_AS_REPORTED) {
		return (void*) (((uintptr_t)reportedAddress) & ~0xF);
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
	char const* ptr = sourceFile + strlen(sourceFile);
	uint32_t slashCount = 0;
	while(ptr > sourceFile) {
		if(*ptr == '\\' || *ptr == '/') {
			slashCount++;
			if(slashCount == 3) {
				return ptr + 1;
			}
		}
		ptr--;
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
		if (CLEAN_REPORTED_ADDRESS(ptr->uncleanReportedAddress) == reportedAddress) {
			return ptr;
		}
		ptr = ptr->next;
	}

	return NULL;
}

static bool GrowReservoir() {
	// Allocate 256 reservoir elements
	reservoir = (AllocUnit *) platformMalloc(sizeof(AllocUnit) * 256);

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

	if (reservoirBufferSize == 0) {
		Mini_MutexCreate(&g_allocMutex);
	}

	MUTEX_LOCK
	// If necessary, grow the reservoir of unused allocation units
	if (!reservoir) {
			if (!GrowReservoir()) {
			MUTEX_UNLOCK
			return NULL;
		}
	}

	if (g_breakOnAllocNumber != 0 && g_breakOnAllocNumber == g_allocCounter + 1) {
		AL2O3_DEBUG_BREAK();
	}

	if(sourceFile == NULL) {
		AL2O3_DEBUG_BREAK();
	}
	if (actualSizedAllocation == NULL) {
		LOGERROR("Request for allocation failed. Out of memory.");
		MUTEX_UNLOCK
		return NULL;
	}

	// Logical flow says this should never happen...
	ASSERT(reservoir != NULL);

	// Grab a new allocaton unit from the front of the reservoir
	AllocUnit *au = reservoir;
	reservoir = au->next;

	// Populate it with some real data
	memset(au, 0, sizeof(AllocUnit));
	au->reportedSize = (reportedSize > 0xFFFFFFFF) ? (uint32_t)(0xFFFFFFFF) : (uint32_t) reportedSize;
	au->uncleanReportedAddress = calculateReportedAddress(actualSizedAllocation);
	au->sourceFile = sourceFile;
	au->sourceLine = sourceLine;
	au->sourceFunc = sourceFunc;
	au->allocationNumber = ++g_allocCounter;

	// Insert the new allocation into the hash table
	uintptr_t hashIndex = (((uintptr_t) au->uncleanReportedAddress) >> 4) & (hashSize - 1);
	if (hashTable[hashIndex]) {
		hashTable[hashIndex]->prev = au;
	}
	au->next = hashTable[hashIndex];
	au->prev = NULL;
	hashTable[hashIndex] = au;

	g_lastSourceFile = NULL;
	g_lastSourceLine = 0;
	g_lastSourceFunc = NULL;

	MUTEX_UNLOCK

	return CLEAN_REPORTED_ADDRESS(au->uncleanReportedAddress);
}

void *TrackedAAlloc(const char *sourceFile,
										const unsigned int sourceLine,
										const char *sourceFunc,
										const size_t reportedSize,
										void *actualSizedAllocation) {
	if (reservoirBufferSize == 0) {
		Mini_MutexCreate(&g_allocMutex);
	}

	MUTEX_LOCK

	if(g_breakOnAllocNumber != 0 && g_breakOnAllocNumber == g_allocCounter+1) {
		AL2O3_DEBUG_BREAK();
	}

	// If necessary, grow the reservoir of unused allocation units
	if (!reservoir) {
		if (!GrowReservoir()) {
			MUTEX_UNLOCK
			return NULL;
		}
	}

	// We don't want to assert with random failures, because we want the application to deal with them.
	if (actualSizedAllocation == NULL) {
		LOGERROR("Request for allocation failed. Out of memory.");
		MUTEX_UNLOCK
		return NULL;
	}

	// Logical flow says this should never happen...
	ASSERT(reservoir != NULL);

	// Grab a new allocaton unit from the front of the reservoir
	AllocUnit *au = reservoir;
	reservoir = au->next;

	// Populate it with some real data
	memset(au, 0, sizeof(AllocUnit));
	au->reportedSize = (reportedSize > 0xFFFFFFFF) ? (uint32_t)(0xFFFFFFFF) : (uint32_t)reportedSize;
	au->uncleanReportedAddress = actualSizedAllocation;
	au->sourceFile = sourceFile;
	au->sourceLine = sourceLine;
	au->sourceFunc = sourceFunc;
	au->allocationNumber = ++g_allocCounter;

	// or in reported == allocated bit
	au->uncleanReportedAddress = (void*)(((uintptr_t)au->uncleanReportedAddress) | REPORTED_ADDRESS_BITES_SAME_AS_REPORTED);

	// Insert the new allocation into the hash table
	uintptr_t hashIndex = (((uintptr_t) au->uncleanReportedAddress) >> 4) & (hashSize - 1);
	if (hashTable[hashIndex]) {
		hashTable[hashIndex]->prev = au;
	}
	au->next = hashTable[hashIndex];
	au->prev = NULL;
	hashTable[hashIndex] = au;

	g_lastSourceFile = NULL;
	g_lastSourceLine = 0;
	g_lastSourceFunc = NULL;

	MUTEX_UNLOCK

	return CLEAN_REPORTED_ADDRESS(au->uncleanReportedAddress);
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
	MUTEX_LOCK

	if(g_breakOnAllocNumber != 0 && g_breakOnAllocNumber == g_allocCounter+1) {
		AL2O3_DEBUG_BREAK();
	}

	// Locate the existing allocation unit
	AllocUnit *au = findAllocUnit(reportedAddress);

	// If you hit this assert, you tried to reallocate RAM that wasn't allocated by this memory manager.
	if (au == NULL) {
		LOGERROR("Request to reallocate RAM that was never allocated");
		MUTEX_UNLOCK
		return NULL;
	}


	// Keep track of the original size
	size_t originalReportedSize = au->reportedSize;

	// Do the reallocation
	void *oldReportedAddress = reportedAddress;
	size_t newActualSize = calculateActualSize(reportedSize);

	if (!actualSizedAllocation) {
		LOGERROR("Request for reallocation failed. Out of memory.");
		MUTEX_UNLOCK
		return NULL;
	}

	// Update the allocation with the new information
	au->reportedSize = (calculateReportedSize(newActualSize) > 0xFFFFFFFF) ? (uint32_t)(0xFFFFFFFF) : (uint32_t)calculateReportedSize(newActualSize);
	au->uncleanReportedAddress = calculateReportedAddress(actualSizedAllocation);
	au->sourceFile = sourceFile;
	au->sourceLine = sourceLine;
	au->sourceFunc = sourceFunc;
	au->allocationNumber = ++g_allocCounter;

	// The reallocation may cause the address to change, so we should relocate our allocation unit within the hash table
	unsigned int hashIndex = ~0;
	if (oldReportedAddress != CLEAN_REPORTED_ADDRESS(au->uncleanReportedAddress)) {
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
		hashIndex = (((uintptr_t) au->uncleanReportedAddress) >> 4) & (hashSize - 1);
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

	MUTEX_UNLOCK

	// Return the (reported) address of the new allocation unit
	return CLEAN_REPORTED_ADDRESS(au->uncleanReportedAddress);
}

bool TrackedFree(const char *sourceFile,
								 const unsigned int sourceLine,
								 const char *sourceFunc,
								 const void *reportedAddress) {
	if (!reportedAddress) {
		return false;
	}

	if (reservoirBufferSize == 0) {
		Mini_MutexCreate(&g_allocMutex);
	}

	MUTEX_LOCK

	if(reservoirBuffer == NULL) {
		LOGERROR("Free after exit (c++ static). No tracking available");
		MUTEX_UNLOCK
		return true; // we can tell if this is an aalloc or other assume other as more common...
	}

	// Go get the allocation unit
	AllocUnit *au = findAllocUnit(reportedAddress);
	if (au == NULL) {
		LOGERROR("Request to deallocate RAM that was never allocated");
		MUTEX_UNLOCK
		return false;
	}
	bool const adjustPtr = (REPORTED_ADDRESS_BITS_MASK(au->uncleanReportedAddress) & REPORTED_ADDRESS_BITES_SAME_AS_REPORTED) == 0;

	// Wipe the deallocated RAM with a new pattern. This doen't actually do us much good in debug mode under WIN32,
	// because Microsoft's memory debugging & tracking utilities will wipe it right after we do. Oh well.

	//	wipeWithPattern(au, releasedPattern);

	// Remove this allocation unit from the hash table
	uintptr_t hashIndex = (((uintptr_t) au->uncleanReportedAddress) >> 4) & (hashSize - 1);
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
	MUTEX_UNLOCK

	return adjustPtr;
}

AL2O3_EXTERN_C void *trackedMalloc(size_t size) {
	void *mem = platformMalloc(calculateActualSize(size));
	return TrackedAlloc(g_lastSourceFile, g_lastSourceLine, g_lastSourceFunc, size, mem);
}

AL2O3_EXTERN_C void *trackedAalloc(size_t size, size_t align) {
	if( align <= 16) {
		return trackedMalloc(size);
	}
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
	MUTEX_LOCK
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
				LOGINFO("%u bytes from %s(%u): %s number: %u", au->reportedSize, fileNameOnly, au->sourceLine, au->sourceFunc, au->allocationNumber);
			} else {
				LOGINFO("%u bytes from an unknown caller number: %u", au->reportedSize, au->allocationNumber);
			}
			au = au->next;
		}
	}

	// free the reservoirs
	for(uint32_t i = 0;i < reservoirBufferSize;++i) {
		platformFree(reservoirBuffer[i]);
	}
	reservoirBuffer = NULL;
	reservoir = NULL;
	reservoirBufferSize = 0;

	memset(hashTable, 0, sizeof(AllocUnit*) * hashSize);
	MUTEX_UNLOCK

	Mini_MutexDestroy(&g_allocMutex);
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
