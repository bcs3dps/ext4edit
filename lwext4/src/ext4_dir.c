/*
 * Copyright (c) 2013 Grzegorz Kostka (kostka.grzegorz@gmail.com)
 *
 *
 * HelenOS:
 * Copyright (c) 2012 Martin Sucha
 * Copyright (c) 2012 Frantisek Princ
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
 * @file  ext4_dir.h
 * @brief Directory handle procedures.
 */

#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_errno.h"
#include "ext4_debug.h"

#include "ext4_trans.h"
#include "ext4_dir.h"
#include "ext4_inline.h"
#include "ext4_casefold.h"	/* ext4edit patch 8 */
#include "ext4_dir_idx.h"
#include "ext4_crc32.h"
#include "ext4_inode.h"
#include "ext4_fs.h"

#include <string.h>
#include <stdlib.h>	/* ext4edit patch 7: free() of a laid-out inline directory */

/****************************************************************************/

/* Walk through a dirent block to find a checksum "dirent" at the tail */
static struct ext4_dir_entry_tail *
ext4_dir_get_tail(struct ext4_inode_ref *inode_ref,
		struct ext4_dir_en *de)
{
	struct ext4_dir_entry_tail *t;
	struct ext4_sblock *sb = &inode_ref->fs->sb;

	t = EXT4_DIRENT_TAIL(de, ext4_sb_get_block_size(sb));

	if (t->reserved_zero1 || t->reserved_zero2)
		return NULL;
	if (to_le16(t->rec_len) != sizeof(struct ext4_dir_entry_tail))
		return NULL;
	if (t->reserved_ft != EXT4_DIRENTRY_DIR_CSUM)
		return NULL;

	return t;
}

#if CONFIG_META_CSUM_ENABLE
static uint32_t ext4_dir_csum(struct ext4_inode_ref *inode_ref,
			      struct ext4_dir_en *dirent, int size)
{
	uint32_t csum;
	struct ext4_sblock *sb = &inode_ref->fs->sb;
	uint32_t ino_index = to_le32(inode_ref->index);
	uint32_t ino_gen = to_le32(ext4_inode_get_generation(inode_ref->inode));

	/* First calculate crc32 checksum against fs uuid */
	csum = ext4_sb_csum_seed(sb); /* ext4edit patch 3 */
	/* Then calculate crc32 checksum against inode number
	 * and inode generation */
	csum = ext4_crc32c(csum, &ino_index, sizeof(ino_index));
	csum = ext4_crc32c(csum, &ino_gen, sizeof(ino_gen));
	/* Finally calculate crc32 checksum against directory entries */
	csum = ext4_crc32c(csum, dirent, size);
	return csum;
}
#else
#define ext4_dir_csum(...) 0
#endif

bool ext4_dir_csum_verify(struct ext4_inode_ref *inode_ref,
			      struct ext4_dir_en *dirent)
{
#ifdef CONFIG_META_CSUM_ENABLE
	struct ext4_dir_entry_tail *t;
	struct ext4_sblock *sb = &inode_ref->fs->sb;

	/* Compute the checksum only if the filesystem supports it */
	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
		t = ext4_dir_get_tail(inode_ref, dirent);
		if (!t) {
			/* There is no space to hold the checksum */
			return false;
		}

		ptrdiff_t __unused diff = (char *)t - (char *)dirent;
		uint32_t csum = ext4_dir_csum(inode_ref, dirent, diff);
		if (t->checksum != to_le32(csum))
			return false;

	}
#endif
	return true;
}

void ext4_dir_init_entry_tail(struct ext4_dir_entry_tail *t)
{
	memset(t, 0, sizeof(struct ext4_dir_entry_tail));
	t->rec_len = to_le16(sizeof(struct ext4_dir_entry_tail));
	t->reserved_ft = EXT4_DIRENTRY_DIR_CSUM;
}

void ext4_dir_set_csum(struct ext4_inode_ref *inode_ref,
			   struct ext4_dir_en *dirent)
{
	struct ext4_dir_entry_tail *t;
	struct ext4_sblock *sb = &inode_ref->fs->sb;

	/* Compute the checksum only if the filesystem supports it */
	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
		t = ext4_dir_get_tail(inode_ref, dirent);
		if (!t) {
			/* There is no space to hold the checksum */
			return;
		}

		ptrdiff_t __unused diff = (char *)t - (char *)dirent;
		uint32_t csum = ext4_dir_csum(inode_ref, dirent, diff);
		t->checksum = to_le32(csum);
	}
}

