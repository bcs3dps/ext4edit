/*----------------------------------------------------------------------------*/
/* ext4_casefold.c                                                            */
/*                                                                            */
/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/* Copyright (c) 2026 B. C. Services                                          */
/*                                                                            */
/* ext4edit patch 8: the casefold feature for lwext4 -- the layer between     */
/* the library's directory code and e2fsprogs' folding tables.  The header    */
/* (ext4_casefold.h) carries the design; this file carries the four small     */
/* decisions: whether a filesystem folds at all, whether one directory does,  */
/* how a name is folded for the hash, and how two names are compared.         */
/*                                                                            */
/* The driver's own shape, for whoever checks this against it: fs/ext4/hash.c */
/* ext4fs_dirhash folds the name into a PATH_MAX buffer with utf8_casefold    */
/* and hashes the result, hashing the raw name when the fold fails;           */
/* fs/ext4/namei.c ext4_ci_compare compares the folded lookup name against    */
/* each entry's folded form with utf8_strncasecmp_folded and, when an entry   */
/* is not valid UTF-8, compares the raw bytes (non-strict) or refuses         */
/* (strict).  lwext4 never refuses.                                           */
/*----------------------------------------------------------------------------*/

/* lwext4's base: configuration, on-disk types, helpers, error codes, debug. */
#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_errno.h"
#include "ext4_debug.h"

/* This module's header, then the superblock and inode accessors it asks. */
#include "ext4_casefold.h"
#include "ext4_fs.h"
#include "ext4_super.h"
#include "ext4_inode.h"

/* The stand-in headers in lwext4\unicode\ that let e2fsprogs' nls_utf8.c   */
/* compile here, and through them the folding table's type and functions    */
/* (ext2fs_load_nls_table, ext2fs_casefold).                                */
#include "ext2_fs.h"
#include "ext2fs.h"
#include "ext2fsP.h"

/* memcmp. */
#include <string.h>

/* The superblock layout gained the encoding fields (ext4_types.h) by          */
/* splitting a padding word; this refuses to compile if that split moved the   */
/* structure's size off the 1024 bytes a superblock occupies on disk (a        */
/* negative array size is a compile error, so a wrong size stops the build     */
/* rather than misreading every superblock field after the split).             */
typedef char ext4_casefold_sb_size_check[(sizeof(struct ext4_sblock) == EXT4_SUPERBLOCK_SIZE) ? 1 : -1];

/*----------------------------------------------------------------------------*/
/* ext4_casefold_table:                                                       */
/*                                                                            */
/* The folding table of a filesystem, or NULL when its names are never        */
/* folded -- the header says when, and who relies on the NULL.                */
/*                                                                            */
/* Arguments:                                                                 */
/*     sb : the superblock.                                                   */
/*                                                                            */
/* Returns:                                                                   */
/*     const struct ext2fs_nls_table * : the table, or NULL.                  */
/*----------------------------------------------------------------------------*/
const struct ext2fs_nls_table *ext4_casefold_table(struct ext4_sblock *sb)
{
    if (!ext4_sb_feature_incom(sb, EXT4_FINCOM_CASEFOLD))
    {   /* No casefold on this filesystem: every name is bytes.  Asked    */
        /* first, because a filesystem without the feature may carry      */
        /* stale bytes in the encoding field (it was padding before the   */
        /* feature existed), which must mean nothing.                     */
        return(NULL);
    }

    /* The table for the version the superblock names; NULL for one the   */
    /* mirrored tables do not know (nls_utf8.c knows only UTF-8 12.1),    */
    /* which ext4_fs.c's feature check then refuses at mount.             */
    return(ext2fs_load_nls_table(ext4_get16(sb, encoding)));
}

/*----------------------------------------------------------------------------*/
/* ext4_casefold_dir:                                                         */
/*                                                                            */
/* The folding table for one directory's names, or NULL -- the header says    */
/* which directories fold.                                                    */
/*                                                                            */
/* Arguments:                                                                 */
/*     dir : the directory's inode reference.                                 */
/*                                                                            */
/* Returns:                                                                   */
/*     const struct ext2fs_nls_table * : the table, or NULL.                  */
/*----------------------------------------------------------------------------*/
const struct ext2fs_nls_table *ext4_casefold_dir(struct ext4_inode_ref *dir)
{
    const struct ext2fs_nls_table *table;
    struct ext4_sblock            *sb;

    /* The superblock, for the table and the type test. */
    sb = &dir->fs->sb;

    /* The filesystem first: most have no casefold at all, and then the   */
    /* inode need not be looked at.                                       */
    table = ext4_casefold_table(sb);

    if (table == NULL)
    {   /* Nothing on this filesystem folds. */
        return(NULL);
    }

    if (!ext4_inode_is_type(sb, dir->inode, EXT4_INODE_MODE_DIRECTORY))
    {   /* Not a directory: the flag, if present, means nothing.  The    */
        /* driver sets it on directories only, but a corrupt or hand-    */
        /* edited inode could carry it on a file, and a file's content   */
        /* is not names -- so the type is checked as well as the flag.   */
        return(NULL);
    }

    if (!ext4_inode_has_flag(dir->inode, EXT4_INODE_FLAG_CASEFOLD))
    {   /* An ordinary, case-sensitive directory. */
        return(NULL);
    }

    /* A casefolded directory: its names are hashed and compared folded. */
    return(table);
}

