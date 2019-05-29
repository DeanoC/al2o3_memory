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

AL2O3_EXTERN_C Memory_Allocator Memory_GlobalAllocator = {
	&malloc,
	&calloc,
	&realloc,
	&free
};
