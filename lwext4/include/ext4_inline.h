/*----------------------------------------------------------------------------*/
/* ext4_inline.h                                                              */
/*                                                                            */
/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/* Copyright (c) 2026 B. C. Services                                          */
/*                                                                            */
/* ext4edit patch 7: the ext4 inline_data feature (incompat 0x8000) for       */
/* lwext4, which had none of it.  A file or directory with the inode flag     */
/* EXT4_INODE_FLAG_INLINE_DATA keeps its content INSIDE the inode: the first  */
/* 60 bytes in the i_block array that would otherwise hold block pointers,    */
/* and the rest in the value of the "system.data" extended attribute in the   */
/* inode's own body -- the only place the kernel puts it, and the only place  */
/* this module accepts it (lwext4's xattr layer would also take it from the   */
/* inode's xattr block).  This module reads that content, presents an inline  */
/* directory as an ordinary directory block so the library's lookup and       */
/* listing code can run over it unchanged, and CONVERTS an inline inode to a  */
/* block-backed one the moment anything would write to it -- the shape the    */
/* Linux driver takes when inline content outgrows the inode, and the one     */
/* that lets every existing write path stay as it is.  Nothing here ever      */
/* makes an inode inline: new files and directories are block-backed.         */
/*                                                                            */
/* The on-disk forms, from the driver's fs/ext4/inline.c:                     */
/*   - a FILE: i_size is the content's length; bytes 0 to 59 sit in i_block,  */
/*     bytes 60 onward in the xattr value (which exists, possibly empty,      */
/*     whenever the flag is set);                                             */
/*   - a DIRECTORY: i_size is 60 plus the xattr value's length; i_block       */
/*     starts with the PARENT's inode number (4 bytes, taking the place of    */
/*     the ".." entry) and holds entries from byte 4 to 60, the xattr value   */
/*     holds more; each of the two regions is laid out like a directory       */
/*     block of its own size (the last entry's length runs to the region's    */
/*     end) and neither has a "." entry nor a checksum tail.                  */
/*----------------------------------------------------------------------------*/

/* Included once however many lwext4 files include it. */
#ifndef EXT4_INLINE_H_
#define EXT4_INLINE_H_

/* lwext4's configuration and on-disk types, and ext4_fs.h for the inode   */
/* reference every function takes.                                         */
#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_fs.h"

/* The fixed-width integers, bool and size_t the prototypes use. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*----------------------------------------------------------------------------*/
/* Layout constants -- documentation table.                                   */
/*----------------------------------------------------------------------------*/
#define EXT4_INLINE_DATA_MIN    60       // bytes of content held in i_block (its whole size); the driver's EXT4_MIN_INLINE_DATA_SIZE
#define EXT4_INLINE_DIR_PARENT  4        // bytes at the start of an inline DIRECTORY's i_block holding the parent inode number
#define EXT4_INLINE_XATTR_NAME  "data"   // the name of the "system.data" attribute, under EXT4_XATTR_INDEX_SYSTEM
#define EXT4_INLINE_XATTR_LEN   4        // its length

/*----------------------------------------------------------------------------*/
/* ext4_inline_has:                                                           */
/*                                                                            */
/* Whether an inode keeps its content inline: the filesystem must carry the   */
/* feature AND the inode the flag.  Every other function here is a no-op or   */
/* an error on an inode for which this is false, so a caller may test once.   */
/*                                                                            */
/* Arguments:                                                                 */
/*     sb    : the superblock.                                                */
/*     inode : the inode.                                                     */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true when the content is inline.                                */
/*----------------------------------------------------------------------------*/
bool ext4_inline_has(struct ext4_sblock *sb, struct ext4_inode *inode);

