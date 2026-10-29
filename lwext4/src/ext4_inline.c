/*----------------------------------------------------------------------------*/
/* ext4_inline.c                                                              */
/*                                                                            */
/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/* Copyright (c) 2026 B. C. Services                                          */
/*                                                                            */
/* ext4edit patch 7: the ext4 inline_data feature for lwext4 -- reading       */
/* inline content, presenting an inline directory as a directory block, and   */
/* converting an inline inode to a block-backed one before any write.  The    */
/* header says what each function is for and what the on-disk forms are;      */
/* this file says how.                                                        */
/*                                                                            */
/* Two decisions shape it.  First, inline content is never WRITTEN inline:    */
/* a write converts first and then runs the library's ordinary block code,    */
/* so every write path -- file writes, truncates, entry adds and removals,    */
/* renames -- needs one call and no new logic, and the result is a plain      */
/* ext4 object the kernel and e2fsck accept (the kernel converts the same way */
/* when inline content outgrows the inode -- its source is not in this tree,  */
/* so that is as found).  The cost is that a single byte written to a 10-byte */
/* file costs it a block, which is what the kernel would also do past the     */
/* inline limit.  The conversion loses nothing: the new block is allocated    */
/* and written BEFORE the inline storage is dropped, so every step that can   */
/* fail happens while the inode is still whole.  Second, an inline            */
/* directory is READ by laying it out as the block it would become, in        */
/* memory, so the lookup, listing and "has children" code run over it         */
/* unchanged -- there is one layout routine, used both for reading and for    */
/* the conversion's write, so the two cannot disagree.                        */
/*----------------------------------------------------------------------------*/

/* lwext4's base: its build configuration, on-disk types, byte-order and   */
/* misc helpers (to_le32), error codes, and debug macros.                  */
#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_errno.h"
#include "ext4_debug.h"

/* This module's own header, then the lwext4 layers it works through:      */
/* inodes, the superblock, directory entries and checksums, extended       */
/* attributes, transactions and the block cache, the block allocator and   */
/* the extent tree's on-disk accessors (the conversion's block).           */
#include "ext4_inline.h"
#include "ext4_fs.h"
#include "ext4_inode.h"
#include "ext4_super.h"
#include "ext4_dir.h"
#include "ext4_xattr.h"
#include "ext4_trans.h"
#include "ext4_blockdev.h"
#include "ext4_balloc.h"
#include "ext4_extent.h"

/* memcpy / memset, and malloc / calloc / free. */
#include <string.h>
#include <stdlib.h>

/*----------------------------------------------------------------------------*/
/* Private constants -- documentation table.                                  */
/*----------------------------------------------------------------------------*/
#define INL_DOT_ENTRY_LEN       12       // bytes of a "." or ".." entry: 8 of header, the name, padded to 4
#define INL_ENTRY_HEADER        8        // bytes of an entry before its name (inode, length, name length, type)

/*----------------------------------------------------------------------------*/
/* ext4_inline_has:                                                           */
/*                                                                            */
/* Whether an inode's content is inline (ext4_inline.h says what that means   */
/* and who asks).                                                             */
/*                                                                            */
/* Arguments:                                                                 */
/*     sb    : the superblock.                                                */
/*     inode : the inode.                                                     */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true when the content is inline.                                */
/*----------------------------------------------------------------------------*/
bool ext4_inline_has(struct ext4_sblock *sb, struct ext4_inode *inode)
{
    if (!ext4_sb_feature_incom(sb, EXT4_FINCOM_INLINE_DATA))
    {   /* The filesystem has no inline content anywhere.  Tested first, so   */
        /* a stray flag bit on a filesystem without the feature is never      */
        /* taken as inline content (its i_block is then block pointers).      */
        return(false);
    }

    /* The feature is on: the inode's own flag decides. */
    return(ext4_inode_has_flag(inode, EXT4_INODE_FLAG_INLINE_DATA));
}

