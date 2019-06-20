#include "al2o3_memory/memory.h"

#if AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
#include "malloc.h"
#endif

AL2O3_EXTERN_C void* Memory_DefaultMalloc(size_t size) {
	return Memory_GlobalAllocator.malloc(size);
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
	&malloc,
	&calloc,
	&realloc,
	&free
};

// TODO specialise
AL2O3_EXTERN_C Memory_Allocator Memory_GlobalTempAllocator = {
	&malloc,
	&calloc,
	&realloc,
	&free
};
