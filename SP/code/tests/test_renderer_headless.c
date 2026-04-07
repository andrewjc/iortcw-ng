/*
===========================================================================
Headless Renderer Integration Tests for iortcw-ng

Tests the full OpenGL rendering pipeline using Xvfb (X Virtual Framebuffer)
and software Mesa rendering. These tests exercise the actual renderer code
paths that can crash during map loading.

Build with: (see Makefile target 'test-renderer')
Run with: ./run_tests.sh --renderer
===========================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <dlfcn.h>

#ifdef USE_LOCAL_HEADERS
#include "SDL.h"
#else
#include <SDL.h>
#endif

#include <GL/gl.h>

/* Test framework */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static jmp_buf test_abort_buf;
static volatile sig_atomic_t got_signal = 0;
static const char *current_test_name = NULL;

static void test_signal_handler(int sig) {
	got_signal = sig;
	longjmp(test_abort_buf, sig);
}

#define TEST_ASSERT(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "  ASSERT FAILED: %s (line %d)\n", msg, __LINE__); \
		return 0; \
	} \
} while(0)

static void run_test(const char *name, int (*test_func)(void)) {
	struct sigaction sa, old_segv, old_fpe;

	tests_run++;
	current_test_name = name;
	got_signal = 0;

	printf("  [TEST] %s ... ", name);
	fflush(stdout);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = test_signal_handler;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, &old_segv);
	sigaction(SIGFPE, &sa, &old_fpe);

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

	sigaction(SIGSEGV, &old_segv, NULL);
	sigaction(SIGFPE, &old_fpe, NULL);
	current_test_name = NULL;
}

/* ================================================================
   SDL / OpenGL Headless Tests
   ================================================================ */

static SDL_Window *test_window = NULL;
static SDL_GLContext test_gl_context = NULL;

/*
 * Test: SDL Video Subsystem Initialization
 * This is the first thing the renderer does - if this fails,
 * the engine will crash with "SDL_Init( SDL_INIT_VIDEO ) FAILED"
 */
static int test_sdl_video_init(void) {
	int result = SDL_Init(SDL_INIT_VIDEO);
	if (result != 0) {
		fprintf(stderr, "  SDL_Init(VIDEO) failed: %s\n", SDL_GetError());
		fprintf(stderr, "  DISPLAY=%s\n", getenv("DISPLAY") ? getenv("DISPLAY") : "(not set)");
	}
	TEST_ASSERT(result == 0, "SDL_Init(SDL_INIT_VIDEO)");

	/* Log SDL version info */
	SDL_version compiled;
	SDL_version linked;
	SDL_VERSION(&compiled);
	SDL_GetVersion(&linked);
	printf("\n    SDL compiled: %d.%d.%d, linked: %d.%d.%d\n    ",
		compiled.major, compiled.minor, compiled.patch,
		linked.major, linked.minor, linked.patch);

	const char *driver = SDL_GetCurrentVideoDriver();
	printf("Video driver: %s\n    ", driver ? driver : "(null)");

	return 1;
}

/*
 * Test: SDL Window Creation (headless)
 * Creates a window using the headless X11 display (Xvfb)
 * This mirrors GLimp_SetMode() in sdl_glimp.c
 */
static int test_sdl_window_creation(void) {
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;

	/* Set GL attributes like the engine does (sdl_glimp.c:640+) */
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	test_window = SDL_CreateWindow(
		"iortcw-ng Test",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		640, 480, flags);

	if (!test_window) {
		fprintf(stderr, "  SDL_CreateWindow failed: %s\n", SDL_GetError());
	}
	TEST_ASSERT(test_window != NULL, "SDL_CreateWindow (640x480)");

	return 1;
}

/*
 * Test: OpenGL Context Creation
 * Creates an OpenGL context - this is where the engine often crashes
 * if the GL driver is incompatible or missing
 */
