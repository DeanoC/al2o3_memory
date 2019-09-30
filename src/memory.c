#include "al2o3_memory/memory.h"

#if AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
#include "malloc.h"
// on win32 we only have 8-byte alignment guaranteed, but the CRT provides special aligned allocation fns
AL2O3_FORCE_INLINE void* platformMalloc(size_t size)
{
	return _aligned_malloc(size, 16);
}

AL2O3_FORCE_INLINE void* platformAalloc(size_t size, size_t align)
{
	return _aligned_malloc(size, align);
}

AL2O3_FORCE_INLINE void* platformCalloc(size_t count, size_t size)
{
	void* mem =  _aligned_malloc(count * size, 16);
	if(mem) {
		memset(mem, 0, count * size);
	}
	return mem;
}

AL2O3_FORCE_INLINE void* platformRealloc(void* ptr, size_t size) {
	return _aligned_realloc(ptr, size, 16);
}

AL2O3_FORCE_INLINE void platformFree(void* ptr)
{
	_aligned_free(ptr);
}

#elif AL2O3_PLATFORM == AL2O3_PLATFORM_UNIX || AL2O3_PLATFORM_OS == AL2O3_OS_OSX

static void* platformMalloc(size_t size)
{
	void* mem;
	posix_memalign(&mem, 16, size);
	return mem;	
}

static void* platformAalloc(size_t size, size_t align)
{
	void* mem;
	posix_memalign(&mem, align, size);
	return mem;
}

static void* platformCalloc(size_t count, size_t size)
{
	void* mem;
	posix_memalign(&mem, 16, count * size);
	if(mem) {
		memset(mem, 0, count * size);
	}
	return mem;
}

static void* platformRealloc(void* ptr, size_t size) {
	// technically this appears to be a bit dodgy but given
	// chromium and ffmpeg do this according to
	// https://trac.ffmpeg.org/ticket/6403
	// i'll live with it and assert it
	ptr = realloc(ptr, size);
	ASSERT(((uintptr_t) ptr & 0xFUL) == 0);
	return ptr;
}

static void platformFree(void* ptr)
{
	free(ptr);
}

#else

#error Unsupported platform

#endif

AL2O3_EXTERN_C void* Memory_DefaultMalloc(size_t size) {
	return Memory_GlobalAllocator.malloc(size);
}

AL2O3_EXTERN_C void* Memory_DefaultAalloc(size_t size, size_t align) {
	return Memory_GlobalAllocator.aalloc(size, align);
}

AL2O3_EXTERN_C void* Memory_DefaultCalloc(size_t count, size_t size) {
	return Memory_GlobalAllocator.calloc(count, size);
}

AL2O3_EXTERN_C void* Memory_DefaultRealloc(void* memory, size_t size) {
	return Memory_GlobalAllocator.realloc(memory, size);
}

AL2O3_EXTERN_C void Memory_DefaultFree(void* memory) {
	Memory_GlobalAllocator.free(memory);
}

AL2O3_EXTERN_C void* Memory_DefaultTempMalloc(size_t size) {
	return Memory_GlobalTempAllocator.malloc(size);
}
AL2O3_EXTERN_C void* Memory_DefaultTempAalloc(size_t count, size_t align) {
	return Memory_GlobalTempAllocator.aalloc(count, align);
}

AL2O3_EXTERN_C void* Memory_DefaultTempCalloc(size_t count, size_t size) {
	return Memory_GlobalTempAllocator.calloc(count, size);
}

AL2O3_EXTERN_C void* Memory_DefaultTempRealloc(void* memory, size_t size) {
	return Memory_GlobalTempAllocator.realloc(memory, size);
}

AL2O3_EXTERN_C void Memory_DefaultTempFree(void* memory) {
	Memory_GlobalTempAllocator.free(memory);
}


AL2O3_EXTERN_C Memory_Allocator Memory_GlobalAllocator = {
		&platformMalloc,
		&platformAalloc,
		&platformCalloc,
		&platformRealloc,
		&platformFree
};

// TODO specialise
AL2O3_EXTERN_C Memory_Allocator Memory_GlobalTempAllocator = {
		&platformMalloc,
		&platformAalloc,
		&platformCalloc,
		&platformRealloc,
		&platformFree
};