/*----------------------------------------------------------------------------*/
/* INL_BodyDataSize:                                                          */
/*                                                                            */
/* Whether the "system.data" attribute is in the inode's own BODY -- the      */
/* in-inode attribute area after the inode's fixed fields -- and how long its */
/* value is.  That is the only place the kernel ever keeps it (lwext4's       */
/* xattr layer would also take one from the inode's xattr block, which holds  */
/* nearly a block), so it is the true measure of how much inline content an   */
/* inode can have: at most 60 bytes in i_block plus this value.  Read         */
/* straight from the inode's bytes, every offset tested against the inode's   */
/* size before it is used, so a damaged area can only answer "not here".      */
/*                                                                            */
/* The area's form: a 4-byte magic (EXT4_XATTR_MAGIC), then entries of a      */
/* 16-byte header and the name, each padded to 4 (EXT4_XATTR_LEN), ended by   */
/* four zero bytes; a value's offset counts from the first entry, and its     */
/* e_value_block is 0 (a value in another inode is not inline content).       */
/*                                                                            */
/* Arguments:                                                                 */
/*     ref      : the inode, held by the caller.                              */
/*     pcbValue : receives the value's length; 0 when not found.              */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true when "system.data" is in the body with its value wholly    */
/*            inside the inode; false otherwise.                              */
/*----------------------------------------------------------------------------*/
static bool INL_BodyDataSize(struct ext4_inode_ref *ref, size_t *pcbValue)
{
    struct ext4_xattr_entry entry;
    const uint8_t          *pbInode;
    size_t                  cbInode;
    size_t                  offBody;
    size_t                  offFirst;
    size_t                  off;
    size_t                  cbEntry;
    size_t                  offValue;
    uint32_t                uiWord;

    /* Nothing found yet; the inode's bytes and its on-disk size (the      */
    /* superblock's inode size, which the inode table is laid out with).   */
    *pcbValue = 0;
    pbInode   = (const uint8_t *)ref->inode;
    cbInode   = ext4_get16(&ref->fs->sb, inode_size);

    if (cbInode <= EXT4_GOOD_OLD_INODE_SIZE)
    {   /* A 128-byte inode has no body past its fixed fields at all. */
        return(false);
    }

    /* The area starts after the fixed 128 bytes and the extra fields the   */
    /* inode declares.                                                      */
    offBody = EXT4_GOOD_OLD_INODE_SIZE + (size_t)ext4_inode_get_extra_isize(ref->inode);

    if (offBody > cbInode || cbInode - offBody < sizeof(struct ext4_xattr_ibody_header) + sizeof(uint32_t))
    {   /* No room for the magic and a terminator: no attribute area. */
        return(false);
    }

    /* The magic, which says the area holds attributes at all. */
    memcpy(&uiWord, pbInode + offBody, sizeof(uiWord));

    if (to_le32(uiWord) != EXT4_XATTR_MAGIC)
    {   /* No attribute area in this inode's body. */
        return(false);
    }

    /* The first entry, right after the magic; values count from here. */
    offFirst = offBody + sizeof(struct ext4_xattr_ibody_header);
    off      = offFirst;

    for (;;)
    {   /* One entry per iteration, until the terminator or the name. */
        if (cbInode - off < sizeof(uint32_t))
        {   /* The table runs off the inode with no terminator: damaged. */
            return(false);
        }

        memcpy(&uiWord, pbInode + off, sizeof(uiWord));

        if (uiWord == 0)
        {   /* The terminator: every entry seen, and none was "system.data". */
            return(false);
        }

        if (cbInode - off < sizeof(entry))
        {   /* Not room for an entry's header before the inode ends. */
            return(false);
        }

        /* The header, and the entry's whole length with its name. */
        memcpy(&entry, pbInode + off, sizeof(entry));
        cbEntry = EXT4_XATTR_LEN(entry.e_name_len);

        if (cbEntry > cbInode - off)
        {   /* The name would run past the inode: damaged. */
            return(false);
        }

        if (entry.e_name_index == EXT4_XATTR_INDEX_SYSTEM && entry.e_name_len == EXT4_INLINE_XATTR_LEN &&
            memcmp(pbInode + off + sizeof(entry), EXT4_INLINE_XATTR_NAME, EXT4_INLINE_XATTR_LEN) == 0)
        {   /* "system.data": its value must be here, in this inode, whole. */
            offValue = offFirst + (size_t)to_le16(entry.e_value_offs);

            if (to_le32(entry.e_value_block) != 0 || offValue > cbInode ||
                (size_t)to_le32(entry.e_value_size) > cbInode - offValue)
            {   /* Kept elsewhere, or running past the inode: not usable. */
                return(false);
            }

            /* Found, with a value that lies wholly inside the inode. */
            *pcbValue = (size_t)to_le32(entry.e_value_size);
            return(true);
        }

        /* On to the next entry (each is at least 16 bytes, so this ends). */
        off += cbEntry;
    }
}

