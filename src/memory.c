#include "al2o3_memory/memory.h"

#include "malloc.h"

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

AL2O3_EXTERN_C void* Memory_TempDefaultMalloc(size_t size) {
	return Memory_GlobalTempAllocator.malloc(size);
}
AL2O3_EXTERN_C void* Memory_TempDefaultCalloc(size_t count, size_t size) {
	return Memory_GlobalTempAllocator.calloc(count, size);
}

AL2O3_EXTERN_C void* Memory_TempDefaultRealloc(void* memory, size_t size) {
	return Memory_GlobalTempAllocator.realloc(memory, size);
}

AL2O3_EXTERN_C void Memory_TempDefaultFree(void* memory) {
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