static int test_gl_context_creation(void) {
	TEST_ASSERT(test_window != NULL, "Window exists for GL context");

	test_gl_context = SDL_GL_CreateContext(test_window);
	if (!test_gl_context) {
		fprintf(stderr, "  SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		/* Try with compatibility profile */
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		test_gl_context = SDL_GL_CreateContext(test_window);
		if (!test_gl_context) {
			fprintf(stderr, "  Fallback GL context also failed: %s\n", SDL_GetError());
		}
	}
	TEST_ASSERT(test_gl_context != NULL, "SDL_GL_CreateContext");

	return 1;
}

/*
 * Test: OpenGL Basic Operations
 * Verifies that GL calls work without crashing - exercises the code
 * paths that happen in GLimp_Init after context creation
 */
static int test_gl_basic_operations(void) {
	TEST_ASSERT(test_gl_context != NULL, "GL context exists");

	/* These are the first GL calls the engine makes (sdl_glimp.c:801-803) */
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	GLenum err = glGetError();
	TEST_ASSERT(err == GL_NO_ERROR, "glClearColor no error");

	glClear(GL_COLOR_BUFFER_BIT);
	err = glGetError();
	TEST_ASSERT(err == GL_NO_ERROR, "glClear no error");

	/* Get renderer string - engine uses this to detect software rendering */
	const char *renderer = (const char *)glGetString(GL_RENDERER);
	TEST_ASSERT(renderer != NULL, "GL_RENDERER not NULL");
	printf("\n    GL_RENDERER: %s\n    ", renderer);

	const char *version = (const char *)glGetString(GL_VERSION);
	TEST_ASSERT(version != NULL, "GL_VERSION not NULL");
	printf("GL_VERSION: %s\n    ", version);

	const char *vendor = (const char *)glGetString(GL_VENDOR);
	TEST_ASSERT(vendor != NULL, "GL_VENDOR not NULL");
	printf("GL_VENDOR: %s\n    ", vendor);

	/* The engine checks for software renderer and rejects it for rend2.
	   In our headless test, software rendering is expected and fine. */
	if (renderer && (strstr(renderer, "llvmpipe") || strstr(renderer, "Software") || strstr(renderer, "softpipe"))) {
		printf("(Software renderer - expected for headless testing)\n    ");
	}

	return 1;
}

/*
 * Test: GL State Operations
 * Tests the GL state management that happens during rendering
 */
static int test_gl_state_operations(void) {
	TEST_ASSERT(test_gl_context != NULL, "GL context exists");

	/* Test depth buffer operations */
	glEnable(GL_DEPTH_TEST);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Enable depth test");

	glDepthFunc(GL_LEQUAL);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Depth func");

	/* Test blend operations */
	glEnable(GL_BLEND);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Enable blend");

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Blend func");

	/* Test viewport - critical for rendering */
	glViewport(0, 0, 640, 480);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Set viewport");

	/* Test scissor - used extensively in UI rendering */
	glEnable(GL_SCISSOR_TEST);
	glScissor(0, 0, 640, 480);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Scissor test");

	/* Test texture operations */
	GLuint tex;
	glGenTextures(1, &tex);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Gen texture");
	TEST_ASSERT(tex > 0, "Texture ID valid");

	glBindTexture(GL_TEXTURE_2D, tex);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Bind texture");

	/* Create a small test texture (like the engine's default image) */
	unsigned char pixels[4 * 4 * 4]; /* 4x4 RGBA */
	memset(pixels, 0xFF, sizeof(pixels));
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Upload texture");

	glDeleteTextures(1, &tex);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Delete texture");

	return 1;
}

/*
 * Test: Framebuffer rendering
 * Actually renders to the framebuffer and reads back pixels
 */
static int test_framebuffer_render(void) {
	TEST_ASSERT(test_gl_context != NULL, "GL context exists");

	/* Clear to a known color */
	glClearColor(1.0f, 0.0f, 0.0f, 1.0f); /* Red */
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Read back a pixel to verify rendering worked */
	unsigned char pixel[4];
	glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	GLenum err = glGetError();
	TEST_ASSERT(err == GL_NO_ERROR, "ReadPixels no error");

	/* Verify the pixel is red (or close to it - software renderers may differ slightly) */
	TEST_ASSERT(pixel[0] > 200, "Red channel high");
	TEST_ASSERT(pixel[1] < 50, "Green channel low");
	TEST_ASSERT(pixel[2] < 50, "Blue channel low");

	printf("\n    Readback pixel: RGBA(%d,%d,%d,%d)\n    ", pixel[0], pixel[1], pixel[2], pixel[3]);

	/* Clear to green and verify */
	glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	TEST_ASSERT(pixel[0] < 50, "Green clear - red low");
	TEST_ASSERT(pixel[1] > 200, "Green clear - green high");

	/* Swap buffers (like GLimp_EndFrame does) */
	SDL_GL_SwapWindow(test_window);

	return 1;
}

/*
 * Test: Multiple window resolutions
 * Tests that different video modes work (exercises GLimp_SetMode paths)
 */
static int test_multiple_resolutions(void) {
	typedef struct { int w, h; } res_t;
	res_t resolutions[] = {
		{320, 240}, {640, 480}, {800, 600}, {1024, 768}
	};
	int num_res = sizeof(resolutions) / sizeof(resolutions[0]);
	int i;

	for (i = 0; i < num_res; i++) {
		SDL_SetWindowSize(test_window, resolutions[i].w, resolutions[i].h);

		int w, h;
		SDL_GetWindowSize(test_window, &w, &h);
		/* Note: under Xvfb, actual size might differ */

		glViewport(0, 0, resolutions[i].w, resolutions[i].h);
		GLenum err = glGetError();
		if (err != GL_NO_ERROR) {
			fprintf(stderr, "  GL error at %dx%d: 0x%x\n",
				resolutions[i].w, resolutions[i].h, err);
			return 0;
		}

		/* Render a frame at this resolution */
		glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		SDL_GL_SwapWindow(test_window);
	}

	return 1;
}

/*
 * Test: Renderer DLL Loading
 * Tests that the renderer shared library can be loaded (USE_RENDERER_DLOPEN path)
 */
static int test_renderer_dll_loading(void) {
	/* Try to load the renderer shared library */
	const char *renderer_path = "./renderer_sp_opengl1_x86_64.so";
	void *lib = dlopen(renderer_path, RTLD_NOW);

	if (!lib) {
		/* Try relative to build directory */
		renderer_path = "../build/release-linux-x86_64/renderer_sp_opengl1_x86_64.so";
		lib = dlopen(renderer_path, RTLD_NOW);
	}

	if (!lib) {
		printf("\n    Renderer DLL not found (build first): %s\n    ", dlerror());
		/* This is not a failure if the DLL hasn't been built */
		printf("SKIP (DLL not available)\n");
		tests_run--; /* Don't count skipped tests */
		return 1;
	}

	/* Check for GetRefAPI symbol - this is how the engine loads the renderer */
	void *sym = dlsym(lib, "GetRefAPI");
	TEST_ASSERT(sym != NULL, "GetRefAPI symbol found in renderer DLL");

	printf("\n    Renderer DLL loaded successfully from %s\n    ", renderer_path);

	dlclose(lib);
	return 1;
}

/*
 * Test: GL Extension Querying
 * Tests that we can query GL extensions (like GLimp_InitExtensions does)
 */
static int test_gl_extensions(void) {
	TEST_ASSERT(test_gl_context != NULL, "GL context exists");

	/* Query extensions string */
	/* Note: In core profile, GL_EXTENSIONS returns NULL and you must use
	   glGetStringi. The engine uses SDL_GL_ExtensionSupported instead. */

	/* Test SDL extension query (this is what the engine actually uses) */
	int has_texture_compression = SDL_GL_ExtensionSupported("GL_EXT_texture_compression_s3tc");
	printf("\n    S3TC texture compression: %s\n    ",
		has_texture_compression ? "yes" : "no");

	int has_multitexture = SDL_GL_ExtensionSupported("GL_ARB_multitexture");
	printf("ARB multitexture: %s\n    ",
		has_multitexture ? "yes" : "no");

	/* Get max texture size */
	GLint max_tex_size;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);
	TEST_ASSERT(glGetError() == GL_NO_ERROR, "Query max texture size");
	TEST_ASSERT(max_tex_size >= 256, "Max texture size >= 256");
	printf("Max texture size: %d\n    ", max_tex_size);

	return 1;
}