/*----------------------------------------------------------------------------*/
/* ext4_inline_read:                                                          */
/*                                                                            */
/* An inline inode's whole content in a buffer of its own, for ext4_fread     */
/* and, through ext4_inline_dir_block, for a directory's listing and its      */
/* conversion.  Reads only: the inode is not changed.  The inode's size is    */
/* checked against what an inode can hold inline BEFORE anything is           */
/* allocated, so a damaged size is reported as damage and never sizes a       */
/* buffer.                                                                    */
/*                                                                            */
/* Arguments:                                                                 */
/*     ref     : the inode, held by the caller.                               */
/*     ppbData : receives the malloc'd content.                               */
/*     pcbData : receives its length.                                         */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK, EINVAL, ENOMEM or EIO as the header says, or the xattr      */
/*           layer's error when the attribute set cannot be read.             */
/*----------------------------------------------------------------------------*/
int ext4_inline_read(struct ext4_inode_ref *ref, uint8_t **ppbData, size_t *pcbData)
{
    struct ext4_sblock   *sb;
    struct ext4_xattr_ref xref;
    uint8_t              *pbData;
    uint64_t              ullSize;
    size_t                cbData;
    size_t                cbHead;
    size_t                cbValue;
    size_t                cbBody;
    int                   rc;

    /* The superblock for the size and feature reads, and the outputs     */
    /* cleared first so a failure below never leaves the caller a stale   */
    /* pointer or length.                                                 */
    sb       = &ref->fs->sb;
    *ppbData = NULL;
    *pcbData = 0;

    if (!ext4_inline_has(sb, ref->inode))
    {   /* Not an inline inode: its i_block is block pointers, which must   */
        /* never be handed out as content.  Nothing is held yet.            */
        return(EINVAL);
    }

    /* The size the inode claims is the whole content's length, as a   */
    /* 64-bit number straight from the inode.                          */
    ullSize = ext4_inode_get_size(sb, ref->inode);

    if (ullSize > EXT4_INLINE_DATA_MIN)
    {   /* More than i_block's 60 bytes: the rest must be the value of       */
        /* "system.data" in the inode's own body, at least as long as the    */
        /* size says (INL_BodyDataSize).  A size no inode can hold this      */
        /* way -- damaged, or crafted -- is refused as damage here, before   */
        /* it sizes a buffer: with this test the content is at most 60       */
        /* bytes plus a value that lies inside the inode, so the buffer      */
        /* below is never larger than an inode and the sum cannot wrap.      */
        if (!INL_BodyDataSize(ref, &cbBody) || ullSize - EXT4_INLINE_DATA_MIN > (uint64_t)cbBody)
        {   /* No such value in the body, or one too short: damaged.  Nothing   */
            /* is held yet.                                                     */
            return(EIO);
        }
    }

    /* The length, now known to be small. */
    cbData = (size_t)ullSize;

    /* One byte more than needed, so an empty file still gets a buffer the   */
    /* caller can free without a special case.                               */
    pbData = (uint8_t *)malloc(cbData + 1);

    if (pbData == NULL)
    {   /* Out of memory.  The outputs are already cleared; nothing else   */
        /* is held.                                                        */
        return(ENOMEM);
    }

    /* STEP 1: the head, from i_block: all of a content of 60 bytes or   */
    /* less, else its first 60 (the rest lives in the attribute).        */
    cbHead = cbData;

    if (cbHead > EXT4_INLINE_DATA_MIN)
    {   /* Only the first 60 bytes live there. */
        cbHead = EXT4_INLINE_DATA_MIN;
    }

    /* Straight out of the inode's i_block array, which for an inline   */
    /* inode is content, not block pointers.                            */
    memcpy(pbData, ref->inode->blocks, cbHead);

    /* STEP 2: the tail, from the attribute, only when there is one.       */
    /* Taking the xattr reference parses the inode's whole attribute set   */
    /* (and reads its xattr block, if it has one -- ext4_xattr.c           */
    /* ext4_fs_get_xattr_ref), so it is skipped when i_block holds         */
    /* everything.                                                         */
    if (cbData > EXT4_INLINE_DATA_MIN)
    {   /* The attribute holds bytes 60 onward. */
        rc = ext4_fs_get_xattr_ref(ref->fs, ref, &xref);

        if (rc != EOK)
        {   /* The attribute set could not be read.  The reference frees     */
            /* what it had parsed and the xattr block it took itself         */
            /* (ext4_xattr.c ext4_fs_get_xattr_ref), so only the buffer is   */
            /* released here.                                                */
            free(pbData);
            return(rc);
        }

        /* The value copied straight into the buffer after the head, at     */
        /* most the bytes the size says are missing; cbValue receives the   */
        /* attribute's real length (ext4_fs_get_xattr copies the smaller    */
        /* of the two and reports the whole), which is checked below.       */
        cbValue = 0;
        rc      = ext4_fs_get_xattr(&xref, EXT4_XATTR_INDEX_SYSTEM, EXT4_INLINE_XATTR_NAME,
                                    EXT4_INLINE_XATTR_LEN, pbData + EXT4_INLINE_DATA_MIN,
                                    cbData - EXT4_INLINE_DATA_MIN, &cbValue);

        /* The reference released.  Its put writes the attribute set     */
        /* back only when something in it changed (ext4_xattr.c          */
        /* ext4_xattr_write_to_disk tests the set's dirty mark), and a   */
        /* read changes nothing -- which is what lets this run on a      */
        /* read-only session.                                            */
        ext4_fs_put_xattr_ref(&xref);

        if (rc != EOK || cbValue < cbData - EXT4_INLINE_DATA_MIN)
        {   /* No attribute (ENODATA), or one shorter than the size claims:   */
            /* the inode is damaged, and a short read would hand the caller   */
            /* uninitialised bytes as content.  Reported as EIO, damage,      */
            /* whatever the lookup said; the reference is already put, so     */
            /* only the buffer is released.  A LONGER attribute is taken:     */
            /* the size decides how much is content.                          */
            free(pbData);
            return(EIO);
        }
    }

    /* The buffer and its length handed over; the caller frees it. */
    *ppbData = pbData;
    *pcbData = cbData;

    /* The whole content is in the buffer. */
    return(EOK);
}

