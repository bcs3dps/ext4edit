/*
 * Copyright (c) 2013 Grzegorz Kostka (kostka.grzegorz@gmail.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup lwext4
 * @{
 */
/**
 * @file  ext4_debug.c
 * @brief Debug printf and assert macros.
 */

#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_errno.h"
#include "ext4_debug.h"

#include <stdarg.h>

static uint32_t debug_mask;

void ext4_dmask_set(uint32_t m)
{
	debug_mask |= m;
}

void ext4_dmask_clr(uint32_t m)
{
	debug_mask &= ~m;
}

uint32_t ext4_dmask_get(void)
{
	return debug_mask;
}

/* ext4edit patch 36: the first failed ext4_assert since the last clear --    */
/* documentation table.  One engine call runs at a time (the engine's own    */
/* rule), so these need no lock.                                              */
static const char *assert_file;	// __FILE__ of the site, a string literal (never freed); NULL: none
static int assert_line;		// its __LINE__; meaningful only while assert_file is set

/*----------------------------------------------------------------------------*/
/* ext4_assert_record:                                                        */
/*                                                                            */
/* ext4edit patch 36.  Note a failed ext4_assert -- the only thing a failed   */
/* assertion does.  The FIRST site since the last clear is kept: a later one  */
/* is most likely a consequence of it, and the first is the one worth         */
/* reporting.  The engine reads it when it records a failure (BFX_Fail in     */
/* bifrost_engine.c) and clears it as each call begins (BFX_Enter).          */
/*                                                                            */
/* Arguments:                                                                 */
/*     file : the site's __FILE__ (a string literal; only the pointer is      */
/*            kept).                                                          */
/*     line : the site's __LINE__.                                            */
/*                                                                            */
/* Returns:                                                                   */
/*     void : the site is recorded unless one already is.                     */
/*----------------------------------------------------------------------------*/
void ext4_assert_record(const char *file, int line)
{
	if (!assert_file) {
		/* Nothing recorded yet: this is the first, so it is kept; a */
		/* site already held is left as it is.                       */
		assert_file = file;
		assert_line = line;
	}
}

/*----------------------------------------------------------------------------*/
/* ext4_assert_last:                                                          */
/*                                                                            */
/* ext4edit patch 36.  The site ext4_assert_record kept, for the engine's     */
/* detail line.  Reading it does not clear it.                                */
/*                                                                            */
/* Arguments:                                                                 */
/*     line : receives the site's line (left alone when none); may be NULL.   */
/*                                                                            */
/* Returns:                                                                   */
/*     const char * : the site's file, or NULL when no assertion failed      */
/*                    since the last clear.                                   */
/*----------------------------------------------------------------------------*/
const char *ext4_assert_last(int *line)
{
	if (assert_file && line) {
		/* A site is held and the caller wants its line: give it. */
		*line = assert_line;
	}

	/* The file, or NULL for none -- which tells the caller whether the */
	/* line was given.                                                  */
	return assert_file;
}

/*----------------------------------------------------------------------------*/
/* ext4_assert_clear:                                                         */
/*                                                                            */
/* ext4edit patch 36.  Forget the recorded site, once it has been reported    */
/* or as an engine call begins, so a later failure never carries a site that  */
/* belongs to an earlier one.                                                 */
/*                                                                            */
/* Arguments:                                                                 */
/*     None.                                                                  */
/*                                                                            */
/* Returns:                                                                   */
/*     void : nothing is recorded afterwards.                                 */
/*----------------------------------------------------------------------------*/
void ext4_assert_clear(void)
{
	/* Nothing held: the next failed assertion is the first again. */
	assert_file = NULL;
	assert_line = 0;
}



/**
 * @}
 */
