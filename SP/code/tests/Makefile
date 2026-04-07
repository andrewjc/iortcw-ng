#
# Makefile for iortcw-ng test suite
#
# Usage:
#   make                    - Build all tests
#   make test-core          - Build and run core subsystem tests
#   make test-renderer      - Build and run headless renderer tests
#   make test-integration   - Build and run engine integration tests
#   make test               - Build and run all tests
#   make clean              - Clean test binaries
#

CC ?= gcc

# Detect architecture
COMPILE_ARCH=$(shell uname -m | sed -e 's/i.86/x86/' -e 's/amd64/x86_64/')
ifeq ($(COMPILE_ARCH),x86_64)
  ARCH_STRING = x86_64
else ifeq ($(COMPILE_ARCH),x86)
  ARCH_STRING = i386
else ifeq ($(COMPILE_ARCH),aarch64)
  ARCH_STRING = arm64
else
  ARCH_STRING = $(COMPILE_ARCH)
endif

CFLAGS = -Wall -Wextra -g -O0 -DARCH_STRING=\"$(ARCH_STRING)\"
LDFLAGS =

# Paths
SRCDIR = ..
TESTDIR = .
BUILDDIR = ../../build/tests

# SDL2 flags
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2 -D_REENTRANT")
SDL_LDFLAGS := $(shell pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2")

# OpenGL flags  
GL_LDFLAGS = -lGL -ldl

# Engine source files needed for core tests
# We compile q_shared.c and q_math.c directly for the unit tests
ENGINE_SRCS = \
	$(SRCDIR)/qcommon/q_shared.c \
	$(SRCDIR)/qcommon/q_math.c

.PHONY: all test test-core test-renderer test-integration clean

all: $(BUILDDIR)/test_core $(BUILDDIR)/test_renderer_headless $(BUILDDIR)/test_engine_integration

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Core subsystem tests - tests pure functions from the engine
$(BUILDDIR)/test_core: $(TESTDIR)/test_main.c $(TESTDIR)/test_stubs.c $(ENGINE_SRCS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) \
		-DSTANDALONE -DNDEBUG \
		-o $@ $< $(TESTDIR)/test_stubs.c $(ENGINE_SRCS) \
		-lm

# Headless renderer tests - tests SDL/GL initialization with Xvfb
$(BUILDDIR)/test_renderer_headless: $(TESTDIR)/test_renderer_headless.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) \
		-o $@ $< \
		$(SDL_LDFLAGS) $(GL_LDFLAGS) -lm

# Engine integration tests - launches actual engine binaries
$(BUILDDIR)/test_engine_integration: $(TESTDIR)/test_engine_integration.c | $(BUILDDIR)
	$(CC) $(CFLAGS) \
		-o $@ $< \
		-lm

# Run targets
test-core: $(BUILDDIR)/test_core
	@echo ""
	@echo "=== Running Core Subsystem Tests ==="
	@echo ""
	$(BUILDDIR)/test_core

test-renderer: $(BUILDDIR)/test_renderer_headless
	@echo ""
	@echo "=== Running Headless Renderer Tests ==="
	@echo ""
	@if [ -z "$$DISPLAY" ]; then \
		echo "Starting Xvfb..."; \
		Xvfb :99 -screen 0 1024x768x24 -ac +extension GLX +render -noreset & \
		XVFB_PID=$$!; \
		export DISPLAY=:99; \
		sleep 1; \
		LIBGL_ALWAYS_SOFTWARE=1 $(BUILDDIR)/test_renderer_headless; \
		TEST_EXIT=$$?; \
		kill $$XVFB_PID 2>/dev/null || true; \
		wait $$XVFB_PID 2>/dev/null || true; \
		exit $$TEST_EXIT; \
	else \
		LIBGL_ALWAYS_SOFTWARE=1 $(BUILDDIR)/test_renderer_headless; \
	fi

test-integration: $(BUILDDIR)/test_engine_integration
	@echo ""
	@echo "=== Running Engine Integration Tests ==="
	@echo ""
	@cd ../../build/release-linux-x86_64 && \
	if [ -z "$$DISPLAY" ]; then \
		echo "Starting Xvfb..."; \
		Xvfb :99 -screen 0 1024x768x24 -ac +extension GLX +render -noreset & \
		XVFB_PID=$$!; \
		export DISPLAY=:99; \
		sleep 1; \
		LIBGL_ALWAYS_SOFTWARE=1 ../../code/tests/../../build/tests/test_engine_integration; \
		TEST_EXIT=$$?; \
		kill $$XVFB_PID 2>/dev/null || true; \
		wait $$XVFB_PID 2>/dev/null || true; \
		exit $$TEST_EXIT; \
	else \
		LIBGL_ALWAYS_SOFTWARE=1 ../../code/tests/../../build/tests/test_engine_integration; \
	fi

test: test-core test-renderer test-integration
	@echo ""
	@echo "=== All test suites completed ==="

clean:
	rm -rf $(BUILDDIR)