/**@brief Do some checks before returning iterator.
 * @param it Iterator to be checked
 * @param block_size Size of data block
 * @return Error code
 */
static int ext4_dir_iterator_set(struct ext4_dir_iter *it,
				 uint32_t block_size)
{
	uint32_t off_in_block = it->curr_off % block_size;
	struct ext4_sblock *sb = &it->inode_ref->fs->sb;

	it->curr = NULL;

	/* Ensure proper alignment */
	if ((off_in_block % 4) != 0)
		return EIO;

	/* Ensure that the core of the entry does not overflow the block */
	if (off_in_block > block_size - 8)
		return EIO;

	struct ext4_dir_en *en;
	en = (void *)(it->curr_blk.data + off_in_block);

	/* Ensure that the whole entry does not overflow the block.  ext4edit  */
	/* patch 36: and that it is long enough to step by -- upstream checks  */
	/* only the overflow, so a rec_len of 0 makes iterator_next seek to    */
	/* the same entry for ever: an empty one spins, a used one is returned */
	/* again and again until a listing runs out of memory.                 */
	uint16_t length = ext4_dir_en_get_entry_len(en);
	if (ext4_dir_en_len_bad(length, off_in_block, block_size)) {
		/* Not a length to step by: the block is damaged. */
		return EIO;
	}

	/* Ensure the name length is not too large */
	if (ext4_dir_en_get_name_len(sb, en) > length - 8)
		return EIO;

	/* Everything OK - "publish" the entry */
	it->curr = en;
	return EOK;
}

/**@brief Seek to next valid directory entry.
 *        Here can be jumped to the next data block.
 * @param it  Initialized iterator
 * @param pos Position of the next entry
 * @return Error code
 */
static int ext4_dir_iterator_seek(struct ext4_dir_iter *it, uint64_t pos)
{
	struct ext4_sblock *sb = &it->inode_ref->fs->sb;
	struct ext4_inode *inode = it->inode_ref->inode;
	struct ext4_blockdev *bdev = it->inode_ref->fs->bdev;
	uint64_t size = ext4_inode_get_size(sb, inode);
	int r;

	/* The iterator is not valid until we seek to the desired position */
	it->curr = NULL;

	/* ext4edit patch 7: an inline directory is served from the block it
	 * would become, laid out once per iterator and freed by _fini.  It is
	 * one block at logical 0 with no cache block behind it (lb_id stays 0,
	 * so nothing below ever tries to release it), and its end is the block
	 * size, not the inode's inline size. */
	if (ext4_inline_has(sb, inode)) {
		uint32_t inline_bsize = ext4_sb_get_block_size(sb);

		if (!it->inline_blk) {
			r = ext4_inline_dir_block(it->inode_ref, &it->inline_blk);
			if (r != EOK)
				return r;
		}

		if (pos >= inline_bsize) {
			it->curr_off = pos;
			return EOK;
		}

		it->curr_blk.lb_id = 0;
		it->curr_blk.buf = NULL;
		it->curr_blk.data = it->inline_blk;
		it->curr_off = pos;
		return ext4_dir_iterator_set(it, inline_bsize);
	}

	/* Are we at the end? */
	if (pos >= size) {
		if (it->curr_blk.lb_id) {

			r = ext4_block_set(bdev, &it->curr_blk);
			it->curr_blk.lb_id = 0;
			if (r != EOK)
				return r;
		}

		it->curr_off = pos;
		return EOK;
	}

	/* Compute next block address */
	uint32_t block_size = ext4_sb_get_block_size(sb);
	uint64_t current_blk_idx = it->curr_off / block_size;
	uint32_t next_blk_idx = (uint32_t)(pos / block_size);

	/*
	 * If we don't have a block or are moving across block boundary,
	 * we need to get another block
	 */
	if ((it->curr_blk.lb_id == 0) ||
	    (current_blk_idx != next_blk_idx)) {
		if (it->curr_blk.lb_id) {
			r = ext4_block_set(bdev, &it->curr_blk);
			it->curr_blk.lb_id = 0;

			if (r != EOK)
				return r;
		}

		ext4_fsblk_t next_blk;
		r = ext4_fs_get_inode_dblk_idx(it->inode_ref, next_blk_idx,
					       &next_blk, false);
		if (r != EOK)
			return r;

		r = ext4_trans_block_get(bdev, &it->curr_blk, next_blk);
		if (r != EOK) {
			it->curr_blk.lb_id = 0;
			return r;
		}
	}

	it->curr_off = pos;
	return ext4_dir_iterator_set(it, block_size);
}

