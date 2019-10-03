#include "al2o3_catch2/catch2.hpp"
#include "al2o3_memory/memory.h"

TEST_CASE("CDict create/destroy", "[al2o3 Memory]") {
	void* m0 = MEMORY_MALLOC(10);
	REQUIRE(m0);
	MEMORY_FREE(m0);
	void* m1 = MEMORY_CALLOC(10,10);
	REQUIRE(m1);
	for(int i =0;i < 10 * 10;++i) {
		REQUIRE( ((uint8_t*)m1)[i] == 0);
	}

	void* m2 = MEMORY_MALLOC(10);
	REQUIRE(m2);
	uint8_t const tst[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	memcpy(m2, tst, 10);
	REQUIRE(memcmp(tst, m2, 10) == 0);
	m2 = MEMORY_REALLOC(m2, 100);
	REQUIRE(memcmp(tst, m2, 10) == 0);

	void* am0 = MEMORY_AALLOC(10, 256);
	REQUIRE(am0);
	REQUIRE((((uintptr_t)am0) & 0xFF) == 0);
	MEMORY_FREE(am0);
	MEMORY_FREE(m2);
	MEMORY_FREE(m1);

	MEMORY_MALLOC(10); // check the leak detector by inspection
}

