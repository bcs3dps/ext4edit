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
 * @file  ext4.h
 * @brief Ext4 high level operations (file, directory, mountpoints...)
 */

#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_errno.h"
#include "ext4_oflags.h"
#include "ext4_debug.h"

#include "ext4.h"
#include "ext4_trans.h"
#include "ext4_blockdev.h"
#include "ext4_fs.h"
#include "ext4_dir.h"
#include "ext4_inline.h"
#include "ext4_casefold.h"	/* ext4edit patch 8 */
#include "ext4_inode.h"
#include "ext4_super.h"
#include "ext4_dir_idx.h"
#include "ext4_xattr.h"
#include "ext4_journal.h"
#include "ext4_block_group.h"	// ext4edit patch 38: the group descriptor counts ext4_trans_abort_recount sums


#include <stdlib.h>
#include <string.h>

/**@brief   Mount point OS dependent lock*/
#define EXT4_MP_LOCK(_m)                                                       \
	do {                                                                   \
		if ((_m)->os_locks)                                            \
			(_m)->os_locks->lock();                                \
	} while (0)

/**@brief   Mount point OS dependent unlock*/
#define EXT4_MP_UNLOCK(_m)                                                     \
	do {                                                                   \
		if ((_m)->os_locks)                                            \
			(_m)->os_locks->unlock();                              \
	} while (0)

/**@brief   Mount point descriptor.*/
struct ext4_mountpoint {

	/**@brief   Mount done flag.*/
	bool mounted;

	/**@brief   Mount point name (@ref ext4_mount)*/
	char name[32];

	/**@brief   OS dependent lock/unlock functions.*/
	const struct ext4_lock *os_locks;

	/**@brief   Ext4 filesystem internals.*/
	struct ext4_fs fs;

	/**@brief   Dynamic allocation cache flag.*/
	bool cache_dynamic;

	struct jbd_fs jbd_fs;
	struct jbd_journal jbd_journal;
};

/**@brief   Block devices descriptor.*/
struct _ext4_devices {

	/**@brief   Block device name (@ref ext4_device_register)*/
	char name[32];

	/**@brief   Block device handle.*/
	struct ext4_blockdev *bd;

	/**@brief   Block cache handle.*/
	struct ext4_bcache *bc;
};

/**@brief   Block devices.*/
struct _ext4_devices _bdevices[CONFIG_EXT4_BLOCKDEVS_COUNT];

/**@brief   Mountpoints.*/
struct ext4_mountpoint _mp[CONFIG_EXT4_MOUNTPOINTS_COUNT];

int ext4_device_register(struct ext4_blockdev *bd, struct ext4_bcache *bc,
			 const char *dev_name)
{
	uint32_t i;
	/* ext4edit patch 36: a missing device or name is refused, where  */
	/* upstream asserts.                                              */
	if (!bd || !dev_name) {
		/* Nothing to register: refused, nothing changed. */
		return EINVAL;
	}

	for (i = 0; i < CONFIG_EXT4_BLOCKDEVS_COUNT; ++i) {
		if (!_bdevices[i].bd) {
			strcpy(_bdevices[i].name, dev_name);
			_bdevices[i].bd = bd;
			_bdevices[i].bc = bc;
			return EOK;
		}

		if (!strcmp(_bdevices[i].name, dev_name))
			return EOK;
	}
	return ENOSPC;
}

/****************************************************************************/

static bool ext4_is_dots(const uint8_t *name, size_t name_size)
{
	if ((name_size == 1) && (name[0] == '.'))
		return true;

	if ((name_size == 2) && (name[0] == '.') && (name[1] == '.'))
		return true;

	return false;
}

static int ext4_has_children(bool *has_children, struct ext4_inode_ref *enode)
{
	struct ext4_sblock *sb = &enode->fs->sb;

	/* Check if node is directory */
	if (!ext4_inode_is_type(sb, enode->inode, EXT4_INODE_MODE_DIRECTORY)) {
		*has_children = false;
		return EOK;
	}

	struct ext4_dir_iter it;
	int rc = ext4_dir_iterator_init(&it, enode, 0);
	if (rc != EOK)
		return rc;

	/* Find a non-empty directory entry */
	bool found = false;
	while (it.curr != NULL) {
		if (ext4_dir_en_get_inode(it.curr) != 0) {
			uint16_t nsize;
			nsize = ext4_dir_en_get_name_len(sb, it.curr);
			if (!ext4_is_dots(it.curr->name, nsize)) {
				found = true;
				break;
			}
		}

		rc = ext4_dir_iterator_next(&it);
		if (rc != EOK) {
			ext4_dir_iterator_fini(&it);
			return rc;
		}
	}

	rc = ext4_dir_iterator_fini(&it);
	if (rc != EOK)
		return rc;

	*has_children = found;

	return EOK;
}

static int ext4_link(struct ext4_mountpoint *mp, struct ext4_inode_ref *parent,
		     struct ext4_inode_ref *ch, const char *n,
		     uint32_t len, bool rename)
{
	/* Check maximum name length */
	if (len > EXT4_DIRECTORY_FILENAME_LEN)
		return EINVAL;

	/* Add entry to parent directory */
	int r = ext4_dir_add_entry(parent, n, len, ch);
	if (r != EOK)
		return r;

	/* Fill new dir -> add '.' and '..' entries.
	 * Also newly allocated inode should have 0 link count.
	 */

	bool is_dir = ext4_inode_is_type(&mp->fs.sb, ch->inode,
			       EXT4_INODE_MODE_DIRECTORY);
	if (is_dir && !rename) {

#if CONFIG_DIR_INDEX_ENABLE
		/* Initialize directory index if supported */
		if (ext4_sb_feature_com(&mp->fs.sb, EXT4_FCOM_DIR_INDEX)) {
			r = ext4_dir_dx_init(ch, parent);
			if (r != EOK)
				return r;

			ext4_inode_set_flag(ch->inode, EXT4_INODE_FLAG_INDEX);
			ch->dirty = true;
		} else
#endif
		{
			r = ext4_dir_add_entry(ch, ".", strlen("."), ch);
			if (r != EOK) {
				ext4_dir_remove_entry(parent, n, strlen(n));
				return r;
			}

			r = ext4_dir_add_entry(ch, "..", strlen(".."), parent);
			if (r != EOK) {
				ext4_dir_remove_entry(parent, n, strlen(n));
				ext4_dir_remove_entry(ch, ".", strlen("."));
				return r;
			}
		}

		/*New empty directory. Two links (. and ..) */
		ext4_inode_set_links_cnt(ch->inode, 2);
		ext4_fs_inode_links_count_inc(parent);
		ch->dirty = true;
		parent->dirty = true;
		return r;
	}
	/*
	 * In case we want to rename a directory,
	 * we reset the original '..' pointer.
	 */
	if (is_dir) {
		bool idx;
		idx = ext4_inode_has_flag(ch->inode, EXT4_INODE_FLAG_INDEX);
		struct ext4_dir_search_result res;

		/* ext4edit patch 7: ".." is rewritten in place below, which
		 * an inline directory does not even store -- convert first. */
		r = ext4_inline_convert(ch);
		if (r != EOK)
			return r;

		if (!idx) {
			r = ext4_dir_find_entry(&res, ch, "..", strlen(".."));
			if (r != EOK)
				return EIO;

			ext4_dir_en_set_inode(res.dentry, parent->index);
			/* ext4edit patch 14 (upstream PR #132): the block's
			 * checksum is recomputed after ".." is rewritten -- as
			 * published it was not, so on metadata_csum every
			 * non-indexed directory moved to a new parent (any
			 * small directory Linux made) failed its checksum. */
			ext4_dir_set_csum(ch, (void *)res.block.data);
			ext4_trans_set_block_dirty(res.block.buf);
			r = ext4_dir_destroy_result(ch, &res);
			if (r != EOK)
				return r;

		} else {
#if CONFIG_DIR_INDEX_ENABLE
			r = ext4_dir_dx_reset_parent_inode(ch, parent->index);
			if (r != EOK)
				return r;

#endif
		}

		ext4_fs_inode_links_count_inc(parent);
		parent->dirty = true;
	}
	if (!rename) {
		ext4_fs_inode_links_count_inc(ch);
		ch->dirty = true;
	}

	return r;
}

static int ext4_unlink(struct ext4_mountpoint *mp,
		       struct ext4_inode_ref *parent,
		       struct ext4_inode_ref *child, const char *name,
		       uint32_t name_len)
{
	bool has_children;
	int rc = ext4_has_children(&has_children, child);
	if (rc != EOK)
		return rc;

	/* Cannot unlink non-empty node */
	if (has_children)
		return ENOTEMPTY;

	/* Remove entry from parent directory */
	rc = ext4_dir_remove_entry(parent, name, name_len);
	if (rc != EOK)
		return rc;

	bool is_dir = ext4_inode_is_type(&mp->fs.sb, child->inode,
					 EXT4_INODE_MODE_DIRECTORY);

	/* If directory - handle links from parent */
	if (is_dir) {
		ext4_fs_inode_links_count_dec(parent);
		parent->dirty = true;
	}

	/*
	 * TODO: Update timestamps of the parent
	 * (when we have wall-clock time).
	 *
	 * ext4_inode_set_change_inode_time(parent->inode, (uint32_t) now);
	 * ext4_inode_set_modification_time(parent->inode, (uint32_t) now);
	 * parent->dirty = true;
	 */

	/*
	 * TODO: Update timestamp for inode.
	 *
	 * ext4_inode_set_change_inode_time(child->inode,
	 *     (uint32_t) now);
	 */
	if (ext4_inode_get_links_cnt(child->inode)) {
		ext4_fs_inode_links_count_dec(child);
		child->dirty = true;
	}

	return EOK;
}

/****************************************************************************/

