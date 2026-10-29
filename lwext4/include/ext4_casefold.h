/*----------------------------------------------------------------------------*/
/* ext4_casefold.h                                                            */
/*                                                                            */
/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/* Copyright (c) 2026 B. C. Services                                          */
/*                                                                            */
/* ext4edit patch 8: the ext4 casefold feature (incompat 0x20000) for         */
/* lwext4, which had none of it.  On such a filesystem a directory carrying   */
/* the inode flag EXT4_INODE_FLAG_CASEFOLD is CASE-INSENSITIVE: the Linux     */
/* driver hashes each name for the directory's htree, and compares a looked-  */
/* up name against the stored entries, by the name's FOLDED form -- the       */
/* bytes left after Unicode NFD normalisation and full case folding under     */
/* the version the superblock's s_encoding names (12.1, the only one defined) */
/* -- while the entries themselves keep the bytes they were created with.     */
/* So "Readme.TXT" finds "readme.txt", "STRASSE" finds "stra\xc3\x9fe", and   */
/* creating a second entry whose name folds to an existing one's is refused   */
/* as EXISTS.  A name that is not valid UTF-8 cannot be folded; the driver    */
/* then hashes and compares its raw bytes (unless the superblock asks for     */
/* strict mode, which refuses such a name outright -- lwext4 does not         */
/* refuse, it treats the name as opaque bytes like a non-strict kernel).      */
/*                                                                            */
/* The folding itself is e2fsprogs' nls_utf8.c with its generated tables,     */
/* mirrored verbatim into lwext4\unicode\ and compiled through the stand-in   */
/* headers beside it; this module is the thin layer that decides WHEN a name  */
/* is folded and does the compare.  The library asks it in four places: the   */
/* index hash (ext4_dir_idx.c -- ext4_dir_hinfo_init takes the directory's    */
/* table, and the hash folds each name it places, re-hashing every entry      */
/* when a leaf is split); the search (ext4_dir.c -- ext4_dir_find_entry takes */
/* the table, ext4_dir_find_in_block folds the looked-up name once and asks   */
/* ext4_casefold_match of each entry); creation (ext4.c ext4_generic_open2 -- */
/* a directory made inside a casefolded one inherits the flag, as the         */
/* driver's EXT4_FL_INHERITED makes it); and the mount (ext4_fs.c             */
/* ext4_fs_check_features refuses an encoding with no table).                 */
/*                                                                            */
/* What is NOT here: a directory never GAINS or LOSES the flag through this   */
/* library except by inheritance at creation; the dirent hash the driver      */
/* stores for a directory that is casefolded AND encrypted (encrypt is not    */
/* supported, so no such directory can be mounted); and the SipHash the       */
/* driver uses for those same directories.                                    */
/*----------------------------------------------------------------------------*/

/* Included once however many lwext4 files include it. */
#ifndef EXT4_CASEFOLD_H_
#define EXT4_CASEFOLD_H_

/* lwext4's configuration and on-disk types, and ext4_fs.h for the   */
/* superblock and inode reference the functions take.                */
#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_fs.h"

/* The fixed-width integers, bool and size_t the prototypes use. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* The table type is e2fsprogs' (lwext4\unicode\ext2fsP.h); callers of this   */
/* module hold only the pointer, which is why it is declared opaquely here.   */
struct ext2fs_nls_table;

/*----------------------------------------------------------------------------*/
/* Limits -- documentation table.                                             */
/*----------------------------------------------------------------------------*/
#define EXT4_CASEFOLD_FOLD_MAX  4096     // the fold buffer's size: NFD and full folding can lengthen a name several times over, and the driver folds into a PATH_MAX buffer, so a form that does not fit (4096 bytes or more -- the fold also writes a terminating NUL) is hashed and compared raw, as the driver does; ext4_dir.c sizes its own fold buffer by this too

