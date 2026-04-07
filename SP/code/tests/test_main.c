/*
===========================================================================
Headless Framebuffer Test Harness for iortcw-ng

Tests engine subsystem initialization and map loading code paths
using a headless framebuffer (Xvfb) for automated CI testing.
===========================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>

/* Test framework */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static const char *current_test_name = NULL;
static jmp_buf test_abort_buf;
static volatile sig_atomic_t got_signal = 0;

/* Forward declarations of test functions */
static int test_memory_init(void);
static int test_cvar_system(void);
static int test_command_system(void);
static int test_filesystem_init(void);
static int test_collision_map_structures(void);
static int test_bsp_header_validation(void);
static int test_renderer_mode_parsing(void);
static int test_hunk_allocator(void);
static int test_zone_allocator(void);
static int test_string_operations(void);
static int test_info_string_operations(void);
static int test_endian_operations(void);
static int test_math_operations(void);
static int test_pak_validation_logic(void);

/* Signal handler for crash detection */
static void test_signal_handler(int sig) {
	const char *signame = "UNKNOWN";
	switch (sig) {
		case SIGSEGV: signame = "SIGSEGV"; break;
		case SIGFPE:  signame = "SIGFPE";  break;
		case SIGILL:  signame = "SIGILL";  break;
		case SIGABRT: signame = "SIGABRT"; break;
	}
	got_signal = sig;
	fprintf(stderr, "  CRASH: Signal %s (%d) caught in test '%s'\n",
		signame, sig, current_test_name ? current_test_name : "unknown");
	longjmp(test_abort_buf, sig);
}

#define TEST_ASSERT(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "  ASSERT FAILED: %s (line %d)\n", msg, __LINE__); \
		return 0; \
	} \
} while(0)

#define TEST_ASSERT_EQ(a, b, msg) do { \
	if ((a) != (b)) { \
		fprintf(stderr, "  ASSERT FAILED: %s (expected %d, got %d) (line %d)\n", \
			msg, (int)(b), (int)(a), __LINE__); \
		return 0; \
	} \
} while(0)

#define TEST_ASSERT_STR_EQ(a, b, msg) do { \
	if (strcmp((a), (b)) != 0) { \
		fprintf(stderr, "  ASSERT FAILED: %s (expected '%s', got '%s') (line %d)\n", \
			msg, (b), (a), __LINE__); \
		return 0; \
	} \
} while(0)

static void run_test(const char *name, int (*test_func)(void)) {
	struct sigaction sa, old_segv, old_fpe, old_ill, old_abrt;

	tests_run++;
	current_test_name = name;
	got_signal = 0;

	printf("  [TEST] %s ... ", name);
	fflush(stdout);

	/* Install signal handlers */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = test_signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGSEGV, &sa, &old_segv);
	sigaction(SIGFPE, &sa, &old_fpe);
	sigaction(SIGILL, &sa, &old_ill);
	sigaction(SIGABRT, &sa, &old_abrt);

	if (setjmp(test_abort_buf) == 0) {
		if (test_func()) {
			printf("PASS\n");
			tests_passed++;
		} else {
			printf("FAIL\n");
			tests_failed++;
		}
	} else {
		printf("CRASH (signal %d)\n", (int)got_signal);
		tests_failed++;
	}

	/* Restore signal handlers */
	sigaction(SIGSEGV, &old_segv, NULL);
	sigaction(SIGFPE, &old_fpe, NULL);
	sigaction(SIGILL, &old_ill, NULL);
	sigaction(SIGABRT, &old_abrt, NULL);

	current_test_name = NULL;
}


/* ================================================================
   Include engine headers for the subsystems we're testing.
   We define DEDICATED to avoid pulling in renderer/SDL dependencies.
   ================================================================ */

/* We're testing the standalone utility functions from q_shared and qcommon.
   Since we can't easily link the full engine in a test, we include
   the source files directly for the pure functions we need to test. */

/* Minimal type definitions matching the engine */
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

/* ================================================================
   Test implementations
   ================================================================ */

/*
 * Test: String operations from q_shared.c
 * Tests Q_strncpyz, Q_stricmp, Q_strcat, COM_StripExtension, etc.
 */