int ext4_dir_iterator_init(struct ext4_dir_iter *it,
			   struct ext4_inode_ref *inode_ref, uint64_t pos)
{
	it->inode_ref = inode_ref;
	it->curr = 0;
	it->curr_off = 0;
	it->curr_blk.lb_id = 0;
	it->inline_blk = NULL;	/* ext4edit patch 7 */

	return ext4_dir_iterator_seek(it, pos);
}

int ext4_dir_iterator_next(struct ext4_dir_iter *it)
{
	int r = EOK;
	uint16_t skip;

	while (r == EOK) {
		skip = ext4_dir_en_get_entry_len(it->curr);
		r = ext4_dir_iterator_seek(it, it->curr_off + skip);

		if (!it->curr)
			break;
		/*Skip NULL referenced entry*/
		if (ext4_dir_en_get_inode(it->curr) != 0)
			break;
	}

	return r;
}

int ext4_dir_iterator_fini(struct ext4_dir_iter *it)
{
	it->curr = 0;

	/* ext4edit patch 7: the laid-out inline directory, if one was made. */
	if (it->inline_blk) {
		free(it->inline_blk);
		it->inline_blk = NULL;
		it->curr_blk.data = NULL;
	}

	if (it->curr_blk.lb_id)
		return ext4_block_set(it->inode_ref->fs->bdev, &it->curr_blk);

	return EOK;
}

/*----------------------------------------------------------------------------*/
/* ext4_dir_inode_entry_type:                                                 */
/*                                                                            */
/* ext4edit patch 36.  The directory-entry type byte that names an inode of   */
/* this mode -- all seven kinds ext4 has, where upstream knows only           */
/* directory, file and symlink and stops on an assertion (a spin for ever,    */
/* with lwext4's own assert) for anything else.  Device nodes, FIFOs and      */
/* sockets are ordinary on a Linux root filesystem -- an OpenWrt overlay's    */
/* whiteouts are 0,0 character devices -- so with upstream's mapping,         */
/* RENAMING one, which writes its entry anew, hangs the engine.  A mode that  */
/* is none of the seven (damage) gives EXT4_DE_UNKNOWN, which the callers     */
/* refuse before anything is written.                                         */
/*                                                                            */
/* Arguments:                                                                 */
/*     sb    : the superblock (the mode's layout depends on it).              */
/*     inode : the inode the entry will name.                                 */
/*                                                                            */
/* Returns:                                                                   */
/*     uint8_t : an EXT4_DE_* type, or EXT4_DE_UNKNOWN.                        */
/*----------------------------------------------------------------------------*/
uint8_t ext4_dir_inode_entry_type(struct ext4_sblock *sb,
				  struct ext4_inode *inode)
{
	switch (ext4_inode_type(sb, inode)) {
	/* One inode type per case: the converse of ext4_fs_correspond_inode_mode. */
	case EXT4_INODE_MODE_DIRECTORY:
		return EXT4_DE_DIR;
	case EXT4_INODE_MODE_FILE:
		return EXT4_DE_REG_FILE;
	case EXT4_INODE_MODE_SOFTLINK:
		return EXT4_DE_SYMLINK;
	case EXT4_INODE_MODE_CHARDEV:
		return EXT4_DE_CHRDEV;
	case EXT4_INODE_MODE_BLOCKDEV:
		return EXT4_DE_BLKDEV;
	case EXT4_INODE_MODE_FIFO:
		return EXT4_DE_FIFO;
	case EXT4_INODE_MODE_SOCKET:
		return EXT4_DE_SOCK;
	default:
		/* Not a type ext4 has: damage. */
		return EXT4_DE_UNKNOWN;
	}
}