int ext4_mount(const char *dev_name, const char *mount_point,
	       bool read_only)
{
	int r;
	int i;

	uint32_t bsize;
	struct ext4_blockdev *bd = 0;
	struct ext4_bcache *bc = 0;
	struct ext4_mountpoint *mp = 0;

	/* ext4edit patch 36: a missing name is refused, where upstream  */
	/* asserts.                                                      */
	if (!mount_point || !dev_name) {
		/* Nothing to mount: refused before anything is claimed. */
		return EINVAL;
	}

	if (mount_point[strlen(mount_point) - 1] != '/')
		return ENOTSUP;

	for (i = 0; i < CONFIG_EXT4_BLOCKDEVS_COUNT; ++i) {
		if (_bdevices[i].name) {
			if (!strcmp(dev_name, _bdevices[i].name)) {
				bd = _bdevices[i].bd;
				bc = _bdevices[i].bc;
				break;
			}
		}
	}

	if (!bd)
		return ENODEV;

	for (i = 0; i < CONFIG_EXT4_MOUNTPOINTS_COUNT; ++i) {
		if (!_mp[i].mounted) {
			strcpy(_mp[i].name, mount_point);
			_mp[i].mounted = 1;
			mp = &_mp[i];
			break;
		}

		if (!strcmp(_mp[i].name, mount_point))
			return EOK;
	}

	if (!mp)
		return ENOMEM;

	r = ext4_block_init(bd);
	if (r != EOK) {
		/* ext4edit patch 4: a failed mount must give the mount point
		 * back.  As published, every failure below this line returned
		 * with mp->mounted still set and the name still in place, so
		 * the NEXT ext4_mount of the same mount point matched the loop
		 * above and returned EOK without opening the device, reading
		 * the superblock or binding a cache: a phantom mount whose
		 * reads fail with EIO and whose ext4_umount dereferences the
		 * NULL cache and crashes the process.  Reached through the GUI
		 * by one refused open followed by another on the same path. */
		mp->mounted = 0;
		mp->name[0] = '\0';
		return r;
	}

	r = ext4_fs_init(&mp->fs, bd, read_only);
	if (r != EOK) {
		ext4_block_fini(bd);
		/* ext4edit patch 4: give the mount point back (see above). */
		mp->mounted = 0;
		mp->name[0] = '\0';
		return r;
	}

	bsize = ext4_sb_get_block_size(&mp->fs.sb);
	/* ext4edit patch 36: a refused size releases as the later failures */
	/* below do -- the device closed, the mount point given back, and   */
	/* NOT ext4_fs_fini, which would write the superblock.  A guard: the */
	/* superblock bounds of patches 24-29 refuse such a block size       */
	/* before this point, so data cannot reach it.                       */
	r = ext4_block_set_lb_size(bd, bsize);
	if (r != EOK) {
		/* Refused: the device closed and the mount point given back. */
		ext4_block_fini(bd);
		mp->mounted = 0;
		mp->name[0] = '\0';
		return r;
	}

	mp->cache_dynamic = 0;

	if (!bc) {
		/*Automatic block cache alloc.*/
		mp->cache_dynamic = 1;
		bc = malloc(sizeof(struct ext4_bcache));

		r = ext4_bcache_init_dynamic(bc, CONFIG_BLOCK_DEV_CACHE_SIZE,
					     bsize);
		if (r != EOK) {
			free(bc);
			ext4_block_fini(bd);
			/* ext4edit patch 4: give the mount point back (see above). */
			mp->mounted = 0;
			mp->name[0] = '\0';
			return r;
		}
	}

	if (bsize != bc->itemsize) {
		/* ext4edit patch 4: as published this exit returned with the
		 * device still open and a dynamic cache still allocated, as
		 * well as the mount point still claimed.  Undo all three. */
		if (mp->cache_dynamic) {
			ext4_bcache_fini_dynamic(bc);
			free(bc);
		}
		ext4_block_fini(bd);
		mp->mounted = 0;
		mp->name[0] = '\0';
		return ENOTSUP;
	}

	/*Bind block cache to block device*/
	r = ext4_block_bind_bcache(bd, bc);
	if (r != EOK) {
		ext4_bcache_cleanup(bc);
		ext4_block_fini(bd);
		if (mp->cache_dynamic) {
			ext4_bcache_fini_dynamic(bc);
			free(bc);
		}
		/* ext4edit patch 4: give the mount point back (see above). */
		mp->mounted = 0;
		mp->name[0] = '\0';
		return r;
	}
	bd->fs = &mp->fs;

	return r;
}


int ext4_umount(const char *mount_point)
{
	int i;
	int r;
	struct ext4_mountpoint *mp = 0;

	for (i = 0; i < CONFIG_EXT4_MOUNTPOINTS_COUNT; ++i) {
		if (!strcmp(_mp[i].name, mount_point)) {
			mp = &_mp[i];
			break;
		}
	}

	if (!mp)
		return ENODEV;

	/* ext4edit patch 4: a mount point that is not mounted, or whose
	 * block device never had a cache bound, is not one this function can
	 * take down -- as published it went on to ext4_bcache_cleanup(NULL)
	 * and crashed.  With the mount-side patch this can no longer be
	 * reached; the guard keeps a future slip an error, not a crash. */
	if (!mp->mounted || !mp->fs.bdev || !mp->fs.bdev->bc)
		return ENODEV;

	r = ext4_fs_fini(&mp->fs);
	if (r != EOK)
		goto Finish;

	mp->mounted = 0;

	ext4_bcache_cleanup(mp->fs.bdev->bc);
	if (mp->cache_dynamic) {
		ext4_bcache_fini_dynamic(mp->fs.bdev->bc);
		free(mp->fs.bdev->bc);
	}
	r = ext4_block_fini(mp->fs.bdev);
Finish:
	mp->fs.bdev->fs = NULL;
	return r;
}

static struct ext4_mountpoint *ext4_get_mount(const char *path)
{
	int i;
	for (i = 0; i < CONFIG_EXT4_MOUNTPOINTS_COUNT; ++i) {

		if (!_mp[i].mounted)
			continue;

		if (!strncmp(_mp[i].name, path, strlen(_mp[i].name)))
			return &_mp[i];
	}
	return NULL;
}

__unused
static int __ext4_journal_start(const char *mount_point)
{
	int r = EOK;
	struct ext4_mountpoint *mp = ext4_get_mount(mount_point);
	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EOK;

	if (ext4_sb_feature_com(&mp->fs.sb,
				EXT4_FCOM_HAS_JOURNAL)) {
		r = jbd_get_fs(&mp->fs, &mp->jbd_fs);
		if (r != EOK)
			goto Finish;

		r = jbd_journal_start(&mp->jbd_fs, &mp->jbd_journal);
		if (r != EOK) {
			mp->jbd_fs.dirty = false;
			jbd_put_fs(&mp->jbd_fs);
			goto Finish;
		}
		mp->fs.jbd_fs = &mp->jbd_fs;
		mp->fs.jbd_journal = &mp->jbd_journal;
	}
Finish:
	return r;
}

__unused
static int __ext4_journal_stop(const char *mount_point)
{
	int r = EOK;
	struct ext4_mountpoint *mp = ext4_get_mount(mount_point);
	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EOK;

	if (ext4_sb_feature_com(&mp->fs.sb,
				EXT4_FCOM_HAS_JOURNAL)) {
		r = jbd_journal_stop(&mp->jbd_journal);
		if (r != EOK) {
			mp->jbd_fs.dirty = false;
			jbd_put_fs(&mp->jbd_fs);
			mp->fs.jbd_journal = NULL;
			mp->fs.jbd_fs = NULL;
			goto Finish;
		}

		r = jbd_put_fs(&mp->jbd_fs);
		if (r != EOK) {
			mp->fs.jbd_journal = NULL;
			mp->fs.jbd_fs = NULL;
			goto Finish;
		}

		mp->fs.jbd_journal = NULL;
		mp->fs.jbd_fs = NULL;
	}
Finish:
	return r;
}

/* ext4edit patch 41: declared ahead of its definition below, for the     */
/* recovery's recount.                                                     */
static void ext4_trans_abort_recount(struct ext4_fs *fs);

__unused
static int __ext4_recover(const char *mount_point)
{
	struct ext4_mountpoint *mp = ext4_get_mount(mount_point);
	if (!mp)
		return ENOENT;

	int r = ENOTSUP;
	EXT4_MP_LOCK(mp);
	if (ext4_sb_feature_com(&mp->fs.sb, EXT4_FCOM_HAS_JOURNAL)) {
		struct jbd_fs *jbd_fs = calloc(1, sizeof(struct jbd_fs));
		if (!jbd_fs) {
			 r = ENOMEM;
			 goto Finish;
		}


		r = jbd_get_fs(&mp->fs, jbd_fs);
		if (r != EOK) {
			free(jbd_fs);
			goto Finish;
		}

		r = jbd_recover(jbd_fs);
		jbd_put_fs(jbd_fs);
		free(jbd_fs);
	}

	/* ext4edit patch 41 (upstream master 2e93de2): after a recovery    */
	/* on a writable mount, the superblock's free block and inode counts */
	/* are made from the block groups'.  A replay restores the groups'   */
	/* descriptors and bitmaps, but the superblock's counts are whatever */
	/* it last held -- the Linux driver keeps exact counts in memory and */
	/* writes them lazily, so after a crash they are stale -- and        */
	/* lwext4 adjusts them from there and writes them at unmount, where  */
	/* e2fsck finds "free blocks count wrong".  The groups are the truth */
	/* after the replay; patch 38's sum, which leaves the counts as they */
	/* were when a group cannot be read, does the work.                  */
	if (r == EOK && !mp->fs.read_only) {
		/* Recovered (or nothing to replay), and the mount may write. */
		ext4_trans_abort_recount(&mp->fs);
	}

Finish:
	EXT4_MP_UNLOCK(mp);
	return r;
}

__unused
static int __ext4_trans_start(struct ext4_mountpoint *mp)
{
	int r = EOK;
	if (mp->fs.jbd_journal && !mp->fs.curr_trans) {
		struct jbd_journal *journal = mp->fs.jbd_journal;
		struct jbd_trans *trans;
		trans = jbd_journal_new_trans(journal);
		if (!trans) {
			r = ENOMEM;
			goto Finish;
		}
		mp->fs.curr_trans = trans;
	}
Finish:
	return r;
}

__unused
static int __ext4_trans_stop(struct ext4_mountpoint *mp)
{
	int r = EOK;
	if (mp->fs.jbd_journal && mp->fs.curr_trans) {
		struct jbd_journal *journal = mp->fs.jbd_journal;
		struct jbd_trans *trans = mp->fs.curr_trans;
		r = jbd_journal_commit_trans(journal, trans);
		mp->fs.curr_trans = NULL;
	}
	return r;
}

/*----------------------------------------------------------------------------*/
/* ext4_trans_abort_recount:                                                  */
/*                                                                            */
/* ext4edit patch 38.  After an aborted transaction, set the superblock's     */
/* free block and free inode counts to the sums of the block groups'.  The    */
/* abort drops the transaction's journaled metadata -- bitmaps and group      */
/* descriptors go back to what they were -- but the allocator lowered the     */
/* SUPERBLOCK's counts in memory as it went (ext4_balloc.c), and the          */
/* superblock is written at unmount, outside the journal, so without this     */
/* the decrements would outlive the abort: a full-disk import that fails      */
/* would leave the superblock's free blocks short of the groups'.  The group  */
/* descriptors are the truth after the abort, so the counts are made from     */
/* them.  A group that cannot be read leaves the counts as they were.         */
/*                                                                            */
/* Arguments:                                                                 */
/*     fs : the mounted filesystem whose transaction was just aborted.        */
/*                                                                            */
/* Returns:                                                                   */
/*     void : the superblock's two free counts match the groups.              */
/*----------------------------------------------------------------------------*/
static void ext4_trans_abort_recount(struct ext4_fs *fs)
{
	struct ext4_block_group_ref ref;
	uint32_t bg_count;
	uint64_t free_blocks;
	uint32_t free_inodes;
	uint32_t i;

	/* Every group, summed: the totals start at nothing. */
	bg_count = ext4_block_group_cnt(&fs->sb);
	free_blocks = 0;
	free_inodes = 0;
	for (i = 0; i < bg_count; i++) {
		/* One group's descriptor per iteration: its two counts added, */
		/* then the descriptor given back.                              */
		if (ext4_fs_get_block_group_ref(fs, i, &ref) != EOK) {
			/* Unreadable: a partial sum would be wrong, so the */
			/* counts are left as they were.                    */
			return;
		}
		free_blocks += ext4_bg_get_free_blocks_count(ref.block_group, &fs->sb);
		free_inodes += ext4_bg_get_free_inodes_count(ref.block_group, &fs->sb);
		ext4_fs_put_block_group_ref(&ref);
	}

	/* The superblock takes the groups' totals. */
	ext4_sb_set_free_blocks_cnt(&fs->sb, free_blocks);
	ext4_set32(&fs->sb, free_inodes_count, free_inodes);
}

__unused
static void __ext4_trans_abort(struct ext4_mountpoint *mp)
{
	if (mp->fs.jbd_journal && mp->fs.curr_trans) {
		struct jbd_journal *journal = mp->fs.jbd_journal;
		struct jbd_trans *trans = mp->fs.curr_trans;
		jbd_journal_free_trans(journal, trans, true);
		mp->fs.curr_trans = NULL;

		/* ext4edit patch 38: the superblock's free counts, which the  */
		/* abort does not take back, made from the groups again.       */
		ext4_trans_abort_recount(&mp->fs);
	}
}

int ext4_journal_start(const char *mount_point __unused)
{
	int r = EOK;
#if CONFIG_JOURNALING_ENABLE
	r = __ext4_journal_start(mount_point);
#endif
	return r;
}

int ext4_journal_stop(const char *mount_point __unused)
{
	int r = EOK;
#if CONFIG_JOURNALING_ENABLE
	r = __ext4_journal_stop(mount_point);
#endif
	return r;
}