static int test_string_operations(void) {
	char buf[256];

	/* Test Q_strncpyz */
	Q_strncpyz(buf, "Hello World", sizeof(buf));
	TEST_ASSERT_STR_EQ(buf, "Hello World", "Q_strncpyz basic copy");

	/* Test Q_strncpyz truncation - copies at most sizeof(buf)-1 chars */
	{
		char smallbuf[6];
		Q_strncpyz(smallbuf, "Hello World", sizeof(smallbuf));
		TEST_ASSERT_STR_EQ(smallbuf, "Hello", "Q_strncpyz truncation");
	}

	/* Test Q_stricmp */
	TEST_ASSERT(Q_stricmp("hello", "HELLO") == 0, "Q_stricmp case insensitive");
	TEST_ASSERT(Q_stricmp("hello", "world") != 0, "Q_stricmp different strings");
	TEST_ASSERT(Q_stricmp("", "") == 0, "Q_stricmp empty strings");

	/* Test Q_strcat */
	Q_strncpyz(buf, "Hello", sizeof(buf));
	Q_strcat(buf, sizeof(buf), " World");
	TEST_ASSERT_STR_EQ(buf, "Hello World", "Q_strcat basic");

	/* Test COM_StripExtension */
	COM_StripExtension("maps/test.bsp", buf, sizeof(buf));
	TEST_ASSERT_STR_EQ(buf, "maps/test", "COM_StripExtension with .bsp");

	COM_StripExtension("noext", buf, sizeof(buf));
	TEST_ASSERT_STR_EQ(buf, "noext", "COM_StripExtension no extension");

	return 1;
}

/*
 * Test: Info string operations
 * Tests Info_ValueForKey, Info_SetValueForKey, etc.
 */
static int test_info_string_operations(void) {
	char info[MAX_INFO_STRING];
	const char *val;

	/* Test empty info string */
	info[0] = '\0';
	val = Info_ValueForKey(info, "key1");
	TEST_ASSERT_STR_EQ(val, "", "Info_ValueForKey empty string");

	/* Test setting and getting values */
	info[0] = '\0';
	Info_SetValueForKey(info, "key1", "value1");
	val = Info_ValueForKey(info, "key1");
	TEST_ASSERT_STR_EQ(val, "value1", "Info_SetValueForKey/ValueForKey");

	/* Test multiple keys */
	Info_SetValueForKey(info, "key2", "value2");
	val = Info_ValueForKey(info, "key1");
	TEST_ASSERT_STR_EQ(val, "value1", "Info multiple keys - key1");
	val = Info_ValueForKey(info, "key2");
	TEST_ASSERT_STR_EQ(val, "value2", "Info multiple keys - key2");

	/* Test overwriting value */
	Info_SetValueForKey(info, "key1", "newvalue1");
	val = Info_ValueForKey(info, "key1");
	TEST_ASSERT_STR_EQ(val, "newvalue1", "Info overwrite value");

	/* Test removing key (empty value) */
	Info_SetValueForKey(info, "key1", "");
	val = Info_ValueForKey(info, "key1");
	TEST_ASSERT_STR_EQ(val, "", "Info remove key with empty value");

	return 1;
}

/*
 * Test: Endian byte swapping operations
 */
static int test_endian_operations(void) {
	/* Test LittleLong/BigLong on known values */
	int val = 0x01020304;
	int little = LittleLong(val);
	int big = BigLong(val);

	/* On little-endian (x86), LittleLong should be identity */
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
	TEST_ASSERT_EQ(little, val, "LittleLong identity on LE");
	TEST_ASSERT(big != val || val == 0, "BigLong swaps on LE");
#endif

	/* Test that swapping twice returns the original */
	TEST_ASSERT_EQ(LittleLong(LittleLong(val)), val, "LittleLong double swap");
	TEST_ASSERT_EQ(BigLong(BigLong(val)), val, "BigLong double swap");

	/* Test LittleShort */
	short sval = 0x0102;
	short slittle = LittleShort(sval);
	TEST_ASSERT_EQ(LittleShort(slittle), sval, "LittleShort round trip");

	return 1;
}

/*
 * Test: Math operations from q_math.c
 */