/* ext4edit patch 36: returns int -- EINVAL, and NOTHING written, for an   */
/* entry longer than a block (a caller's arithmetic, never data) or a      */
/* child whose mode is no known type (damage) -- where upstream asserts (a */
/* spin for ever) on both, and on every device node, FIFO and socket       */
/* besides.  Every caller checks it; ext4_dir_add_entry also refuses an    */
/* unknown type before it changes anything, so data cannot reach the late  */
/* refusal.                                                                */
int ext4_dir_write_entry(struct ext4_sblock *sb, struct ext4_dir_en *en,
			 uint16_t entry_len, struct ext4_inode_ref *child,
			 const char *name, size_t name_len)
{
	uint8_t de_type;

	/* Both refusals come before the first byte is written. */
	if (entry_len > ext4_sb_get_block_size(sb)) {
		/* Longer than a block: refused. */
		return EINVAL;
	}

	/* The type the entry will carry, from the child's mode. */
	de_type = ext4_dir_inode_entry_type(sb, child->inode);
	if (de_type == EXT4_DE_UNKNOWN) {
		/* No type an entry can name: refused. */
		return EINVAL;
	}

	/* Set type of entry */
	ext4_dir_en_set_inode_type(sb, en, de_type);

	/* Set basic attributes */
	ext4_dir_en_set_inode(en, child->index);
	ext4_dir_en_set_entry_len(en, entry_len);
	ext4_dir_en_set_name_len(sb, en, (uint16_t)name_len);

	/* Write name */
	memcpy(en->name, name, name_len);

	/* The entry is whole. */
	return EOK;
}

int ext4_dir_add_entry(struct ext4_inode_ref *parent, const char *name,
		       uint32_t name_len, struct ext4_inode_ref *child)
{
	int r;
	struct ext4_fs *fs = parent->fs;
	struct ext4_sblock *sb = &parent->fs->sb;

	/* ext4edit patch 36: a child whose mode is no type an entry can name  */
	/* (damage) is refused HERE, before the inline conversion, the index   */
	/* or a new block changes anything -- every path below ends in         */
	/* ext4_dir_write_entry, which would refuse it only after a block had  */
	/* been claimed for it.                                                */
	if (ext4_dir_inode_entry_type(sb, child->inode) == EXT4_DE_UNKNOWN) {
		/* Damage: refused, nothing changed. */
		return EINVAL;
	}

	/* ext4edit patch 7: an inline directory becomes a block-backed one
	 * before an entry is added to it; the linear code below then applies. */
	r = ext4_inline_convert(parent);
	if (r != EOK)
		return r;

#if CONFIG_DIR_INDEX_ENABLE
	/* Index adding (if allowed) */
	if ((ext4_sb_feature_com(sb, EXT4_FCOM_DIR_INDEX)) &&
	    (ext4_inode_has_flag(parent->inode, EXT4_INODE_FLAG_INDEX))) {
		/* ext4edit patch 50: the name's own length passed on. */
		r = ext4_dir_dx_add_entry(parent, child, name, name_len);

		/* Check if index is not corrupted */
		if (r != EXT4_ERR_BAD_DX_DIR) {
			if (r != EOK)
				return r;

			return EOK;
		}

		/* Needed to clear dir index flag if corrupted */
		ext4_inode_clear_flag(parent->inode, EXT4_INODE_FLAG_INDEX);
		parent->dirty = true;
	}
#endif

	/* Linear algorithm */
	uint32_t iblock = 0;
	ext4_fsblk_t fblock = 0;
	uint32_t block_size = ext4_sb_get_block_size(sb);
	uint64_t inode_size = ext4_inode_get_size(sb, parent->inode);
	uint32_t total_blocks = (uint32_t)(inode_size / block_size);

	/* Find block, where is space for new entry and try to add */
	bool success = false;
	for (iblock = 0; iblock < total_blocks; ++iblock) {
		r = ext4_fs_get_inode_dblk_idx(parent, iblock, &fblock, false);
		if (r != EOK)
			return r;

		struct ext4_block block;
		r = ext4_trans_block_get(fs->bdev, &block, fblock);
		if (r != EOK)
			return r;

		if (!ext4_dir_csum_verify(parent, (void *)block.data)) {
			ext4_dbg(DEBUG_DIR,
				 DBG_WARN "Leaf block checksum failed."
				 "Inode: %" PRIu32", "
				 "Block: %" PRIu32"\n",
				 parent->index,
				 iblock);
		}

		/* If adding is successful, function can finish */
		r = ext4_dir_try_insert_entry(sb, parent, &block, child,
						name, name_len);
		if (r == EOK)
			success = true;

		/* ext4edit patch 36: only ENOSPC means "try the next block"; */
		/* any other failure (EIO on a damaged block) is the answer --  */
		/* moving on would add the name to a later or a NEW block of a  */
		/* directory known to be damaged.  The insert's result is kept  */
		/* here, since r is reused for the release.                     */
		int r_insert;
		r_insert = r;

		r = ext4_block_set(fs->bdev, &block);
		if (r != EOK)
			return r;

		if (success)
			return EOK;
		if (r_insert != ENOSPC) {
			/* A failure other than a full block: the answer. */
			return r_insert;
		}
	}

	/* No free block found - needed to allocate next data block */

	iblock = 0;
	fblock = 0;
	r = ext4_fs_append_inode_dblk(parent, &fblock, &iblock);
	if (r != EOK)
		return r;

	/* Load new block */
	struct ext4_block b;

	r = ext4_trans_block_get_noread(fs->bdev, &b, fblock);
	if (r != EOK)
		return r;

	/* Fill block with zeroes */
	memset(b.data, 0, block_size);
	struct ext4_dir_en *blk_en = (void *)b.data;

	/* Save new block */
	uint16_t el;	// ext4edit patch 36: the entry's room, kept for the fallback below
	int r_write;	// ext4edit patch 36: the entry write's result
	el = block_size;
	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
		el = block_size - sizeof(struct ext4_dir_entry_tail);
		r_write = ext4_dir_write_entry(sb, blk_en, el, child, name,
					       name_len);
		ext4_dir_init_entry_tail(EXT4_DIRENT_TAIL(b.data, block_size));
	} else {
		r_write = ext4_dir_write_entry(sb, blk_en, block_size, child,
					       name, name_len);
	}

	/* ext4edit patch 36: the type was checked on the way in and the     */
	/* length is the block's, so this cannot fail from data.  If it does, */
	/* the block -- already the directory's, its size grown -- must not   */
	/* be left zeroed: a rec_len of 0 is exactly the damage every walk    */
	/* refuses.  So it becomes one EMPTY record (inode 0) over the same   */
	/* room, a valid block holding nothing, and the failure is returned.  */
	if (r_write != EOK) {
		/* The write failed: one empty record over the whole room. */
		ext4_dir_en_set_inode(blk_en, 0);
		ext4_dir_en_set_entry_len(blk_en, el);
		ext4_dir_en_set_name_len(sb, blk_en, 0);
	}

	ext4_dir_set_csum(parent, (void *)b.data);
	ext4_trans_set_block_dirty(b.buf);
	r = ext4_block_set(fs->bdev, &b);

	/* ext4edit patch 36: the entry's own failure outranks the release's  */
	/* success.                                                           */
	if (r_write != EOK) {
		/* The entry was not written: that is the result. */
		return r_write;
	}

	return r;
}