int ext4_recover(const char *mount_point __unused)
{
	int r = EOK;
#if CONFIG_JOURNALING_ENABLE
	r = __ext4_recover(mount_point);
#endif
	return r;
}

static int ext4_trans_start(struct ext4_mountpoint *mp __unused)
{
	int r = EOK;
#if CONFIG_JOURNALING_ENABLE
	r = __ext4_trans_start(mp);
#endif
	return r;
}

static int ext4_trans_stop(struct ext4_mountpoint *mp __unused)
{
	int r = EOK;
#if CONFIG_JOURNALING_ENABLE
	r = __ext4_trans_stop(mp);
#endif
	return r;
}

static void ext4_trans_abort(struct ext4_mountpoint *mp __unused)
{
#if CONFIG_JOURNALING_ENABLE
	__ext4_trans_abort(mp);
#endif
}


int ext4_mount_point_stats(const char *mount_point,
			   struct ext4_mount_stats *stats)
{
	struct ext4_mountpoint *mp = ext4_get_mount(mount_point);

	if (!mp)
		return ENOENT;

	EXT4_MP_LOCK(mp);
	stats->inodes_count = ext4_get32(&mp->fs.sb, inodes_count);
	stats->free_inodes_count = ext4_get32(&mp->fs.sb, free_inodes_count);
	stats->blocks_count = ext4_sb_get_blocks_cnt(&mp->fs.sb);
	stats->free_blocks_count = ext4_sb_get_free_blocks_cnt(&mp->fs.sb);
	stats->block_size = ext4_sb_get_block_size(&mp->fs.sb);

	stats->block_group_count = ext4_block_group_cnt(&mp->fs.sb);
	stats->blocks_per_group = ext4_get32(&mp->fs.sb, blocks_per_group);
	stats->inodes_per_group = ext4_get32(&mp->fs.sb, inodes_per_group);

	memcpy(stats->volume_name, mp->fs.sb.volume_name, 16);
	EXT4_MP_UNLOCK(mp);

	return EOK;
}

int ext4_mount_setup_locks(const char *mount_point,
			   const struct ext4_lock *locks)
{
	uint32_t i;
	struct ext4_mountpoint *mp = 0;

	for (i = 0; i < CONFIG_EXT4_MOUNTPOINTS_COUNT; ++i) {
		if (!strcmp(_mp[i].name, mount_point)) {
			mp = &_mp[i];
			break;
		}
	}
	if (!mp)
		return ENOENT;

	mp->os_locks = locks;
	return EOK;
}

/********************************FILE OPERATIONS*****************************/

static int ext4_path_check(const char *path, bool *is_goal)
{
	int i;

	/* ext4edit patch 52: the loop must reach index 255 to see the '/' or */
	/* NUL that ends a name of exactly EXT4_DIRECTORY_FILENAME_LEN bytes; */
	/* stopping at 254 returned 0 for such a name, and every operation on */
	/* it then failed with ENOENT although ext4 allows it.  A name of 256 */
	/* or more still returns 0, and ext4_link refuses it on creation.     */
	for (i = 0; i <= EXT4_DIRECTORY_FILENAME_LEN; ++i) {

		if (path[i] == '/') {
			*is_goal = false;
			return i;
		}

		if (path[i] == 0) {
			*is_goal = true;
			return i;
		}
	}

	return 0;
}

static bool ext4_parse_flags(const char *flags, uint32_t *file_flags)
{
	if (!flags)
		return false;

	if (!strcmp(flags, "r") || !strcmp(flags, "rb")) {
		*file_flags = O_RDONLY;
		return true;
	}

	if (!strcmp(flags, "w") || !strcmp(flags, "wb")) {
		*file_flags = O_WRONLY | O_CREAT | O_TRUNC;
		return true;
	}

	if (!strcmp(flags, "a") || !strcmp(flags, "ab")) {
		*file_flags = O_WRONLY | O_CREAT | O_APPEND;
		return true;
	}

	if (!strcmp(flags, "r+") || !strcmp(flags, "rb+") ||
	    !strcmp(flags, "r+b")) {
		*file_flags = O_RDWR;
		return true;
	}

	if (!strcmp(flags, "w+") || !strcmp(flags, "wb+") ||
	    !strcmp(flags, "w+b")) {
		*file_flags = O_RDWR | O_CREAT | O_TRUNC;
		return true;
	}

	if (!strcmp(flags, "a+") || !strcmp(flags, "ab+") ||
	    !strcmp(flags, "a+b")) {
		*file_flags = O_RDWR | O_CREAT | O_APPEND;
		return true;
	}

	return false;
}

static int ext4_trunc_inode(struct ext4_mountpoint *mp,
			    uint32_t index, uint64_t new_size)
{
	int r = EOK;
	struct ext4_fs *const fs = &mp->fs;
	struct ext4_inode_ref inode_ref;
	uint64_t inode_size;
	bool has_trans = mp->fs.jbd_journal && mp->fs.curr_trans;
	r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
	if (r != EOK)
		return r;

	inode_size = ext4_inode_get_size(&fs->sb, inode_ref.inode);
	ext4_fs_put_inode_ref(&inode_ref);
	if (has_trans)
		ext4_trans_stop(mp);

	while (inode_size > new_size + CONFIG_MAX_TRUNCATE_SIZE) {

		inode_size -= CONFIG_MAX_TRUNCATE_SIZE;

		ext4_trans_start(mp);
		r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
		if (r != EOK) {
			ext4_trans_abort(mp);
			break;
		}
		r = ext4_fs_truncate_inode(&inode_ref, inode_size);
		if (r != EOK)
			ext4_fs_put_inode_ref(&inode_ref);
		else
			r = ext4_fs_put_inode_ref(&inode_ref);

		if (r != EOK) {
			ext4_trans_abort(mp);
			goto Finish;
		} else
			ext4_trans_stop(mp);
	}

	if (inode_size > new_size) {

		inode_size = new_size;

		ext4_trans_start(mp);
		r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
		if (r != EOK) {
			ext4_trans_abort(mp);
			goto Finish;
		}
		r = ext4_fs_truncate_inode(&inode_ref, inode_size);
		if (r != EOK)
			ext4_fs_put_inode_ref(&inode_ref);
		else
			r = ext4_fs_put_inode_ref(&inode_ref);

		if (r != EOK)
			ext4_trans_abort(mp);
		else
			ext4_trans_stop(mp);

	}

Finish:

	if (has_trans)
		ext4_trans_start(mp);

	return r;
}

static int ext4_trunc_dir(struct ext4_mountpoint *mp,
			  struct ext4_inode_ref *parent,
			  struct ext4_inode_ref *dir)
{
	int r = EOK;
	bool is_dir = ext4_inode_is_type(&mp->fs.sb, dir->inode,
			EXT4_INODE_MODE_DIRECTORY);
	uint32_t block_size = ext4_sb_get_block_size(&mp->fs.sb);
	if (!is_dir)
		return EINVAL;

#if CONFIG_DIR_INDEX_ENABLE
	/* Initialize directory index if supported */
	if (ext4_sb_feature_com(&mp->fs.sb, EXT4_FCOM_DIR_INDEX)) {
		r = ext4_dir_dx_init(dir, parent);
		if (r != EOK)
			return r;

		r = ext4_trunc_inode(mp, dir->index,
				     EXT4_DIR_DX_INIT_BCNT * block_size);
		if (r != EOK)
			return r;
	} else
#endif
	{
		r = ext4_trunc_inode(mp, dir->index, block_size);
		if (r != EOK)
			return r;
	}

	return ext4_fs_truncate_inode(dir, 0);
}

/*
 * NOTICE: if filetype is equal to EXT4_DIRENTRY_UNKNOWN,
 * any filetype of the target dir entry will be accepted.
 */
static int ext4_generic_open2(ext4_file *f, const char *path, int flags,
			      int ftype, uint32_t *parent_inode,
			      uint32_t *name_off)
{
	bool is_goal = false;
	uint32_t imode = EXT4_INODE_MODE_DIRECTORY;
	uint32_t next_inode;

	int r;
	int len;
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	struct ext4_dir_search_result result;
	struct ext4_inode_ref ref;

	f->mp = 0;

	if (!mp)
		return ENOENT;

	struct ext4_fs *const fs = &mp->fs;
	struct ext4_sblock *const sb = &mp->fs.sb;

	if (fs->read_only && flags & O_CREAT)
		return EROFS;

	f->flags = flags;

	/*Skip mount point*/
	path += strlen(mp->name);

	if (name_off)
		*name_off = strlen(mp->name);

	/*Load root*/
	r = ext4_fs_get_inode_ref(fs, EXT4_INODE_ROOT_INDEX, &ref);
	if (r != EOK)
		return r;

	if (parent_inode)
		*parent_inode = ref.index;

	/* ext4edit patch 48 (upstream master 9b38aef): no transaction is     */
	/* started here.  A creating open is journalled by its CALLER --      */
	/* ext4_generic_open and ext4_fopen2 open one around it, and          */
	/* ext4_fsymlink's own covers the link's target too.  Started here,   */
	/* inside a caller's transaction, it was a no-op while this           */
	/* function's stop COMMITTED the caller's transaction: what the       */
	/* caller wrote next (a symlink's target) went unjournaled, and its   */
	/* abort aborted nothing; and the failure exit after the walk below   */
	/* returned without ending it.                                        */
	len = ext4_path_check(path, &is_goal);
	while (1) {

		len = ext4_path_check(path, &is_goal);
		if (!len) {
			/*If root open was request.*/
			if (ftype == EXT4_DE_DIR || ftype == EXT4_DE_UNKNOWN)
				if (is_goal)
					break;

			r = ENOENT;
			break;
		}

		r = ext4_dir_find_entry(&result, &ref, path, len);
		if (r != EOK) {

			/*Destroy last result*/
			ext4_dir_destroy_result(&ref, &result);
			if (r != ENOENT)
				break;

			if (!(f->flags & O_CREAT))
				break;

			/*O_CREAT allows create new entry*/
			struct ext4_inode_ref child_ref;
			r = ext4_fs_alloc_inode(fs, &child_ref,
					is_goal ? ftype : EXT4_DE_DIR);
			if (r != EOK)
				break;

			/* ext4edit patch 8: a DIRECTORY made inside a casefolded
			 * directory is casefolded too, as the Linux driver makes it
			 * (EXT4_CASEFOLD_FL is in EXT4_FL_INHERITED, and only a
			 * directory keeps it); a file or link made there is not. */
			if ((!is_goal || ftype == EXT4_DE_DIR) &&
			    ext4_casefold_dir(&ref))
				ext4_inode_set_flag(child_ref.inode,
						    EXT4_INODE_FLAG_CASEFOLD);

			/*Link with root dir.*/
			r = ext4_link(mp, &ref, &child_ref, path, len, false);
			if (r != EOK) {
				/*Fail. Free new inode.*/
				ext4_fs_free_inode(&child_ref);
				/*We do not want to write new inode.
				  But block has to be released.*/
				child_ref.dirty = false;
				ext4_fs_put_inode_ref(&child_ref);
				break;
			}

			ext4_fs_put_inode_ref(&child_ref);
			continue;
		}

		if (parent_inode)
			*parent_inode = ref.index;

		next_inode = ext4_dir_en_get_inode(result.dentry);
		/* ext4edit patch 36: the entry's own type when it names one    */
		/* (all seven map), else -- no filetype feature, or an entry    */
		/* whose type byte is UNKNOWN or out of range -- the child      */
		/* inode's mode, read.  Upstream asserts (a spin for ever) on   */
		/* every type but directory, file and symlink.  A failed read   */
		/* releases the search result before leaving, which upstream's  */
		/* no-filetype branch does not (a cache block held for ever).   */
		imode = 0;
		if (ext4_sb_feature_incom(sb, EXT4_FINCOM_FILETYPE)) {
			uint8_t t;
			t = ext4_dir_en_get_inode_type(sb, result.dentry);
			imode = ext4_fs_correspond_inode_mode(t);
		}
		if (imode == 0) {
			/* The entry does not say: ask the inode. */
			struct ext4_inode_ref child_ref;
			r = ext4_fs_get_inode_ref(fs, next_inode, &child_ref);
			if (r != EOK) {
				/* Unreadable: the search result released, */
				/* and the walk ends with the error.        */
				ext4_dir_destroy_result(&ref, &result);
				break;
			}

			imode = ext4_inode_type(sb, child_ref.inode);
			ext4_fs_put_inode_ref(&child_ref);
		}

		r = ext4_dir_destroy_result(&ref, &result);
		if (r != EOK)
			break;

		/*If expected file error*/
		if (imode != EXT4_INODE_MODE_DIRECTORY && !is_goal) {
			r = ENOENT;
			break;
		}
		if (ftype != EXT4_DE_UNKNOWN) {
			bool df = imode != ext4_fs_correspond_inode_mode(ftype);
			if (df && is_goal) {
				r = ENOENT;
				break;
			}
		}

		/* ext4edit patch 19 (upstream PR #156): these two exits
		 * RETURN rather than break -- the error path after the loop
		 * puts `ref` again, which after a failed put is a second put,
		 * and after a failed get is a put of a ref never filled. */
		r = ext4_fs_put_inode_ref(&ref);
		if (r != EOK)
			return r;

		r = ext4_fs_get_inode_ref(fs, next_inode, &ref);
		if (r != EOK)
			return r;

		if (is_goal)
			break;

		path += len + 1;

		if (name_off)
			*name_off += len + 1;
	};

	if (r != EOK) {
		ext4_fs_put_inode_ref(&ref);
		return r;
	}

	if (is_goal) {

		if ((f->flags & O_TRUNC) && (imode == EXT4_INODE_MODE_FILE)) {
			r = ext4_trunc_inode(mp, ref.index, 0);
			if (r != EOK) {
				ext4_fs_put_inode_ref(&ref);
				return r;
			}
		}

		f->mp = mp;
		f->fsize = ext4_inode_get_size(sb, ref.inode);
		f->inode = ref.index;
		f->fpos = 0;

		if (f->flags & O_APPEND)
			f->fpos = f->fsize;

	}

	/* ext4edit patch 48: the reference given back is the result; a      */
	/* creating caller's transaction is the caller's to end.             */
	return ext4_fs_put_inode_ref(&ref);
}

