/*----------------------------------------------------------------------------*/
/* ext2fsP.h                                                                  */
/*                                                                            */
/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/* Copyright (c) 2026 B. C. Services                                          */
/*                                                                            */
/* ext4edit patch 8: a stand-in for e2fsprogs' lib/ext2fs/ext2fsP.h -- the    */
/* library's private header -- carrying ONLY the two structures nls_utf8.c   */
/* (mirrored verbatim from e2fsprogs 1.47.4) defines instances of: the table  */
/* that names a Unicode version, and the operations a table offers.  They    */
/* are copied member for member from e2fsprogs 1.47.4, because nls_utf8.c     */
/* fills them with designated initialisers (.casefold = ..., .version = ...)  */
/* and the names must match.  The rest of the real header is e2fsprogs'      */
/* internals and is not reproduced.                                          */
/*----------------------------------------------------------------------------*/

/* Included once, under a guard name of its own (not e2fsprogs' real one). */
#ifndef BFE_UNICODE_EXT2FSP_H_
#define BFE_UNICODE_EXT2FSP_H_

/* size_t, for the lengths in the operations. */
#include <stddef.h>

/*----------------------------------------------------------------------------*/
/* ext2fs_nls_table -- one encoding: which Unicode version folds names, and   */
/* the functions that do it.  nls_utf8.c holds the one instance (UTF-8 12.1) */
/* and ext2fs_load_nls_table hands it out by encoding number.                */
/*----------------------------------------------------------------------------*/
struct ext2fs_nls_table {
    int                          version;    // the Unicode version as UNICODE_AGE(major, minor, revision): (12 << 16) | (1 << 8) | 0 for 12.1.0
    const struct ext2fs_nls_ops *ops;        // the folding, validating and comparing functions for this version
};

/*----------------------------------------------------------------------------*/
/* ext2fs_nls_ops -- the operations.  Each takes the table so one body can   */
/* serve several versions.  casefold writes the NFD-normalised, case-folded   */
/* form of a name and returns its length (0 for a name of nothing but        */
/* ignorable code points), or -EINVAL / -ENAMETOOLONG -- which, as found in   */
/* nls_utf8.c, do not separate "not valid UTF-8" from "does not fit" (see     */
/* ext2fs_casefold in ext2fs.h); validate returns 0 when a name               */
/* is valid UTF-8, else 1 with *pos at the first bad byte; casefold_cmp       */
/* returns 0 when two names fold to the same bytes, a non-zero difference     */
/* otherwise, and -1 when either is not valid UTF-8 (a value a genuine       */
/* difference can also take, which is why the engine does not use it).      */
/*----------------------------------------------------------------------------*/
struct ext2fs_nls_ops {
    int (*casefold)(const struct ext2fs_nls_table *charset,
                    const unsigned char *str, size_t len,
                    unsigned char *dest, size_t dlen);                       // fold str (len bytes) into dest (dlen bytes); see above
    int (*validate)(const struct ext2fs_nls_table *table,
                    char *s, size_t len, char **pos);                        // is s (len bytes) valid UTF-8; *pos marks the first bad byte
    int (*casefold_cmp)(const struct ext2fs_nls_table *table,
                        const unsigned char *str1, size_t len1,
                        const unsigned char *str2, size_t len2);             // compare two names by their folded forms; see above
};

/* The end of the once-only guard. */
#endif /* BFE_UNICODE_EXT2FSP_H_ */
