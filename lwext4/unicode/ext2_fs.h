/*----------------------------------------------------------------------------*/
/* ext2_fs.h                                                                  */
/*                                                                            */
/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/* Copyright (c) 2026 B. C. Services                                          */
/*                                                                            */
/* ext4edit patch 8: a stand-in for e2fsprogs' lib/ext2fs/ext2_fs.h -- the    */
/* on-disk format header -- carrying what nls_utf8.c (mirrored verbatim from  */
/* e2fsprogs 1.47.4) takes from it, the encoding number a superblock's        */
/* s_encoding field may hold (EXT4_ENC_UTF8_12_1, its one use), and beside it */
/* the flag its s_encoding_flags may carry, which nothing reads.  The values  */
/* are the kernel's (fs/ext4/ext4.h) and e2fsprogs' alike; the rest of the    */
/* real header describes structures lwext4 has its own copies of in           */
/* ext4_types.h, and is not reproduced.                                       */
/*----------------------------------------------------------------------------*/

/* Included once, under a guard name of its own (not e2fsprogs' real one). */
#ifndef BFE_UNICODE_EXT2_FS_H_
#define BFE_UNICODE_EXT2_FS_H_

/*----------------------------------------------------------------------------*/
/* Filename encodings -- documentation table.  s_encoding of the superblock.  */
/* Only one has ever been defined (as found in e2fsprogs 1.47.4); a casefold  */
/* filesystem made by mke2fs carries it.                                      */
/*----------------------------------------------------------------------------*/
#define EXT4_ENC_UTF8_12_1          1        // UTF-8, normalised and case-folded by the Unicode 12.1 tables (utf8data.h); the one number ext2fs_load_nls_table knows

/*----------------------------------------------------------------------------*/
/* Encoding flags -- documentation table.  s_encoding_flags of the superblock.*/
/*----------------------------------------------------------------------------*/
#define EXT4_ENC_STRICT_MODE_FL     (1 << 0) // the kernel refuses a name that is not valid UTF-8; defined for reference only -- nothing in the engine reads it, as lwext4 never refuses (it falls back to a byte compare)

/* The end of the once-only guard. */
#endif /* BFE_UNICODE_EXT2_FS_H_ */