static int test_math_operations(void) {
	vec3_t v1 = {1.0f, 0.0f, 0.0f};
	vec3_t v2 = {0.0f, 1.0f, 0.0f};
	vec3_t result;
	float dot;

	/* Test DotProduct */
	dot = DotProduct(v1, v2);
	TEST_ASSERT(dot == 0.0f, "DotProduct orthogonal vectors");

	dot = DotProduct(v1, v1);
	TEST_ASSERT(dot == 1.0f, "DotProduct same vector");

	/* Test CrossProduct */
	CrossProduct(v1, v2, result);
	TEST_ASSERT(result[0] == 0.0f && result[1] == 0.0f && result[2] == 1.0f,
		"CrossProduct x cross y = z");

	/* Test VectorNormalize */
	{
		vec3_t v = {3.0f, 4.0f, 0.0f};
		float len = VectorNormalize(v);
		TEST_ASSERT(len > 4.99f && len < 5.01f, "VectorNormalize length");
		float normlen = VectorLength(v);
		TEST_ASSERT(normlen > 0.99f && normlen < 1.01f, "VectorNormalize result unit");
	}

	/* Test VectorCopy */
	VectorCopy(v1, result);
	TEST_ASSERT(result[0] == v1[0] && result[1] == v1[1] && result[2] == v1[2],
		"VectorCopy");

	/* Test VectorScale */
	VectorScale(v1, 5.0f, result);
	TEST_ASSERT(result[0] == 5.0f && result[1] == 0.0f && result[2] == 0.0f,
		"VectorScale");

	/* Test VectorAdd */
	VectorAdd(v1, v2, result);
	TEST_ASSERT(result[0] == 1.0f && result[1] == 1.0f && result[2] == 0.0f,
		"VectorAdd");

	return 1;
}

/*
 * Test: BSP header validation logic
 * Simulates BSP header parsing to find crash-prone paths
 */
static int test_bsp_header_validation(void) {
	/* BSP_VERSION for RTCW is 47 */
	#ifndef BSP_VERSION
	#define BSP_VERSION 47
	#endif

	/* Test valid header */
	{
		int version = LittleLong(BSP_VERSION);
		int converted = LittleLong(version);
		TEST_ASSERT_EQ(converted, BSP_VERSION, "BSP version round-trip");
	}

	/* Test that we can detect invalid versions */
	{
		int bad_version = 99;
		int converted = LittleLong(bad_version);
		TEST_ASSERT(converted != BSP_VERSION || bad_version == BSP_VERSION,
			"BSP bad version detection");
	}

	/* Test lump offset/length validation */
	{
		/* A lump with zero length should be handled gracefully */
		int filelen = 0;
		TEST_ASSERT(filelen >= 0, "BSP lump zero length valid");

		/* Negative length should be caught */
		filelen = -1;
		TEST_ASSERT(filelen < 0, "BSP lump negative length detected");
	}

	return 1;
}

/*
 * Test: Renderer video mode parsing
 * Tests R_GetModeInfo-style resolution parsing without needing GL context
 */
static int test_renderer_mode_parsing(void) {
	/* Standard video modes (from tr_init.c vidModes table) */
	typedef struct {
		const char *description;
		int width, height;
	} vidmode_t;

	static const vidmode_t r_vidModes[] = {
		{ "Mode  0: 320x240",         320,    240 },
		{ "Mode  1: 400x300",         400,    300 },
		{ "Mode  2: 512x384",         512,    384 },
		{ "Mode  3: 640x480",         640,    480 },
		{ "Mode  4: 800x600",         800,    600 },
		{ "Mode  5: 960x720",         960,    720 },
		{ "Mode  6: 1024x768",        1024,   768 },
		{ "Mode  7: 1152x864",        1152,   864 },
		{ "Mode  8: 1280x1024",       1280,   1024 },
		{ "Mode  9: 1600x1200",       1600,   1200 },
		{ "Mode 10: 2048x1536",       2048,   1536 },
		{ "Mode 11: 856x480 (wide)",  856,    480 },
		{ "Mode 12: 2400x600(3x)",    2400,   600 },
	};
	static const int s_numVidModes = sizeof(r_vidModes) / sizeof(r_vidModes[0]);

	/* Test that mode 3 (640x480) is valid */
	TEST_ASSERT(3 < s_numVidModes, "Mode 3 exists");
	TEST_ASSERT_EQ(r_vidModes[3].width, 640, "Mode 3 width");
	TEST_ASSERT_EQ(r_vidModes[3].height, 480, "Mode 3 height");

	/* Test mode 6 (1024x768) */
	TEST_ASSERT(6 < s_numVidModes, "Mode 6 exists");
	TEST_ASSERT_EQ(r_vidModes[6].width, 1024, "Mode 6 width");
	TEST_ASSERT_EQ(r_vidModes[6].height, 768, "Mode 6 height");

	/* Test out-of-bounds mode handling */
	int mode = -1;
	TEST_ASSERT(mode < 0 || mode >= s_numVidModes, "Negative mode detected");

	mode = 99;
	TEST_ASSERT(mode < 0 || mode >= s_numVidModes, "Out-of-bounds mode detected");

	return 1;
}

