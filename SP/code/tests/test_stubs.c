/*
===========================================================================
Test stubs for iortcw-ng engine functions

Provides minimal implementations of engine functions that are referenced
by q_shared.c and q_math.c, allowing the test suite to link without
the full engine.
===========================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "../qcommon/q_shared.h"

/*
 * Com_Printf - Print to console (test version prints to stdout)
 */
void QDECL Com_Printf(const char *fmt, ...) {
	va_list argptr;
	va_start(argptr, fmt);
	vprintf(fmt, argptr);
	va_end(argptr);
}

/*
 * Com_Error - Error handling (test version prints and optionally aborts)
 */
void QDECL Com_Error(int level, const char *fmt, ...) {
	va_list argptr;
	char msg[4096];

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	fprintf(stderr, "Com_Error(%d): %s\n", level, msg);

	/* Engine marks this noreturn - always exit in test context. */
	exit(1);
}