/* Cleanup */
static void cleanup(void) {
	if (test_gl_context) {
		SDL_GL_DeleteContext(test_gl_context);
		test_gl_context = NULL;
	}
	if (test_window) {
		SDL_DestroyWindow(test_window);
		test_window = NULL;
	}
	SDL_Quit();
}

int main(int argc, char *argv[]) {
	(void)argc;
	printf("========================================\n");
	printf("iortcw-ng Headless Renderer Tests\n");
	printf("========================================\n");
	printf("DISPLAY=%s\n\n", getenv("DISPLAY") ? getenv("DISPLAY") : "(not set)");

	/* Check if we have a display (Xvfb or real) */
	if (!getenv("DISPLAY")) {
		fprintf(stderr, "ERROR: No DISPLAY set. Run with Xvfb:\n");
		fprintf(stderr, "  Xvfb :99 -screen 0 1024x768x24 &\n");
		fprintf(stderr, "  DISPLAY=:99 %s\n", argv[0]);
		return 2;
	}

	/* Use software rendering for headless testing */
	setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
	setenv("MESA_GL_VERSION_OVERRIDE", "3.3", 0);

	printf("--- SDL/Video Initialization ---\n");
	run_test("sdl_video_init", test_sdl_video_init);
	run_test("sdl_window_creation", test_sdl_window_creation);
	run_test("gl_context_creation", test_gl_context_creation);

	printf("\n--- OpenGL Operations ---\n");
	run_test("gl_basic_operations", test_gl_basic_operations);
	run_test("gl_state_operations", test_gl_state_operations);
	run_test("gl_extensions", test_gl_extensions);

	printf("\n--- Framebuffer Rendering ---\n");
	run_test("framebuffer_render", test_framebuffer_render);
	run_test("multiple_resolutions", test_multiple_resolutions);

	printf("\n--- Renderer DLL ---\n");
	run_test("renderer_dll_loading", test_renderer_dll_loading);

	printf("\n========================================\n");
	printf("Results: %d/%d passed, %d failed\n",
		tests_passed, tests_run, tests_failed);
	printf("========================================\n");

	cleanup();

	return tests_failed > 0 ? 1 : 0;
}