/****************************************************************************/

static int ext4_generic_open(ext4_file *f, const char *path, const char *flags,
			     bool file_expect, uint32_t *parent_inode,
			     uint32_t *name_off)
{
	uint32_t iflags;
	int filetype;
	int r;
	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (ext4_parse_flags(flags, &iflags) == false)
		return EINVAL;

	if (file_expect == true)
		filetype = EXT4_DE_REG_FILE;
	else
		filetype = EXT4_DE_DIR;

	/* ext4edit patch 48 (upstream master 9b38aef): a creating open is    */
	/* one transaction, opened here around the walk that makes every      */
	/* missing level, committed when it succeeds and aborted when it      */
	/* fails -- ext4_generic_open2 no longer opens one of its own.        */
	if (iflags & O_CREAT) {
		/* Creating: the transaction around the open. */
		ext4_trans_start(mp);
	}

	r = ext4_generic_open2(f, path, iflags, filetype, parent_inode,
			       name_off);

	if (iflags & O_CREAT) {
		/* Creating: committed on success, aborted on failure. */
		if (r == EOK) {
			/* Made: committed. */
			ext4_trans_stop(mp);
		} else {
			/* Refused or failed: whatever it began is dropped. */
			ext4_trans_abort(mp);
		}
	}

	return r;
}

static int ext4_create_hardlink(const char *path,
		struct ext4_inode_ref *child_ref, bool rename)
{
	bool is_goal = false;
	uint32_t inode_mode = EXT4_INODE_MODE_DIRECTORY;
	uint32_t next_inode;

	int r;
	int len;
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	struct ext4_dir_search_result result;
	struct ext4_inode_ref ref;

	if (!mp)
		return ENOENT;

	struct ext4_fs *const fs = &mp->fs;
	struct ext4_sblock *const sb = &mp->fs.sb;

	/*Skip mount point*/
	path += strlen(mp->name);

	/*Load root*/
	r = ext4_fs_get_inode_ref(fs, EXT4_INODE_ROOT_INDEX, &ref);
	if (r != EOK)
		return r;

	len = ext4_path_check(path, &is_goal);
	while (1) {

		len = ext4_path_check(path, &is_goal);
		if (!len) {
			/*If root open was request.*/
			r = is_goal ? EINVAL : ENOENT;
			break;
		}

		r = ext4_dir_find_entry(&result, &ref, path, len);
		if (r != EOK) {

			/*Destroy last result*/
			ext4_dir_destroy_result(&ref, &result);

			if (r != ENOENT || !is_goal)
				break;

			/*Link with root dir.*/
			r = ext4_link(mp, &ref, child_ref, path, len, rename);
			break;
		} else if (r == EOK && is_goal) {
			/*Destroy last result*/
			ext4_dir_destroy_result(&ref, &result);
			r = EEXIST;
			break;
		}

		next_inode = result.dentry->inode;
		/* ext4edit patch 36: as in ext4_generic_open2 -- the entry's */
		/* type when it names one, else the child inode's mode; the     */
		/* search result released on a failed read.                     */
		inode_mode = 0;
		if (ext4_sb_feature_incom(sb, EXT4_FINCOM_FILETYPE)) {
			uint8_t t;
			t = ext4_dir_en_get_inode_type(sb, result.dentry);
			inode_mode = ext4_fs_correspond_inode_mode(t);
		}
		if (inode_mode == 0) {
			/* The entry does not say: ask the inode. */
			struct ext4_inode_ref child_ref;
			r = ext4_fs_get_inode_ref(fs, next_inode, &child_ref);
			if (r != EOK) {
				/* Unreadable: the search result released, */
				/* and the walk ends with the error.        */
				ext4_dir_destroy_result(&ref, &result);
				break;
			}

			inode_mode = ext4_inode_type(sb, child_ref.inode);
			ext4_fs_put_inode_ref(&child_ref);
		}

		r = ext4_dir_destroy_result(&ref, &result);
		if (r != EOK)
			break;

		if (inode_mode != EXT4_INODE_MODE_DIRECTORY) {
			r = is_goal ? EEXIST : ENOENT;
			break;
		}

		/* ext4edit patch 19 (upstream PR #156): these two exits
		 * RETURN rather than break -- the error path after the loop
		 * puts `ref` again, which after a failed put is a second put,
		 * and after a failed get is a put of a ref never filled. */
		r = ext4_fs_put_inode_ref(&ref);
		if (r != EOK)
			return r;

		r = ext4_fs_get_inode_ref(fs, next_inode, &ref);
		if (r != EOK)
			return r;

		if (is_goal)
			break;

		path += len + 1;
	};

	if (r != EOK) {
		ext4_fs_put_inode_ref(&ref);
		return r;
	}

	r = ext4_fs_put_inode_ref(&ref);
	return r;
}

static int ext4_remove_orig_reference(const char *path, uint32_t name_off,
				      struct ext4_inode_ref *parent_ref,
				      struct ext4_inode_ref *child_ref)
{
	bool is_goal;
	int r;
	int len;
	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (!mp)
		return ENOENT;

	/*Set path*/
	path += name_off;

	len = ext4_path_check(path, &is_goal);

	/* Remove entry from parent directory */
	r = ext4_dir_remove_entry(parent_ref, path, len);
	if (r != EOK)
		goto Finish;

	if (ext4_inode_is_type(&mp->fs.sb, child_ref->inode,
			       EXT4_INODE_MODE_DIRECTORY)) {
		ext4_fs_inode_links_count_dec(parent_ref);
		parent_ref->dirty = true;
	}
Finish:
	return r;
}

int ext4_flink(const char *path, const char *hardlink_path)
{
	int r;
	ext4_file f;
	uint32_t name_off;
	bool child_loaded = false;
	uint32_t parent_inode, child_inode;
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	struct ext4_mountpoint *target_mp = ext4_get_mount(hardlink_path);
	struct ext4_inode_ref child_ref;

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	/* Will that happen? Anyway return EINVAL for such case. */
	if (mp != target_mp)
		return EINVAL;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDONLY, EXT4_DE_UNKNOWN,
			       &parent_inode, &name_off);
	if (r != EOK)
		goto Finish;

	child_inode = f.inode;
	ext4_fclose(&f);

	/*We have file to unlink. Load it.*/
	r = ext4_fs_get_inode_ref(&mp->fs, child_inode, &child_ref);
	if (r != EOK)
		goto Finish;

	child_loaded = true;

	/* Creating hardlink for directory is not allowed. */
	if (ext4_inode_is_type(&mp->fs.sb, child_ref.inode,
			       EXT4_INODE_MODE_DIRECTORY)) {
		r = EINVAL;
		goto Finish;
	}

	r = ext4_create_hardlink(hardlink_path, &child_ref, false);

Finish:
	if (child_loaded)
		ext4_fs_put_inode_ref(&child_ref);

	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;

}

int ext4_frename(const char *path, const char *new_path)
{
	int r;
	ext4_file f;
	uint32_t name_off;
	bool parent_loaded = false, child_loaded = false;
	uint32_t parent_inode, child_inode;
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	struct ext4_inode_ref child_ref, parent_ref;

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDONLY, EXT4_DE_UNKNOWN,
				&parent_inode, &name_off);
	if (r != EOK)
		goto Finish;

	child_inode = f.inode;
	ext4_fclose(&f);

	/*Load parent*/
	r = ext4_fs_get_inode_ref(&mp->fs, parent_inode, &parent_ref);
	if (r != EOK)
		goto Finish;

	parent_loaded = true;

	/*We have file to unlink. Load it.*/
	r = ext4_fs_get_inode_ref(&mp->fs, child_inode, &child_ref);
	if (r != EOK)
		goto Finish;

	child_loaded = true;

	r = ext4_create_hardlink(new_path, &child_ref, true);
	if (r != EOK)
		goto Finish;

	r = ext4_remove_orig_reference(path, name_off, &parent_ref, &child_ref);
	if (r != EOK)
		goto Finish;

Finish:
	if (parent_loaded)
		ext4_fs_put_inode_ref(&parent_ref);

	if (child_loaded)
		ext4_fs_put_inode_ref(&child_ref);

	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;

}

/****************************************************************************/

int ext4_get_sblock(const char *mount_point, struct ext4_sblock **sb)
{
	struct ext4_mountpoint *mp = ext4_get_mount(mount_point);

	if (!mp)
		return ENOENT;

	*sb = &mp->fs.sb;
	return EOK;
}

int ext4_cache_write_back(const char *path, bool on)
{
	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (!mp)
		return ENOENT;

	EXT4_MP_LOCK(mp);
	ext4_block_cache_write_back(mp->fs.bdev, on);
	EXT4_MP_UNLOCK(mp);
	return EOK;
}