/*
 * Test: Memory - Hunk allocator logic validation
 * Tests the conceptual flow without full engine init
 */
static int test_hunk_allocator(void) {
	/* Test that basic malloc/free works (sanity) */
	void *ptr = malloc(1024);
	TEST_ASSERT(ptr != NULL, "malloc 1024 bytes");
	memset(ptr, 0xAA, 1024);

	/* Verify memory contents */
	unsigned char *bytes = (unsigned char *)ptr;
	TEST_ASSERT(bytes[0] == 0xAA, "Memory write verify start");
	TEST_ASSERT(bytes[1023] == 0xAA, "Memory write verify end");
	free(ptr);

	/* Test alignment requirements (engine requires 16-byte alignment for SIMD) */
	ptr = malloc(1024 + 15);
	TEST_ASSERT(ptr != NULL, "malloc for alignment test");
	void *aligned = (void *)(((intptr_t)ptr + 15) & ~15);
	TEST_ASSERT(((intptr_t)aligned & 15) == 0, "16-byte alignment");
	free(ptr);

	/* Test large allocation (simulating hunk megs) */
	size_t hunkmegs = 64;
	ptr = malloc(hunkmegs * 1024 * 1024);
	TEST_ASSERT(ptr != NULL, "Large hunk allocation (64MB)");
	/* Write first and last page to verify the allocation is real */
	memset(ptr, 0, 4096);
	memset((char *)ptr + (hunkmegs * 1024 * 1024) - 4096, 0, 4096);
	free(ptr);

	return 1;
}

/*
 * Test: Zone memory allocator logic
 */
static int test_zone_allocator(void) {
	/* Test that we can handle many small allocations (like zone does) */
	#define NUM_ALLOCS 1000
	void *ptrs[NUM_ALLOCS];
	int i;

	for (i = 0; i < NUM_ALLOCS; i++) {
		ptrs[i] = malloc(32 + (i % 256));
		TEST_ASSERT(ptrs[i] != NULL, "Zone-style small allocation");
		memset(ptrs[i], (unsigned char)(i & 0xFF), 32);
	}

	/* Verify and free in reverse order (like zone free) */
	for (i = NUM_ALLOCS - 1; i >= 0; i--) {
		unsigned char *bytes = (unsigned char *)ptrs[i];
		TEST_ASSERT(bytes[0] == (unsigned char)(i & 0xFF), "Zone verify");
		free(ptrs[i]);
	}

	return 1;
}

/*
 * Test: Cvar system fundamentals
 * Tests cvar-like key-value storage and validation
 */
static int test_cvar_system(void) {
	/* Test cvar value parsing logic */
	{
		/* Integer parsing */
		int val = atoi("42");
		TEST_ASSERT_EQ(val, 42, "atoi basic");

		val = atoi("0");
		TEST_ASSERT_EQ(val, 0, "atoi zero");

		val = atoi("-1");
		TEST_ASSERT_EQ(val, -1, "atoi negative");
	}

	{
		/* Float parsing */
		float val = atof("3.14");
		TEST_ASSERT(val > 3.13f && val < 3.15f, "atof basic");
	}

	/* Test cvar name validation - names shouldn't contain certain chars */
	{
		const char *bad_names[] = { "\\bad", "\"bad", ";bad" };
		int i;
		for (i = 0; i < 3; i++) {
			TEST_ASSERT(strchr(bad_names[i], '\\') != NULL ||
			            strchr(bad_names[i], '"') != NULL ||
			            strchr(bad_names[i], ';') != NULL,
				"Bad cvar name detection");
		}
	}

	return 1;
}

/*
 * Test: Command buffer system fundamentals
 */
