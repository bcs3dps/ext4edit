/*----------------------------------------------------------------------------*/
/* config.h                                                                   */
/*                                                                            */
/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/* Copyright (c) 2026 B. C. Services                                          */
/*                                                                            */
/* ext4edit patch 8: a stand-in for e2fsprogs' generated config.h, so that    */
/* nls_utf8.c -- mirrored verbatim from e2fsprogs 1.47.4 (lib/ext2fs) -- can  */
/* be compiled here without e2fsprogs' build.  That file includes "config.h"  */
/* first and uses nothing from it (it tests no macro config.h would define:   */
/* its only conditionals are #if 0 blocks -- checked); the quoted include     */
/* finds this file before any system one because it sits beside the          */
/* including source.  So it is deliberately empty.                            */
/*----------------------------------------------------------------------------*/

/* Included once, under a guard name of its own (not e2fsprogs' real one). */
#ifndef BFE_UNICODE_CONFIG_H_
#define BFE_UNICODE_CONFIG_H_

/* Nothing: see the header above. */

/* The end of the once-only guard. */
#endif /* BFE_UNICODE_CONFIG_H_ */