int ext4_dir_find_entry(struct ext4_dir_search_result *result,
			struct ext4_inode_ref *parent, const char *name,
			uint32_t name_len)
{
	int r;
	struct ext4_sblock *sb = &parent->fs->sb;

	/* ext4edit patch 8: the folding table when this directory is
	 * casefolded, NULL otherwise; the index search reads the same thing
	 * for itself, the two linear searches below take it from here. */
	const struct ext2fs_nls_table *casefold = ext4_casefold_dir(parent);

	/* Entry clear */
	result->block.lb_id = 0;
	result->dentry = NULL;
	result->inline_blk = NULL;

	/* ext4edit patch 7: an inline directory is searched in the block it
	 * would become, which the result then owns (ext4_dir_destroy_result
	 * frees it).  The entry found points into that memory, so a caller
	 * that would WRITE through it converts the directory first (see
	 * ext4_dir_remove_entry and ext4_link). */
	if (ext4_inline_has(sb, parent->inode)) {
		struct ext4_block fake;
		struct ext4_dir_en *de;
		uint8_t *blk;

		r = ext4_inline_dir_block(parent, &blk);
		if (r != EOK)
			return r;

		memset(&fake, 0, sizeof(fake));
		fake.data = blk;
		r = ext4_dir_find_in_block(&fake, sb, casefold, name_len, name,
					   &de);
		if (r == EOK) {
			result->block = fake;
			result->dentry = de;
			result->inline_blk = blk;
			return EOK;
		}

		free(blk);
		return r;
	}

#if CONFIG_DIR_INDEX_ENABLE
	/* Index search */
	if ((ext4_sb_feature_com(sb, EXT4_FCOM_DIR_INDEX)) &&
	    (ext4_inode_has_flag(parent->inode, EXT4_INODE_FLAG_INDEX))) {
		r = ext4_dir_dx_find_entry(result, parent, name_len, name);
		/* Check if index is not corrupted */
		if (r != EXT4_ERR_BAD_DX_DIR) {
			if (r != EOK)
				return r;

			return EOK;
		}

		/* Needed to clear dir index flag if corrupted */
		ext4_inode_clear_flag(parent->inode, EXT4_INODE_FLAG_INDEX);
		parent->dirty = true;
	}
#endif

	/* Linear algorithm */

	uint32_t iblock;
	ext4_fsblk_t fblock;
	uint32_t block_size = ext4_sb_get_block_size(sb);
	uint64_t inode_size = ext4_inode_get_size(sb, parent->inode);
	uint32_t total_blocks = (uint32_t)(inode_size / block_size);

	/* Walk through all data blocks */
	for (iblock = 0; iblock < total_blocks; ++iblock) {
		/* Load block address */
		r = ext4_fs_get_inode_dblk_idx(parent, iblock, &fblock, false);
		if (r != EOK)
			return r;

		/* Load data block */
		struct ext4_block b;
		r = ext4_trans_block_get(parent->fs->bdev, &b, fblock);
		if (r != EOK)
			return r;

		if (!ext4_dir_csum_verify(parent, (void *)b.data)) {
			ext4_dbg(DEBUG_DIR,
				 DBG_WARN "Leaf block checksum failed."
				 "Inode: %" PRIu32", "
				 "Block: %" PRIu32"\n",
				 parent->index,
				 iblock);
		}

		/* Try to find entry in block */
		struct ext4_dir_en *res_entry;
		r = ext4_dir_find_in_block(&b, sb, casefold, name_len, name,
					   &res_entry);
		if (r == EOK) {
			result->block = b;
			result->dentry = res_entry;
			return EOK;
		}

		/* Entry not found - put block and continue to the next block */

		/* ext4edit patch 36: ...unless the block was DAMAGED (EIO from  */
		/* its walk): that is the lookup's answer, not "look further" -- */
		/* upstream goes on, so a name in the damaged block reads as     */
		/* absent and could be created a second time.  The walk's result */
		/* is kept here, since r is reused for the release.              */
		int r_find;
		r_find = r;

		r = ext4_block_set(parent->fs->bdev, &b);
		if (r != EOK)
			return r;
		if (r_find != ENOENT) {
			/* The block was damaged: that is the answer. */
			return r_find;
		}
	}

	return ENOENT;
}