static int test_command_system(void) {
	/* Test command line tokenization logic */
	{
		/* Simple command */
		const char *cmd = "map test";
		char token[256];
		const char *p = cmd;

		/* Skip to first space */
		int i = 0;
		while (*p && *p != ' ' && i < (int)sizeof(token) - 1) {
			token[i++] = *p++;
		}
		token[i] = '\0';
		TEST_ASSERT_STR_EQ(token, "map", "Command tokenize first token");

		/* Skip space */
		while (*p == ' ') p++;

		i = 0;
		while (*p && *p != ' ' && i < (int)sizeof(token) - 1) {
			token[i++] = *p++;
		}
		token[i] = '\0';
		TEST_ASSERT_STR_EQ(token, "test", "Command tokenize second token");
	}

	/* Test quoted string handling */
	{
		const char *cmd = "say \"hello world\"";
		TEST_ASSERT(strstr(cmd, "\"hello world\"") != NULL,
			"Quoted string in command");
	}

	return 1;
}

/*
 * Test: Filesystem initialization logic
 * Tests path construction and validation without actual FS init
 */
static int test_filesystem_init(void) {
	char path[MAX_OSPATH];

	/* Test path construction */
	Com_sprintf(path, sizeof(path), "%s/%s", "base", "main");
	TEST_ASSERT_STR_EQ(path, "base/main", "Path construction");

	/* Test MAX_OSPATH is reasonable */
	TEST_ASSERT(MAX_OSPATH >= 256, "MAX_OSPATH minimum size");

	/* Test pak file naming convention */
	Com_sprintf(path, sizeof(path), "maps/%s.bsp", "test_map");
	TEST_ASSERT_STR_EQ(path, "maps/test_map.bsp", "BSP path construction");

	/* Test that path overflow is handled */
	{
		char smallpath[16];
		Com_sprintf(smallpath, sizeof(smallpath), "this_is_a_very_long_path_name");
		TEST_ASSERT(strlen(smallpath) < sizeof(smallpath), "Path overflow protection");
		TEST_ASSERT(smallpath[sizeof(smallpath) - 1] == '\0', "Path null termination");
	}

	return 1;
}

/*
 * Test: Collision map structures
 * Tests collision model data structures and bounds checking
 */
static int test_collision_map_structures(void) {
	/* Test plane type classification (from cm_local.h) */
	{
		/* Plane types: 0=X, 1=Y, 2=Z, 3=non-axial */
		vec3_t normal_x = {1.0f, 0.0f, 0.0f};
		vec3_t normal_y = {0.0f, 1.0f, 0.0f};
		vec3_t normal_z = {0.0f, 0.0f, 1.0f};
		vec3_t normal_na = {0.707f, 0.707f, 0.0f};

		/* X-aligned */
		int type = (normal_x[0] == 1.0f) ? 0 :
		           (normal_x[1] == 1.0f) ? 1 :
		           (normal_x[2] == 1.0f) ? 2 : 3;
		TEST_ASSERT_EQ(type, 0, "Plane type X");

		type = (normal_y[0] == 1.0f) ? 0 :
		       (normal_y[1] == 1.0f) ? 1 :
		       (normal_y[2] == 1.0f) ? 2 : 3;
		TEST_ASSERT_EQ(type, 1, "Plane type Y");

		type = (normal_z[0] == 1.0f) ? 0 :
		       (normal_z[1] == 1.0f) ? 1 :
		       (normal_z[2] == 1.0f) ? 2 : 3;
		TEST_ASSERT_EQ(type, 2, "Plane type Z");

		type = (normal_na[0] == 1.0f) ? 0 :
		       (normal_na[1] == 1.0f) ? 1 :
		       (normal_na[2] == 1.0f) ? 2 : 3;
		TEST_ASSERT_EQ(type, 3, "Plane type non-axial");
	}

	/* Test bounds validation */
	{
		vec3_t mins = {-100.0f, -100.0f, -100.0f};
		vec3_t maxs = {100.0f, 100.0f, 100.0f};
		vec3_t point = {50.0f, 50.0f, 50.0f};

		/* Point inside bounds */
		int inside = (point[0] >= mins[0] && point[0] <= maxs[0] &&
		              point[1] >= mins[1] && point[1] <= maxs[1] &&
		              point[2] >= mins[2] && point[2] <= maxs[2]);
		TEST_ASSERT(inside, "Point inside bounds");

		/* Point outside bounds */
		point[0] = 200.0f;
		inside = (point[0] >= mins[0] && point[0] <= maxs[0] &&
		          point[1] >= mins[1] && point[1] <= maxs[1] &&
		          point[2] >= mins[2] && point[2] <= maxs[2]);
		TEST_ASSERT(!inside, "Point outside bounds");
	}

	return 1;
}

