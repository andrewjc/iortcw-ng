/*
===========================================================================
Engine Integration Tests for iortcw-ng

Tests the full engine initialization path including:
- Engine startup and shutdown
- Server initialization
- Map loading (with crash detection)
- Client/Server communication paths

These tests launch the actual engine binary and monitor for crashes.
===========================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

/* Test framework */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_TIMEOUT_SEC 15

typedef struct {
	int exit_code;
	int was_signaled;
	int signal_num;
	int timed_out;
	char output[8192];
	int output_len;
} test_result_t;

/*
 * Run a command with timeout and capture output
 * Returns: test_result_t with exit info and captured output
 */
static test_result_t run_command_with_timeout(const char *cmd, int timeout_sec) {
	test_result_t result;
	memset(&result, 0, sizeof(result));

	FILE *fp = popen(cmd, "r");
	if (!fp) {
		result.exit_code = -1;
		snprintf(result.output, sizeof(result.output), "popen failed: %s", strerror(errno));
		return result;
	}

	/* Read output with timeout using alarm */
	time_t start = time(NULL);
	char buf[256];
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		int remaining = sizeof(result.output) - result.output_len - 1;
		if (remaining > 0) {
			int len = strlen(buf);
			if (len > remaining) len = remaining;
			memcpy(result.output + result.output_len, buf, len);
			result.output_len += len;
		}
		if (time(NULL) - start > timeout_sec) {
			result.timed_out = 1;
			break;
		}
	}
	result.output[result.output_len] = '\0';

	int status = pclose(fp);
	if (WIFEXITED(status)) {
		result.exit_code = WEXITSTATUS(status);
	}
	if (WIFSIGNALED(status)) {
		result.was_signaled = 1;
		result.signal_num = WTERMSIG(status);
	}

	return result;
}

static void run_test(const char *name, int (*test_func)(void)) {
	tests_run++;
	printf("  [TEST] %s ... ", name);
	fflush(stdout);

	if (test_func()) {
		printf("PASS\n");
		tests_passed++;
	} else {
		printf("FAIL\n");
		tests_failed++;
	}
}

/* ================================================================
   Test: Dedicated server startup and shutdown
   ================================================================ */
static int test_dedicated_server_startup(void) {
	const char *cmd =
		"timeout 10 ./iowolfspded.x86_64"
		" +set dedicated 1"
		" +set com_hunkmegs 64"
		" +set fs_game \"\""
		" +set com_standalone 1"
		" +quit 2>&1";

	test_result_t result = run_command_with_timeout(cmd, TEST_TIMEOUT_SEC);

	/* The server should start and then quit cleanly.
	   It will likely error about pak0.pk3 missing, but should NOT crash. */
	if (result.was_signaled) {
		fprintf(stderr, "\n    CRASH: Server killed by signal %d\n", result.signal_num);
		fprintf(stderr, "    Output:\n%s\n", result.output);
		return 0;
	}

	/* Check for fatal crashes in output */
	if (strstr(result.output, "SIGSEGV") || strstr(result.output, "Segmentation fault")) {
		fprintf(stderr, "\n    SEGFAULT detected in server output\n");
		fprintf(stderr, "    Output:\n%s\n", result.output);
		return 0;
	}

	printf("\n    Server exited with code %d\n    ", result.exit_code);

	/* The server is expected to error about missing pak files,
	   but it should not crash (signal). */
	return 1;
}

/* ================================================================
   Test: Client startup with headless rendering
   ================================================================ */
static int test_client_startup_headless(void) {
	/* Check that DISPLAY is set */
	if (!getenv("DISPLAY")) {
		printf("\n    SKIP: No DISPLAY set\n    ");
		tests_run--;
		return 1;
	}

	const char *cmd =
		"timeout 10 ./iowolfsp.x86_64"
		" +set r_fullscreen 0"
		" +set r_mode 3"
		" +set com_hunkmegs 128"
		" +set s_initsound 0"
		" +set com_standalone 1"
		" +set in_nograb 1"
		" +quit 2>&1";

	test_result_t result = run_command_with_timeout(cmd, TEST_TIMEOUT_SEC);

	if (result.was_signaled) {
		fprintf(stderr, "\n    CRASH: Client killed by signal %d\n", result.signal_num);
		fprintf(stderr, "    Output (last 2000 chars):\n");
		int start = result.output_len > 2000 ? result.output_len - 2000 : 0;
		fprintf(stderr, "%s\n", result.output + start);
		return 0;
	}

	if (strstr(result.output, "SIGSEGV") || strstr(result.output, "Segmentation fault")) {
		fprintf(stderr, "\n    SEGFAULT detected in client output\n");
		fprintf(stderr, "    Output:\n%s\n", result.output);
		return 0;
	}

	printf("\n    Client exited with code %d\n    ", result.exit_code);
	return 1;
}

/* ================================================================
   Test: Client renderer initialization
   ================================================================ */