int ext4_dir_remove_entry(struct ext4_inode_ref *parent, const char *name,
			  uint32_t name_len)
{
	struct ext4_sblock *sb = &parent->fs->sb;
	/* Check if removing from directory */
	if (!ext4_inode_is_type(sb, parent->inode, EXT4_INODE_MODE_DIRECTORY))
		return ENOTDIR;

	/* ext4edit patch 7: the entry is invalidated in place below, which
	 * must land in a real block, so an inline directory converts first. */
	int rc = ext4_inline_convert(parent);
	if (rc != EOK)
		return rc;

	/* Try to find entry */
	struct ext4_dir_search_result result;
	rc = ext4_dir_find_entry(&result, parent, name, name_len);
	if (rc != EOK)
		return rc;

	/* Store entry position in block */
	uint32_t pos = (uint8_t *)result.dentry - result.block.data;

	/* ext4edit patch 36: the predecessor is FOUND, and the walk to it    */
	/* checked, BEFORE the entry is touched.  Upstream invalidates the    */
	/* entry first and then walks by each rec_len unchecked: a 0 never    */
	/* advances (a spin for ever with no assertion to see it), and a      */
	/* length that steps over the entry fires the assertion after the     */
	/* block is already half changed.  Here a walk that does not land     */
	/* exactly on the entry is damage -- EIO, the block released          */
	/* unchanged.                                                         */
	uint32_t block_size;
	uint32_t offset;
	struct ext4_dir_en *tmp_de;
	uint16_t de_len;

	/* The walk's state, before it starts: no predecessor yet. */
	block_size = ext4_sb_get_block_size(&parent->fs->sb);
	offset = 0;
	tmp_de = NULL;
	de_len = 0;

	if (pos != 0) {
		/* Not the first entry: walk to its direct predecessor. */
		tmp_de = (void *)result.block.data;
		de_len = ext4_dir_en_get_entry_len(tmp_de);

		while (1) {
			/* One entry before the removed one per iteration. */
			if (ext4_dir_en_len_bad(de_len, offset, block_size)) {
				/* Damaged: the walk stops; refused below. */
				break;
			}
			if (offset + de_len >= pos) {
				/* This one reaches the removed entry. */
				break;
			}

			/* Step to the next entry. */
			offset += de_len;
			tmp_de = (void *)(result.block.data + offset);
			de_len = ext4_dir_en_get_entry_len(tmp_de);
		}

		if (ext4_dir_en_len_bad(de_len, offset, block_size) ||
		    (offset + de_len != pos)) {
			/* The walk did not land on the entry: damage. */
			ext4_dir_destroy_result(parent, &result);
			return EIO;
		}
	}

	/* Invalidate entry */
	ext4_dir_en_set_inode(result.dentry, 0);

	/*
	 * If entry is not the first in block, it must be merged
	 * with previous entry
	 */
	if (pos != 0) {
		/* Add to removed entry length to predecessor's length */
		uint16_t del_len;
		del_len = ext4_dir_en_get_entry_len(result.dentry);
		ext4_dir_en_set_entry_len(tmp_de, de_len + del_len);
	}

	ext4_dir_set_csum(parent,
			(struct ext4_dir_en *)result.block.data);
	ext4_trans_set_block_dirty(result.block.buf);

	return ext4_dir_destroy_result(parent, &result);
}

