/*----------------------------------------------------------------------------*/
/* ext2fs.h                                                                   */
/*                                                                            */
/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/* Copyright (c) 2026 B. C. Services                                          */
/*                                                                            */
/* ext4edit patch 8: a stand-in for e2fsprogs' lib/ext2fs/ext2fs.h -- the     */
/* library's public header -- carrying ONLY the prototypes of what           */
/* nls_utf8.c (mirrored verbatim from e2fsprogs 1.47.4) exports, plus the one */
/* function this copy adds to that file (ext2fs_casefold, the exported       */
/* wrapper around its static utf8_casefold).  This is the header the rest of  */
/* the library includes to reach the folding; it declares the table type     */
/* opaquely, and ext2fsP.h beside it gives the definition to whoever needs   */
/* the members.  The real header is e2fsprogs' whole API and is not          */
/* reproduced.                                                               */
/*----------------------------------------------------------------------------*/

/* Included once, under a guard name of its own (not e2fsprogs' real one). */
#ifndef BFE_UNICODE_EXT2FS_H_
#define BFE_UNICODE_EXT2FS_H_

/* size_t, for the lengths below. */
#include <stddef.h>

/* The table is defined in ext2fsP.h; callers of the functions below need    */
/* only the pointer.                                                          */
struct ext2fs_nls_table;

/*----------------------------------------------------------------------------*/
/* ext2fs_load_nls_table:                                                     */
/*                                                                            */
/* The folding table for a superblock's s_encoding value: the UTF-8 12.1     */
/* table for EXT4_ENC_UTF8_12_1, NULL for any other number -- which is how a  */
/* mount learns the filesystem's names cannot be folded here, and refuses.   */
/*                                                                            */
/* Arguments:                                                                 */
/*     encoding : the superblock's s_encoding.                                */
/*                                                                            */
/* Returns:                                                                   */
/*     const struct ext2fs_nls_table * : the table, or NULL when unknown.     */
/*----------------------------------------------------------------------------*/
const struct ext2fs_nls_table *ext2fs_load_nls_table(int encoding);

/*----------------------------------------------------------------------------*/
/* ext2fs_check_encoded_name:                                                 */
/*                                                                            */
/* Whether a name is valid UTF-8 under the table.  Not used by lwext4, which  */
/* treats an invalid name as an opaque byte string the way a non-strict      */
/* kernel does; declared because the mirrored file defines it.               */
/*                                                                            */
/* Arguments:                                                                 */
/*     table : the folding table.                                             */
/*     name  : the name.                                                      */
/*     len   : its length in bytes.                                           */
/*     pos   : receives a pointer to the first invalid byte, when there is    */
/*             one.                                                           */
/*                                                                            */
/* Returns:                                                                   */
/*     int : 0 when valid, 1 when not (with *pos set).                        */
/*----------------------------------------------------------------------------*/
int ext2fs_check_encoded_name(const struct ext2fs_nls_table *table,
                              char *name, size_t len, char **pos);

/*----------------------------------------------------------------------------*/
/* ext2fs_casefold_cmp:                                                       */
/*                                                                            */
/* Compare two names by their folded forms.  Not used by lwext4 (its -1 for   */
/* "not valid UTF-8" cannot be told from a genuine difference of -1, so the   */
/* engine folds both sides itself and compares the bytes); declared because  */
/* the mirrored file defines it.                                              */
/*                                                                            */
/* Arguments:                                                                 */
/*     table : the folding table.                                             */
/*     str1  : the first name.                                                */
/*     len1  : its length in bytes.                                           */
/*     str2  : the second name.                                               */
/*     len2  : its length in bytes.                                           */
/*                                                                            */
/* Returns:                                                                   */
/*     int : 0 when the folded forms are equal, non-zero otherwise, -1 when   */
/*           either name is not valid UTF-8.                                  */
/*----------------------------------------------------------------------------*/
int ext2fs_casefold_cmp(const struct ext2fs_nls_table *table,
                        const unsigned char *str1, size_t len1,
                        const unsigned char *str2, size_t len2);

/*----------------------------------------------------------------------------*/
/* ext2fs_casefold:                                                           */
/*                                                                            */
/* The exported fold (ext4edit patch 8; e2fsprogs keeps utf8_casefold         */
/* static and reaches it only through ext2fs_dirhash2, which lwext4 does not  */
/* have).  Writes the NFD-normalised, fully case-folded form of a name --     */
/* the bytes the Linux driver hashes and compares in a casefolded directory   */
/* -- into dest.                                                              */
/*                                                                            */
/* Arguments:                                                                 */
/*     table : the folding table.                                             */
/*     str   : the name, NOT NUL-terminated.                                  */
/*     len   : its length in bytes.                                           */
/*     dest  : receives the folded form, NUL-terminated when it fits.         */
/*     dlen  : the size of dest in bytes.                                     */
/*                                                                            */
/* Returns:                                                                   */
/*     int : the folded form's length without the NUL (0 for a name of        */
/*           nothing but ignorable code points); otherwise -EINVAL or         */
/*           -ENAMETOOLONG.  As found in nls_utf8.c, -EINVAL comes only for   */
/*           a name that STARTS with a UTF-8 continuation byte; an invalid    */
/*           sequence met later gives -ENAMETOOLONG, as does a form that      */
/*           does not fit in dest -- so the two codes do not tell "invalid"   */
/*           from "too long", and lwext4 treats them alike.                   */
/*----------------------------------------------------------------------------*/
int ext2fs_casefold(const struct ext2fs_nls_table *table,
                    const unsigned char *str, size_t len,
                    unsigned char *dest, size_t dlen);

/* The end of the once-only guard. */
#endif /* BFE_UNICODE_EXT2FS_H_ */