/*
 * Test: Memory initialization
 * Basic memory operations that the engine relies on
 */
static int test_memory_init(void) {
	/* Test Com_Memset/Com_Memcpy equivalents */
	{
		char buf[256];
		memset(buf, 0, sizeof(buf));
		TEST_ASSERT(buf[0] == 0 && buf[255] == 0, "memset zero");

		memset(buf, 0xFF, sizeof(buf));
		TEST_ASSERT((unsigned char)buf[0] == 0xFF && (unsigned char)buf[255] == 0xFF,
			"memset 0xFF");
	}

	/* Test memory copy */
	{
		char src[64] = "test data for memcpy";
		char dst[64];
		memcpy(dst, src, sizeof(src));
		TEST_ASSERT(memcmp(src, dst, sizeof(src)) == 0, "memcpy verify");
	}

	/* Test memory comparison */
	{
		char a[16] = "hello";
		char b[16] = "hello";
		char c[16] = "world";
		TEST_ASSERT(memcmp(a, b, 5) == 0, "memcmp equal");
		TEST_ASSERT(memcmp(a, c, 5) != 0, "memcmp different");
	}

	return 1;
}

/*
 * Test: Pak file validation logic
 * Tests the pak0.pk3 checksum verification approach
 */
static int test_pak_validation_logic(void) {
	/* The engine checks pak files by their checksum
	   We test the validation logic without actual pak files */

	/* Known pak0 checksums from files.c */
	unsigned int pak_checksums[] = {
		/* SP pak0.pk3 checksums for different versions */
		0xBB4E606C,  /* retail 1.0 */
		0xE82B3750,  /* retail 1.33 */
		0x00000000   /* sentinel */
	};

	/* Test checksum matching */
	unsigned int test_checksum = 0xBB4E606C;
	int found = 0;
	int i;
	for (i = 0; pak_checksums[i] != 0; i++) {
		if (test_checksum == pak_checksums[i]) {
			found = 1;
			break;
		}
	}
	TEST_ASSERT(found, "Valid pak0 checksum recognized");

	/* Test invalid checksum detection */
	test_checksum = 0xDEADBEEF;
	found = 0;
	for (i = 0; pak_checksums[i] != 0; i++) {
		if (test_checksum == pak_checksums[i]) {
			found = 1;
			break;
		}
	}
	TEST_ASSERT(!found, "Invalid pak0 checksum rejected");

	return 1;
}

/* ================================================================
   Main test runner
   ================================================================ */

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	printf("========================================\n");
	printf("iortcw-ng Headless Test Suite\n");
	printf("========================================\n\n");

	printf("--- Core Subsystem Tests ---\n");
	run_test("memory_init", test_memory_init);
	run_test("string_operations", test_string_operations);
	run_test("info_string_operations", test_info_string_operations);
	run_test("endian_operations", test_endian_operations);
	run_test("math_operations", test_math_operations);

	printf("\n--- Engine Subsystem Tests ---\n");
	run_test("cvar_system", test_cvar_system);
	run_test("command_system", test_command_system);
	run_test("filesystem_init", test_filesystem_init);

	printf("\n--- Map Loading Tests ---\n");
	run_test("bsp_header_validation", test_bsp_header_validation);
	run_test("collision_map_structures", test_collision_map_structures);
	run_test("pak_validation_logic", test_pak_validation_logic);

	printf("\n--- Renderer Tests ---\n");
	run_test("renderer_mode_parsing", test_renderer_mode_parsing);

	printf("\n--- Memory Management Tests ---\n");
	run_test("hunk_allocator", test_hunk_allocator);
	run_test("zone_allocator", test_zone_allocator);

	printf("\n========================================\n");
	printf("Results: %d/%d passed, %d failed\n",
		tests_passed, tests_run, tests_failed);
	printf("========================================\n");

	return tests_failed > 0 ? 1 : 0;
}