int ext4_dir_try_insert_entry(struct ext4_sblock *sb,
			      struct ext4_inode_ref *inode_ref,
			      struct ext4_block *dst_blk,
			      struct ext4_inode_ref *child, const char *name,
			      uint32_t name_len)
{
	/* Compute required length entry and align it to 4 bytes */
	uint32_t block_size = ext4_sb_get_block_size(sb);
	uint16_t required_len = sizeof(struct ext4_fake_dir_entry) + name_len;

	if ((required_len % 4) != 0)
		required_len += 4 - (required_len % 4);

	/* Initialize pointers, stop means to upper bound */
	struct ext4_dir_en *start = (void *)dst_blk->data;
	struct ext4_dir_en *stop = (void *)(dst_blk->data + block_size);

	/*
	 * Walk through the block and check for invalid entries
	 * or entries with free space for new entry
	 */
	while (start < stop) {
		uint32_t inode = ext4_dir_en_get_inode(start);
		uint16_t rec_len = ext4_dir_en_get_entry_len(start);
		uint8_t itype = ext4_dir_en_get_inode_type(sb, start);

		/* ext4edit patch 36: the walk steps by rec_len, so a length   */
		/* no entry can have is damage, refused before anything in the */
		/* block changes -- upstream steps by it unchecked, and a 0    */
		/* spins for ever adding a name to a damaged directory.        */
		if (ext4_dir_en_len_bad(rec_len,
				(uint32_t)((uint8_t *)start - dst_blk->data),
				block_size)) {
			/* Damaged: refused, the block unchanged. */
			return EIO;
		}

		/* If invalid and large enough entry, use it */
		if ((inode == 0) && (itype != EXT4_DIRENTRY_DIR_CSUM) &&
		    (rec_len >= required_len)) {
			/* ext4edit patch 36: the writer's refusal leaves the  */
			/* block as it was.                                    */
			int r;
			r = ext4_dir_write_entry(sb, start, rec_len, child,
						 name, name_len);
			if (r != EOK) {
				/* Refused: nothing written, the result returned. */
				return r;
			}
			ext4_dir_set_csum(inode_ref, (void *)dst_blk->data);
			ext4_trans_set_block_dirty(dst_blk->buf);

			return EOK;
		}

		/* Valid entry, try to split it */
		if (inode != 0) {
			uint16_t used_len;
			used_len = ext4_dir_en_get_name_len(sb, start);

			uint16_t sz;
			sz = sizeof(struct ext4_fake_dir_entry) + used_len;

			if ((used_len % 4) != 0)
				sz += 4 - (used_len % 4);

			/* ext4edit patch 36: a name longer than its own   */
			/* record is damage -- with upstream's unsigned     */
			/* free_space below it wraps to a huge "room" and   */
			/* the new entry is written past the record, over   */
			/* its neighbours.                                  */
			if (sz > rec_len) {
				/* Damaged: refused, the block unchanged. */
				return EIO;
			}

			uint16_t free_space = rec_len - sz;

			/* There is free space for new entry */
			if (free_space >= required_len) {
				/* ext4edit patch 36: the new entry is written  */
				/* FIRST and the current one cut only once it   */
				/* is (upstream cuts first, so a refused write  */
				/* would orphan the tail).                      */
				struct ext4_dir_en * new_entry;
				int r;
				new_entry = (void *)((uint8_t *)start + sz);
				r = ext4_dir_write_entry(sb, new_entry,
						free_space, child, name, name_len);
				if (r != EOK) {
					/* Refused: the current entry is not cut. */
					return r;
				}
				/* Cut tail of current entry */
				ext4_dir_en_set_entry_len(start, sz);

				ext4_dir_set_csum(inode_ref,
						  (void *)dst_blk->data);
				ext4_trans_set_block_dirty(dst_blk->buf);
				return EOK;
			}
		}

		/* Jump to the next entry */
		start = (void *)((uint8_t *)start + rec_len);
	}

	/* No free space found for new entry */
	return ENOSPC;
}