/*----------------------------------------------------------------------------*/
/* INL_CopyEntries:                                                           */
/*                                                                            */
/* Append the live entries of one region of an inline directory -- the        */
/* i_block part after the parent word, or the attribute's value -- to the     */
/* block ext4_inline_dir_block is building, each at its natural length.       */
/* Refuses a malformed region rather than copy from it, and refuses entries   */
/* that would not fit the room the block has for them -- a bound of its own,  */
/* whatever the content's size was checked against before.                    */
/*                                                                            */
/* Arguments:                                                                 */
/*     sb       : the superblock (for the name-length and type accessors).    */
/*     pbRegion : the region's first byte.                                    */
/*     cbRegion : its length.                                                 */
/*     pbBlock  : the block being built.                                      */
/*     cbLimit  : the bytes of the block the entries may fill: the block,     */
/*                less its checksum tail when there is one.                   */
/*     pcbUsed  : in: bytes of the block already used (at most cbLimit);      */
/*                out: after the copy.                                        */
/*     ppLast   : in/out: the last entry written to the block, whose length   */
/*                the caller finally extends to cbLimit.                      */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK, or EIO when an entry is malformed or would not fit.         */
/*----------------------------------------------------------------------------*/
static int INL_CopyEntries(struct ext4_sblock *sb, const uint8_t *pbRegion, size_t cbRegion,
                           uint8_t *pbBlock, size_t cbLimit, size_t *pcbUsed, struct ext4_dir_en **ppLast)
{
    struct ext4_dir_en *pSrc;
    struct ext4_dir_en *pDst;
    size_t              off;
    size_t              cbEntry;
    size_t              cbCopy;
    uint16_t            cbName;

    /* A region is laid out like a tiny directory block: each entry's       */
    /* length leads to the next, and the last one's runs to the region's    */
    /* end.  So the walk starts at the region's first byte and stops when   */
    /* not even an entry header is left.                                    */
    off = 0;

    while (off + INL_ENTRY_HEADER <= cbRegion)
    {   /* One stored entry per iteration, live or dead. */
        pSrc    = (struct ext4_dir_en *)(pbRegion + off);
        cbEntry = ext4_dir_en_get_entry_len(pSrc);

        if (cbEntry < INL_ENTRY_HEADER || (cbEntry % 4) != 0 || off + cbEntry > cbRegion)
        {   /* A length that cannot be -- shorter than a header, not a       */
            /* multiple of 4, or running past the region (which would read   */
            /* beyond the inline area): the region is damaged.  The test     */
            /* also guarantees progress, since every length is at least 8.   */
            /* Nothing is held here; the caller frees its buffers.           */
            return(EIO);
        }

        /* The name's length, read the way the filesystem's revision   */
        /* stores it (lwext4's accessor handles the old high byte).    */
        cbName = ext4_dir_en_get_name_len(sb, pSrc);

        if (cbName > cbEntry - INL_ENTRY_HEADER)
        {   /* A name longer than its entry: copying it would read past   */
            /* the entry, so the region is refused as damaged.            */
            return(EIO);
        }

        if (ext4_dir_en_get_inode(pSrc) != 0 && cbName != 0)
        {   /* A live entry: copy it at its own natural length (header plus   */
            /* name, padded to 4), which is what a block entry would be --    */
            /* the slack a deleted neighbour left is not carried over.  Its   */
            /* length field is set to that natural length, and it becomes     */
            /* the last entry so far (the caller stretches the final one).    */
            /* Unused (inode 0) and nameless slots are skipped.               */
            cbCopy = (INL_ENTRY_HEADER + cbName + 3) & ~(size_t)3;

            if (cbCopy > cbLimit - *pcbUsed)
            {   /* The entry would pass the room the block has (*pcbUsed is   */
                /* never above cbLimit, so the subtraction cannot wrap):      */
                /* more entries than one block holds is not a sound inline    */
                /* directory, and copying on would write past the block.      */
                return(EIO);
            }

            pDst = (struct ext4_dir_en *)(pbBlock + *pcbUsed);
            memcpy(pDst, pSrc, INL_ENTRY_HEADER + cbName);
            ext4_dir_en_set_entry_len(pDst, (uint16_t)cbCopy);
            *pcbUsed += cbCopy;
            *ppLast   = pDst;
        }

        /* On to the next stored entry by its stored length. */
        off += cbEntry;
    }

    /* Every entry of the region is accounted for. */
    return(EOK);
}