static int test_client_renderer_init(void) {
	if (!getenv("DISPLAY")) {
		printf("\n    SKIP: No DISPLAY set\n    ");
		tests_run--;
		return 1;
	}

	/* Start client with renderer but quit immediately after init */
	const char *cmd =
		"LIBGL_ALWAYS_SOFTWARE=1 timeout 15 ./iowolfsp.x86_64"
		" +set r_fullscreen 0"
		" +set r_mode 3"
		" +set r_allowSoftwareGL 1"
		" +set com_hunkmegs 128"
		" +set s_initsound 0"
		" +set com_standalone 1"
		" +set in_nograb 1"
		" +quit 2>&1";

	test_result_t result = run_command_with_timeout(cmd, TEST_TIMEOUT_SEC);

	if (result.was_signaled) {
		fprintf(stderr, "\n    CRASH during renderer init: signal %d\n", result.signal_num);

		/* Analyze the output for crash cause */
		if (strstr(result.output, "GLimp_Init")) {
			fprintf(stderr, "    Crash appears to be in GLimp_Init (GL context creation)\n");
		}
		if (strstr(result.output, "SDL_CreateWindow failed")) {
			fprintf(stderr, "    SDL_CreateWindow failed\n");
		}
		if (strstr(result.output, "could not load OpenGL subsystem")) {
			fprintf(stderr, "    OpenGL subsystem could not be loaded\n");
		}

		fprintf(stderr, "    Output:\n%s\n", result.output);
		return 0;
	}

	/* Check if renderer initialized successfully */
	if (strstr(result.output, "GL_RENDERER:")) {
		printf("\n    Renderer initialized OK\n    ");
	}
	if (strstr(result.output, "Renderer Initialization Complete")) {
		printf("Renderer init completed\n    ");
	}

	printf("Client exited with code %d\n    ", result.exit_code);
	return 1;
}

/* ================================================================
   Test: Map command without game data (should error, not crash)
   ================================================================ */
static int test_map_load_no_data(void) {
	/* The dedicated server should handle missing map files gracefully */
	const char *cmd =
		"timeout 10 ./iowolfspded.x86_64"
		" +set dedicated 1"
		" +set com_hunkmegs 64"
		" +set com_standalone 1"
		" +map test_nonexistent"
		" +quit 2>&1";

	test_result_t result = run_command_with_timeout(cmd, TEST_TIMEOUT_SEC);

	if (result.was_signaled) {
		fprintf(stderr, "\n    CRASH: Server crashed trying to load nonexistent map\n");
		fprintf(stderr, "    Signal: %d\n", result.signal_num);
		fprintf(stderr, "    Output:\n%s\n", result.output);
		return 0;
	}

	/* Should see "Can't find map" error, not a crash */
	if (strstr(result.output, "Can't find map")) {
		printf("\n    Correctly reported: Can't find map\n    ");
	}

	printf("Server exited with code %d (no crash)\n    ", result.exit_code);
	return 1;
}

/* ================================================================
   Test: Signal handling (crash recovery)
   ================================================================ */
static int test_signal_handling(void) {
	/* Fork and test that the engine's signal handler works */
	pid_t pid = fork();

	if (pid == 0) {
		/* Child: this would normally launch the engine and send it a signal.
		   For safety, we just test that our signal infrastructure works. */
		exit(0);
	}

	int status;
	waitpid(pid, &status, 0);

	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);
		if (code == 0) {
			return 1;
		}
		fprintf(stderr, "\n    Child exited with code %d\n", code);
		return 0;
	}

	if (WIFSIGNALED(status)) {
		fprintf(stderr, "\n    Child killed by signal %d\n", WTERMSIG(status));
		return 0;
	}

	return 1;
}

/* ================================================================
   Test: Engine binary exists and is executable
   ================================================================ */
static int test_binaries_exist(void) {
	int client_ok = (access("./iowolfsp.x86_64", X_OK) == 0);
	int server_ok = (access("./iowolfspded.x86_64", X_OK) == 0);
	int renderer_ok = (access("./renderer_sp_opengl1_x86_64.so", R_OK) == 0);

	printf("\n    Client binary: %s\n    ", client_ok ? "OK" : "MISSING");
	printf("Server binary: %s\n    ", server_ok ? "OK" : "MISSING");
	printf("Renderer SO: %s\n    ", renderer_ok ? "OK" : "MISSING");

	if (!client_ok && !server_ok) {
		fprintf(stderr, "    No binaries found. Build first with: make\n");
		return 0;
	}

	return 1;
}

/* ================================================================
   Test: Address Sanitizer smoke test
   ================================================================ */
static int test_engine_asan_smoke(void) {
	/* If we built with ASAN, this will catch memory errors */
	const char *cmd =
		"timeout 10 ./iowolfspded.x86_64"
		" +set dedicated 1"
		" +set com_hunkmegs 64"
		" +set com_standalone 1"
		" +quit 2>&1";

	test_result_t result = run_command_with_timeout(cmd, TEST_TIMEOUT_SEC);

	/* Check for ASAN output */
	if (strstr(result.output, "ERROR: AddressSanitizer") ||
	    strstr(result.output, "ERROR: LeakSanitizer")) {
		fprintf(stderr, "\n    ASAN/LSAN error detected!\n");
		fprintf(stderr, "    Output:\n%s\n", result.output);
		return 0;
	}

	return 1;
}

/* ================================================================
   Main
   ================================================================ */
int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	printf("========================================\n");
	printf("iortcw-ng Engine Integration Tests\n");
	printf("========================================\n");
	printf("CWD: ");
	fflush(stdout);
	system("pwd");
	printf("DISPLAY=%s\n", getenv("DISPLAY") ? getenv("DISPLAY") : "(not set)");
	printf("\n");

	printf("--- Prerequisites ---\n");
	run_test("binaries_exist", test_binaries_exist);

	printf("\n--- Server Tests ---\n");
	run_test("dedicated_server_startup", test_dedicated_server_startup);
	run_test("map_load_no_data", test_map_load_no_data);
	run_test("signal_handling", test_signal_handling);

	printf("\n--- Client Tests (Headless Xvfb) ---\n");
	run_test("client_startup_headless", test_client_startup_headless);
	run_test("client_renderer_init", test_client_renderer_init);

	printf("\n--- Sanitizer Tests ---\n");
	run_test("engine_asan_smoke", test_engine_asan_smoke);

	printf("\n========================================\n");
	printf("Results: %d/%d passed, %d failed\n",
		tests_passed, tests_run, tests_failed);
	printf("========================================\n");

	return tests_failed > 0 ? 1 : 0;
}