int ext4_dir_find_in_block(struct ext4_block *block, struct ext4_sblock *sb,
			   const struct ext2fs_nls_table *casefold,
			   size_t name_len, const char *name,
			   struct ext4_dir_en **res_entry)
{
	/* Start from the first entry in block */
	struct ext4_dir_en *de = (struct ext4_dir_en *)block->data;

	/* Set upper bound for cycling */
	uint8_t *addr_limit = block->data + ext4_sb_get_block_size(sb);

	/* ext4edit patch 8: in a casefolded directory the looked-up name is
	 * folded ONCE here, and each entry is compared against that form by
	 * ext4_casefold_match (which folds the entry).  A name that cannot be
	 * folded is compared raw, as the Linux driver compares it on a
	 * non-strict filesystem; the buffer is the driver's PATH_MAX. */
	char folded_buf[EXT4_CASEFOLD_FOLD_MAX];
	const char *folded = NULL;
	size_t folded_len = 0;
	if (casefold) {
		int n = ext4_casefold_fold(casefold, name, name_len, folded_buf,
					   sizeof(folded_buf));
		if (n >= 0) {
			folded = folded_buf;
			folded_len = (size_t)n;
		}
	}

	/* Walk through the block and check entries */
	while ((uint8_t *)de < addr_limit) {
		/* Termination condition (ext4edit patch 8: a folded match can
		 * have a different length, so the raw-length bound applies only
		 * to the byte compare; the entry's header must fit either way) */
		if ((uint8_t *)de + sizeof(struct ext4_fake_dir_entry) > addr_limit)
			break;
		if (!casefold && (uint8_t *)de + name_len > addr_limit)
			break;

		/* Valid entry - check it */
		if (ext4_dir_en_get_inode(de) != 0) {
			uint16_t el = ext4_dir_en_get_name_len(sb, de);
			if (casefold) {
				/* ext4edit patch 8: compare the folded forms -- of
				 * a name that lies inside the block */
				if ((uint8_t *)de->name + el > addr_limit)
					break;
				if (ext4_casefold_match(casefold, name, name_len,
							folded, folded_len,
							(const char *)de->name,
							el)) {
					*res_entry = de;
					return EOK;
				}
			} else if (el == name_len) {
				/* For more efficient compare only lengths firstly,
				 * then the names */
				if (memcmp(name, de->name, name_len) == 0) {
					*res_entry = de;
					return EOK;
				}
			}
		}

		uint16_t de_len = ext4_dir_en_get_entry_len(de);

		/* Corrupted entry.  ext4edit patch 36: not only a 0 -- a       */
		/* length of 1-11 steps into the middle of the next entry, and  */
		/* one running past the block reads beyond the buffer; all are  */
		/* EIO, a damaged block, where upstream gives EINVAL for 0 only. */
		if (ext4_dir_en_len_bad(de_len,
				(uint32_t)((uint8_t *)de - block->data),
				ext4_sb_get_block_size(sb))) {
			/* Not a length to step by: the block is damaged. */
			return EIO;
		}

		/* Jump to next entry */
		de = (struct ext4_dir_en *)((uint8_t *)de + de_len);
	}

	/* Entry not found */
	return ENOENT;
}

int ext4_dir_destroy_result(struct ext4_inode_ref *parent,
			    struct ext4_dir_search_result *result)
{
	/* ext4edit patch 7: a result from an inline directory holds the
	 * laid-out block, not a cache block. */
	if (result->inline_blk) {
		free(result->inline_blk);
		result->inline_blk = NULL;
		result->block.data = NULL;
		result->dentry = NULL;
		return EOK;
	}

	if (result->block.lb_id)
		return ext4_block_set(parent->fs->bdev, &result->block);

	return EOK;
}

/**
 * @}
 */