/*----------------------------------------------------------------------------*/
/* ext4_inline_dir_block:                                                     */
/*                                                                            */
/* An inline directory as one ordinary directory block, built in memory so    */
/* that it is indistinguishable from one the library wrote itself: what the   */
/* listing and lookup read (ext4_dir.c), and what ext4_inline_convert writes. */
/* The room for entries -- the block less "." and ".." and any checksum       */
/* tail -- is fixed before the first copy, and an inline area whose entries   */
/* would not fit it is refused as damaged.                                    */
/*                                                                            */
/* Arguments:                                                                 */
/*     ref      : the directory inode, held by the caller.                    */
/*     ppbBlock : receives the malloc'd block.                                */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK, EINVAL, ENOMEM or EIO as the header says.                   */
/*----------------------------------------------------------------------------*/
int ext4_inline_dir_block(struct ext4_inode_ref *ref, uint8_t **ppbBlock)
{
    struct ext4_sblock         *sb;
    struct ext4_dir_en         *pDot;
    struct ext4_dir_en         *pDotDot;
    struct ext4_dir_en         *pLast;
    struct ext4_dir_entry_tail *pTail;
    uint8_t                    *pbRaw;
    uint8_t                    *pbBlock;
    size_t                      cbRaw;
    size_t                      cbUsed;
    size_t                      cbLimit;
    size_t                      cbRoom;
    uint32_t                    uiBlockSize;
    uint32_t                    uiParent;
    int                         rc;

    /* The superblock for the feature, size and entry accessors, and the   */
    /* output cleared first so no failure leaves a stale pointer.          */
    sb        = &ref->fs->sb;
    *ppbBlock = NULL;

    if (!ext4_inline_has(sb, ref->inode) ||
        !ext4_inode_is_type(sb, ref->inode, EXT4_INODE_MODE_DIRECTORY))
    {   /* Not an inline directory: an inline FILE's content is not entries,   */
        /* and a block-backed directory is read from its blocks.  Nothing      */
        /* is held yet.                                                        */
        return(EINVAL);
    }

    /* STEP 1: the raw inline area -- the parent word, then both regions. */
    rc = ext4_inline_read(ref, &pbRaw, &cbRaw);

    if (rc != EOK)
    {   /* Could not be read; ext4_inline_read has released its own buffer. */
        return(rc);
    }

    if (cbRaw < EXT4_INLINE_DIR_PARENT)
    {   /* Not even the parent's number: damaged.  The raw area is the only   */
        /* thing held.                                                        */
        free(pbRaw);
        return(EIO);
    }

    /* The block being built, zero-filled, so every byte the entries do   */
    /* not cover reads as zero -- the tail area included, until it is     */
    /* written.                                                           */
    uiBlockSize = ext4_sb_get_block_size(sb);
    pbBlock     = (uint8_t *)calloc(1, uiBlockSize);

    if (pbBlock == NULL)
    {   /* Out of memory: the raw area is released; nothing else is held. */
        free(pbRaw);
        return(ENOMEM);
    }

    /* STEP 2: "." and "..", as a directory block always begins.  The      */
    /* parent's number is the inline area's first word, little-endian on   */
    /* disk like every other inode number.                                 */
    memcpy(&uiParent, pbRaw, sizeof(uiParent));
    uiParent = to_le32(uiParent);

    /* ".": this directory's own inode, a 12-byte entry (8 of header, the   */
    /* one-character name, padded to 4), typed as a directory -- the type   */
    /* is written only where the filesystem's revision stores types         */
    /* (lwext4's accessor decides).                                         */
    pDot = (struct ext4_dir_en *)pbBlock;
    ext4_dir_en_set_inode(pDot, ref->index);
    ext4_dir_en_set_entry_len(pDot, INL_DOT_ENTRY_LEN);
    ext4_dir_en_set_name_len(sb, pDot, 1);
    ext4_dir_en_set_inode_type(sb, pDot, EXT4_DE_DIR);
    pDot->name[0] = '.';

    /* "..": the parent, the same way, straight after ".". */
    pDotDot = (struct ext4_dir_en *)(pbBlock + INL_DOT_ENTRY_LEN);
    ext4_dir_en_set_inode(pDotDot, uiParent);
    ext4_dir_en_set_entry_len(pDotDot, INL_DOT_ENTRY_LEN);
    ext4_dir_en_set_name_len(sb, pDotDot, 2);
    ext4_dir_en_set_inode_type(sb, pDotDot, EXT4_DE_DIR);
    pDotDot->name[0] = '.';
    pDotDot->name[1] = '.';

    /* 24 bytes used so far, and ".." is the last entry -- the one that   */
    /* is stretched to the block's end if the regions add nothing.        */
    cbUsed = 2 * INL_DOT_ENTRY_LEN;
    pLast  = pDotDot;

    /* The room the entries may fill, fixed BEFORE the first copy: the   */
    /* block, less the 12-byte checksum tail when the filesystem         */
    /* checksums directory blocks.  Every copy below is held to it       */
    /* (INL_CopyEntries), and the last entry is stretched to it.         */
    cbRoom = uiBlockSize;

    if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM))
    {   /* The last 12 bytes are the checksum tail. */
        cbRoom -= sizeof(struct ext4_dir_entry_tail);
    }

    /* STEP 3: the entries of the i_block region (after the parent word),    */
    /* then of the attribute region when the inline area extends that far.   */
    /* When the area is exactly the parent word, neither runs and rc still   */
    /* holds STEP 1's EOK.                                                   */
    if (cbRaw > EXT4_INLINE_DIR_PARENT)
    {   /* The i_block region: from the parent word to byte 60, or to the   */
        /* end of a shorter area.                                           */
        cbLimit = cbRaw;

        if (cbLimit > EXT4_INLINE_DATA_MIN)
        {   /* The first region stops at 60; the rest is the second. */
            cbLimit = EXT4_INLINE_DATA_MIN;
        }

        rc = INL_CopyEntries(sb, pbRaw + EXT4_INLINE_DIR_PARENT, cbLimit - EXT4_INLINE_DIR_PARENT,
                             pbBlock, cbRoom, &cbUsed, &pLast);
    }

    if (rc == EOK && cbRaw > EXT4_INLINE_DATA_MIN)
    {   /* The attribute region, from byte 60 to the area's end -- a value   */
        /* ext4_inline_read has found in the inode's own body, so smaller    */
        /* than an inode; the room test in the copy holds whatever its       */
        /* size.                                                             */
        rc = INL_CopyEntries(sb, pbRaw + EXT4_INLINE_DATA_MIN, cbRaw - EXT4_INLINE_DATA_MIN,
                             pbBlock, cbRoom, &cbUsed, &pLast);
    }

    /* The raw area is no longer needed, whatever happened. */
    free(pbRaw);

    if (rc != EOK)
    {   /* A malformed region: the half-built block is released too, so   */
        /* nothing is handed back.                                        */
        free(pbBlock);
        return(rc);
    }

    /* STEP 4: the last entry owns the rest of the room -- the block, less    */
    /* the tail when the filesystem checksums directory blocks; then the      */
    /* tail.  The last entry's length runs from where it starts to the        */
    /* room's end, so the entries chain exactly to the tail (or the block's   */
    /* end), as the reader's walk requires; every copy was held to that       */
    /* room, so the length is never less than the entry's own.                */
    ext4_dir_en_set_entry_len(pLast, (uint16_t)(cbRoom - (size_t)((uint8_t *)pLast - pbBlock)));

    if (cbRoom != uiBlockSize)
    {   /* metadata_csum is on: the tail's fixed fields at the block's end   */
        /* (lwext4's own initialiser), then the checksum over the block as   */
        /* built (its own routine, which finds the tail and also covers      */
        /* this directory's inode number and generation) -- so the block     */
        /* verifies like one lwext4 wrote.  Done last, as it covers every    */
        /* byte before the tail.                                             */
        pTail = EXT4_DIRENT_TAIL(pbBlock, uiBlockSize);
        ext4_dir_init_entry_tail(pTail);
        ext4_dir_set_csum(ref, (struct ext4_dir_en *)pbBlock);
    }

    /* The block handed over; the caller frees it. */
    *ppbBlock = pbBlock;

    /* The directory as one block. */
    return(EOK);
}