/*----------------------------------------------------------------------------*/
/* ext4_inline_read:                                                          */
/*                                                                            */
/* Copy an inline inode's whole content into a malloc'd buffer: i_size bytes  */
/* for a file; for a directory the raw inline area (parent number, then       */
/* entries).  Works on a read-only session: nothing is marked dirty.          */
/*                                                                            */
/* Arguments:                                                                 */
/*     ref     : the inode, held by the caller.                               */
/*     ppbData : receives the buffer, the caller's to free (never NULL on     */
/*               success, even for an empty file).                            */
/*     pcbData : receives its length, i_size.                                 */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK; EINVAL when the inode is not inline; ENOMEM; EIO when       */
/*           i_size is more than the inode can hold inline -- 60 bytes plus a */
/*           "system.data" value in the inode's body, checked before any      */
/*           allocation -- or the xattr is missing or shorter than i_size     */
/*           says; or the xattr layer's error when the inode's attribute set  */
/*           cannot be read.                                                  */
/*----------------------------------------------------------------------------*/
int ext4_inline_read(struct ext4_inode_ref *ref, uint8_t **ppbData, size_t *pcbData);

/*----------------------------------------------------------------------------*/
/* ext4_inline_dir_block:                                                     */
/*                                                                            */
/* Lay an inline DIRECTORY out as one ordinary directory block of the         */
/* filesystem's block size, malloc'd: "." and ".." first (the parent taken    */
/* from the inline area's first word), then every live entry, the last one's  */
/* length running to the end, and the checksum tail when metadata_csum is     */
/* on.  It is both what the lookup and listing code reads in place of a       */
/* block on a read-only session, and exactly the block ext4_inline_convert    */
/* writes to disk.                                                            */
/*                                                                            */
/* Arguments:                                                                 */
/*     ref      : the directory inode, held by the caller.                    */
/*     ppbBlock : receives the block, the caller's to free.                   */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK; EINVAL when not an inline directory; ENOMEM; EIO when the   */
/*           inline entries are malformed or more than one block holds; or    */
/*           ext4_inline_read's errors.                                       */
/*----------------------------------------------------------------------------*/
int ext4_inline_dir_block(struct ext4_inode_ref *ref, uint8_t **ppbBlock);

/*----------------------------------------------------------------------------*/
/* ext4_inline_convert:                                                       */
/*                                                                            */
/* Turn an inline inode into a block-backed one holding the same content:     */
/* the content is read, one data block allocated and written -- a directory   */
/* as ext4_inline_dir_block lays it out, a file as its bytes followed by      */
/* zeros -- and only then the inline storage dropped (ext4_inline_drop), the  */
/* block attached and the size set.  Every step that can fail comes before    */
/* the drop, so a failure leaves the inode inline and whole.                  */
/* Writable session only; the inode is left dirty for the caller's put.  A    */
/* non-inline inode is left alone with EOK, so every write path may call      */
/* this unconditionally before it touches blocks.                             */
/*                                                                            */
/* Arguments:                                                                 */
/*     ref : the inode, held by the caller.                                   */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK; ENOMEM; ENOSPC when no block is free; EIO as above (a file  */
/*           whose content runs past i_block with its "system.data" missing   */
/*           or short cannot be read, so it is refused here, never taken as   */
/*           shorter); or the error of the step that failed.  On any failure  */
/*           the inode is left as it was, inline, its content whole.          */
/*----------------------------------------------------------------------------*/
int ext4_inline_convert(struct ext4_inode_ref *ref);

/*----------------------------------------------------------------------------*/
/* ext4_inline_drop:                                                          */
/*                                                                            */
/* Discard an inline inode's content without keeping it: remove the           */
/* "system.data" attribute, clear the flag, zero i_block and give it the      */
/* empty block map a new inode gets (an extent header on an extents           */
/* filesystem), and set the size to 0.  This is "truncate to nothing" for an  */
/* inline inode, used before an inode is freed and by a truncate to 0, and    */
/* it is the first half of ext4_inline_convert.  Writable session only; the   */
/* inode is left dirty for the caller's put.                                  */
/*                                                                            */
/* Arguments:                                                                 */
/*     ref : the inode, held by the caller.                                   */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK (also for a non-inline inode, untouched, and for one whose   */
/*           "system.data" is missing: the content is discarded anyway);      */
/*           otherwise the error of reading the attribute set.                */
/*----------------------------------------------------------------------------*/
int ext4_inline_drop(struct ext4_inode_ref *ref);

/* The end of the once-only guard. */
#endif // EXT4_INLINE_H_