/*----------------------------------------------------------------------------*/
/* ext4_casefold_table:                                                       */
/*                                                                            */
/* The folding table of a filesystem, or NULL when its names are never        */
/* folded: the casefold feature is off, or its s_encoding names a version     */
/* the mirrored tables do not know.  The second case is also how a mount      */
/* refuses such a filesystem (ext4_fs_check_features), since a lookup that    */
/* folded by the wrong tables would miss names the driver finds.              */
/*                                                                            */
/* Arguments:                                                                 */
/*     sb : the superblock.                                                   */
/*                                                                            */
/* Returns:                                                                   */
/*     const struct ext2fs_nls_table * : the table, or NULL.                  */
/*----------------------------------------------------------------------------*/
const struct ext2fs_nls_table *ext4_casefold_table(struct ext4_sblock *sb);

/*----------------------------------------------------------------------------*/
/* ext4_casefold_dir:                                                         */
/*                                                                            */
/* The folding table to use for the names in ONE directory: the filesystem's  */
/* table when the inode is a directory carrying the casefold flag, else NULL  */
/* -- which every caller reads as "compare and hash the bytes as they are".   */
/* The test is per directory, not per filesystem: on a casefold filesystem    */
/* most directories are ordinary, and only those marked (by chattr +F, or by  */
/* inheritance) fold their names.                                             */
/*                                                                            */
/* Arguments:                                                                 */
/*     dir : the directory's inode reference.                                 */
/*                                                                            */
/* Returns:                                                                   */
/*     const struct ext2fs_nls_table * : the table, or NULL.                  */
/*----------------------------------------------------------------------------*/
const struct ext2fs_nls_table *ext4_casefold_dir(struct ext4_inode_ref *dir);

/*----------------------------------------------------------------------------*/
/* ext4_casefold_fold:                                                        */
/*                                                                            */
/* Fold a name: write its NFD-normalised, fully case-folded form into the     */
/* caller's buffer.  This is the form the htree hash is taken over and the    */
/* form two names are compared by.  A name that is not valid UTF-8, or whose  */
/* form does not fit, cannot be folded: the caller then uses the raw bytes,   */
/* which is what the driver does in both cases.                               */
/*                                                                            */
/* Arguments:                                                                 */
/*     table   : the folding table (never NULL here).                         */
/*     name    : the name, NOT NUL-terminated.                                */
/*     len     : its length in bytes.                                         */
/*     buf     : receives the folded form.                                    */
/*     bufsize : the size of buf in bytes (EXT4_CASEFOLD_FOLD_MAX is the      */
/*               figure the driver's behaviour is matched at).                */
/*                                                                            */
/* Returns:                                                                   */
/*     int : the folded form's length in bytes (0 is possible: a name of      */
/*           nothing but ignorable code points folds to nothing), or -1 when  */
/*           the name cannot be folded.                                       */
/*----------------------------------------------------------------------------*/
int ext4_casefold_fold(const struct ext2fs_nls_table *table, const char *name,
                       size_t len, char *buf, size_t bufsize);

/*----------------------------------------------------------------------------*/
/* ext4_casefold_match:                                                       */
/*                                                                            */
/* Whether a stored entry's name matches the name being looked up, under a    */
/* casefolded directory's rules.  The looked-up name is folded ONCE by the    */
/* caller (ext4_dir_find_in_block, for the whole block it scans) and passed   */
/* here in both forms; each entry is folded here and the two folded forms     */
/* compared byte for byte.  When either side cannot be folded the raw bytes   */
/* are compared instead, exactly as the driver's ext4_ci_compare falls back   */
/* on a non-strict filesystem.                                                */
/*                                                                            */
/* Arguments:                                                                 */
/*     table      : the folding table (never NULL here).                      */
/*     name       : the name being looked up, raw.                            */
/*     name_len   : its length in bytes.                                      */
/*     folded     : its folded form, or NULL when it could not be folded.     */
/*     folded_len : the folded form's length (ignored when folded is NULL).   */
/*     de_name    : the stored entry's name, raw, NOT NUL-terminated.         */
/*     de_len     : its length in bytes.                                      */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true when the entry is the one looked up.                       */
/*----------------------------------------------------------------------------*/
bool ext4_casefold_match(const struct ext2fs_nls_table *table,
                         const char *name, size_t name_len,
                         const char *folded, size_t folded_len,
                         const char *de_name, size_t de_len);

/* The end of the once-only guard. */
#endif // EXT4_CASEFOLD_H_