/*----------------------------------------------------------------------------*/
/* ext4_inline_drop:                                                          */
/*                                                                            */
/* An inline inode emptied and made block-backed, its content discarded --    */
/* ext4_inline.h names its users.  A missing "system.data" attribute is not   */
/* an error here: the content it would hold is being discarded anyway, so     */
/* the drop goes on exactly as for an inode that has one.  (The kernel always */
/* writes the attribute; an inline inode without it is damaged, and the drop  */
/* is what lets it be written, truncated or deleted.)                         */
/*                                                                            */
/* Arguments:                                                                 */
/*     ref : the inode, held by the caller.                                   */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK -- a missing attribute included -- or the error of reading   */
/*           the attribute set.                                               */
/*----------------------------------------------------------------------------*/
int ext4_inline_drop(struct ext4_inode_ref *ref)
{
    struct ext4_sblock   *sb;
    struct ext4_xattr_ref xref;
    int                   rc;

    /* The superblock, for the inline test. */
    sb = &ref->fs->sb;

    if (!ext4_inline_has(sb, ref->inode))
    {   /* Nothing inline to drop: the inode is left untouched, and EOK   */
        /* lets a caller call this unconditionally.                       */
        return(EOK);
    }

    /* STEP 1: remove "system.data" first, through the xattr layer: the    */
    /* attribute set is parsed into a reference, the item removed there,   */
    /* and the put writes the set back without it.                         */
    rc = ext4_fs_get_xattr_ref(ref->fs, ref, &xref);

    if (rc != EOK)
    {   /* The attribute set could not be read: nothing has changed yet,   */
        /* so the inode is still whole and inline, and the reference has   */
        /* released what it took.                                          */
        return(rc);
    }

    /* The removal, in the parsed set only (nothing is written yet). */
    rc = ext4_fs_remove_xattr(&xref, EXT4_XATTR_INDEX_SYSTEM, EXT4_INLINE_XATTR_NAME,
                              EXT4_INLINE_XATTR_LEN);

    /* The put writes the set back without the attribute -- into the   */
    /* inode body, which marks the inode dirty -- and releases the     */
    /* reference.  It runs whatever the removal said: with nothing     */
    /* removed the set is not dirty and nothing is written.            */
    ext4_fs_put_xattr_ref(&xref);

    if (rc != EOK && rc != ENOENT)
    {   /* A failure removing it.  A MISSING attribute is let through by     */
        /* the test: ENOENT is what lwext4's removal answers for one         */
        /* (ext4_xattr.c ext4_xattr_remove_item -- not ENODATA, which only   */
        /* its lookup gives), and its content is being discarded anyway.     */
        /* This lwext4's removal returns no other code, so the branch is a   */
        /* guard: a failure the xattr layer might report one day is          */
        /* refused rather than taken for a removal, with the inode not       */
        /* yet changed -- still inline and whole.                            */
        return(rc);
    }

    /* STEP 2: the inode's content is now nothing, and it is made to       */
    /* look like a freshly allocated one as far as content goes: the       */
    /* flag cleared (so i_block is block pointers again), i_block zeroed   */
    /* with an empty extent header on an extents filesystem                */
    /* (ext4_fs_inode_blocks_init, the same call a new inode gets --       */
    /* checked in ext4_fs.c), and the size 0.  Marked dirty; the           */
    /* caller's put writes it.                                             */
    ext4_inode_clear_flag(ref->inode, EXT4_INODE_FLAG_INLINE_DATA);
    ext4_fs_inode_blocks_init(ref->fs, ref);
    ext4_inode_set_size(ref->inode, 0);
    ref->dirty = true;

    /* Empty, and block-backed from here on. */
    return(EOK);
}