int ext4_fremove(const char *path)
{
	ext4_file f;
	uint32_t parent_inode;
	uint32_t name_off;
	bool is_goal;
	int r;
	int len;
	struct ext4_inode_ref child;
	struct ext4_inode_ref parent;
	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN,
			       &parent_inode, &name_off);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	/*Load parent*/
	r = ext4_fs_get_inode_ref(&mp->fs, parent_inode, &parent);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	/*We have file to delete. Load it.*/
	r = ext4_fs_get_inode_ref(&mp->fs, f.inode, &child);
	if (r != EOK) {
		ext4_fs_put_inode_ref(&parent);
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}
	/* We do not allow opening files here. */
	if (ext4_inode_type(&mp->fs.sb, child.inode) ==
	    EXT4_INODE_MODE_DIRECTORY) {
		ext4_fs_put_inode_ref(&parent);
		ext4_fs_put_inode_ref(&child);
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	/*Link count will be zero, the inode should be freed. */
	if (ext4_inode_get_links_cnt(child.inode) == 1) {
		ext4_block_cache_write_back(mp->fs.bdev, 1);
		r = ext4_trunc_inode(mp, child.index, 0);
		if (r != EOK) {
			/* ext4edit patch 18 (upstream PR #160): end the
			 * write-back mode started above before returning -- as
			 * published this exit left the device's write-back count
			 * raised (the shape of patch 2). */
			ext4_block_cache_write_back(mp->fs.bdev, 0);
			ext4_fs_put_inode_ref(&parent);
			ext4_fs_put_inode_ref(&child);
			ext4_trans_abort(mp);
			EXT4_MP_UNLOCK(mp);
			return r;
		}
		ext4_block_cache_write_back(mp->fs.bdev, 0);
	}

	/*Set path*/
	path += name_off;

	len = ext4_path_check(path, &is_goal);

	/*Unlink from parent*/
	r = ext4_unlink(mp, &parent, &child, path, len);
	if (r != EOK)
		goto Finish;

	/*Link count is zero, the inode should be freed. */
	if (!ext4_inode_get_links_cnt(child.inode)) {
		ext4_inode_set_del_time(child.inode, -1L);

		r = ext4_fs_free_inode(&child);
		if (r != EOK)
			goto Finish;
	}

Finish:
	ext4_fs_put_inode_ref(&child);
	ext4_fs_put_inode_ref(&parent);

	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_fill_raw_inode(const char *path, uint32_t *ret_ino,
			struct ext4_inode *inode)
{
	int r;
	ext4_file f;
	struct ext4_inode_ref inode_ref;
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	uint32_t ino;

	if (!mp)
		return ENOENT;

	EXT4_MP_LOCK(mp);

	r = ext4_generic_open2(&f, path, O_RDONLY, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK) {
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	ino = f.inode;
	ext4_fclose(&f);

	/*Load parent*/
	r = ext4_fs_get_inode_ref(&mp->fs, ino, &inode_ref);
	if (r != EOK) {
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	memcpy(inode, inode_ref.inode, sizeof(struct ext4_inode));
	ext4_fs_put_inode_ref(&inode_ref);
	EXT4_MP_UNLOCK(mp);

	if (ret_ino)
		*ret_ino = ino;

	return r;
}

int ext4_fopen(ext4_file *f, const char *path, const char *flags)
{
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	int r;

	if (!mp)
		return ENOENT;

	EXT4_MP_LOCK(mp);

	ext4_block_cache_write_back(mp->fs.bdev, 1);
	r = ext4_generic_open(f, path, flags, true, 0, 0);
	ext4_block_cache_write_back(mp->fs.bdev, 0);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_fopen2(ext4_file *f, const char *path, int flags)
{
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	int r;
	int filetype;

	if (!mp)
		return ENOENT;

        filetype = EXT4_DE_REG_FILE;

	EXT4_MP_LOCK(mp);

	ext4_block_cache_write_back(mp->fs.bdev, 1);

	/* ext4edit patch 48 (upstream master 9b38aef): a creating open is    */
	/* one transaction, opened here, as ext4_generic_open opens it --     */
	/* ext4_generic_open2 no longer opens one of its own.                 */
	if (flags & O_CREAT) {
		/* Creating: the transaction around the open. */
		ext4_trans_start(mp);
	}

	r = ext4_generic_open2(f, path, flags, filetype, NULL, NULL);

	if (flags & O_CREAT) {
		/* Creating: committed on success, aborted on failure. */
		if (r == EOK) {
			/* Made: committed. */
			ext4_trans_stop(mp);
		} else {
			/* Refused or failed: whatever it began is dropped. */
			ext4_trans_abort(mp);
		}
	}

	ext4_block_cache_write_back(mp->fs.bdev, 0);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_fclose(ext4_file *f)
{
	/* ext4edit patch 36: a file never opened is refused, where  */
	/* upstream asserts.                                         */
	if (!f || !f->mp) {
		/* No file, or not open: refused, nothing touched. */
		return EINVAL;
	}

	f->mp = 0;
	f->flags = 0;
	f->inode = 0;
	f->fpos = f->fsize = 0;

	return EOK;
}

static int ext4_ftruncate_no_lock(ext4_file *f, uint64_t size)
{
	struct ext4_inode_ref ref;
	int r;


	r = ext4_fs_get_inode_ref(&f->mp->fs, f->inode, &ref);
	if (r != EOK) {
		EXT4_MP_UNLOCK(f->mp);
		return r;
	}

	/*Sync file size*/
	f->fsize = ext4_inode_get_size(&f->mp->fs.sb, ref.inode);
	if (f->fsize <= size) {
		r = EOK;
		goto Finish;
	}

	/*Start write back cache mode.*/
	r = ext4_block_cache_write_back(f->mp->fs.bdev, 1);
	if (r != EOK)
		goto Finish;

	r = ext4_trunc_inode(f->mp, ref.index, size);
	if (r != EOK)
		goto Finish;

	f->fsize = size;
	if (f->fpos > size)
		f->fpos = size;

	/*Stop write back cache mode*/
	ext4_block_cache_write_back(f->mp->fs.bdev, 0);

	if (r != EOK)
		goto Finish;

Finish:
	ext4_fs_put_inode_ref(&ref);
	return r;

}

int ext4_ftruncate(ext4_file *f, uint64_t size)
{
	int r;
	/* ext4edit patch 36: a file never opened is refused, where  */
	/* upstream asserts.                                         */
	if (!f || !f->mp) {
		/* No file, or not open: refused, nothing touched. */
		return EINVAL;
	}

	if (f->mp->fs.read_only)
		return EROFS;

	if (f->flags & O_RDONLY)
		return EPERM;

	EXT4_MP_LOCK(f->mp);

	ext4_trans_start(f->mp);
	r = ext4_ftruncate_no_lock(f, size);
	if (r != EOK)
		ext4_trans_abort(f->mp);
	else
		ext4_trans_stop(f->mp);

	EXT4_MP_UNLOCK(f->mp);
	return r;
}

int ext4_fread(ext4_file *f, void *buf, size_t size, size_t *rcnt)
{
	uint32_t unalg;
	uint32_t iblock_idx;
	uint32_t iblock_last;
	uint32_t block_size;

	ext4_fsblk_t fblock;
	ext4_fsblk_t fblock_start;
	uint32_t fblock_count;

	uint8_t *u8_buf = buf;
	int r;
	struct ext4_inode_ref ref;

	/* ext4edit patch 36: a file never opened is refused, where  */
	/* upstream asserts.                                         */
	if (!f || !f->mp) {
		/* No file, or not open: refused, nothing touched. */
		return EINVAL;
	}

	if (f->flags & O_WRONLY)
		return EPERM;

	if (!size)
		return EOK;

	EXT4_MP_LOCK(f->mp);

	struct ext4_fs *const fs = &f->mp->fs;
	struct ext4_sblock *const sb = &f->mp->fs.sb;

	if (rcnt)
		*rcnt = 0;

	r = ext4_fs_get_inode_ref(fs, f->inode, &ref);
	if (r != EOK) {
		EXT4_MP_UNLOCK(f->mp);
		return r;
	}

	/*Sync file size*/
	f->fsize = ext4_inode_get_size(sb, ref.inode);

	block_size = ext4_sb_get_block_size(sb);
	size = ((uint64_t)size > (f->fsize - f->fpos))
		? ((size_t)(f->fsize - f->fpos)) : size;

	iblock_idx = (uint32_t)((f->fpos) / block_size);
	iblock_last = (uint32_t)((f->fpos + size) / block_size);
	unalg = (f->fpos) % block_size;

	/*If the size of symlink is smaller than 60 bytes*/
	bool softlink;
	/* ext4edit patch 24 (upstream #108): a fast symlink may have an
	 * xattr block, which a block count of zero would not allow. */
	softlink = ext4_inode_is_fast_symlink(sb, ref.inode);
	if (softlink) {

		char *content = (char *)ref.inode->blocks;
		if (f->fpos < f->fsize) {
			size_t len = size;
			if (unalg + size > (uint32_t)f->fsize)
				len = (uint32_t)f->fsize - unalg;
			memcpy(buf, content + unalg, len);
			if (rcnt)
				*rcnt = len;

		}

		r = EOK;
		goto Finish;
	}

	/* ext4edit patch 7: an inline file's content comes out of the inode,
	 * not from blocks; `size` is already clamped to what is left. */
	if (ext4_inline_has(sb, ref.inode)) {
		uint8_t *inline_data;
		size_t inline_size;

		r = ext4_inline_read(&ref, &inline_data, &inline_size);
		if (r != EOK)
			goto Finish;

		if (f->fpos < inline_size) {
			size_t len = size;
			if (len > inline_size - f->fpos)
				len = inline_size - f->fpos;

			memcpy(buf, inline_data + f->fpos, len);
			f->fpos += len;
			if (rcnt)
				*rcnt = len;
		}

		free(inline_data);
		goto Finish;
	}

	if (unalg) {
		size_t len =  size;
		if (size > (block_size - unalg))
			len = block_size - unalg;

		r = ext4_fs_get_inode_dblk_idx(&ref, iblock_idx, &fblock, true);
		if (r != EOK)
			goto Finish;

		/* Do we get an unwritten range? */
		if (fblock != 0) {
			uint64_t off = fblock * block_size + unalg;
			r = ext4_block_readbytes(f->mp->fs.bdev, off, u8_buf, len);
			if (r != EOK)
				goto Finish;

		} else {
			/* Yes, we do. */
			memset(u8_buf, 0, len);
		}

		u8_buf += len;
		size -= len;
		f->fpos += len;

		if (rcnt)
			*rcnt += len;

		iblock_idx++;
	}

	/* ext4edit patch 15 (upstream PR #103): whole blocks are read one
	 * RUN at a time, and a HOLE (a block whose number is 0) reads as
	 * zeros.  As published the loop never tested for 0: a hole was read
	 * from physical block 0 of the filesystem, and the runs after one were
	 * mis-tracked, so a sparse file read back wrong. */
	fblock_start = 0;
	fblock_count = 0;
	while (size >= block_size) {
		r = ext4_fs_get_inode_dblk_idx(&ref, iblock_idx, &fblock, true);
		if (r != EOK)
			goto Finish;

		if (fblock == 0) {
			/* A hole: one block of zeros. */
			memset(u8_buf, 0, block_size);
			fblock_count = 1;
		} else {
			/* A run of blocks that follow each other on disk. */
			fblock_start = fblock;
			fblock_count = 1;
			while ((uint64_t)(fblock_count + 1) * block_size <= size) {
				r = ext4_fs_get_inode_dblk_idx(&ref,
						iblock_idx + fblock_count,
						&fblock, true);
				if (r != EOK)
					goto Finish;

				if (fblock != fblock_start + fblock_count)
					break;

				fblock_count++;
			}

			r = ext4_blocks_get_direct(f->mp->fs.bdev, u8_buf,
						   fblock_start, fblock_count);
			if (r != EOK)
				goto Finish;
		}

		iblock_idx += fblock_count;
		size -= block_size * fblock_count;
		u8_buf += block_size * fblock_count;
		f->fpos += block_size * fblock_count;

		if (rcnt)
			*rcnt += block_size * fblock_count;
	}

	if (size) {
		uint64_t off;
		r = ext4_fs_get_inode_dblk_idx(&ref, iblock_idx, &fblock, true);
		if (r != EOK)
			goto Finish;

		/* ext4edit patch 15: a trailing hole reads as zeros too. */
		if (fblock == 0) {
			memset(u8_buf, 0, size);
		} else {
			off = fblock * block_size;
			r = ext4_block_readbytes(f->mp->fs.bdev, off, u8_buf, size);
			if (r != EOK)
				goto Finish;
		}

		f->fpos += size;

		if (rcnt)
			*rcnt += size;
	}

Finish:
	ext4_fs_put_inode_ref(&ref);
	EXT4_MP_UNLOCK(f->mp);
	return r;
}

int ext4_fwrite(ext4_file *f, const void *buf, size_t size, size_t *wcnt)
{
	uint32_t unalg;
	uint32_t iblk_idx;
	uint32_t iblock_last;
	uint32_t ifile_blocks;
	uint32_t block_size;

	uint32_t fblock_count;
	ext4_fsblk_t fblk;
	ext4_fsblk_t fblock_start;

	struct ext4_inode_ref ref;
	const uint8_t *u8_buf = buf;
	int r, rr = EOK;

	/* ext4edit patch 36: a file never opened is refused, where  */
	/* upstream asserts.                                         */
	if (!f || !f->mp) {
		/* No file, or not open: refused, nothing touched. */
		return EINVAL;
	}

	if (f->mp->fs.read_only)
		return EROFS;

	if (f->flags & O_RDONLY)
		return EPERM;

	if (!size)
		return EOK;

	EXT4_MP_LOCK(f->mp);
	ext4_trans_start(f->mp);

	struct ext4_fs *const fs = &f->mp->fs;
	struct ext4_sblock *const sb = &f->mp->fs.sb;

	if (wcnt)
		*wcnt = 0;

	r = ext4_fs_get_inode_ref(fs, f->inode, &ref);
	if (r != EOK) {
		ext4_trans_abort(f->mp);
		EXT4_MP_UNLOCK(f->mp);
		return r;
	}

	/* ext4edit patch 7: a write goes to blocks, so an inline file gets
	 * its block first (its content and size are kept). */
	r = ext4_inline_convert(&ref);
	if (r != EOK)
		goto Finish;

	/*Sync file size*/
	f->fsize = ext4_inode_get_size(sb, ref.inode);
	block_size = ext4_sb_get_block_size(sb);

	iblock_last = (uint32_t)((f->fpos + size) / block_size);
	iblk_idx = (uint32_t)(f->fpos / block_size);
	ifile_blocks = (uint32_t)((f->fsize + block_size - 1) / block_size);

	unalg = (f->fpos) % block_size;

	if (unalg) {
		size_t len =  size;
		uint64_t off;
		if (size > (block_size - unalg))
			len = block_size - unalg;

		r = ext4_fs_init_inode_dblk_idx(&ref, iblk_idx, &fblk);
		if (r != EOK)
			goto Finish;

		off = fblk * block_size + unalg;
		r = ext4_block_writebytes(f->mp->fs.bdev, off, u8_buf, len);
		if (r != EOK)
			goto Finish;

		u8_buf += len;
		size -= len;
		f->fpos += len;

		if (wcnt)
			*wcnt += len;

		iblk_idx++;
	}

	/*Start write back cache mode.*/
	r = ext4_block_cache_write_back(f->mp->fs.bdev, 1);
	if (r != EOK)
		goto Finish;

	fblock_start = 0;
	fblock_count = 0;
	while (size >= block_size) {

		while (iblk_idx < iblock_last) {
			if (iblk_idx < ifile_blocks) {
				r = ext4_fs_init_inode_dblk_idx(&ref, iblk_idx,
								&fblk);
				if (r != EOK) {
					/* ext4edit patch 2: this exit, too,
					 * must stop write back cache mode. */
					ext4_block_cache_write_back(
					    f->mp->fs.bdev, 0);
					goto Finish;
				}
			} else {
				rr = ext4_fs_append_inode_dblk(&ref, &fblk,
							       &iblk_idx);
				if (rr != EOK) {
					/* Unable to append more blocks. But
					 * some block might be allocated already
					 * */
					break;
				}
			}

			iblk_idx++;

			if (!fblock_start) {
				fblock_start = fblk;
			}

			if ((fblock_start + fblock_count) != fblk)
				break;

			fblock_count++;
		}

		r = ext4_blocks_set_direct(f->mp->fs.bdev, u8_buf, fblock_start,
					   fblock_count);
		if (r != EOK)
			break;

		size -= block_size * fblock_count;
		u8_buf += block_size * fblock_count;
		f->fpos += block_size * fblock_count;

		if (wcnt)
			*wcnt += block_size * fblock_count;

		fblock_start = fblk;
		fblock_count = 1;

		if (rr != EOK) {
			/*ext4_fs_append_inode_block has failed and no
			 * more blocks might be written. But node size
			 * should be updated.*/
			r = rr;
			/* ext4edit patch 2: stop write back cache mode on
			 * this exit too.  As published, the goto skipped the
			 * matching stop below and left the device's
			 * cache_write_back count one too high for good. */
			ext4_block_cache_write_back(f->mp->fs.bdev, 0);
			goto out_fsize;
		}
	}

	/*Stop write back cache mode*/
	ext4_block_cache_write_back(f->mp->fs.bdev, 0);

	if (r != EOK)
		goto Finish;

	if (size) {
		uint64_t off;
		if (iblk_idx < ifile_blocks) {
			r = ext4_fs_init_inode_dblk_idx(&ref, iblk_idx, &fblk);
			if (r != EOK)
				goto Finish;
		} else {
			r = ext4_fs_append_inode_dblk(&ref, &fblk, &iblk_idx);
			if (r != EOK)
				/*Node size sholud be updated.*/
				goto out_fsize;
		}

		off = fblk * block_size;
		r = ext4_block_writebytes(f->mp->fs.bdev, off, u8_buf, size);
		if (r != EOK)
			goto Finish;

		f->fpos += size;

		if (wcnt)
			*wcnt += size;
	}

out_fsize:
	if (f->fpos > f->fsize) {
		f->fsize = f->fpos;
		ext4_inode_set_size(ref.inode, f->fsize);
		ref.dirty = true;
	}

Finish:
	/* ext4edit patch 20 (upstream PR #150): the FIRST failure is the
	 * one returned -- as published the put's result was assigned over
	 * r, so a write that failed (an I/O error, a full filesystem)
	 * returned EOK and was committed. */
	{
		int r_put = ext4_fs_put_inode_ref(&ref);
		if (r == EOK)
			r = r_put;
	}

	if (r != EOK)
		ext4_trans_abort(f->mp);
	else
		ext4_trans_stop(f->mp);

	EXT4_MP_UNLOCK(f->mp);
	return r;
}

int ext4_fseek(ext4_file *f, uint64_t offset, uint32_t origin)
{
	switch (origin) {
	case SEEK_SET:
		if (offset > f->fsize)
			return EINVAL;

		f->fpos = offset;
		return EOK;
	case SEEK_CUR:
		if ((offset + f->fpos) > f->fsize)
			return EINVAL;

		f->fpos += offset;
		return EOK;
	case SEEK_END:
		if (offset > f->fsize)
			return EINVAL;

		f->fpos = f->fsize - offset;
		return EOK;
	}
	return EINVAL;
}

uint64_t ext4_ftell(ext4_file *f)
{
	return f->fpos;
}

uint64_t ext4_fsize(ext4_file *f)
{
	return f->fsize;
}

int ext4_chmod(const char *path, uint32_t mode)
{
	int r;
	uint32_t ino, orig_mode;
	ext4_file f;
	struct ext4_sblock *sb;
	struct ext4_inode_ref inode_ref;
	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}
	ino = f.inode;
	sb = &mp->fs.sb;
	ext4_fclose(&f);
	r = ext4_fs_get_inode_ref(&mp->fs, ino, &inode_ref);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	orig_mode = ext4_inode_get_mode(sb, inode_ref.inode);
	orig_mode &= ~0xFFF;
	orig_mode |= mode & 0xFFF;
	ext4_inode_set_mode(sb, inode_ref.inode, orig_mode);
	inode_ref.dirty = true;

	r = ext4_fs_put_inode_ref(&inode_ref);
	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_chown(const char *path, uint32_t uid, uint32_t gid)
{
	int r;
	ext4_file f;
	uint32_t ino;
	struct ext4_inode_ref inode_ref;
	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}
	ino = f.inode;
	ext4_fclose(&f);
	r = ext4_fs_get_inode_ref(&mp->fs, ino, &inode_ref);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	ext4_inode_set_uid(inode_ref.inode, uid);
	ext4_inode_set_gid(inode_ref.inode, gid);
	inode_ref.dirty = true;

	r = ext4_fs_put_inode_ref(&inode_ref);
	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_file_set_atime(const char *path, uint32_t atime)
{
	int r;
	ext4_file f;
	uint32_t ino;
	struct ext4_inode_ref inode_ref;
	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}
	ino = f.inode;
	ext4_fclose(&f);
	r = ext4_fs_get_inode_ref(&mp->fs, ino, &inode_ref);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	ext4_inode_set_access_time(inode_ref.inode, atime);
	inode_ref.dirty = true;

	r = ext4_fs_put_inode_ref(&inode_ref);
	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_file_set_mtime(const char *path, uint32_t mtime)
{
	int r;
	ext4_file f;
	uint32_t ino;
	struct ext4_inode_ref inode_ref;
	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}
	ino = f.inode;
	ext4_fclose(&f);
	r = ext4_fs_get_inode_ref(&mp->fs, ino, &inode_ref);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	ext4_inode_set_modif_time(inode_ref.inode, mtime);
	inode_ref.dirty = true;

	r = ext4_fs_put_inode_ref(&inode_ref);
	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_file_set_ctime(const char *path, uint32_t ctime)
{
	int r;
	ext4_file f;
	uint32_t ino;
	struct ext4_inode_ref inode_ref;
	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}
	ino = f.inode;
	ext4_fclose(&f);
	r = ext4_fs_get_inode_ref(&mp->fs, ino, &inode_ref);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	ext4_inode_set_change_inode_time(inode_ref.inode, ctime);
	inode_ref.dirty = true;

	r = ext4_fs_put_inode_ref(&inode_ref);
	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;
}

static int ext4_fsymlink_set(ext4_file *f, const void *buf, uint32_t size)
{
	struct ext4_inode_ref ref;
	uint32_t sblock;
	ext4_fsblk_t fblock;
	uint32_t block_size;
	int r;

	/* ext4edit patch 36: a file never opened is refused, where  */
	/* upstream asserts.                                         */
	if (!f || !f->mp) {
		/* No file, or not open: refused, nothing touched. */
		return EINVAL;
	}

	if (!size)
		return EOK;

	r = ext4_fs_get_inode_ref(&f->mp->fs, f->inode, &ref);
	if (r != EOK)
		return r;

	/*Sync file size*/
	block_size = ext4_sb_get_block_size(&f->mp->fs.sb);
	/* ext4edit patch 12: a target as long as a block is refused too,
	 * not only a longer one -- e2fsck calls a link whose size is a
	 * block or more invalid. */
	if (size >= block_size) {
		r = EINVAL;
		goto Finish;
	}
	r = ext4_ftruncate_no_lock(f, 0);
	if (r != EOK)
		goto Finish;

	/*Start write back cache mode.*/
	r = ext4_block_cache_write_back(f->mp->fs.bdev, 1);
	if (r != EOK)
		goto Finish;

	/*If the size of symlink is smaller than 60 bytes*/
	if (size < sizeof(ref.inode->blocks)) {
		memset(ref.inode->blocks, 0, sizeof(ref.inode->blocks));
		memcpy(ref.inode->blocks, buf, size);
		ext4_inode_clear_flag(ref.inode, EXT4_INODE_FLAG_EXTENTS);
	} else {
		uint8_t *block_buf;

		ext4_fs_inode_blocks_init(&f->mp->fs, &ref);
		r = ext4_fs_append_inode_dblk(&ref, &fblock, &sblock);
		/* ext4edit patch 12: every failure from here on leaves through
		 * Stop_wb, so the write-back mode started above is ended (as
		 * published they jumped past it, leaving the device's
		 * write-back count raised -- the shape of patch 2). */
		if (r != EOK)
			goto Stop_wb;

		/* ext4edit patch 12: as published the text was written with
		 * ext4_block_writebytes at byte 0 of the FILESYSTEM -- over the
		 * boot area and, past 1 KiB, the superblock -- while the block
		 * just allocated kept what it held, so every link of 60 bytes
		 * or more read back empty and e2fsck called it invalid.  The
		 * text now goes into that block, whole, with the rest of the
		 * block zeroed (e2fsck requires the text to end at i_size).
		 * It is written DIRECTLY to the device, as ext4_fwrite writes a
		 * file's data, and NOT through the block cache: ext4_fread --
		 * which ext4_readlink uses -- reads data blocks directly, so a
		 * block left dirty in the cache would read back as what the
		 * disk held until the cache flushed (a large cache shows it:
		 * a link copied in the same session came back empty). */
		block_buf = calloc(1, block_size);
		if (!block_buf) {
			r = ENOMEM;
			goto Stop_wb;
		}

		memcpy(block_buf, buf, size);
		r = ext4_blocks_set_direct(f->mp->fs.bdev, block_buf, fblock, 1);
		free(block_buf);
	}

Stop_wb:
	/*Stop write back cache mode*/
	ext4_block_cache_write_back(f->mp->fs.bdev, 0);

	if (r != EOK)
		goto Finish;

	ext4_inode_set_size(ref.inode, size);
	ref.dirty = true;

	f->fsize = size;
	if (f->fpos > size)
		f->fpos = size;

Finish:
	ext4_fs_put_inode_ref(&ref);
	return r;
}

int ext4_fsymlink(const char *target, const char *path)
{
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	int r;
	ext4_file f;
	int filetype;

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	/* ext4edit patch 12: a target that cannot fit is refused HERE,
	 * before anything is made -- ext4_fsymlink_set's own test comes
	 * after ext4_generic_open2 has created the inode and its entry,
	 * and on a filesystem with no journal no abort takes those back,
	 * so a refused link would be left behind empty, and e2fsck calls
	 * an empty link invalid. */
	if (strlen(target) >= ext4_sb_get_block_size(&mp->fs.sb))
		return EINVAL;

	filetype = EXT4_DE_SYMLINK;

	/* ext4edit patch 48 (upstream master 9b38aef): ONE transaction      */
	/* covers the inode, its entry and the target, now that the creating */
	/* open below opens none of its own (before, its stop committed this */
	/* one and the target was written with none).  It sits inside the   */
	/* write-back window, as master orders it, so the commit comes        */
	/* before write-back ends.                                            */
	EXT4_MP_LOCK(mp);
	ext4_block_cache_write_back(mp->fs.bdev, 1);
	ext4_trans_start(mp);

	/* ext4edit patch 12: a name already taken is refused with EEXIST,
	 * as symlink(2) refuses it.  ext4_generic_open2 has no O_EXCL: as
	 * published, an existing LINK was opened and its text replaced,
	 * and an existing file or directory came back as ENOENT ("no such
	 * file") because its type differed.  An open of any type that
	 * succeeds is the test; it creates nothing. */
	r = ext4_generic_open2(&f, path, O_RDONLY, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r == EOK) {
		r = EEXIST;
		goto Finish;
	}

	r = ext4_generic_open2(&f, path, O_RDWR|O_CREAT, filetype, NULL, NULL);
	if (r == EOK)
		r = ext4_fsymlink_set(&f, target, strlen(target));
	else
		goto Finish;

	ext4_fclose(&f);

Finish:
	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	/* ext4edit patch 48: write-back ends after the transaction does. */
	ext4_block_cache_write_back(mp->fs.bdev, 0);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_readlink(const char *path, char *buf, size_t bufsize, size_t *rcnt)
{
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	int r;
	ext4_file f;
	int filetype;

	if (!mp)
		return ENOENT;

	if (!buf)
		return EINVAL;

	filetype = EXT4_DE_SYMLINK;

	EXT4_MP_LOCK(mp);
	ext4_block_cache_write_back(mp->fs.bdev, 1);
	r = ext4_generic_open2(&f, path, O_RDONLY, filetype, NULL, NULL);
	if (r == EOK)
		r = ext4_fread(&f, buf, bufsize, rcnt);
	else
		goto Finish;

	ext4_fclose(&f);

Finish:
	ext4_block_cache_write_back(mp->fs.bdev, 0);
	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_setxattr(const char *path, const char *name, size_t name_len,
		  const void *data, size_t data_size, bool replace)
{
	int r = EOK;
	ext4_file f;
	uint32_t inode;
	uint8_t name_index;
	const char *dissected_name = NULL;
	size_t dissected_len = 0;
	struct ext4_xattr_ref xattr_ref;
	struct ext4_inode_ref inode_ref;
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	dissected_name = ext4_extract_xattr_name(name, name_len,
				&name_index, &dissected_len);
	if (!dissected_len)
		return EINVAL;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK)
		goto Finish;
	inode = f.inode;
	ext4_fclose(&f);

	r = ext4_fs_get_inode_ref(&mp->fs, inode, &inode_ref);
	if (r != EOK)
		goto Finish;

	r = ext4_fs_get_xattr_ref(&mp->fs, &inode_ref, &xattr_ref);
	if (r != EOK) {
		ext4_fs_put_inode_ref(&inode_ref);
		goto Finish;
	}

	r = ext4_fs_set_xattr(&xattr_ref, name_index, dissected_name,
			dissected_len, data, data_size, replace);

	ext4_fs_put_xattr_ref(&xattr_ref);
	ext4_fs_put_inode_ref(&inode_ref);
Finish:
	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_getxattr(const char *path, const char *name, size_t name_len,
		  void *buf, size_t buf_size, size_t *data_size)
{
	int r = EOK;
	ext4_file f;
	uint32_t inode;
	uint8_t name_index;
	const char *dissected_name = NULL;
	size_t dissected_len = 0;
	struct ext4_xattr_ref xattr_ref;
	struct ext4_inode_ref inode_ref;
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	if (!mp)
		return ENOENT;

	dissected_name = ext4_extract_xattr_name(name, name_len,
				&name_index, &dissected_len);
	if (!dissected_len)
		return EINVAL;

	EXT4_MP_LOCK(mp);
	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK)
		goto Finish;
	inode = f.inode;
	ext4_fclose(&f);

	r = ext4_fs_get_inode_ref(&mp->fs, inode, &inode_ref);
	if (r != EOK)
		goto Finish;

	r = ext4_fs_get_xattr_ref(&mp->fs, &inode_ref, &xattr_ref);
	if (r != EOK) {
		ext4_fs_put_inode_ref(&inode_ref);
		goto Finish;
	}

	r = ext4_fs_get_xattr(&xattr_ref, name_index, dissected_name,
				dissected_len, buf, buf_size, data_size);

	ext4_fs_put_xattr_ref(&xattr_ref);
	ext4_fs_put_inode_ref(&inode_ref);
Finish:
	EXT4_MP_UNLOCK(mp);
	return r;
}

struct ext4_listxattr_iterator {
	char *list;
	char *list_ptr;
	size_t size;
	size_t ret_size;
	bool list_too_small;
	bool get_required_size;
};

static int ext4_iterate_ea_list(struct ext4_xattr_ref *ref,
				struct ext4_xattr_item *item)
{
	struct ext4_listxattr_iterator *lxi;
	lxi = ref->iter_arg;
	if (!lxi->get_required_size) {
		size_t plen;
		const char *prefix;
		prefix = ext4_get_xattr_name_prefix(item->name_index, &plen);
		if (lxi->ret_size + plen + item->name_len + 1 > lxi->size) {
			lxi->list_too_small = 1;
			return EXT4_XATTR_ITERATE_STOP;
		}
		if (prefix) {
			memcpy(lxi->list_ptr, prefix, plen);
			lxi->list_ptr += plen;
			lxi->ret_size += plen;
		}
		memcpy(lxi->list_ptr, item->name, item->name_len);
		lxi->list_ptr[item->name_len] = 0;
		lxi->list_ptr += item->name_len + 1;
	}
	lxi->ret_size += item->name_len + 1;
	return EXT4_XATTR_ITERATE_CONT;
}

int ext4_listxattr(const char *path, char *list, size_t size, size_t *ret_size)
{
	int r = EOK;
	ext4_file f;
	uint32_t inode;
	struct ext4_xattr_ref xattr_ref;
	struct ext4_inode_ref inode_ref;
	struct ext4_listxattr_iterator lxi;
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	if (!mp)
		return ENOENT;

	lxi.list = list;
	lxi.list_ptr = list;
	lxi.size = size;
	lxi.ret_size = 0;
	lxi.list_too_small = false;
	lxi.get_required_size = (!size) ? true : false;

	EXT4_MP_LOCK(mp);
	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK)
		goto Finish;
	inode = f.inode;
	ext4_fclose(&f);

	r = ext4_fs_get_inode_ref(&mp->fs, inode, &inode_ref);
	if (r != EOK)
		goto Finish;

	r = ext4_fs_get_xattr_ref(&mp->fs, &inode_ref, &xattr_ref);
	if (r != EOK) {
		ext4_fs_put_inode_ref(&inode_ref);
		goto Finish;
	}

	xattr_ref.iter_arg = &lxi;
	ext4_fs_xattr_iterate(&xattr_ref, ext4_iterate_ea_list);
	if (lxi.list_too_small)
		r = ERANGE;

	if (r == EOK) {
		if (ret_size)
			*ret_size = lxi.ret_size;

	}
	ext4_fs_put_xattr_ref(&xattr_ref);
	ext4_fs_put_inode_ref(&inode_ref);
Finish:
	EXT4_MP_UNLOCK(mp);
	return r;

}

int ext4_removexattr(const char *path, const char *name, size_t name_len)
{
	int r = EOK;
	ext4_file f;
	uint32_t inode;
	uint8_t name_index;
	const char *dissected_name = NULL;
	size_t dissected_len = 0;
	struct ext4_xattr_ref xattr_ref;
	struct ext4_inode_ref inode_ref;
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	dissected_name = ext4_extract_xattr_name(name, name_len,
						&name_index, &dissected_len);
	if (!dissected_len)
		return EINVAL;

	EXT4_MP_LOCK(mp);
	ext4_trans_start(mp);

	r = ext4_generic_open2(&f, path, O_RDWR, EXT4_DE_UNKNOWN, NULL, NULL);
	if (r != EOK)
		goto Finish;
	inode = f.inode;
	ext4_fclose(&f);

	r = ext4_fs_get_inode_ref(&mp->fs, inode, &inode_ref);
	if (r != EOK)
		goto Finish;

	r = ext4_fs_get_xattr_ref(&mp->fs, &inode_ref, &xattr_ref);
	if (r != EOK) {
		ext4_fs_put_inode_ref(&inode_ref);
		goto Finish;
	}

	r = ext4_fs_remove_xattr(&xattr_ref, name_index, dissected_name,
				dissected_len);

	ext4_fs_put_xattr_ref(&xattr_ref);
	ext4_fs_put_inode_ref(&inode_ref);
Finish:
	if (r != EOK)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;

}

/*********************************DIRECTORY OPERATION************************/

int ext4_dir_rm(const char *path)
{
	int r;
	int len;
	ext4_file f;

	struct ext4_mountpoint *mp = ext4_get_mount(path);
	struct ext4_inode_ref act;
	struct ext4_inode_ref child;
	struct ext4_dir_iter it;

	uint32_t name_off;
	uint32_t inode_up;
	uint32_t inode_current;
	uint32_t depth = 1;

	bool has_children;
	bool is_goal;
	bool dir_end;

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	EXT4_MP_LOCK(mp);

	struct ext4_fs *const fs = &mp->fs;

	/*Check if exist.*/
	r = ext4_generic_open(&f, path, "r", false, &inode_up, &name_off);
	if (r != EOK) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	path += name_off;
	len = ext4_path_check(path, &is_goal);

	inode_current = f.inode;

	ext4_block_cache_write_back(mp->fs.bdev, 1);

	do {

		uint64_t act_curr_pos = 0;
		has_children = false;
		dir_end = false;

		while (r == EOK && !has_children && !dir_end) {

			/*Load directory node.*/
			r = ext4_fs_get_inode_ref(fs, inode_current, &act);
			if (r != EOK) {
				break;
			}

			/*Initialize iterator.*/
			r = ext4_dir_iterator_init(&it, &act, act_curr_pos);
			if (r != EOK) {
				ext4_fs_put_inode_ref(&act);
				break;
			}

			if (!it.curr) {
				dir_end = true;
				goto End;
			}

			ext4_trans_start(mp);

			/*Get up directory inode when ".." entry*/
			if ((it.curr->name_len == 2) &&
			    ext4_is_dots(it.curr->name, it.curr->name_len)) {
				inode_up = ext4_dir_en_get_inode(it.curr);
			}

			/*If directory or file entry,  but not "." ".." entry*/
			if (!ext4_is_dots(it.curr->name, it.curr->name_len)) {

				/*Get child inode reference do unlink
				 * directory/file.*/
				uint32_t cinode;
				uint32_t inode_type;
				cinode = ext4_dir_en_get_inode(it.curr);
				r = ext4_fs_get_inode_ref(fs, cinode, &child);
				if (r != EOK)
					goto End;

				/*If directory with no leaf children*/
				r = ext4_has_children(&has_children, &child);
				if (r != EOK) {
					ext4_fs_put_inode_ref(&child);
					goto End;
				}

				if (has_children) {
					/*Has directory children. Go into this
					 * directory.*/
					inode_up = inode_current;
					inode_current = cinode;
					depth++;
					ext4_fs_put_inode_ref(&child);
					goto End;
				}
				inode_type = ext4_inode_type(&mp->fs.sb,
						child.inode);

				/* ext4edit patch 13 (upstream PR #148): as
				 * published every non-directory was truncated
				 * and freed here whatever its link count, so a
				 * file in the tree with another name OUTSIDE it
				 * was left as a freed inode under that name.  Now
				 * as ext4_fremove does it: a file is truncated
				 * only when this is its last name, ext4_unlink
				 * takes one link away, and the inode is freed
				 * only when none is left. */
				if (inode_type != EXT4_INODE_MODE_DIRECTORY) {
					if (ext4_inode_get_links_cnt(child.inode) == 1)
						r = ext4_trunc_inode(mp, child.index, 0);
					else
						r = EOK;
				} else
					r = ext4_trunc_dir(mp, &act, &child);

				if (r != EOK) {
					ext4_fs_put_inode_ref(&child);
					goto End;
				}

				/*No children in child directory or file. Just
				 * unlink.*/
				r = ext4_unlink(f.mp, &act, &child,
						(char *)it.curr->name,
						it.curr->name_len);
				if (r != EOK) {
					ext4_fs_put_inode_ref(&child);
					goto End;
				}

				/* ext4edit patch 13: a directory goes
				 * whole; a file only when it has no name left. */
				if (inode_type == EXT4_INODE_MODE_DIRECTORY ||
				    !ext4_inode_get_links_cnt(child.inode)) {
					ext4_inode_set_del_time(child.inode, -1L);
					ext4_inode_set_links_cnt(child.inode, 0);
					child.dirty = true;

					r = ext4_fs_free_inode(&child);
					if (r != EOK) {
						ext4_fs_put_inode_ref(&child);
						goto End;
					}
				} else
					child.dirty = true;

				r = ext4_fs_put_inode_ref(&child);
				if (r != EOK)
					goto End;

			}

			r = ext4_dir_iterator_next(&it);
			if (r != EOK)
				goto End;

			act_curr_pos = it.curr_off;
End:
			ext4_dir_iterator_fini(&it);
			if (r == EOK)
				r = ext4_fs_put_inode_ref(&act);
			else
				ext4_fs_put_inode_ref(&act);

			if (r != EOK)
				ext4_trans_abort(mp);
			else
				ext4_trans_stop(mp);
		}

		if (dir_end) {
			/*Directory iterator reached last entry*/
			depth--;
			if (depth)
				inode_current = inode_up;

		}

		if (r != EOK)
			break;

	} while (depth);

	/*Last unlink*/
	if (r == EOK && !depth) {
		/*Load parent.*/
		struct ext4_inode_ref parent;
		r = ext4_fs_get_inode_ref(&f.mp->fs, inode_up,
				&parent);
		if (r != EOK)
			goto Finish;
		r = ext4_fs_get_inode_ref(&f.mp->fs, inode_current,
				&act);
		if (r != EOK) {
			/* ext4edit patch 17 (upstream PR #158): release the
			 * PARENT, which was loaded -- as published this put
			 * `act`, which the failed call never filled, and left
			 * the parent held. */
			ext4_fs_put_inode_ref(&parent);
			goto Finish;
		}

		ext4_trans_start(mp);

		/* In this place all directories should be
		 * unlinked.
		 * Last unlink from root of current directory*/
		r = ext4_unlink(f.mp, &parent, &act,
				(char *)path, len);
		if (r != EOK) {
			ext4_fs_put_inode_ref(&parent);
			ext4_fs_put_inode_ref(&act);
			goto Finish;
		}

		if (ext4_inode_get_links_cnt(act.inode) == 2) {
			ext4_inode_set_del_time(act.inode, -1L);
			ext4_inode_set_links_cnt(act.inode, 0);
			act.dirty = true;
			/*Truncate*/
			r = ext4_trunc_dir(mp, &parent, &act);
			if (r != EOK) {
				ext4_fs_put_inode_ref(&parent);
				ext4_fs_put_inode_ref(&act);
				goto Finish;
			}

			r = ext4_fs_free_inode(&act);
			if (r != EOK) {
				ext4_fs_put_inode_ref(&parent);
				ext4_fs_put_inode_ref(&act);
				goto Finish;
			}
		}

		r = ext4_fs_put_inode_ref(&parent);
		if (r != EOK)
			goto Finish;

		r = ext4_fs_put_inode_ref(&act);
	Finish:
		if (r != EOK)
			ext4_trans_abort(mp);
		else
			ext4_trans_stop(mp);
	}

	ext4_block_cache_write_back(mp->fs.bdev, 0);
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_dir_mk(const char *path)
{
	int r;
	ext4_file f;

	struct ext4_mountpoint *mp = ext4_get_mount(path);

	if (!mp)
		return ENOENT;

	if (mp->fs.read_only)
		return EROFS;

	EXT4_MP_LOCK(mp);

	/* ext4edit patch 48 (upstream master 9b38aef): no transaction of    */
	/* this function's own -- the creating open below is one transaction */
	/* by itself (ext4_generic_open), and the existence check writes      */
	/* nothing.                                                           */

	/*Check if exist.*/
	r = ext4_generic_open(&f, path, "r", false, 0, 0);
	if (r == EOK) {
		/*Directory already created*/
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	/*Create new dir*/
	r = ext4_generic_open(&f, path, "w", false, 0, 0);

	/* Made, or the creating open's failure (its transaction aborted). */
	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_dir_open(ext4_dir *d, const char *path)
{
	struct ext4_mountpoint *mp = ext4_get_mount(path);
	int r;

	if (!mp)
		return ENOENT;

	EXT4_MP_LOCK(mp);
	r = ext4_generic_open(&d->f, path, "r", false, 0, 0);
	d->next_off = 0;
	/* ext4edit patch 35: a fresh walk has no failure yet. */
	d->last_err = EOK;
	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_dir_close(ext4_dir *d)
{
    return ext4_fclose(&d->f);
}

const ext4_direntry *ext4_dir_entry_next(ext4_dir *d)
{
#define EXT4_DIR_ENTRY_OFFSET_TERM (uint64_t)(-1)

	int r;
	ext4_direntry *de = 0;
	struct ext4_inode_ref dir;
	struct ext4_dir_iter it;
	uint16_t name_len;	// ext4edit patch 39: the entry's name length, as the iterator checked it

	EXT4_MP_LOCK(d->f.mp);

	if (d->next_off == EXT4_DIR_ENTRY_OFFSET_TERM) {
		EXT4_MP_UNLOCK(d->f.mp);
		return 0;
	}

	/* ext4edit patch 35: a failure to load the directory's inode or to
	 * read the block at the walk's position ends the walk and is kept
	 * in d->last_err, so the caller can tell it from the end. */
	r = ext4_fs_get_inode_ref(&d->f.mp->fs, d->f.inode, &dir);
	if (r != EOK) {
		d->last_err = r;
		d->next_off = EXT4_DIR_ENTRY_OFFSET_TERM;
		goto Finish;
	}

	r = ext4_dir_iterator_init(&it, &dir, d->next_off);
	if (r != EOK) {
		d->last_err = r;
		d->next_off = EXT4_DIR_ENTRY_OFFSET_TERM;
		ext4_fs_put_inode_ref(&dir);
		goto Finish;
	}

	/* ext4edit patch 28 (upstream #112): the iterator succeeds with
	 * no entry when the position is at or past the directory's size --
	 * a directory whose size field is 0, say -- and the copy below read
	 * through a NULL pointer, crashing the program.  No entry is the end
	 * of the listing. */
	if (!it.curr) {
		d->next_off = EXT4_DIR_ENTRY_OFFSET_TERM;
		ext4_dir_iterator_fini(&it);
		ext4_fs_put_inode_ref(&dir);
		goto Finish;
	}

	/* ext4edit patch 39: the entry is copied by its CONTENT length -- its */
	/* 8-byte header and its name -- never by the size of ext4_direntry.   */
	/* That size is 263 bytes (a 255-byte name), so copying it whole from  */
	/* an entry that starts in a block's last 262 bytes read past the end  */
	/* of the block's buffer: a heap over-read that crashed the program    */
	/* only when the buffer happened to end at an unmapped page.  The      */
	/* name length is taken through ext4_dir_en_get_name_len, as the       */
	/* iterator's check took it (ext4_dir_iterator_set has already proved  */
	/* the header and the name lie inside the entry and the block), and    */
	/* held to the 255 bytes de.name holds.  The rest of d->de is zeroed,  */
	/* so no byte of an earlier entry, or of the block's slack, is left    */
	/* in it.  The record length still arrives as de.entry_length.         */
	name_len = ext4_dir_en_get_name_len(&d->f.mp->fs.sb, it.curr);
	if (name_len > sizeof(d->de.name)) {
		/* Longer than any name ext4 allows: only what de.name holds. */
		name_len = sizeof(d->de.name);
	}

	/* The cleared entry, then the header and the name over it. */
	memset(&d->de, 0, sizeof(ext4_direntry));
	memcpy(&d->de, it.curr, 8 + (size_t)name_len);
	de = &d->de;

	/* ext4edit patch 35: as published the result of advancing was
	 * ignored, so a block that could not be read ended the walk as if
	 * it were the directory's end.  Now its error is kept and the walk
	 * stops; the entry already copied above is still returned, and the
	 * caller sees the error once its loop has ended. */
	r = ext4_dir_iterator_next(&it);
	if (r != EOK)
		d->last_err = r;

	d->next_off = (r == EOK && it.curr) ? it.curr_off : EXT4_DIR_ENTRY_OFFSET_TERM;

	ext4_dir_iterator_fini(&it);
	ext4_fs_put_inode_ref(&dir);

Finish:
	EXT4_MP_UNLOCK(d->f.mp);
	return de;
}

void ext4_dir_entry_rewind(ext4_dir *d)
{
	d->next_off = 0;
}

/**
 * @}
 */