/*----------------------------------------------------------------------------*/
/* ext4_casefold_fold:                                                        */
/*                                                                            */
/* Fold a name into the caller's buffer, or say it cannot be folded -- the    */
/* header says what folding is and who asks.                                  */
/*                                                                            */
/* Arguments:                                                                 */
/*     table   : the folding table.                                           */
/*     name    : the name, NOT NUL-terminated.                                */
/*     len     : its length in bytes.                                         */
/*     buf     : receives the folded form.                                    */
/*     bufsize : the size of buf in bytes.                                    */
/*                                                                            */
/* Returns:                                                                   */
/*     int : the folded length (0 possible), or -1 when the name cannot be    */
/*           folded.                                                          */
/*----------------------------------------------------------------------------*/
int ext4_casefold_fold(const struct ext2fs_nls_table *table, const char *name,
                       size_t len, char *buf, size_t bufsize)
{
    int n;

    /* e2fsprogs' fold (nls_utf8.c, reached through ext4edit patch 8's    */
    /* ext2fs_casefold): the folded length, or -EINVAL / -ENAMETOOLONG.   */
    /* It also writes a terminating NUL into the buffer, so a form must   */
    /* be at most bufsize - 1 bytes to fit; checked in nls_utf8.c.        */
    n = ext2fs_casefold(table, (const unsigned char *)name, len,
                        (unsigned char *)buf, bufsize);

    if (n < 0)
    {   /* Not valid UTF-8, or too long once folded: both become the one   */
        /* answer "cannot be folded", because the driver treats both the   */
        /* same way -- it hashes and compares the raw bytes.               */
        return(-1);
    }

    /* Folded: the form's length, without the NUL. */
    return(n);
}

/*----------------------------------------------------------------------------*/
/* ext4_casefold_match:                                                       */
/*                                                                            */
/* Whether a stored entry matches the looked-up name under a casefolded       */
/* directory's rules -- the header says how the work is split with the        */
/* caller.                                                                    */
/*                                                                            */
/* Arguments:                                                                 */
/*     table      : the folding table.                                        */
/*     name       : the name being looked up, raw.                            */
/*     name_len   : its length in bytes.                                      */
/*     folded     : its folded form, or NULL when it could not be folded.     */
/*     folded_len : the folded form's length.                                 */
/*     de_name    : the stored entry's name, raw.                             */
/*     de_len     : its length in bytes.                                      */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true when the entry is the one looked up.                       */
/*----------------------------------------------------------------------------*/
bool ext4_casefold_match(const struct ext2fs_nls_table *table,
                         const char *name, size_t name_len,
                         const char *folded, size_t folded_len,
                         const char *de_name, size_t de_len)
{
    char de_folded[EXT4_CASEFOLD_FOLD_MAX];          // the entry's folded form: the driver's PATH_MAX, on the stack for this call only
    int  n;                                          // its length, or -1 when the entry cannot be folded

    if (folded != NULL)
    {   /* The looked-up name folded (the caller folded it once for the    */
        /* whole block scan, rather than once per entry); fold the entry   */
        /* into the buffer and compare the forms.  The buffer lives only   */
        /* for this call, so its 4 KiB is not multiplied by recursion      */
        /* above it; the caller's own 4 KiB fold buffer is the other one   */
        /* alive at the time.                                              */
        n = ext4_casefold_fold(table, de_name, de_len, de_folded, sizeof(de_folded));

        if (n >= 0)
        {   /* Both folded: equal forms are a match, whatever the raw bytes   */
            /* or lengths were.                                               */
            return(((size_t)n == folded_len) && (memcmp(folded, de_folded, folded_len) == 0));
        }
    }

    /* One side or the other cannot be folded: the driver's non-strict   */
    /* fallback is a plain byte compare of the raw names.                */
    return((de_len == name_len) && (memcmp(name, de_name, name_len) == 0));
}