/*----------------------------------------------------------------------------*/
/* ext4_inline_convert:                                                       */
/*                                                                            */
/* An inline inode made block-backed with the same content, before any write  */
/* touches it -- every write path in lwext4 calls this first (ext4.c,         */
/* ext4_dir.c, ext4_fs.c).                                                    */
/*                                                                            */
/* THE ORDER IS THE POINT: every step that can fail happens while the inode   */
/* is still inline and whole.  The content is read, a block is ALLOCATED and  */
/* the content WRITTEN into it, and only then is the inline storage dropped;  */
/* attaching the block to the emptied inode is setting fields in memory,      */
/* which cannot fail.  So a full filesystem (no block to allocate), an I/O    */
/* error or a refused drop costs the caller the write it asked for, never the */
/* content: the reserved block is freed again and the inode is as it was.     */
/* (Dropping first and appending a block after -- the library's own append    */
/* -- would leave an emptied inode on a failure, which no later step could    */
/* undo: lwext4's attribute write-back may move attributes between the inode  */
/* and its xattr block, so the old inode bytes cannot simply be put back.)    */
/*                                                                            */
/* Arguments:                                                                 */
/*     ref : the inode, held by the caller.                                   */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK, ENOMEM, ENOSPC or EIO as the header says.                   */
/*----------------------------------------------------------------------------*/
int ext4_inline_convert(struct ext4_inode_ref *ref)
{
    struct ext4_sblock         *sb;
    struct ext4_block           blk;
#if CONFIG_EXTENT_ENABLE
    struct ext4_extent_header  *pHeader;
    struct ext4_extent         *pExtent;
#endif
    uint8_t                    *pbData;
    size_t                      cbData;
    ext4_fsblk_t                goal;
    ext4_fsblk_t                fblock;
    uint32_t                    uiBlockSize;
    bool                        bDir;
    int                         rc;

    /* The superblock, for the inline test and the block size. */
    sb = &ref->fs->sb;

    if (!ext4_inline_has(sb, ref->inode))
    {   /* Already block-backed: nothing to do, and EOK so every write   */
        /* path may call this unconditionally.                           */
        return(EOK);
    }

    /* The block size, and whether this is a directory (its content is   */
    /* laid out as entries) or a file (its bytes as they are).           */
    uiBlockSize = ext4_sb_get_block_size(sb);
    bDir        = ext4_inode_is_type(sb, ref->inode, EXT4_INODE_MODE_DIRECTORY);

    /* STEP 1: the content, as the block will hold it -- in memory   */
    /* BEFORE the inline storage goes, since the drop discards it.   */
    if (bDir)
    {   /* A directory becomes exactly one laid-out block: the block a   */
        /* listing reads, written as it is.                              */
        rc     = ext4_inline_dir_block(ref, &pbData);
        cbData = uiBlockSize;
    }
    else
    {   /* A file's bytes, i_size of them. */
        rc = ext4_inline_read(ref, &pbData, &cbData);
    }

    if (rc != EOK)
    {   /* Could not be read: nothing has changed, the inode is still   */
        /* inline and whole, and the reader released its own buffer.    */
        return(rc);
    }

    if (cbData > uiBlockSize)
    {   /* Cannot happen (ext4_inline_read holds a file's content inside   */
        /* an inode, and a directory's block is exactly one block), but    */
        /* the copy below is bounded anyway.                               */
        cbData = uiBlockSize;
    }

    /* STEP 2: the block, ALLOCATED while the inode is still inline.  The   */
    /* goal is the first block of the inode's own block group, so the       */
    /* data lands near its inode (ext4_fs_inode_to_goal_block gives the     */
    /* group); the allocator searches on from there.  It also counts the    */
    /* block in the inode's block count, which the attach below keeps and   */
    /* a free on failure takes back.  fblock is cleared first: the          */
    /* allocator can report a failure after it has taken a block, and a     */
    /* block it handed back is freed whatever it said.                      */
    fblock = 0;
    goal   = ext4_balloc_get_block_of_bgid(sb, (uint32_t)ext4_fs_inode_to_goal_block(ref));
    rc     = ext4_balloc_alloc_block(ref, goal, &fblock);

    if (rc != EOK)
    {   /* No block free (ENOSPC), or the allocator failed: the inode is   */
        /* untouched, so only the content copy -- and a block the          */
        /* allocator took before failing -- is given back.                 */
        if (fblock != 0)
        {   /* Taken, then the bookkeeping failed: freed again. */
            ext4_balloc_free_block(ref, fblock);
        }

        free(pbData);
        return(rc);
    }

    /* STEP 3: the content WRITTEN into the block, still before the drop.   */
    /* The block is taken into the cache without reading it (every byte     */
    /* is about to be written), zeroed so a file's tail reads as zeros,     */
    /* filled, marked dirty in the current transaction and released to      */
    /* the cache, which writes it out.                                      */
    rc = ext4_trans_block_get_noread(ref->fs->bdev, &blk, fblock);

    if (rc != EOK)
    {   /* The block could not be brought into the cache: freed again, and   */
        /* the inode is still inline and whole.                              */
        ext4_balloc_free_block(ref, fblock);
        free(pbData);
        return(rc);
    }

    memset(blk.data, 0, uiBlockSize);
    memcpy(blk.data, pbData, cbData);
    free(pbData);
    ext4_trans_set_block_dirty(blk.buf);
    rc = ext4_block_set(ref->fs->bdev, &blk);

    if (rc != EOK)
    {   /* The block could not be released to the cache (lwext4 answers   */
        /* EIO when the device is not open): freed again; the inode is    */
        /* still inline and whole.                                        */
        ext4_balloc_free_block(ref, fblock);
        return(rc);
    }

    /* STEP 4: the inline storage dropped, leaving an empty block-backed   */
    /* inode with the empty block map a new inode gets.                    */
    rc = ext4_inline_drop(ref);

    if (rc != EOK)
    {   /* The drop failed: the attribute set could not be read (or the   */
        /* removal's guard refused).  The drop changes the inode only     */
        /* after the removal, so it is still inline and whole; the        */
        /* reserved block is freed again.                                 */
        ext4_balloc_free_block(ref, fblock);
        return(rc);
    }

    /* STEP 5: the written block attached as the inode's block 0 -- fields   */
    /* set in memory, nothing that can fail.  On an extents inode (the       */
    /* drop gave it an empty extent root in i_block) one extent: logical     */
    /* block 0, one block long, written (not "unwritten"), starting at the   */
    /* block; on a block-map inode, direct pointer 0.                        */
#if CONFIG_EXTENT_ENABLE
    if (ext4_sb_feature_incom(sb, EXT4_FINCOM_EXTENTS) &&
        ext4_inode_has_flag(ref->inode, EXT4_INODE_FLAG_EXTENTS))
    {   /* The extent root, given its one extent. */
        pHeader = ext4_inode_get_extent_header(ref->inode);
        pExtent = EXT4_EXTENT_FIRST(pHeader);
        ext4_extent_set_first_block(pExtent, 0);
        ext4_extent_set_block_count(pExtent, 1, false);
        ext4_extent_set_start(pExtent, fblock);
        ext4_extent_header_set_entries_count(pHeader, 1);
    }
    else
#endif
    {   /* A block-map inode: the first direct pointer. */
        ext4_inode_set_direct_block(ref->inode, 0, (uint32_t)fblock);
    }

    /* The size: one block for a directory (its laid-out block); the   */
    /* content's own length for a file.                                */
    if (bDir)
    {   /* A directory is the whole block. */
        ext4_inode_set_size(ref->inode, uiBlockSize);
    }
    else
    {   /* A file is as long as it was. */
        ext4_inode_set_size(ref->inode, cbData);
    }

    /* The inode changed (block map, size, flag): the caller's put writes   */
    /* it.                                                                  */
    ref->dirty = true;

    /* Block-backed, same content. */
    return(EOK);
}
