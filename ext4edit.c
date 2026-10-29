/*------------------------------------------------------------------------------*/
/* ext4edit.c                                                                   */
/*                                                                              */
/* SPDX-License-Identifier: GPL-2.0-or-later                                    */
/* Copyright (c) 2026 B. C. Services                                            */
/*                                                                              */
/* Licensed under the GNU General Public License, version 2 or (at your         */
/* option) any later version: see LICENSE and GPL-2.0.txt beside this source.   */
/* This wrapper links the BUNDLED lwext4 library (in lwext4/, (c) Grzegorz      */
/* Kostka, BSD-licensed -- see the per-file headers there), which carries       */
/* numbered fixes and additions of ours, each marked "ext4edit patch N" where   */
/* it sits and explained with its diff in LWEXT4_UPSTREAM.md.  lwext4's own     */
/* terms are retained on its files; the files the patches add are ours under    */
/* this program's licence, and lwext4/unicode/ holds e2fsprogs' GPL folding     */
/* code, mirrored verbatim.                                                     */
/*                                                                              */
/* ext4edit -- a tiny Windows command-line ext2/3/4 image editor, built on      */
/* lwext4, that operates on an image FILE (e.g. a partition image carved out    */
/* of a raw disk dump) with no Linux host and no untrusted Windows ext4         */
/* driver.  It lists and edits an ext filesystem in userspace: ls / stat /      */
/* cat / readlink / dump / rdump to read, and rm / rmdir / rm-tree / mkdir /    */
/* write / rwrite / ln / symlink / mv / chmod / chown to edit.                  */
/*                                                                              */
/* It is deliberately conservative: every editing command is a DRY RUN unless   */
/* `--write` is given, so you see exactly what would change before anything is  */
/* touched.                                                                     */
/* ALWAYS take a full-image backup first (dd4win) and fsck the result           */
/* afterward -- lwext4 is capable but this writes a real filesystem.            */
/*                                                                              */
/* Build: see build.bat (compiles the vendored lwext4 plus this file into a     */
/* single static exe with the WinLibs MinGW-w64 gcc, from cmd.exe).             */
/*------------------------------------------------------------------------------*/

/* The headers, lwext4's first (its public API, then the pieces of its internals   */
/* this wrapper reaches into), then the C runtime, then the Win32 surface for      */
/* the host-path layer.  What each is for:                                         */
#include <ext4.h>                           // the lwext4 API: mount, files, directories, links, modes, owners, times
#include <ext4_blockdev.h>                  // struct ext4_blockdev + the static-instance macro (our own device)
#include <ext4_errno.h>                     // EOK and the errno values the device hooks return
#include <ext4_inode.h>                     // ext4_inode_get_* -- the inode fields `stat` prints, and EXT4_INODE_MODE_* for the type nibble
#include <ext4_super.h>                     // ext4_sb_feature_com / _incom -- the superblock's feature tests (the journal refusals)
#include <ext4_misc.h>                      // ext4_get32 -- a superblock field read in host order (the journal inode number)
#include <ext4_debug.h>                     // ext4_assert_last / _clear -- the site of a failed lwext4 assertion, reported at the close

#include <stdio.h>           // the listings and messages, and the host file I/O (fopen family, fread / fwrite)
#include <stdlib.h>          // malloc / calloc / realloc / free, strtoul, qsort for the manifest records, atexit for the console restore
#include <string.h>          // the path and name work: strlen, strcmp, memcpy, strrchr, strerror
#include <stdbool.h>         // bool / true / false, the project's one boolean form (lwext4 requires this header)
#include <stdarg.h>          // Log_Err's variadic arguments
#include <errno.h>           // errno after the host opens, and the E* codes lwext4 returns
#include <io.h>              // _setmode / _fileno -- binary stdout for `cat`
#include <fcntl.h>           // _O_BINARY
#include <time.h>            // time_t for host timestamp values; time() for this run's one clock read
#include <sys/types.h>       // base types for the stat family
#include <sys/stat.h>        // struct _stat64 / _wstat64 -- host type + 64-bit size
#include <sys/utime.h>       // __utimbuf64 / _wutime64 -- host file times (-t)
#include <direct.h>          // _wmkdir -- host directory create (rdump)
#include <wchar.h>           // _wfopen plus the wide CRT path helpers
#define WIN32_LEAN_AND_MEAN  // trim the Win32 surface (host-path layer below)
#include <windows.h>         // MultiByteToWideChar / GetFullPathNameW / FindFirstFileW
#include <shellapi.h>        // CommandLineToArgvW -- the Unicode command line (LEAN_AND_MEAN drops it)

/*----------------------------------------------------------------------------*/
/* Definitions -- documentation table.                                        */
/*----------------------------------------------------------------------------*/
#define EXT4EDIT_VERSION  "1.4.0"          // tool version, printed by --version
#define E4_DEV_NAME       "ext4_fs"        // lwext4 registered block-device name
#define E4_MP             "/"              // lwext4 mount point (fs paths are absolute)
#define E4_MAX_PATH       4096             // longest path this tool composes, in UTF-8 bytes (host or image), NUL included; Linux's own PATH_MAX, so no path an image can hold is too long.  A tree walk keeps path buffers of this size in each level's heap-held frame (E4_ImportFrame, E4_ExtractFrame), none on the thread's stack
#define E4_MAX_NAME       1024             // buffer for ONE host entry name as UTF-8: a Windows name is at most 255 UTF-16 units, at most 3 bytes each, plus the NUL
#define E4_MAX_WPATH      8192             // wide \\?\ host-path buffer (wchars): an E4_MAX_PATH path made absolute and prefixed still fits
#define E4_MANIFEST_NAME  "_ext4meta.txt"  // the manifest rdump -p writes at the top of the host tree, and rwrite -p reads back
#define E4_SIDECAR_SUFFIX ".ext4meta"      // appended to a host file's name for the sidecar dump -p writes and write -p reads back
#define E4_HEAL_NAME      ".ext4edit-heal" // the probe entry E4_NewDirHeal adds to and removes from a new directory so lwext4 rewrites its block
#define E4_SECTOR_BYTES   512              // physical block size the image block device presents to lwext4

#define E4_HD_ENTRY       1                 // E4_HostDirRead: an entry's name was handed back
#define E4_HD_END         0                 // E4_HostDirRead: the directory has no more entries
#define E4_HD_ERROR       (-1)              // E4_HostDirRead: the enumeration itself failed part-way
#define E4_HD_BADNAME     (-2)              // E4_HostDirRead: an entry exists but its name cannot be handed back (not valid UTF-16, or too long)
#define E4_FIND_FOUND     1                 // E4_FindInDir: the name is in the directory (its type handed back)
#define E4_FIND_ABSENT    0                 // E4_FindInDir: the directory was read whole and the name is not in it, or the directory itself is missing
#define E4_FIND_ERROR     (-1)              // E4_FindInDir: the directory could not be opened or read in full, so nothing can be said (reported)
#define E4_IO_BUF         65536             // streaming buffer for cat / write (bytes)
#define E4_MAX_POSARGS    4                 // positional args after the command (chown has 3)
#define E4_IMPORT_FILE_MODE 0644            // default perms for a file rwrite imports
#define E4_IMPORT_DIR_MODE  0755            // perms for a dir rwrite imports (needs traverse)

#define E4_SUCCESS        0                 // process exit code: success
#define E4_FAILURE        1                 // process exit code: usage / I/O failure

/*------------------------------------------------------------------------------*/
/* A collected image directory entry -- its name and lwext4 inode_type.         */
/* The extract walk collects a whole directory into an array and CLOSES it      */
/* before descending or reading files -- collect first, then act -- so only one */
/* lwext4 directory iterator is ever live at a time, whatever the depth.        */
/*------------------------------------------------------------------------------*/
struct E4_DirEnt
{
    char szName[256];   // entry name (name_length is a uint8_t, so 256 always fits)
    int  iType;         // the ext4_direntry.inode_type (EXT4_DE_*)
};

/*-----------------------------------------------------------------------------*/
/* A host (Windows) directory walk in progress.  The host tree walk uses the   */
/* wide FindFirstFileW / FindNextFileW instead of the CRT opendir / readdir so */
/* it can enumerate a directory whose path exceeds 260 characters: MinGW's     */
/* _wopendir has a long-path defect (it can silently read the CURRENT          */
/* directory), and only the wide \\?\ Win32 calls honour the extended length.  */
/* FindFirstFileW returns the first entry immediately, so bPrimed marks that   */
/* entry as waiting -- the first read hands it out before any advance.         */
/*-----------------------------------------------------------------------------*/
struct E4_HostDir
{
    HANDLE           hFind;   // FindFirstFileW search handle; INVALID_HANDLE_VALUE = none open
    WIN32_FIND_DATAW wfd;     // the most recent entry FindFirstFileW / FindNextFileW produced
    bool             bPrimed; // true while wfd holds an entry not yet handed to the caller
};

/*----------------------------------------------------------------------------*/
/* One level of a host tree being IMPORTED (rwrite), held on the heap by the  */
/* explicit stack E4_RwriteTree walks instead of recursing: a frame is pushed */
/* when a host directory is entered and popped when its children are done,    */
/* so the depth a tree may have is bounded by memory, never by the thread's   */
/* stack.  What a recursive call would have kept in locals lives here.        */
/*----------------------------------------------------------------------------*/
struct E4_ImportFrame
{
    char              szHost[E4_MAX_PATH]; // this directory's host path
    char              szImg[E4_MAX_PATH];  // its path inside the image
    char              szRel[E4_MAX_PATH];  // its path relative to the import's top ("" at the top), the manifest key
    struct E4_HostDir hd;                  // the open walk of its host children
    uint32_t          uiMtime;             // the modify time it is stamped with once its children are in
    int               iRc;                 // E4_SUCCESS unless a child of it failed
};

/*----------------------------------------------------------------------------*/
/* One level of an image tree being EXTRACTED (rdump), held on the heap by    */
/* the explicit stack E4_RdumpTree walks instead of recursing; the same       */
/* reasoning as E4_ImportFrame.  The level's entries were collected in full   */
/* (E4_CollectDir) when the frame was pushed, so no lwext4 directory iterator */
/* stays open across the walk.                                                */
/*----------------------------------------------------------------------------*/
struct E4_ExtractFrame
{
    char              szImg[E4_MAX_PATH];  // this directory's path inside the image
    char              szHost[E4_MAX_PATH]; // the matching host directory (already created)
    char              szRel[E4_MAX_PATH];  // its path relative to the extract's top ("" at the top), for the manifest
    struct E4_DirEnt *pEnts;               // its collected entries (E4_CollectDir's array; freed at the pop)
    int               nEnts;               // how many entries pEnts holds
    int               iNext;               // the next entry to act on
    bool              bStamp;              // true to set the host directory's times at the pop (-t, and not the top, whose host directory is the caller's)
    int               iRc;                 // E4_SUCCESS unless an entry under it failed
};

/*----------------------------------------------------------------------------*/
/* The metadata options of one creating command (`write`, `rwrite`, `mkdir`,  */
/* `symlink`): what the command line asked for.  Each value has its own "was  */
/* it given" flag, because an option that was NOT given must leave the choice */
/* to the next source down -- a manifest record, then the existing file, then */
/* the defaults -- and 0 is a legal value for every one of them.              */
/*----------------------------------------------------------------------------*/
struct E4_ImportOpts
{
    bool     bMode;      // true when --mode was given
    uint32_t uiMode;     // --mode: permission bits for imported FILES and for a mkdir'd directory (an imported tree's directories stay 0755 unless a manifest says otherwise)
    bool     bOwner;     // true when --owner was given
    uint32_t uiUid;      // --owner: numeric owner for everything created
    uint32_t uiGid;      // --owner: numeric group for everything created
    bool     bMtime;     // true when --mtime was given a number or "now"
    uint32_t uiMtime;    // --mtime: modify time for everything created, UTC epoch seconds ("now" is this run's moment, E4_uiNow)
    bool     bMtimeHost; // true when --mtime host was given: write and rwrite keep each host file's own modify time instead of this run's moment
};

/*-----------------------------------------------------------------------------*/
/* One record of a metadata manifest or sidecar, as `rdump -p` / `dump -p`     */
/* wrote it and `rwrite -p` / `write -p` reads it back.  The two strings point */
/* INTO the loaded text of the file (E4_MetaSet.pszText), which is cut into    */
/* NUL-terminated pieces in place, so a manifest of any size costs one copy of */
/* its text plus this small struct per entry.                                  */
/*-----------------------------------------------------------------------------*/
struct E4_MetaRec
{
    const char *pszPath;    // the `path:` value -- relative to the tree's top, '/'-separated; never NULL in a kept record
    const char *pszTarget;  // the `target:` value of a symlink record; NULL when the block had none
    int         iType;      // EXT4_DE_* decoded from the `type:` line; EXT4_DE_UNKNOWN for a label this tool does not write
    bool        bMode;      // true when the block had a `mode:` line
    uint32_t    uiMode;     // permission bits (octal in the file)
    bool        bOwner;     // true when the block had BOTH `uid:` and `gid:`
    uint32_t    uiUid;      // numeric owner
    uint32_t    uiGid;      // numeric group
    bool        bMtime;     // true when the block had a `mtime:` line
    uint32_t    uiMtime;    // modify time, UTC epoch seconds
    bool        bAtime;     // true when the block had an `atime:` line
    uint32_t    uiAtime;    // access time, UTC epoch seconds
    bool        bUsed;      // set when an imported entry took this record; what is still false at the end was in the manifest and not on the host
};

/*-----------------------------------------------------------------------------*/
/* A loaded manifest: the file's text and the records cut out of it, sorted by */
/* path so each imported entry finds its record by binary search.              */
/*-----------------------------------------------------------------------------*/
struct E4_MetaSet
{
    char              *pszText;  // the whole file, malloc'd, cut into strings in place; the records point into it
    struct E4_MetaRec *pRecs;    // malloc'd array of records, sorted by pszPath (strcmp order)
    int                nRecs;    // how many records pRecs holds
};

/*------------------------------------------------------------------------------*/
/* Forward declarations for the host-path layer.  These wide / \\?\ host-file   */
/* wrappers and the host-name validator are DEFINED further down beside the     */
/* other host helpers, but the image block device and some commands that appear */
/* earlier in the file call them, so they are prototyped here.                  */
/*------------------------------------------------------------------------------*/
static int   E4_HostWiden(const char *pszHost, wchar_t *pwOut, size_t cchOut);
static FILE *E4_HostFopen(const char *pszHost, const char *pszMode);
static int   E4_HostStat(const char *pszHost, struct _stat64 *pst);
static int   E4_HostMkdir(const char *pszHost);
static int   E4_HostUtime(const char *pszHost, time_t atime, time_t mtime);
static int   E4_HostDirOpen(struct E4_HostDir *pd, const char *pszHostDir);
static int   E4_HostDirRead(struct E4_HostDir *pd, char *pszNameOut, size_t cchOut);
static void  E4_HostDirClose(struct E4_HostDir *pd);
static bool  E4_HostNameValid(const char *pszPath);

/*----------------------------------------------------------------------------*/
/* Forward declarations for the import-metadata helpers, defined beside the   */
/* manifest writer further down and called from Cmd_Mkdir, Cmd_Write and      */
/* Cmd_Symlink above them.                                                    */
/*----------------------------------------------------------------------------*/
static int   E4_MetaLoad(const char *pszHostFile, struct E4_MetaSet *pSet);
static void  E4_MetaFree(struct E4_MetaSet *pSet);
static void  E4_ApplyImportMeta(const char *pszImgPath, bool bIsDir,
                                const struct E4_ImportOpts *pOpts,
                                const struct E4_MetaRec *pRec,
                                uint32_t uiDefMode, uint32_t uiDefUid,
                                uint32_t uiDefGid, uint32_t uiDefMtime);

/*-----------------------------------------------------------------------------*/
/* The image block device's state -- documentation table.  Set by E4_Open,     */
/* used by the four E4bd_ hooks lwext4 calls.  One image per process, so these */
/* are plain statics.                                                          */
/*-----------------------------------------------------------------------------*/
static wchar_t  E4bd_wszImage[E4_MAX_WPATH];    // the image file's wide \\?\ path, built by E4_Open before the mount
static bool     E4bd_bReadOnly;                 // true: open the file "rb" and refuse every block write
static FILE    *E4bd_pFile;                     // the open image file; NULL while closed
static int      E4bd_iOpenErrno;                // errno from the last failed open of the image file; 0 when the file opened

/*----------------------------------------------------------------------------*/
/* The console code page in force when the program started, so it can be put  */
/* back at exit (see E4_ConsoleUtf8).                                         */
/*----------------------------------------------------------------------------*/
static UINT     E4_uiOldConsoleCp;              // the console's output code page at start; 0 = none recorded, nothing to restore

/*-----------------------------------------------------------------------------*/
/* The clock, read ONCE at the start of the run (main).  Every time this tool  */
/* stamps on its own account -- a created entry's times, a changed inode's     */
/* change time, a parent directory's times -- is this one moment, so a whole   */
/* rwrite shares one timestamp instead of a time that creeps as it copies, and */
/* the entries of one command can be told apart from those of the next.        */
/*-----------------------------------------------------------------------------*/
static uint32_t E4_uiNow;                       // this run's moment, UTC epoch seconds; set before any command runs

/*----------------------------------------------------------------------------*/
/* Log_Err:                                                                   */
/*                                                                            */
/* Error / status line to stderr with a fixed "ext4edit: " prefix and a       */
/* trailing newline.  lwext4's own debug output is compiled off, so this is   */
/* the tool's only diagnostic channel; normal listing output goes to stdout.  */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszFmt : printf-style format string.                                   */
/*     ...    : the format arguments.                                         */
/*                                                                            */
/* Returns:                                                                   */
/*     void : writes one line to stderr.                                      */
/*----------------------------------------------------------------------------*/
static void Log_Err(const char *pszFmt, ...)
{
    va_list args;

    /* The line is emitted as THREE separate stdio writes rather than composed     */
    /* into one local buffer first.  That means no fixed-size scratch buffer and   */
    /* so no truncation of a long message, and it is safe because the program is   */
    /* single-threaded -- nothing can interleave between the three writes and      */
    /* split one diagnostic across two lines.                                      */

    /* Write 1: the fixed prefix that tags every diagnostic as this tool's, so a   */
    /* line is identifiable when stdout and stderr are mixed in a console.         */
    fputs("ext4edit: ", stderr);

    /* Write 2: the caller's printf-style message, expanded straight onto    */
    /* stderr through the va_list -- va_start / va_end bracket exactly the   */
    /* vfprintf that consumes the variadic arguments.                        */
    va_start(args, pszFmt);
    vfprintf(stderr, pszFmt, args);
    va_end(args);

    /* Write 3: the terminating newline.  Callers pass their message WITHOUT a   */
    /* trailing '\n', so it is added in exactly one place here.                  */
    fputc('\n', stderr);
}

/*----------------------------------------------------------------------------*/
/* E4_TypeName:                                                               */
/*                                                                            */
/* Map an lwext4 directory-entry inode_type to a short label for listings.    */
/*                                                                            */
/* Arguments:                                                                 */
/*     iType : the ext4_direntry.inode_type value (EXT4_DE_*).                */
/*                                                                            */
/* Returns:                                                                   */
/*     const char * : a static label; "?" for anything unrecognised.          */
/*----------------------------------------------------------------------------*/
static const char *E4_TypeName(int iType)
{
    switch (iType)
    {   /* Map each EXT4_DE_* type to a short label; default names the rest.      */
        /* The labels are the same words E4_ModeTypeName gives an inode's mode,   */
        /* so `ls` and `stat` describe one entry identically.                     */
        /* A regular file.                                                        */
        case EXT4_DE_REG_FILE: return("file");
        /* A subdirectory. */
        case EXT4_DE_DIR:      return("dir");
        /* A symbolic link. */
        case EXT4_DE_SYMLINK:  return("symlink");
        /* A character-special device node. */
        case EXT4_DE_CHRDEV:   return("chardev");
        /* A block-special device node. */
        case EXT4_DE_BLKDEV:   return("blockdev");
        /* A named pipe (FIFO). */
        case EXT4_DE_FIFO:     return("fifo");
        /* A unix-domain socket. */
        case EXT4_DE_SOCK:     return("socket");
        /* Any EXT4_DE_* value not named above (EXT4_DE_UNKNOWN included). */
        default:               return("?");
    }
}

/*----------------------------------------------------------------------------*/
/* E4_ModeTypeName:                                                           */
/*                                                                            */
/* Map the file-type bits of an inode mode word to the same short label that  */
/* E4_TypeName gives a directory entry, so `stat` and `ls` read alike.  The   */
/* type is the top nibble of the mode (EXT4_INODE_MODE_TYPE_MASK); the low 12 */
/* bits are the permission bits and are not consulted here.                   */
/*                                                                            */
/* Arguments:                                                                 */
/*     uiMode : the inode mode word (an ext4_inode_get_mode result).          */
/*                                                                            */
/* Returns:                                                                   */
/*     const char * : a static label; "?" for an unrecognised type.           */
/*----------------------------------------------------------------------------*/
static const char *E4_ModeTypeName(uint32_t uiMode)
{
    switch (uiMode & EXT4_INODE_MODE_TYPE_MASK)
    {   /* One case per inode mode-type nibble (the top bits of the mode word);   */
        /* the value beside each is the nibble as ext4 stores it.                 */
        /* 0x8000: a regular file.                                                */
        case EXT4_INODE_MODE_FILE:      return("file");
        /* 0x4000: a directory. */
        case EXT4_INODE_MODE_DIRECTORY: return("dir");
        /* 0xA000: a symbolic link. */
        case EXT4_INODE_MODE_SOFTLINK:  return("symlink");
        /* 0x2000: a character device. */
        case EXT4_INODE_MODE_CHARDEV:   return("chardev");
        /* 0x6000: a block device. */
        case EXT4_INODE_MODE_BLOCKDEV:  return("blockdev");
        /* 0x1000: a named pipe. */
        case EXT4_INODE_MODE_FIFO:      return("fifo");
        /* 0xC000: a unix socket. */
        case EXT4_INODE_MODE_SOCKET:    return("socket");
        /* Any mode-type nibble not named above. */
        default:                        return("?");
    }
}

/*----------------------------------------------------------------------------*/
/* E4_ParseUint:                                                              */
/*                                                                            */
/* Parse a whole unsigned integer in a given base -- base 8 for a chmod mode, */
/* base 10 for a uid / gid -- rejecting anything that is not a clean number   */
/* so a typo cannot be silently turned into 0 and written to an inode.        */
/*                                                                            */
/* Arguments:                                                                 */
/*     psz    : the text to parse (must start with a digit).                  */
/*     iBase  : the numeric base (8 for octal mode, 10 for uid / gid).        */
/*     pulOut : receives the parsed value on success.                         */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true on a clean parse, false on empty / sign / junk / overflow. */
/*----------------------------------------------------------------------------*/
static bool E4_ParseUint(const char *psz, int iBase, unsigned long *pulOut)
{
    char          *pszEnd;
    unsigned long  ulVal;

    /* Reject a non-digit FIRST CHARACTER before strtoul ever sees the token.      */
    /* strtoul is deliberately lenient -- it skips leading spaces and accepts a    */
    /* leading '+' or '-' -- so "-1" would parse to ULONG_MAX and " 8" to 8, and   */
    /* either value, handed on to chmod or chown, silently writes a garbage mode   */
    /* or owner onto a real inode.  A command-line mode / id is a bare run of      */
    /* digits, so anything that does not START with one is refused up front.       */
    if (psz == NULL || psz[0] < '0' || psz[0] > '9')
    {   /* Empty, or a leading sign / space / letter -- not a plain number. */
        return(false);
    }

    /* Convert, then demand strtoul consumed the WHOLE token.  errno set is      */
    /* overflow (did not fit unsigned long); pszEnd == psz is "no digits at      */
    /* all"; *pszEnd != '\0' is trailing junk like "0755x".  The full-           */
    /* consumption test is the point: strtoul's default is to parse as far as    */
    /* it can and stop, so without it "755bad" would be accepted as 755 -- and   */
    /* a half-read mode is worse than a rejected one, because it looks like it   */
    /* worked.                                                                   */
    errno  = 0;
    pszEnd = NULL;
    ulVal  = strtoul(psz, &pszEnd, iBase);

    if (errno != 0 || pszEnd == psz || *pszEnd != '\0')
    {   /* Overflow, no digits consumed, or trailing junk after the number. */
        return(false);
    }

    /* Clean and fully consumed: store the value and report success, so the   */
    /* caller (a chmod mode, or a chown uid / gid) uses *pulOut only when     */
    /* true is returned -- it is left untouched on every reject path above.   */
    *pulOut = ulVal;
    return(true);
}

/*-----------------------------------------------------------------------------*/
/* E4bd_DevOpen:                                                               */
/*                                                                             */
/* lwext4's "open device" hook for the image file.  This tool carries its own  */
/* file block device instead of lwext4's bundled one for three reasons, each a */
/* defect of the bundled one: it opened the file read+write even for a         */
/* read-only mount (so `ls` failed on a write-protected image), it took a      */
/* narrow path (so no long path and no name outside the ANSI code page), and   */
/* it measured the file with a 32-bit ftell.  Here a read-only session opens   */
/* "rb", the path is the wide \\?\ form, and the size is read with the 64-bit  */
/* calls.  lwext4 calls this from inside ext4_mount.                           */
/*                                                                             */
/* Arguments:                                                                  */
/*     pDev : the device being opened (always E4bd_device).                    */
/*                                                                             */
/* Returns:                                                                    */
/*     int : EOK when the file is open and measured; EIO when it cannot be     */
/*           opened or measured (E4bd_iOpenErrno then holds the reason an      */
/*           open failed, for E4_Open's message).                              */
/*-----------------------------------------------------------------------------*/
static int E4bd_DevOpen(struct ext4_blockdev *pDev)
{
    long long llSize;

    /* Assume the open works; the failure branch below records the real reason. */
    E4bd_iOpenErrno = 0;

    if (E4bd_bReadOnly)
    {   /* Read-only session: never ask the OS for write access, so a             */
        /* write-protected image (or one on read-only media) can still be read.   */
        E4bd_pFile = _wfopen(E4bd_wszImage, L"rb");
    }
    else
    {   /* Writable session: read and write the EXISTING file in place.  "r+b"     */
        /* never creates and never truncates -- a mistyped image name must fail,   */
        /* not leave a new empty file behind.                                      */
        E4bd_pFile = _wfopen(E4bd_wszImage, L"r+b");
    }

    if (E4bd_pFile == NULL)
    {   /* Missing, locked by another program, or no permission.  Keep errno       */
        /* NOW: lwext4 reports only EIO upward, and later calls overwrite errno.   */
        E4bd_iOpenErrno = errno;
        return(EIO);
    }

    /* No C-runtime buffer.  lwext4 has its own block cache and decides when a   */
    /* block is written; a second buffer here would only delay the moment a      */
    /* write reaches the file, and would make a flush mean less than it says.    */
    setbuf(E4bd_pFile, NULL);

    /* Measure the file with the 64-bit calls: an image is routinely over 2 GiB,   */
    /* where the 32-bit ftell reports a failure or a wrapped length.               */
    if (_fseeki64(E4bd_pFile, 0, SEEK_END) != 0)
    {   /* The file cannot be seeked (not a regular file). */
        fclose(E4bd_pFile);
        E4bd_pFile = NULL;
        return(EIO);
    }

    llSize = _ftelli64(E4bd_pFile);

    if (llSize < 0)
    {   /* The size could not be read. */
        fclose(E4bd_pFile);
        E4bd_pFile = NULL;
        return(EIO);
    }

    /* Tell lwext4 the device's extent: the filesystem is the WHOLE file (this     */
    /* tool takes a partition image, not a disk image), so the partition starts    */
    /* at 0 and runs the file's length, and the physical block count is that       */
    /* length in sectors.  A tail shorter than one sector is not addressable and   */
    /* is left out of the count.                                                   */
    pDev->part_offset   = 0;
    pDev->part_size     = (uint64_t)llSize;
    pDev->bdif->ph_bcnt = (uint64_t)llSize / pDev->bdif->ph_bsize;

    /* The image is open and its extent is set. */
    return(EOK);
}

/*----------------------------------------------------------------------------*/
/* E4bd_DevRead:                                                              */
/*                                                                            */
/* lwext4's "read blocks" hook: read uiCount physical blocks starting at      */
/* block ullBlock, counted from the start of the file.                        */
/*                                                                            */
/* Arguments:                                                                 */
/*     pDev     : the device (supplies the physical block size).              */
/*     pvBuf    : receives uiCount * ph_bsize bytes.                          */
/*     ullBlock : first physical block, from the start of the file.           */
/*     uiCount  : how many blocks; 0 is a legal no-op lwext4 does issue.      */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK on a complete read, EIO on a seek failure or a short read.   */
/*----------------------------------------------------------------------------*/
static int E4bd_DevRead(struct ext4_blockdev *pDev, void *pvBuf, uint64_t ullBlock, uint32_t uiCount)
{
    size_t cbWanted;

    if (E4bd_pFile == NULL)
    {   /* The device is not open. */
        return(EIO);
    }

    /* Seek with the 64-bit call: block * 512 passes 2 GiB on any real image. */
    if (_fseeki64(E4bd_pFile, (long long)(ullBlock * pDev->bdif->ph_bsize), SEEK_SET) != 0)
    {   /* The block lies outside the file. */
        return(EIO);
    }

    if (uiCount == 0)
    {   /* Nothing to transfer; the seek alone was the request. */
        return(EOK);
    }

    /* The byte count of the request, in size_t so the product cannot narrow:   */
    /* at most 2^32 blocks of 512 bytes, well inside a 64-bit size_t.           */
    cbWanted = (size_t)pDev->bdif->ph_bsize * (size_t)uiCount;

    if (fread(pvBuf, 1, cbWanted, E4bd_pFile) != cbWanted)
    {   /* A short read: the image is truncated or the disk failed.  It is an    */
        /* error, never "the rest is zeros" -- a filesystem read back short is   */
        /* a damaged filesystem.                                                 */
        return(EIO);
    }

    /* Every requested byte was read. */
    return(EOK);
}

/*-----------------------------------------------------------------------------*/
/* E4bd_DevWrite:                                                              */
/*                                                                             */
/* lwext4's "write blocks" hook: write uiCount physical blocks starting at     */
/* block ullBlock.  A read-only session refuses here as well as at the mount,  */
/* so no path through lwext4 can modify an image that was opened for a dry run */
/* or a listing -- the dry-run promise does not rest on lwext4 alone.          */
/*                                                                             */
/* Arguments:                                                                  */
/*     pDev     : the device (supplies the physical block size).               */
/*     pvBuf    : the uiCount * ph_bsize bytes to write.                       */
/*     ullBlock : first physical block, from the start of the file.            */
/*     uiCount  : how many blocks; 0 is a legal no-op.                         */
/*                                                                             */
/* Returns:                                                                    */
/*     int : EOK on a complete write, EROFS on a read-only session, EIO on a   */
/*           seek failure or a short write (a full host disk).                 */
/*-----------------------------------------------------------------------------*/
static int E4bd_DevWrite(struct ext4_blockdev *pDev, const void *pvBuf, uint64_t ullBlock, uint32_t uiCount)
{
    size_t cbWanted;

    if (E4bd_pFile == NULL)
    {   /* The device is not open. */
        return(EIO);
    }

    if (E4bd_bReadOnly)
    {   /* The file itself is open "rb"; a write could not succeed anyway. */
        return(EROFS);
    }

    if (_fseeki64(E4bd_pFile, (long long)(ullBlock * pDev->bdif->ph_bsize), SEEK_SET) != 0)
    {   /* The block lies outside the file. */
        return(EIO);
    }

    if (uiCount == 0)
    {   /* Nothing to transfer. */
        return(EOK);
    }

    /* The byte count of the request, as in the read hook. */
    cbWanted = (size_t)pDev->bdif->ph_bsize * (size_t)uiCount;

    if (fwrite(pvBuf, 1, cbWanted, E4bd_pFile) != cbWanted)
    {   /* A short write: the HOST disk holding the image is full, or failed. */
        return(EIO);
    }

    /* Every byte was handed to the OS (the stream is unbuffered). */
    return(EOK);
}

/*----------------------------------------------------------------------------*/
/* E4bd_DevClose:                                                             */
/*                                                                            */
/* lwext4's "close device" hook: close the image file.  lwext4 calls it from  */
/* ext4_umount, after it has written its cache out.                           */
/*                                                                            */
/* Arguments:                                                                 */
/*     pDev : the device being closed (not needed; there is only one).        */
/*                                                                            */
/* Returns:                                                                   */
/*     int : EOK when the file closed cleanly (or was not open); EIO when the */
/*           close itself failed, which on a written image means the last     */
/*           bytes may not have reached the disk.                             */
/*----------------------------------------------------------------------------*/
static int E4bd_DevClose(struct ext4_blockdev *pDev)
{
    struct ext4_blockdev *pUnused;
    int                   iRc;

    /* The interface supplies the device; this single-device tool needs no part   */
    /* of it.  A dummy assignment keeps the parameter "used".                     */
    pUnused = pDev;
    pUnused = pUnused;

    /* Nothing open means nothing to do, and that is not a failure. */
    iRc = EOK;

    if (E4bd_pFile != NULL)
    {   /* Close the image and forget the handle whatever the close returned. */
        if (fclose(E4bd_pFile) != 0)
        {   /* The OS refused the final flush (a full or failed host disk). */
            iRc = EIO;
        }

        E4bd_pFile = NULL;
    }

    /* EOK, or EIO when the close failed. */
    return(iRc);
}

/*----------------------------------------------------------------------------*/
/* The one static image block device: 512-byte physical blocks, a block count */
/* filled in at open, and no lock functions (the tool is single-threaded).    */
/*----------------------------------------------------------------------------*/
EXT4_BLOCKDEV_STATIC_INSTANCE(E4bd_device, E4_SECTOR_BYTES, 0, E4bd_DevOpen,
                              E4bd_DevRead, E4bd_DevWrite, E4bd_DevClose, 0, 0);

/*------------------------------------------------------------------------------*/
/* E4_Open:                                                                     */
/*                                                                              */
/* Point the image block device at the image file, register it with lwext4      */
/* and mount it.  For a writable mount the journal is recovered (any pending    */
/* transaction replayed) and started, and the block cache is put in             */
/* write-back mode, so a batch of changes commits efficiently and consistently. */
/* Two filesystems are REFUSED for writing first, off the superblock: one       */
/* whose journal is on another device, and a fast_commit one that still needs   */
/* recovery (the body says why each); both still open read-only.                */
/*                                                                              */
/* Arguments:                                                                   */
/*     pszImage  : path to the ext2/3/4 image file.                             */
/*     bReadOnly : true to mount read-only (listing / dry run), false to        */
/*                 mount read-write (actual removal).                           */
/*                                                                              */
/* Returns:                                                                     */
/*     int : E4_SUCCESS on a mounted filesystem, E4_FAILURE otherwise (with     */
/*           the reason already reported).                                      */
/*------------------------------------------------------------------------------*/
static int E4_Open(const char *pszImage, bool bReadOnly)
{
    struct ext4_sblock *sb;
    int                 rc;

    /* Give the block device its two settings BEFORE the mount, because lwext4   */
    /* opens the device from inside ext4_mount (E4bd_DevOpen) and reads them     */
    /* there.  The path is widened to the \\?\ form by the same routine every    */
    /* host file goes through, so the image may sit under a long path or carry   */
    /* a name outside the ANSI code page like any other host file.  The device   */
    /* is a single static, which is why this tool opens ONE image at a time.     */
    if (E4_HostWiden(pszImage, E4bd_wszImage, E4_MAX_WPATH) != E4_SUCCESS)
    {   /* The image path is not valid text, or is too long to make absolute. */
        Log_Err("cannot use image path: %s", pszImage);
        return(E4_FAILURE);
    }

    /* The device's two settings: the access the open hook asks for, and a clean   */
    /* open-error slot for the hook to fill if that open fails.                    */
    E4bd_bReadOnly  = bReadOnly;
    E4bd_iOpenErrno = 0;

    /* Register the device under E4_DEV_NAME so ext4_mount can find it by       */
    /* that name below.  The name itself is arbitrary, but the register and     */
    /* the mount MUST agree on it -- it is a single #define for exactly that    */
    /* reason, so the two cannot drift apart into a silent "device not found"   */
    /* at mount time.                                                           */
    rc = ext4_device_register(&E4bd_device, NULL, E4_DEV_NAME);
    if (rc != 0)
    {   /* Registration failed (name clash or allocation). */
        Log_Err("device register failed: %s", strerror(rc));
        return(E4_FAILURE);
    }

    /* Mount at "/" (E4_MP).  Mounting at the root is why every path this tool       */
    /* takes is ABSOLUTE -- there is no working directory inside the image for a     */
    /* relative path to resolve against.  This is also the first call that TOUCHES   */
    /* the image file: lwext4 opens the device under ext4_mount.  So a failure       */
    /* here is one of two different things, and the two are told apart by whether    */
    /* the device recorded an open error: the FILE could not be opened (missing,     */
    /* locked, or write-protected on a writable run), or it opened and is not a      */
    /* filesystem lwext4 can mount.  One message for both sent the operator to       */
    /* check a good image when the real fault was a wrong path.                      */
    rc = ext4_mount(E4_DEV_NAME, E4_MP, bReadOnly);
    if (rc != 0)
    {   /* The mount failed; say which of the two causes it was. */
        if (E4bd_iOpenErrno != 0)
        {   /* The image file itself could not be opened. */
            Log_Err("cannot open image file %s: %s", pszImage, strerror(E4bd_iOpenErrno));
        }
        else
        {   /* The file opened; it is not a mountable ext2/3/4 filesystem. */
            Log_Err("mount failed (is this an ext2/3/4 image?): %s", strerror(rc));
        }

        return(E4_FAILURE);
    }

    if (!bReadOnly)
    {   /* A writable mount first refuses the two journals it must not write        */
        /* through, then brings the journal up in this exact order: recover, then   */
        /* start, then enable write-back.                                           */

        /* The superblock, for the two refusals below.  Without it the journal     */
        /* cannot be judged, and a write that cannot be judged safe is not made.   */
        sb = NULL;
        rc = ext4_get_sblock(E4_MP, &sb);

        if (rc != 0 || sb == NULL)
        {   /* No superblock to read the journal's shape from. */
            Log_Err("cannot read the superblock: %s", strerror(rc));
            ext4_umount(E4_MP);
            return(E4_FAILURE);
        }

        if (ext4_sb_feature_com(sb, EXT4_FCOM_HAS_JOURNAL) &&
            ext4_get32(sb, journal_inode_number) == 0)
        {   /* has_journal with no journal inode: the journal lives on ANOTHER       */
            /* device (a filesystem made with mke2fs -J device=...).  lwext4 would   */
            /* look for it at inode 0 and fail the recovery with "Result too         */
            /* large", a reason nobody can act on -- and a journal it cannot reach   */
            /* is one it can neither replay nor write, so no write is safe.  The     */
            /* read-only commands never touch the journal and still work.            */
            Log_Err("refusing to write: the filesystem's journal is on a separate device (an external journal), which this tool cannot reach; the read-only commands still work");
            ext4_umount(E4_MP);
            return(E4_FAILURE);
        }

        if (ext4_sb_feature_com(sb, EXT4_FCOM_FAST_COMMIT) &&
            ext4_sb_feature_incom(sb, EXT4_FINCOM_RECOVER))
        {   /* fast_commit (Linux 5.10 onward) keeps a second log of small         */
            /* changes at the end of the journal that only the Linux driver can    */
            /* replay.  While the filesystem is marked as needing recovery those   */
            /* fast commits may be the newest truth, and replaying the ordinary    */
            /* journal alone -- which is what ext4_recover below does -- would     */
            /* write over them.  A cleanly unmounted fast_commit filesystem has    */
            /* nothing pending and is written as usual.  Nothing has touched the   */
            /* journal here, so a Linux mount can still replay it.                 */
            Log_Err("refusing to write: the image was not cleanly unmounted and its journal may hold fast commits this tool cannot replay; mount it on Linux once, or use the read-only commands");
            ext4_umount(E4_MP);
            return(E4_FAILURE);
        }

        /* RECOVER FIRST.  An image carved straight off a live device can carry a   */
        /* journal holding a transaction that was committed but not yet written     */
        /* back into the main filesystem.  Replaying it here makes the filesystem   */
        /* match what the live system intended BEFORE we add our own edits; if we   */
        /* skipped it and wrote anyway, a later mount that replays that stale       */
        /* journal could overwrite exactly what we changed.  ENOTSUP is not an      */
        /* error -- it just means there is no journal (an ext2 image, or an ext4    */
        /* without one), the common case for a clean dump -- so only a different    */
        /* code is warned, and even then not fatally, since the mount succeeded.    */
        rc = ext4_recover(E4_MP);

        if (rc != 0 && rc != ENOTSUP)
        {   /* A genuine recovery error -- worth a warning, not a stop (see above). */
            Log_Err("warning: journal recovery returned %s", strerror(rc));
        }

        /* START THE JOURNAL so each removal below runs inside a transaction: a   */
        /* crash mid-batch then leaves the filesystem at a transaction boundary   */
        /* rather than half-rewritten.  It is transparent on a non-journaled      */
        /* image, so a real failure here means the write path cannot be made      */
        /* crash-safe -- unmount and refuse rather than write unprotected.        */
        rc = ext4_journal_start(E4_MP);
        if (rc != 0)
        {   /* The write path could not be made crash-safe, so do not write. */
            Log_Err("journal start failed: %s", strerror(rc));
            ext4_umount(E4_MP);
            return(E4_FAILURE);
        }

        /* Write-back cache ON so a batch of removals coalesces its metadata         */
        /* writes instead of forcing a flush after every unlink.  This pairs with    */
        /* E4_Close, which drains the cache and stops the journal before unmount:    */
        /* a crash between here and that drain loses the batch, which is the whole   */
        /* reason E4_Close exists and must be the counterpart to this call.          */
        ext4_cache_write_back(E4_MP, true);
    }

    /* Mounted; a writable mount also has its journal replayed and started and its   */
    /* write-back cache on, so the caller may read or (if !bReadOnly) edit.          */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* E4_Close:                                                                  */
/*                                                                            */
/* Flush and unmount the image opened by E4_Open.  For a writable mount this  */
/* drains the write-back cache and stops the journal first, so the on-disk    */
/* image is consistent when the process exits.  Then, once per run, it says   */
/* whether an internal check of lwext4's failed during the session.           */
/*                                                                            */
/* Arguments:                                                                 */
/*     bReadOnly : the same flag passed to E4_Open for this session.          */
/*                                                                            */
/* Returns:                                                                   */
/*     void : the image is flushed (when writable) and unmounted.  The one    */
/*            lwext4 device E4_Open registered stays registered (harmless --  */
/*            see the body).                                                  */
/*----------------------------------------------------------------------------*/
static void E4_Close(bool bReadOnly)
{
    const char *pszSite;
    int         iLine;

    if (!bReadOnly)
    {   /* Undo E4_Open's writable bring-up in REVERSE order: drain the write-back   */
        /* cache to the image, THEN stop the journal, THEN unmount below.  The       */
        /* order is load-bearing -- draining after the journal had stopped, or       */
        /* unmounting with the cache still dirty, would strand committed edits in    */
        /* memory and leave an on-disk image that fsck flags or that silently lost   */
        /* this session's last removals.                                             */
        ext4_cache_write_back(E4_MP, false);
        ext4_journal_stop(E4_MP);
    }

    /* Unmount.  A read-only session armed none of the teardown above (E4_Open      */
    /* skipped it), so for a read there is nothing to drain or stop and this        */
    /* unmount is all that is owed.  The unmount is also what closes the image      */
    /* file (lwext4 calls E4bd_DevClose from it).  The lwext4 DEVICE that E4_Open   */
    /* registered is left registered, which is harmless for a tool that runs one    */
    /* command per process and then exits.                                          */
    ext4_umount(E4_MP);

    /* lwext4's assertions no longer stop the program: every assertion that data     */
    /* can reach became an error return its callers handle, and the macro that is    */
    /* left records the FIRST site that failed and lets execution go on.  A site     */
    /* recorded here means the library met a state it never expects -- damage in     */
    /* the image, or a fault of the library's own -- so this run's results deserve   */
    /* a second look; it is said once, at the close, where every command ends.       */
    iLine   = 0;
    pszSite = ext4_assert_last(&iLine);

    if (pszSite != NULL)
    {   /* An internal check failed during this session: say where. */
        Log_Err("warning: lwext4 reported an internal inconsistency at %s:%d -- check the image with e2fsck",
                pszSite, iLine);
        ext4_assert_clear();
    }
}

/*----------------------------------------------------------------------------*/
/* E4_SplitParent:                                                            */
/*                                                                            */
/* Split an absolute path into its parent directory and final component, so   */
/* the parent can be scanned for the component (the robust way to test that a */
/* name exists, symlinks included, without following it).                     */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszPath : the absolute path to split.                                  */
/*     pszDir  : buffer receiving the parent directory.                       */
/*     cchDir  : size of pszDir.                                              */
/*     ppszBase: receives a pointer to the final component within pszPath.    */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on a well-formed path, E4_FAILURE otherwise.          */
/*----------------------------------------------------------------------------*/
static int E4_SplitParent(const char *pszPath, char *pszDir, size_t cchDir,
                          const char **ppszBase)
{
    const char *pszSlash;
    size_t      cchParent;

    /* The LAST '/' is the split point: everything before it is the parent,   */
    /* everything after is the final component.  strrchr (last) not strchr    */
    /* (first), so "/a/b/c" splits into "/a/b" + "c", not "" + "a/b/c".       */
    pszSlash = strrchr(pszPath, '/');

    if (pszSlash == NULL || pszSlash == pszPath)
    {   /* Two cases fold together because both have the ROOT as the parent: a      */
        /* bare "name" with no slash (pszSlash == NULL), and a top-level "/name"    */
        /* whose only slash is at index 0 (pszSlash == pszPath).  The second is     */
        /* why this is special-cased: the normal path below would make its parent   */
        /* the empty string, and an empty parent fails to open as a directory, so   */
        /* the top level must yield "/" explicitly instead of falling through.      */
        if (cchDir < 2)
        {   /* No room even for "/" plus its NUL. */
            return(E4_FAILURE);
        }

        /* The parent is the root directory, written out as "/" explicitly. */
        pszDir[0] = '/';
        pszDir[1] = '\0';

        if (pszSlash == NULL)
        {   /* No slash at all: the whole path is the base name. */
            *ppszBase = pszPath;
        }
        else
        {   /* The base is the part just past the leading slash. */
            *ppszBase = pszSlash + 1;
        }

        /* The root parent and its base are set. */
        return(E4_SUCCESS);
    }

    /* Normal case: the parent is the span up to (not including) the last slash.   */
    /* The fit is checked BEFORE the memcpy -- a path longer than pszDir would     */
    /* overrun it, and silently truncating the parent would then scan the WRONG    */
    /* directory, so an over-long path is refused rather than clipped.             */
    cchParent = (size_t)(pszSlash - pszPath);

    if (cchParent + 1 > cchDir)
    {   /* Parent path does not fit the caller's buffer. */
        return(E4_FAILURE);
    }

    /* Copy the parent out and terminate it.  The component needs no buffer: it   */
    /* is handed back as a pointer INTO the caller's own path string, just past   */
    /* the slash, so it lives as long as that string does.                        */
    memcpy(pszDir, pszPath, cchParent);
    pszDir[cchParent] = '\0';
    *ppszBase = pszSlash + 1;

    /* Parent directory and final component split out for the caller. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* E4_TouchDir:                                                               */
/*                                                                            */
/* Give a directory this run's moment as its modify and change times, as a    */
/* kernel does whenever an entry is added to, removed from or renamed within  */
/* it.  lwext4 leaves a directory's times alone on every such change (its own */
/* code for it is commented out), so without this a directory edited by this  */
/* tool would still show the time of its last Linux change, and a backup tool */
/* keyed on directory times would miss what was done here.                    */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszDir : the directory inside the image.                               */
/*                                                                            */
/* Returns:                                                                   */
/*     void : the two times are set.  The lwext4 return codes go unchecked:   */
/*            the entry change this follows has already happened, and a time  */
/*            that could not be set is not worth failing the command over.    */
/*----------------------------------------------------------------------------*/
static void E4_TouchDir(const char *pszDir)
{
    /* Both times, the same moment: the content changed (mtime) and so did the   */
    /* inode (ctime).  The access time is left as it was -- reading the          */
    /* directory to change it is not an access a user made.                      */
    ext4_file_set_mtime(pszDir, E4_uiNow);
    ext4_file_set_ctime(pszDir, E4_uiNow);
}

/*----------------------------------------------------------------------------*/
/* E4_TouchParent:                                                            */
/*                                                                            */
/* E4_TouchDir on the directory that HOLDS a path -- the one whose entries    */
/* just changed when the path was created, removed or renamed.  The parent is */
/* found by E4_SplitParent, which gives "/" for a top-level path.             */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszPath : the absolute path whose containing directory changed.        */
/*                                                                            */
/* Returns:                                                                   */
/*     void : the parent's times are set; a path that cannot be split (too    */
/*            long for the buffer) touches nothing.                           */
/*----------------------------------------------------------------------------*/
static void E4_TouchParent(const char *pszPath)
{
    char        szDir[E4_MAX_PATH];
    const char *pszBase;

    if (E4_SplitParent(pszPath, szDir, sizeof(szDir), &pszBase) == E4_SUCCESS)
    {   /* The parent is known: stamp it. */
        E4_TouchDir(szDir);
    }
}

/*----------------------------------------------------------------------------*/
/* E4_NewDirHeal:                                                             */
/*                                                                            */
/* Settle the block checksum of a directory lwext4 has JUST created.  On a    */
/* filesystem with both metadata_csum and dir_index, lwext4 v1.0.0 builds the */
/* new directory's block but leaves its checksum stale until the directory is */
/* next modified, so e2fsck would report "directory passes checks but fails   */
/* checksum" for every directory created and left empty.  Any modification    */
/* makes lwext4 write the checksum properly, so this adds one entry and takes */
/* it away again, which leaves the directory empty and its block right.  The  */
/* step is harmless where it is not needed, so it is not made conditional on  */
/* the feature flags.  Called before the directory is stamped, so the stamp's */
/* times are what the directory ends with.                                    */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszDir : the directory just created, empty.                            */
/*                                                                            */
/* Returns:                                                                   */
/*     int : 0 when the entry went in and came out; otherwise the lwext4      */
/*           error (typically ENOSPC on a full filesystem), or ENAMETOOLONG   */
/*           when the probe's path would not fit.  A failure after the entry  */
/*           went in leaves the probe behind, which the caller reports.       */
/*----------------------------------------------------------------------------*/
static int E4_NewDirHeal(const char *pszDir)
{
    char      szProbe[E4_MAX_PATH];
    ext4_file f;
    int       rc;

    /* The probe's path: the directory, a slash, the fixed name.  The fit is     */
    /* tested first because _snprintf truncates silently, and a truncated path   */
    /* would create the probe somewhere else.                                    */
    if (strlen(pszDir) + 1 + sizeof(E4_HEAL_NAME) > sizeof(szProbe))
    {   /* No room for the probe's name after the directory's path. */
        return(ENAMETOOLONG);
    }

    _snprintf(szProbe, sizeof(szProbe), "%s/%s", pszDir, E4_HEAL_NAME);
    szProbe[sizeof(szProbe) - 1] = '\0';

    /* Create the probe as an ordinary empty file: this adds the entry, which   */
    /* is the modification that makes lwext4 rewrite the block and its          */
    /* checksum.  The directory is brand new and so empty, which is why the     */
    /* name cannot already be taken.                                            */
    rc = ext4_fopen(&f, szProbe, "wb");

    if (rc != 0)
    {   /* The directory could not be written to (a full filesystem, say). */
        return(rc);
    }

    /* Close the probe and remove it.  The removal rewrites the block again,   */
    /* leaving the directory empty with its checksum right.                    */
    ext4_fclose(&f);
    rc = ext4_fremove(szProbe);

    /* 0, or the removal's failure (the probe is then still there). */
    return(rc);
}

/*-----------------------------------------------------------------------------*/
/* E4_FindInDir:                                                               */
/*                                                                             */
/* Scan a directory for an entry with an exact name, reporting whether it      */
/* exists and its type.  Used to confirm a fixed-path target is present        */
/* before reporting or removing it.  The answer has THREE values, because a    */
/* directory that cannot be read whole can neither confirm nor deny a name:    */
/* reporting "absent" on it would let a removal say "nothing to do" and exit   */
/* clean on a damaged directory.                                               */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszDir  : the directory to scan.                                        */
/*     pszName : the exact entry name to look for.                             */
/*     piType  : receives the entry's inode_type when found.                   */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_FIND_FOUND with *piType set; E4_FIND_ABSENT when the directory */
/*           was read whole without the name, or is itself missing;            */
/*           E4_FIND_ERROR (reported) when it could not be opened for another  */
/*           reason or could not be read in full.                              */
/*-----------------------------------------------------------------------------*/
static int E4_FindInDir(const char *pszDir, const char *pszName, int *piType)
{
    ext4_dir             dir;
    const ext4_direntry *pEnt;
    char                 szName[256];
    int                  rc;
    int                  iFound;

    /* Default answer is "not present"; the scan below flips it only on an exact   */
    /* name match.                                                                 */
    iFound = E4_FIND_ABSENT;

    rc = ext4_dir_open(&dir, pszDir);
    if (rc != 0)
    {   /* The parent directory could not be opened.  MISSING is a "no" answer,     */
        /* not an error: callers use this to decide whether to remove something,    */
        /* and "the containing directory is gone" is the same outcome for them as   */
        /* "the entry is gone".  Any other reason -- the path names a file, the     */
        /* inode is damaged, a read failed -- is an error, since nothing can be     */
        /* said about a directory that did not open.                                */
        if (rc == ENOENT)
        {   /* No such directory. */
            return(E4_FIND_ABSENT);
        }

        Log_Err("cannot open directory %s: %s", pszDir, strerror(rc));
        return(E4_FIND_ERROR);
    }

    /* Scan the PARENT's own entries for an exact name.  Reading the name out of   */
    /* the directory, rather than opening the path, is the whole reason this       */
    /* function exists: opening would FOLLOW a symlink and report the target's     */
    /* type, but a target is frequently a symlink itself, so the type must come    */
    /* from the directory entry -- which names the link, not what it points at.    */
    for (;;)
    {   /* One directory entry per iteration. */
        pEnt = ext4_dir_entry_next(&dir);

        if (pEnt == NULL)
        {   /* End of the directory with no match found. */
            break;
        }

        if (pEnt->inode == 0)
        {   /* A dead slot: a deleted entry ext4 left in place with its inode   */
            /* field cleared, not a real name -- skip it.                       */
            continue;
        }

        /* ext4 names are counted, not NUL-terminated, so copy out and terminate     */
        /* before strcmp -- otherwise strcmp would read past this entry into the     */
        /* next record's bytes.  name_length is a uint8_t (<= 255), so the copy is   */
        /* always inside szName[256].                                                */
        memcpy(szName, pEnt->name, pEnt->name_length);
        szName[pEnt->name_length] = '\0';

        if (strcmp(szName, pszName) == 0)
        {   /* An exact-name match: record the entry's OWN type -- so the caller   */
            /* learns it is a symlink without following it -- and stop, since a    */
            /* name is unique within a directory.                                  */
            *piType = (int)pEnt->inode_type;
            iFound = E4_FIND_FOUND;
            break;
        }
    }

    /* Scan finished (end of directory reached, or an exact match broke out of   */
    /* the loop); release the directory handle.  last_err survives the close.    */
    ext4_dir_close(&dir);

    if (dir.last_err != EOK)
    {   /* The walk ended early: the directory's inode or one of its blocks could   */
        /* not be read, so the entries seen are NOT the whole directory and         */
        /* "absent" cannot be said.  (A match breaks out before the walk can end    */
        /* this way, so a FOUND answer never arrives here.)                         */
        Log_Err("cannot read directory %s in full: %s", pszDir, strerror(dir.last_err));
        return(E4_FIND_ERROR);
    }

    /* FOUND or ABSENT; *piType carries the type when found. */
    return(iFound);
}

/*----------------------------------------------------------------------------*/
/* Cmd_Ls:                                                                    */
/*                                                                            */
/* List a directory in the image (read-only): one line per entry with its     */
/* type and name.  "." and ".." are shown; unused slots are skipped.          */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszImage : the image file.                                             */
/*     pszDir   : the directory within the image to list.                     */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on a listed directory, E4_FAILURE otherwise.          */
/*----------------------------------------------------------------------------*/
static int Cmd_Ls(const char *pszImage, const char *pszDir)
{
    ext4_dir             dir;
    const ext4_direntry *pEnt;
    char                 szName[256];
    int                  rc;
    int                  iCount;

    /* Mount READ-ONLY.  Listing never changes the image, and a read-only mount     */
    /* skips the journal-recovery and write-back cache that E4_Open arms only for   */
    /* a write -- so ls stays the one command that works even on an image a prior   */
    /* tool left mid-transaction, and it can never be the thing that dirties one.   */
    if (E4_Open(pszImage, true) != E4_SUCCESS)
    {   /* Could not mount the image (already reported). */
        return(E4_FAILURE);
    }

    /* Open the directory itself.  The mount above already proved the image IS a    */
    /* filesystem, so a failure HERE means specifically "no such directory in the   */
    /* image" (or the path names a file, not a directory) -- reported as its own    */
    /* message so the operator can tell a bad path apart from a bad image.          */
    rc = ext4_dir_open(&dir, pszDir);
    if (rc != 0)
    {   /* The directory does not exist in the image. */
        Log_Err("cannot open directory %s: %s", pszDir, strerror(rc));
        E4_Close(true);
        return(E4_FAILURE);
    }

    /* Zero the entry tally and print the directory's own name as a header line. */
    iCount = 0;
    printf("%s:\n", pszDir);

    /* Walk the directory with lwext4's iterator: ext4_dir_entry_next hands back     */
    /* one on-disk entry per call and NULL at the end.  "." and ".." are             */
    /* deliberately NOT filtered here -- unlike E4_CollectDir, which drops them so   */
    /* the extract walk never climbs back up the tree -- because ls is a human       */
    /* listing and a person expects to see the self and parent links in it.          */
    for (;;)
    {   /* One directory entry per iteration. */
        pEnt = ext4_dir_entry_next(&dir);

        if (pEnt == NULL)
        {   /* End of the directory. */
            break;
        }

        if (pEnt->inode == 0)
        {   /* A DEAD slot, not a real entry.  ext4 does not compact a directory     */
            /* when a name is removed: it zeroes the slot's inode field and folds    */
            /* the freed bytes into the preceding record, so a live directory is     */
            /* dotted with these holes.  The inode field -- not the name -- is the   */
            /* test for "is this slot live"; printing one would show a stale name    */
            /* with nothing behind it, so skip it.                                   */
            continue;
        }

        /* ext4 directory names are COUNTED, not NUL-terminated: pEnt->name runs     */
        /* straight on into the next record's bytes.  So the name is copied out to   */
        /* a sized buffer and terminated before any printf / string call touches     */
        /* it -- handing pEnt->name to printf directly would spill the following     */
        /* entries' raw bytes onto the line.  The copy is unconditionally in         */
        /* bounds: name_length is a uint8_t (<= 255) and szName holds 256.           */
        memcpy(szName, pEnt->name, pEnt->name_length);
        szName[pEnt->name_length] = '\0';

        /* One line per entry: the type label taken from the entry's OWN         */
        /* inode_type field, so listing a directory costs no second inode read   */
        /* per name, then the name.                                              */
        printf("  %-8s %s\n", E4_TypeName((int)pEnt->inode_type), szName);
        iCount++;
    }

    /* Listing finished: close the directory handle (last_err survives the close). */
    ext4_dir_close(&dir);

    if (dir.last_err != EOK)
    {   /* The walk ended early: the directory's inode or one of its blocks could   */
        /* not be read.  The lines printed so far are real entries, but they are    */
        /* not the directory, so the count line is replaced by the failure and      */
        /* the command fails -- a damaged directory must not list as complete.      */
        Log_Err("cannot read directory %s in full: %s (%d entries were read)",
                pszDir, strerror(dir.last_err), iCount);
        E4_Close(true);
        return(E4_FAILURE);
    }

    /* The whole directory was read; unmount the image. */
    E4_Close(true);

    /* Trailing count, so an empty or truncated listing is unambiguous. */
    printf("(%d entries)\n", iCount);

    /* The directory's entries were listed. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* Cmd_RemovePath:                                                            */
/*                                                                            */
/* Remove ONE named path (dry run unless bWrite).  Existence is confirmed by  */
/* scanning the parent directory, so a symlink is detected without following  */
/* it.  The image must already be mounted by the caller with a matching       */
/* read-only / read-write flag.                                               */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszPath   : the absolute path to remove.                               */
/*     bWrite    : true to actually remove, false to only report.             */
/*     pnFound   : incremented when the path exists.                          */
/*     pnRemoved : incremented when the path is actually removed.             */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on a dry run, an absent path or a completed removal;  */
/*           E4_FAILURE (reported) when the path is malformed, its parent     */
/*           could not be read, or the removal itself failed.  Each outcome   */
/*           is also printed and the counters updated.                        */
/*----------------------------------------------------------------------------*/
static int Cmd_RemovePath(const char *pszPath, bool bWrite, int *pnFound,
                          int *pnRemoved)
{
    char        szDir[E4_MAX_PATH];
    const char *pszBase;
    int         iType;
    int         iFind;
    int         rc;

    if (E4_SplitParent(pszPath, szDir, sizeof(szDir), &pszBase) != E4_SUCCESS)
    {   /* Malformed path; nothing to do, and the command fails for it. */
        Log_Err("skipping malformed path: %s", pszPath);
        return(E4_FAILURE);
    }

    /* Test existence by scanning the PARENT for the component, so a symlink is      */
    /* found as a symlink and not followed (E4_FindInDir sets iType from the         */
    /* directory entry).  An absent name is REPORTED, not treated as a hard error:   */
    /* rm of a path that is not there prints "absent" and leaves the found /         */
    /* removed tally at zero, rather than failing the whole command.  A parent       */
    /* that could not be read is different -- nothing can be said about the name     */
    /* -- and fails the command rather than pass as "absent".                        */
    iType = EXT4_DE_UNKNOWN;
    iFind = E4_FindInDir(szDir, pszBase, &iType);

    if (iFind == E4_FIND_ERROR)
    {   /* The parent could not be read (already reported). */
        return(E4_FAILURE);
    }

    if (iFind == E4_FIND_ABSENT)
    {   /* Not present: report it and return, leaving the tally unchanged. */
        printf("  absent : %s\n", pszPath);
        return(E4_SUCCESS);
    }

    /* The path exists: count it found, whether this is a dry run or a real run,   */
    /* so the preview tally and the real tally line up.                            */
    (*pnFound)++;

    if (!bWrite)
    {   /* Dry run: name what WOULD go, with its type, and touch nothing -- this   */
        /* is the preview the operator reads before committing with --write.       */
        printf("  would remove : %s (%s)\n", pszPath, E4_TypeName(iType));
        return(E4_SUCCESS);
    }

    /* Write mode: ext4_fremove unlinks the NAME -- it removes a file or a symlink   */
    /* (not a non-empty directory, which is rm-tree's job), which is exactly the     */
    /* right primitive for the file / link targets this is handed.                   */
    rc = ext4_fremove(pszPath);

    if (rc != 0)
    {   /* Removal failed: report it and return WITHOUT bumping the removed count   */
        /* (found was already counted), so the summary reflects what truly went.    */
        Log_Err("FAILED to remove %s: %s", pszPath, strerror(rc));
        return(E4_FAILURE);
    }

    /* Actually removed: count it and report the path with the type it had. */
    (*pnRemoved)++;
    printf("  removed : %s (%s)\n", pszPath, E4_TypeName(iType));

    /* The directory that held the name has changed: its times, as a kernel   */
    /* would set them.                                                        */
    E4_TouchParent(pszPath);

    /* The path is gone. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* Cmd_Rm:                                                                    */
/*                                                                            */
/* The `rm` command: remove one explicit path (dry run unless bWrite).        */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszImage : the image file.                                             */
/*     pszPath  : the path within the image to remove.                        */
/*     bWrite   : true to apply, false for a dry run.                         */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on a dry run, an absent path or a completed removal;  */
/*           E4_FAILURE if the image cannot be mounted or a path that was     */
/*           found could not be removed.                                      */
/*----------------------------------------------------------------------------*/
static int Cmd_Rm(const char *pszImage, const char *pszPath, bool bWrite)
{
    int nFound;
    int nRemoved;
    int iRc;

    /* Tally of paths found vs actually removed, for the summary line at the end. */
    nFound   = 0;
    nRemoved = 0;

    /* Mount read-only for a dry run, read-write only for the real thing.  This      */
    /* makes the dry-run promise STRUCTURAL rather than a mere branch: on a dry      */
    /* run the filesystem is mounted read-only, so even a bug could not change the   */
    /* image.  The one bWrite flag drives the mount mode here, the branch below,     */
    /* and every E4_Close, so the three can never fall out of step.                  */
    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    if (bWrite)
    {   /* A modifying run: label the output so it cannot be mistaken for a preview. */
        printf("rm (WRITE) on %s:\n", pszImage);
    }
    else
    {   /* A dry run: label it and name the flag that makes it real. */
        printf("rm (dry run -- add --write to apply) on %s:\n", pszImage);
    }

    /* One explicit path: the shared remover reports the outcome, bumps the two   */
    /* counters the summary prints, and says whether the command succeeded.       */
    iRc = Cmd_RemovePath(pszPath, bWrite, &nFound, &nRemoved);

    /* Flush (only when this was a write) and unmount, matching the mount mode. */
    E4_Close(!bWrite);

    /* The tally is what lets a dry run be checked against the real run: the same   */
    /* "found" should carry across, with "removed" rising from 0 to match it.       */
    printf("%d found, %d removed.\n", nFound, nRemoved);

    if (iRc != E4_SUCCESS)
    {   /* The path was there and the removal FAILED, or its parent could not be   */
        /* read (both already reported).  Either is a failed command and must      */
        /* exit non-zero: a script that removes a file and checks the exit code    */
        /* would otherwise carry on as though it had gone.  An ABSENT path is      */
        /* different -- nothing found, nothing to remove -- and stays a success.   */
        return(E4_FAILURE);
    }

    /* A dry run, an absent path, or a path that was found and removed. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* E4_FwriteText:                                                             */
/*                                                                            */
/* Write a buffer to stdout translating each LF to CRLF, WITHOUT doubling an  */
/* LF that already follows a CR -- the `cat --text` path, for reading a       */
/* Unix (LF-only) text file into a Windows editor.  The "was the previous     */
/* emitted byte a CR" state is carried across calls through *pbPrevCR, so a   */
/* CRLF split across two read chunks is still not doubled.  stdout must be in */
/* binary mode (we emit the CRs ourselves).                                   */
/*                                                                            */
/* Arguments:                                                                 */
/*     pBuf     : the bytes to write.                                         */
/*     cBytes   : how many bytes pBuf holds.                                  */
/*     pbPrevCR : in/out -- true if the previously emitted byte was a CR.     */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true if every byte reached stdout, false on a write error.      */
/*----------------------------------------------------------------------------*/
static bool E4_FwriteText(const unsigned char *pBuf, size_t cBytes, bool *pbPrevCR)
{
    size_t i;

    for (i = 0; i < cBytes; i++)
    {   /* One source byte per iteration. */

        /* Insert a CR only before an LF that is NOT already preceded by one, so a   */
        /* file that already uses CRLF stays CRLF instead of becoming CR CR LF.      */
        /* "Already preceded" has to span CALLS: a CRLF can straddle two read        */
        /* chunks -- CR at the end of one, LF at the start of the next -- and if     */
        /* the previous-byte state were not carried in *pbPrevCR, the LF opening     */
        /* the next chunk would look bare and gain a spurious CR.  The symptom is    */
        /* a doubled ending at every 64 KB boundary that lands on a CRLF, which a    */
        /* small-file test never reaches.                                            */
        if (pBuf[i] == '\n' && !(*pbPrevCR))
        {   /* A bare LF: emit a CR first so the ending becomes CRLF. */
            if (fputc('\r', stdout) == EOF)
            {   /* stdout rejected the inserted CR. */
                return(false);
            }
        }

        if (fputc((int)pBuf[i], stdout) == EOF)
        {   /* stdout rejected this byte. */
            return(false);
        }

        /* Carry "was the last emitted byte a CR" forward -- into the next         */
        /* iteration AND, through *pbPrevCR, into the next call -- which is what   */
        /* makes the cross-chunk case above work.                                  */
        *pbPrevCR = (pBuf[i] == '\r');
    }

    /* Every byte, with a CR inserted where needed, reached stdout. */
    return(true);
}

/*-----------------------------------------------------------------------------*/
/* Cmd_Cat:                                                                    */
/*                                                                             */
/* Write a file's bytes from the image to stdout (read-only), streamed through */
/* a fixed buffer so size is no concern.  stdout is set to BINARY either way:  */
/* the default is an exact byte copy (classic cat -- what a binary file or a   */
/* round-trip needs), and `--text` (bText) instead inserts the CRs itself via  */
/* E4_FwriteText so an LF-only config file reads as CRLF in a Windows editor.  */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszImage : the image file.                                              */
/*     pszPath  : the file within the image to print.                          */
/*     bText    : true to translate LF -> CRLF on output, false for raw bytes. */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS on a complete read, E4_FAILURE otherwise.              */
/*-----------------------------------------------------------------------------*/
static int Cmd_Cat(const char *pszImage, const char *pszPath, bool bText)
{
    ext4_file f;
    char      abBuf[E4_IO_BUF];
    size_t    rcnt;
    int       rc;
    int       iRc;
    bool      bPrevCR;

    if (E4_Open(pszImage, true) != E4_SUCCESS)
    {   /* Could not mount the image (already reported). */
        return(E4_FAILURE);
    }

    /* Open the in-image file for reading ("rb" is lwext4's mode string).  A    */
    /* failure here is the path missing, naming a directory, or otherwise not   */
    /* readable -- reported apart from a mount failure so the cause is clear.   */
    rc = ext4_fopen(&f, pszPath, "rb");
    if (rc != 0)
    {   /* The path is missing, is a directory, or is otherwise not readable. */
        Log_Err("cannot open %s: %s", pszPath, strerror(rc));
        E4_Close(true);
        return(E4_FAILURE);
    }

    /* Put stdout in BINARY for BOTH modes, and this is the subtle part.  In         */
    /* --text mode THIS code inserts the CRs (E4_FwriteText); if stdout were left    */
    /* in text mode the C runtime would ALSO translate every \n to \r\n, ending      */
    /* each line \r\r\n.  In the default mode binary is required for an exact byte   */
    /* copy -- a text-mode stdout would corrupt a binary file by injecting CRs.      */
    /* Either way the CRT must do no translation of its own.                         */
    _setmode(_fileno(stdout), _O_BINARY);

    /* Start the result optimistic; a read or write error in the loop flips it to   */
    /* failure.  bPrevCR seeds E4_FwriteText's cross-chunk "last emitted byte was   */
    /* a CR" state (false = none pending) and goes unused in the default mode.      */
    iRc     = E4_SUCCESS;
    bPrevCR = false;

    for (;;)
    {   /* One read chunk per iteration. */

        /* Clear the count before each read: ext4_fread sets it, but a failing   */
        /* call might not, and a stale non-zero would be mistaken for data.      */
        rcnt = 0;

        rc = ext4_fread(&f, abBuf, sizeof(abBuf), &rcnt);
        if (rc != 0)
        {   /* A read error ends the copy with a failure. */
            Log_Err("read error on %s: %s", pszPath, strerror(rc));
            iRc = E4_FAILURE;
            break;
        }

        if (rcnt == 0)
        {   /* End of file: the copy is complete. */
            break;
        }

        if (bText)
        {   /* --text: stream through the LF -> CRLF translator, which carries its    */
            /* CR state across chunks so a CRLF on a 64 KB boundary is not doubled.   */
            if (!E4_FwriteText((const unsigned char *)abBuf, rcnt, &bPrevCR))
            {   /* stdout could not take the translated bytes. */
                Log_Err("write to stdout failed");
                iRc = E4_FAILURE;
                break;
            }
        }
        else
        {   /* Default: the exact bytes, no translation -- what a binary file or a   */
            /* byte-for-byte round-trip (dump / write back) needs.                   */
            if (fwrite(abBuf, 1, rcnt, stdout) != rcnt)
            {   /* stdout could not take the bytes (a closed pipe, a full disk). */
                Log_Err("write to stdout failed");
                iRc = E4_FAILURE;
                break;
            }
        }
    }

    /* Flush stdout (streamed bytes may still be buffered), close the in-image   */
    /* file, then unmount the image.                                             */
    fflush(stdout);
    ext4_fclose(&f);
    E4_Close(true);

    /* iRc carries the result of the streaming loop above. */
    return(iRc);
}

/*-----------------------------------------------------------------------------*/
/* Cmd_Stat:                                                                   */
/*                                                                             */
/* Print one path's inode facts (read-only): inode number, type, permission    */
/* bits, link count, owner uid / gid, byte size, 512-byte block count and the  */
/* three POSIX timestamps.  The size, type-from-mode and block count need the  */
/* superblock; the rest come straight off the raw inode.  Times are printed    */
/* as raw epoch seconds, which is honest on a filesystem whose clock was never */
/* set (whose file times read as implausible dates).                           */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszImage : the image file.                                              */
/*     pszPath  : the path within the image to describe.                       */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS on success, E4_FAILURE if the path cannot be read.     */
/*-----------------------------------------------------------------------------*/
static int Cmd_Stat(const char *pszImage, const char *pszPath)
{
    struct ext4_inode   inode;
    struct ext4_sblock *sb;
    uint32_t            uiIno;
    uint32_t            uiMode;
    int                 rc;

    if (E4_Open(pszImage, true) != E4_SUCCESS)
    {   /* Could not mount the image (already reported). */
        return(E4_FAILURE);
    }

    /* Clear the superblock pointer; ext4_get_sblock sets it on success. */
    sb = NULL;

    /* Fetch the mounted superblock first.  The size / mode / block-count           */
    /* accessors below take it because those values depend on filesystem-wide       */
    /* facts the inode alone does not carry -- the block size, and the feature      */
    /* flags that decide how a large file's size is encoded across two inode        */
    /* fields.  The uid / gid / times / link-count accessors read raw inode         */
    /* fields and need no sb, which is why only some of the prints below pass it.   */
    rc = ext4_get_sblock(E4_MP, &sb);
    if (rc != 0 || sb == NULL)
    {   /* Without the superblock the size / mode / block accessors cannot run. */
        Log_Err("cannot read superblock: %s", strerror(rc));
        E4_Close(true);
        return(E4_FAILURE);
    }

    /* Read the raw on-disk inode for the path into `inode`, its number into       */
    /* uiIno.  A failure here is "the path does not resolve to an inode" -- this   */
    /* is stat's existence test.                                                   */
    uiIno = 0;
    memset(&inode, 0, sizeof(inode));

    rc = ext4_fill_raw_inode(pszPath, &uiIno, &inode);
    if (rc != 0)
    {   /* The path does not resolve to an inode. */
        Log_Err("cannot stat %s: %s", pszPath, strerror(rc));
        E4_Close(true);
        return(E4_FAILURE);
    }

    /* Decode the full mode word once (the type nibble plus the permission bits);   */
    /* both the "type" and "mode" lines below are derived from it.                  */
    uiMode = ext4_inode_get_mode(sb, &inode);

    /* One fact per line for a human reader.  The mode line prints the octal        */
    /* PERMISSION bits alone -- the low 12, with the type nibble masked off -- so   */
    /* it reads as "0644", not "0100644", and then the full mode word in hex for    */
    /* anyone who wants the type bits too.  Times are raw epoch seconds (see the    */
    /* header): honest even on a filesystem whose clock was never set.              */
    printf("%s\n", pszPath);
    printf("  inode   : %lu\n", (unsigned long)uiIno);
    printf("  type    : %s\n", E4_ModeTypeName(uiMode));
    printf("  mode    : %04lo (full 0x%04lx)\n",
           (unsigned long)(uiMode & ~((uint32_t)EXT4_INODE_MODE_TYPE_MASK)),
           (unsigned long)uiMode);
    printf("  links   : %u\n", (unsigned)ext4_inode_get_links_cnt(&inode));
    printf("  uid/gid : %lu / %lu\n",
           (unsigned long)ext4_inode_get_uid(&inode),
           (unsigned long)ext4_inode_get_gid(&inode));
    printf("  size    : %llu bytes\n",
           (unsigned long long)ext4_inode_get_size(sb, &inode));
    printf("  blocks  : %llu (512-byte)\n",
           (unsigned long long)ext4_inode_get_blocks_count(sb, &inode));
    printf("  atime   : %lu (UTC epoch seconds)\n",
           (unsigned long)ext4_inode_get_access_time(&inode));
    printf("  mtime   : %lu (UTC epoch seconds)\n",
           (unsigned long)ext4_inode_get_modif_time(&inode));
    printf("  ctime   : %lu (UTC epoch seconds)\n",
           (unsigned long)ext4_inode_get_change_inode_time(&inode));

    /* All inode facts printed; unmount the image. */
    E4_Close(true);

    /* The inode facts were printed in full. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* Cmd_Readlink:                                                              */
/*                                                                            */
/* Print the target a symbolic link points to (read-only), without following  */
/* it: the link's own stored text, whether or not that target exists.         */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszImage : the image file.                                             */
/*     pszPath  : the symlink within the image to read.                       */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on success, E4_FAILURE if it is not a readable link.  */
/*----------------------------------------------------------------------------*/
static int Cmd_Readlink(const char *pszImage, const char *pszPath)
{
    char   szTarget[E4_MAX_PATH];
    size_t rcnt;
    int    rc;

    if (E4_Open(pszImage, true) != E4_SUCCESS)
    {   /* Could not mount the image (already reported). */
        return(E4_FAILURE);
    }

    /* Clear the length out-parameter; ext4_readlink reports the target's length   */
    /* through it.                                                                 */
    rcnt = 0;

    /* ext4_readlink writes the target text but does NOT terminate it -- it       */
    /* reports the length in rcnt instead -- so it is handed one byte LESS than   */
    /* the buffer, and the NUL is placed by hand at rcnt just below.  A failure   */
    /* is "not a symlink" or "target longer than the buffer".                     */
    rc = ext4_readlink(pszPath, szTarget, sizeof(szTarget) - 1, &rcnt);
    if (rc != 0)
    {   /* Not a symlink, or the target is longer than the buffer. */
        Log_Err("cannot readlink %s: %s", pszPath, strerror(rc));
        E4_Close(true);
        return(E4_FAILURE);
    }

    if (rcnt >= sizeof(szTarget))
    {   /* Defensive: never index past the buffer end, even though the size-1   */
        /* above means rcnt should already be within it.                        */
        rcnt = sizeof(szTarget) - 1;
    }

    /* Terminate the target text at the reported length, then print it as   */
    /* "path -> target".                                                    */
    szTarget[rcnt] = '\0';
    printf("%s -> %s\n", pszPath, szTarget);

    /* Target printed; unmount the image. */
    E4_Close(true);

    /* The link target was printed. */
    return(E4_SUCCESS);
}

/*-----------------------------------------------------------------------------*/
/* Cmd_Mkdir:                                                                  */
/*                                                                             */
/* Create a directory in the image (dry run unless bWrite).  The parent must   */
/* already exist -- lwext4 does not create intermediate directories, so this   */
/* makes a single level, like a plain mkdir with no -p.  The new directory is  */
/* 0755, owner 0:0, dated this run's moment, unless --mode / --owner / --mtime */
/* say otherwise, and its parent's times are set as a kernel would.  The new   */
/* directory's block checksum is settled at once (E4_NewDirHeal says why), so  */
/* the image is e2fsck-clean after a mkdir on a metadata_csum filesystem.      */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszImage : the image file.                                              */
/*     pszPath  : the directory path to create.                                */
/*     bWrite   : true to apply, false for a dry run.                          */
/*     pOpts    : the command line's --mode / --owner / --mtime.               */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS on success, E4_FAILURE on mount / create failure.      */
/*-----------------------------------------------------------------------------*/
static int Cmd_Mkdir(const char *pszImage, const char *pszPath, bool bWrite,
                     const struct E4_ImportOpts *pOpts)
{
    uint32_t uiShowMode;
    int      rc;

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    /* The mode the directory will get: --mode when given, else the directory   */
    /* default -- shown by the preview and applied by the real run alike.       */
    uiShowMode = (uint32_t)E4_IMPORT_DIR_MODE;

    if (pOpts->bMode)
    {   /* --mode names the directory itself here, so it applies. */
        uiShowMode = pOpts->uiMode;
    }

    if (!bWrite)
    {   /* Dry run: state the intent and touch nothing.  It does NOT pre-verify      */
        /* the parent exists (unlike write / ln / mv, which scan for their           */
        /* target) -- a single-level mkdir has little else to check -- so a          */
        /* missing parent surfaces only on the real run below, not in the preview.   */
        printf("would create directory : %s  (0%03o)\n", pszPath, (unsigned)uiShowMode);
        printf("(dry run -- add --write to apply; the parent must already exist)\n");
        E4_Close(true);
        return(E4_SUCCESS);
    }

    /* ext4_dir_mk makes ONE level only -- lwext4 has no mkdir -p -- so a missing   */
    /* intermediate directory is the usual failure here, alongside the name being   */
    /* taken or a write error.                                                      */
    rc = ext4_dir_mk(pszPath);

    if (rc != 0)
    {   /* Parent missing, the name is taken, or a write error. */
        Log_Err("mkdir %s failed: %s", pszPath, strerror(rc));
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* Settle the new directory's block checksum before it is stamped, so the     */
    /* stamp's times are what it keeps (E4_NewDirHeal says why this is needed).   */
    rc = E4_NewDirHeal(pszPath);

    if (rc != 0)
    {   /* The directory exists but its block could not be rewritten (a full       */
        /* filesystem, say): said as such, since the directory IS there, and the   */
        /* command fails so a script does not take the image as finished.          */
        Log_Err("mkdir %s: created, but its block could not be settled: %s", pszPath, strerror(rc));
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* Stamp it.  lwext4 leaves a new directory as 0777, owner 0:0 and dated       */
    /* 1970; this gives it what an imported directory gets -- 0755, 0:0 and this   */
    /* run's moment -- or what --mode / --owner / --mtime ask for.  bIsDir is      */
    /* FALSE on purpose: that flag keeps --mode off the directories of an          */
    /* imported TREE (a file mode would make them untraversable), but here the     */
    /* directory is the very thing the option names.  Then the parent's times:     */
    /* it gained an entry.                                                         */
    E4_ApplyImportMeta(pszPath, false, pOpts, NULL,
                       (uint32_t)E4_IMPORT_DIR_MODE, 0, 0, E4_uiNow);
    E4_TouchParent(pszPath);

    /* Created: report it, then flush and unmount (this path was a write). */
    printf("created directory : %s  (0%03o)\n", pszPath, (unsigned)uiShowMode);
    E4_Close(false);

    /* The directory was created. */
    return(E4_SUCCESS);
}

/*------------------------------------------------------------------------------*/
/* Cmd_Write:                                                                   */
/*                                                                              */
/* Copy a HOST file's bytes INTO the image at pszPath (dry run unless bWrite).  */
/* The in-image file is created, or truncated if it exists ("wb"), and its      */
/* parent directory must already exist.  The host file is read in binary mode   */
/* and streamed through a fixed buffer.  A dry run reports the host file's      */
/* size and whether the target already exists, and writes nothing.  A real      */
/* write that fails part-way removes the partial file from the image.           */
/*                                                                              */
/* THE METADATA THE FILE ENDS UP WITH.  A file that did not exist gets what     */
/* rwrite gives an imported file: mode 0644, owner root:root, and this run's    */
/* moment as its times (`--mtime host` keeps the host file's modify time        */
/* instead, as cp -p would).  A file that is OVERWRITTEN keeps the mode and     */
/* owner it had -- replacing a config file in a root filesystem must not        */
/* quietly change who may read it -- and takes the new time.  `-p` reads the    */
/* sidecar `<hostfile>.ext4meta` that `dump -p` wrote and restores from it      */
/* instead; --mode / --owner / --mtime outrank all of these (E4_ApplyImportMeta */
/* has the order).  A NEW file also sets its parent directory's times, as a     */
/* kernel does.  lwext4 alone would leave the file 0666, dated 1970.            */
/*                                                                              */
/* Arguments:                                                                   */
/*     pszImage : the image file.                                               */
/*     pszPath  : the destination path inside the image.                        */
/*     pszHost  : the host (Windows) file to read bytes from.                   */
/*     bWrite   : true to apply, false for a dry run.                           */
/*     pOpts    : the command line's --mode / --owner / --mtime.                */
/*     bMeta    : true to restore from the <hostfile>.ext4meta sidecar (-p).    */
/*                                                                              */
/* Returns:                                                                     */
/*     int : E4_SUCCESS on success, E4_FAILURE on any error.                    */
/*------------------------------------------------------------------------------*/
static int Cmd_Write(const char *pszImage, const char *pszPath,
                     const char *pszHost, bool bWrite,
                     const struct E4_ImportOpts *pOpts, bool bMeta)
{
    FILE               *pHost;
    ext4_file           f;
    char                abBuf[E4_IO_BUF];
    char                szDir[E4_MAX_PATH];
    char                szMeta[E4_MAX_PATH];
    const char         *pszBase;
    struct E4_MetaSet   meta;
    struct E4_MetaRec  *pRec;
    struct _stat64      st;
    struct ext4_inode   inode;
    struct ext4_sblock *sb;
    uint32_t            uiIno;
    uint32_t            uiDefMode;
    uint32_t            uiDefUid;
    uint32_t            uiDefGid;
    uint32_t            uiDefMtime;
    long long           llSize;
    int                 iType;
    size_t              rcnt;
    size_t              wcnt;
    unsigned long long  ullTotal;
    bool                bExisted;
    int                 rc;
    int                 iRc;

    /* No sidecar loaded yet; E4_MetaFree is then safe on every exit below. */
    meta.pszText = NULL;
    meta.pRecs   = NULL;
    meta.nRecs   = 0;
    pRec         = NULL;

    if (bMeta)
    {   /* -p: load the sidecar BEFORE anything is opened, so a missing or        */
        /* unreadable one fails the command with the image untouched.  The name   */
        /* is tested for fit first; _snprintf would truncate it silently.         */
        if (strlen(pszHost) + sizeof(E4_SIDECAR_SUFFIX) > sizeof(szMeta))
        {   /* The host path leaves no room for the sidecar's suffix. */
            Log_Err("host path too long for a sidecar: %s", pszHost);
            return(E4_FAILURE);
        }

        /* The sidecar's name is the host file's plus the suffix; the fit was    */
        /* proved just above, and the terminator is forced all the same, since   */
        /* _snprintf does not write one when it truncates.                       */
        _snprintf(szMeta, sizeof(szMeta), "%s" E4_SIDECAR_SUFFIX, pszHost);
        szMeta[sizeof(szMeta) - 1] = '\0';

        if (E4_MetaLoad(szMeta, &meta) != E4_SUCCESS)
        {   /* No usable sidecar (already reported). */
            return(E4_FAILURE);
        }

        if (meta.nRecs > 0)
        {   /* A sidecar holds ONE block, for this file; its `path:` is the path    */
            /* the file had in the image it came from, which need not be the        */
            /* destination here, so the record is taken by position, not by name.   */
            pRec = &meta.pRecs[0];
        }
    }

    /* The default time of the written file is this run's moment -- the file is   */
    /* being made now, as a plain cp dates its copy.  `--mtime host` keeps the    */
    /* host file's own modify time instead (the cp -p form); a stat that fails    */
    /* then leaves the moment in place, and the open just below reports the       */
    /* missing file in its own words.                                             */
    uiDefMtime = E4_uiNow;

    if (pOpts->bMtimeHost && E4_HostStat(pszHost, &st) == 0)
    {   /* The host file's own modify time, asked for. */
        uiDefMtime = (uint32_t)st.st_mtime;
    }

    /* Open the host SOURCE before mounting the image.  A mistyped source path     */
    /* is the common error, and opening it first makes that failure instant and    */
    /* free -- no mount to do and undo -- instead of surfacing only after the      */
    /* image is up; the open handle is also what lets a dry run measure the size   */
    /* below.  "rb" because the copy is byte-exact: no LF/CRLF translation (that   */
    /* is cat's `--text` job, never write's).                                      */
    pHost = E4_HostFopen(pszHost, "rb");
    if (pHost == NULL)
    {   /* The Windows source file is missing or unreadable. */
        Log_Err("cannot open host file %s: %s", pszHost, strerror(errno));
        return(E4_FAILURE);
    }

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        fclose(pHost);
        return(E4_FAILURE);
    }

    if (!bWrite)
    {   /* Dry run: report the source size and whether it would overwrite, and      */
        /* write nothing.  Size is a BEST EFFORT -- seek to the end, ask where it   */
        /* is -- and llSize stays -1 (and the report drops the byte count) if the   */
        /* seek fails on an unseekable source, rather than printing a misleading    */
        /* 0.  The 64-bit calls, as every host measure in this tool: a host file    */
        /* of 2 GiB or more then shows its true size, where the 32-bit ftell would  */
        /* have failed and shown none.                                              */
        llSize = -1;

        if (_fseeki64(pHost, 0, SEEK_END) == 0)
        {   /* Seek succeeded: _ftelli64 now gives the byte length. */
            llSize = _ftelli64(pHost);
        }

        if (llSize >= 0)
        {   /* Include the byte count when it could be measured. */
            printf("would write : %s  (%lld bytes from %s)\n", pszPath, llSize, pszHost);
        }
        else
        {   /* Size unknown, but still name the source. */
            printf("would write : %s  (from %s)\n", pszPath, pszHost);
        }

        /* Warn when the target already exists, because the real write opens "wb"    */
        /* and TRUNCATES it -- there is no merge.  Existence is tested by scanning   */
        /* the parent (E4_FindInDir), the same not-following-a-symlink check used    */
        /* elsewhere, so the warning is honest even when the target is a link.  A    */
        /* parent that cannot be read is reported by the scan and the preview goes   */
        /* on: the real run meets the same fault at its open.                        */
        iType = EXT4_DE_UNKNOWN;

        if (E4_SplitParent(pszPath, szDir, sizeof(szDir), &pszBase) == E4_SUCCESS &&
            E4_FindInDir(szDir, pszBase, &iType) == E4_FIND_FOUND)
        {   /* The target is present: a real run would overwrite it wholesale. */
            printf("(note: %s already exists and would be OVERWRITTEN)\n", pszPath);
        }

        printf("(dry run -- add --write to apply; the parent dir must exist)\n");
        fclose(pHost);
        E4_Close(true);
        return(E4_SUCCESS);
    }

    /* Write mode.  rewind FIRST: a dry run can leave the host handle at EOF (it     */
    /* seeked there to measure), so the pointer is reset before the real read --     */
    /* without this the copy would start at end-of-file and write zero bytes.  The   */
    /* image side opens "wb", which CREATES the file or TRUNCATES an existing one    */
    /* (the overwrite the dry-run note warned of); lwext4 does not create missing    */
    /* parents, hence "the parent dir must exist".                                   */
    rewind(pHost);

    /* Decide the DEFAULT mode and owner before the open below destroys the        */
    /* evidence: "wb" truncates an existing file, but it is the same inode, so     */
    /* its mode and owner are read now and put back after the copy (the header     */
    /* says why an overwrite keeps them).  A file that is not there yet gets the   */
    /* import defaults.  The superblock is needed to decode the mode word.         */
    uiDefMode = (uint32_t)E4_IMPORT_FILE_MODE;
    uiDefUid  = 0;
    uiDefGid  = 0;
    iType     = EXT4_DE_UNKNOWN;
    sb        = NULL;
    uiIno     = 0;
    bExisted  = false;
    memset(&inode, 0, sizeof(inode));

    if (E4_SplitParent(pszPath, szDir, sizeof(szDir), &pszBase) == E4_SUCCESS &&
        E4_FindInDir(szDir, pszBase, &iType) == E4_FIND_FOUND &&
        ext4_get_sblock(E4_MP, &sb) == 0 && sb != NULL &&
        ext4_fill_raw_inode(pszPath, &uiIno, &inode) == 0)
    {   /* The destination exists and its inode was read: keep what it has.  It   */
        /* is also remembered as existing, because only a NEW entry changes the   */
        /* parent directory (below).                                              */
        uiDefMode = ext4_inode_get_mode(sb, &inode) & ~((uint32_t)EXT4_INODE_MODE_TYPE_MASK);
        uiDefUid  = ext4_inode_get_uid(&inode);
        uiDefGid  = ext4_inode_get_gid(&inode);
        bExisted  = true;
    }

    /* The image-side open: "wb" creates the file or truncates the one that was   */
    /* there (whose mode and owner were just saved), inside the parent, which     */
    /* must already exist.                                                        */
    rc = ext4_fopen(&f, pszPath, "wb");
    if (rc != 0)
    {   /* Parent missing, the name is a directory, or a write error. */
        Log_Err("cannot create %s: %s", pszPath, strerror(rc));
        fclose(pHost);
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* Running count of bytes written, and an optimistic result the loop flips to   */
    /* failure on any read or write error.                                          */
    ullTotal = 0;
    iRc      = E4_SUCCESS;

    for (;;)
    {   /* One host read chunk per iteration, streamed through abBuf so any size   */
        /* copies in a fixed buffer.                                               */
        rcnt = fread(abBuf, 1, sizeof(abBuf), pHost);

        if (rcnt == 0)
        {   /* fread returns 0 for BOTH end-of-file and a read error, so ferror is   */
            /* what distinguishes them.  Without this check a mid-file read error    */
            /* would read as a clean EOF and silently truncate the imported file     */
            /* -- a short file reported as a complete write.                         */
            if (ferror(pHost))
            {   /* A genuine read error on the source ends the write. */
                Log_Err("read error on host file %s", pszHost);
                iRc = E4_FAILURE;
            }
            break;
        }

        /* Clear the written-count; ext4_fwrite reports how much it actually took. */
        wcnt = 0;

        rc = ext4_fwrite(&f, abBuf, rcnt, &wcnt);
        if (rc != 0 || wcnt != rcnt)
        {   /* The image is full or broken.  lwext4 reports a FULL filesystem as a   */
            /* short count with a ZERO status, so strerror(rc) would print "No       */
            /* error" for the commonest failure there is; the two cases are worded   */
            /* apart.                                                                */
            if (rc != 0)
            {   /* lwext4 gave a reason. */
                Log_Err("write to %s failed after %llu bytes: %s",
                        pszPath, ullTotal, strerror(rc));
            }
            else
            {   /* A short count with no error code: no room left. */
                Log_Err("write to %s failed after %llu bytes: the filesystem in the image is full",
                        pszPath, ullTotal);
            }

            iRc = E4_FAILURE;
            break;
        }

        /* The whole chunk went in; add it to the running byte total. */
        ullTotal += (unsigned long long)rcnt;
    }

    /* Done streaming: close the image file and the host file. */
    ext4_fclose(&f);
    fclose(pHost);

    if (iRc != E4_SUCCESS)
    {   /* The copy stopped part-way.  The entry now holds only the first part of   */
        /* the source under the full name, which reads as a complete file to        */
        /* anything that does not compare sizes -- and on a full image it also      */
        /* holds every block that was free, so nothing else can be written until    */
        /* it goes.  Remove it.  (The file that was there before, if any, was       */
        /* already truncated by the "wb" open and cannot be brought back.)          */
        rc = ext4_fremove(pszPath);

        if (rc == 0)
        {   /* Say so: the operator would otherwise look for the partial file. */
            Log_Err("the partial file %s was removed from the image", pszPath);
        }
        else
        {   /* It could not be removed; it is still there and incomplete. */
            Log_Err("warning: the partial file %s could not be removed: %s",
                    pszPath, strerror(rc));
        }
    }
    else
    {   /* The bytes are in: stamp the mode, owner and times (the header gives   */
        /* the order of the sources).  This comes AFTER the close, because the   */
        /* stamps act on the path and the file must not be open under them.      */
        E4_ApplyImportMeta(pszPath, false, pOpts, pRec,
                           uiDefMode, uiDefUid, uiDefGid, uiDefMtime);

        if (!bExisted)
        {   /* A new entry in the parent: its times, as a kernel would set them.   */
            /* An overwrite changes the file, not the directory that names it.     */
            E4_TouchParent(pszPath);
        }
    }

    /* The sidecar's record has been applied (or was never loaded); release it.   */
    /* The error exits above leave it to the end of the process, which follows    */
    /* them at once.                                                              */
    E4_MetaFree(&meta);

    /* Flush and unmount (this path was a write). */
    E4_Close(false);

    if (iRc == E4_SUCCESS)
    {   /* Report the completed write. */
        printf("wrote %llu bytes to %s\n", ullTotal, pszPath);
    }

    /* iRc carries the result of the streaming loop above. */
    return(iRc);
}

/*----------------------------------------------------------------------------*/
/* Cmd_Ln:                                                                    */
/*                                                                            */
/* Create a HARD link in the image (dry run unless bWrite): a second name for */
/* an existing file's inode.  pszTarget is the existing file; pszLink is the  */
/* new name.  A dry run notes it if the target is not present.                */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszImage  : the image file.                                            */
/*     pszTarget : the existing file to link to.                              */
/*     pszLink   : the new hard-link name to create.                          */
/*     bWrite    : true to apply, false for a dry run.                        */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on success, E4_FAILURE on any error.                  */
/*----------------------------------------------------------------------------*/
static int Cmd_Ln(const char *pszImage, const char *pszTarget,
                  const char *pszLink, bool bWrite)
{
    char        szDir[E4_MAX_PATH];
    const char *pszBase;
    int         iType;
    int         rc;

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    if (!bWrite)
    {   /* Dry run: a hard link needs its target to ALREADY EXIST (it is a second    */
        /* name for that file's inode, not a new file), so the preview scans the     */
        /* target's parent and warns if it is missing -- a real run would then       */
        /* fail at ext4_flink.  The scan does not follow a symlink, which is right   */
        /* here: a hard link binds to the named entry itself.  A parent that         */
        /* cannot be read is reported by the scan; the preview still shows the       */
        /* intent, and the real run meets the fault at ext4_flink.                   */
        iType = EXT4_DE_UNKNOWN;

        if (E4_SplitParent(pszTarget, szDir, sizeof(szDir), &pszBase) == E4_SUCCESS &&
            E4_FindInDir(szDir, pszBase, &iType) == E4_FIND_ABSENT)
        {   /* The file to link to is not there; a real run would fail. */
            printf("(note: target %s does not exist)\n", pszTarget);
        }

        printf("would hard-link : %s -> %s\n", pszLink, pszTarget);
        printf("(dry run -- add --write to apply)\n");
        E4_Close(true);
        return(E4_SUCCESS);
    }

    /* Argument order matters: ext4_flink(EXISTING, NEWNAME) adds pszLink as a      */
    /* second name for pszTarget's inode.  The two names then share one inode, so   */
    /* an edit through either is seen through the other -- that is what a hard      */
    /* link IS, and why the two paths must not be "tidied" into one.                */
    rc = ext4_flink(pszTarget, pszLink);

    if (rc != 0)
    {   /* Target missing, the link name is taken, or a cross-type link. */
        Log_Err("hard-link %s -> %s failed: %s", pszLink, pszTarget, strerror(rc));
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* The inode gained a name, which is a change to it (its link count moved):    */
    /* its change time, as link(2) sets it -- reached through either name, since   */
    /* both are the one inode.  Then the directory that gained the entry.          */
    ext4_file_set_ctime(pszLink, E4_uiNow);
    E4_TouchParent(pszLink);

    /* Linked: report it, then flush and unmount (this path was a write). */
    printf("hard-linked : %s -> %s\n", pszLink, pszTarget);
    E4_Close(false);

    /* The hard link was created. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* Cmd_Symlink:                                                               */
/*                                                                            */
/* Create a SYMBOLIC link in the image (dry run unless bWrite).  pszTarget is */
/* the path the link will point to (stored verbatim as the link's text, and   */
/* not required to exist); pszLink is the new symlink entry.  The link is     */
/* owner 0:0 and dated this run's moment unless --owner / --mtime say         */
/* otherwise; its mode is 0777, as Linux keeps every symlink's, and --mode is */
/* not applied.  Its parent's times are set as a kernel would.                */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszImage  : the image file.                                            */
/*     pszTarget : the path the symlink will point to.                        */
/*     pszLink   : the new symlink entry to create.                           */
/*     bWrite    : true to apply, false for a dry run.                        */
/*     pOpts     : the command line's --owner / --mtime (--mode is ignored).  */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on success, E4_FAILURE on any error.                  */
/*----------------------------------------------------------------------------*/
static int Cmd_Symlink(const char *pszImage, const char *pszTarget,
                       const char *pszLink, bool bWrite,
                       const struct E4_ImportOpts *pOpts)
{
    int rc;

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    if (!bWrite)
    {   /* Dry run verifies NOTHING, deliberately -- unlike ln and mv, which scan   */
        /* for their operand.  A symlink stores its target as plain TEXT and the    */
        /* target need not exist (a dangling symlink is legal and common), so       */
        /* there is nothing to pre-check; the preview only echoes the intent.       */
        printf("would symlink : %s -> %s\n", pszLink, pszTarget);
        printf("(dry run -- add --write to apply)\n");
        E4_Close(true);
        return(E4_SUCCESS);
    }

    /* Argument order as for ext4_flink: ext4_fsymlink(TARGET, PATH) creates       */
    /* pszLink holding the text pszTarget.  The target is stored verbatim, never   */
    /* resolved, so a relative or not-yet-existing target is written exactly as    */
    /* given.                                                                      */
    rc = ext4_fsymlink(pszTarget, pszLink);

    if (rc != 0)
    {   /* The link name is taken, the parent is missing, or a write error. */
        Log_Err("symlink %s -> %s failed: %s", pszLink, pszTarget, strerror(rc));
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* Stamp it.  lwext4 leaves a new symlink as 0666, owner 0:0, dated 1970;     */
    /* Linux keeps every symlink at 0777 (its mode is never consulted), so that   */
    /* is the mode set here, and bIsDir is TRUE so that --mode is left aside      */
    /* the way it is for a tree's directories -- a symlink has no mode of its     */
    /* own to set.  Owner and times come from the options or the defaults; then   */
    /* the parent, which gained an entry.  lwext4's path walk never follows a     */
    /* symlink, so the stamps land on the link's own inode, not its target's.     */
    E4_ApplyImportMeta(pszLink, true, pOpts, NULL, 0777, 0, 0, E4_uiNow);
    E4_TouchParent(pszLink);

    /* Created: report it, then flush and unmount (this path was a write). */
    printf("symlinked : %s -> %s\n", pszLink, pszTarget);
    E4_Close(false);

    /* The symbolic link was created. */
    return(E4_SUCCESS);
}

/*-----------------------------------------------------------------------------*/
/* Cmd_Mv:                                                                     */
/*                                                                             */
/* Rename / move an entry within the image (dry run unless bWrite).  A dry run */
/* notes it if the source is not present.  A move of a directory INTO ITS OWN  */
/* SUBTREE is refused, in the preview as in the real run (the body says why).  */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszImage : the image file.                                              */
/*     pszSrc   : the existing path to move.                                   */
/*     pszDst   : the new path.                                                */
/*     bWrite   : true to apply, false for a dry run.                          */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS on success, E4_FAILURE on any error.                   */
/*-----------------------------------------------------------------------------*/
static int Cmd_Mv(const char *pszImage, const char *pszSrc,
                  const char *pszDst, bool bWrite)
{
    char        szDir[E4_MAX_PATH];
    const char *pszBase;
    size_t      cchSrc;
    int         iType;
    int         rc;

    /* A directory moved INTO ITS OWN SUBTREE -- the destination is the source      */
    /* followed by "/" -- would be unlinked from its parent and linked under        */
    /* itself: an orphaned loop no path reaches, its contents lost.  lwext4 does    */
    /* not refuse it, so this does, before the image is even opened.  Only a        */
    /* whole component counts ("/a" to "/ab" is an ordinary rename), and the test   */
    /* is exact because both paths passed Arg_ImagePath -- no trailing "/", no      */
    /* "." or ".." -- so "/d" cannot arrive spelled "/d/" or "/d/x/..".             */
    cchSrc = strlen(pszSrc);

    if (strncmp(pszDst, pszSrc, cchSrc) == 0 && pszDst[cchSrc] == '/')
    {   /* Into itself: refused with nothing opened and nothing changed. */
        Log_Err("cannot move %s into itself (%s)", pszSrc, pszDst);
        return(E4_FAILURE);
    }

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    if (!bWrite)
    {   /* Dry run: a rename needs the SOURCE to exist, so the preview scans the    */
        /* source's parent and warns if it is missing (a real run would then fail   */
        /* at ext4_frename).  The destination is NOT pre-checked: ext4_frename      */
        /* replaces an existing destination of a compatible type, the POSIX         */
        /* rename contract, and that is left to the real run.  A parent that        */
        /* cannot be read is reported by the scan; the preview still shows the      */
        /* intent, and the real run meets the fault at ext4_frename.                */
        iType = EXT4_DE_UNKNOWN;

        if (E4_SplitParent(pszSrc, szDir, sizeof(szDir), &pszBase) == E4_SUCCESS &&
            E4_FindInDir(szDir, pszBase, &iType) == E4_FIND_ABSENT)
        {   /* Nothing at the source; a real run would fail. */
            printf("(note: source %s does not exist)\n", pszSrc);
        }

        printf("would rename : %s -> %s\n", pszSrc, pszDst);
        printf("(dry run -- add --write to apply)\n");
        E4_Close(true);
        return(E4_SUCCESS);
    }

    /* ext4_frename(SRC, DST) moves the entry by relinking the inode under the new    */
    /* name and unlinking the old -- the data is never copied, only the directory     */
    /* entry moves.  Per the POSIX rename contract it also REPLACES an existing dst   */
    /* of a compatible type, which is why the dry run above deliberately did not      */
    /* pre-check the destination.                                                     */
    rc = ext4_frename(pszSrc, pszDst);

    if (rc != 0)
    {   /* Source missing, the destination parent is missing, or a write error. */
        Log_Err("rename %s -> %s failed: %s", pszSrc, pszDst, strerror(rc));
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* The moved inode's change time, as rename(2) sets it, and the times of     */
    /* both directories: the one that lost the entry and the one that gained     */
    /* it.  When they are the same directory it is stamped twice with the same   */
    /* moment, which costs one extra inode write and nothing else.               */
    ext4_file_set_ctime(pszDst, E4_uiNow);
    E4_TouchParent(pszSrc);
    E4_TouchParent(pszDst);

    /* Renamed: report it, then flush and unmount (this path was a write). */
    printf("renamed : %s -> %s\n", pszSrc, pszDst);
    E4_Close(false);

    /* The entry was renamed. */
    return(E4_SUCCESS);
}

/*-----------------------------------------------------------------------------*/
/* Cmd_RmTree:                                                                 */
/*                                                                             */
/* Recursively remove a directory and everything under it (dry run unless      */
/* bWrite).  This is the directory counterpart of `rm`, which removes a single */
/* file or link.  A dry run confirms the path exists and warns that the remove */
/* is recursive and cannot be undone.                                          */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszImage : the image file.                                              */
/*     pszPath  : the directory to remove recursively.                         */
/*     bWrite   : true to apply, false for a dry run.                          */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS on success, E4_FAILURE on any error.                   */
/*-----------------------------------------------------------------------------*/
static int Cmd_RmTree(const char *pszImage, const char *pszPath, bool bWrite)
{
    char        szDir[E4_MAX_PATH];
    const char *pszBase;
    int         iType;
    int         iFind;
    int         rc;

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    if (!bWrite)
    {   /* Dry run: an absent path is reported and treated as SUCCESS (nothing to   */
        /* remove is not a failure), and a present one is previewed with an         */
        /* explicit "and everything under it" and "cannot be undone" -- because     */
        /* this is the recursive, unguarded remove, and the preview is the          */
        /* operator's one chance to catch a wrong path before it takes a subtree.   */
        /* A parent that cannot be read fails the preview: nothing can be said.     */
        iType = EXT4_DE_UNKNOWN;
        iFind = E4_FIND_ABSENT;

        if (E4_SplitParent(pszPath, szDir, sizeof(szDir), &pszBase) == E4_SUCCESS)
        {   /* The parent is known: ask it for the entry. */
            iFind = E4_FindInDir(szDir, pszBase, &iType);
        }

        if (iFind == E4_FIND_ERROR)
        {   /* The parent could not be read (already reported). */
            E4_Close(true);
            return(E4_FAILURE);
        }

        if (iFind == E4_FIND_ABSENT)
        {   /* Nothing there to remove. */
            printf("absent : %s\n", pszPath);
            E4_Close(true);
            return(E4_SUCCESS);
        }

        printf("would recursively remove : %s (%s, and everything under it)\n",
               pszPath, E4_TypeName(iType));
        printf("(dry run -- add --write to apply; this cannot be undone)\n");
        E4_Close(true);
        return(E4_SUCCESS);
    }

    /* ext4_dir_rm removes the directory AND its whole subtree -- this is the        */
    /* difference from rmdir, which refuses a non-empty directory.  The only guard   */
    /* is the --write flag itself; there is deliberately no second confirmation.     */
    rc = ext4_dir_rm(pszPath);

    if (rc != 0)
    {   /* The path is missing, is not a directory, or a write error. */
        Log_Err("rm-tree %s failed: %s", pszPath, strerror(rc));
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* The directory that held the tree's top has changed: its times. */
    E4_TouchParent(pszPath);

    /* Removed: report it, then flush and unmount (this path was a write). */
    printf("recursively removed : %s\n", pszPath);
    E4_Close(false);

    /* The directory tree was removed. */
    return(E4_SUCCESS);
}

/*-----------------------------------------------------------------------------*/
/* Cmd_Chmod:                                                                  */
/*                                                                             */
/* Change an entry's permission bits (dry run unless bWrite).  The mode is an  */
/* OCTAL number, as for the shell chmod (for example 0755); the file-type bits */
/* are preserved by lwext4.                                                    */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszImage : the image file.                                              */
/*     pszMode  : the new mode as an octal string (e.g. "0644").               */
/*     pszPath  : the path whose mode to change.                               */
/*     bWrite   : true to apply, false for a dry run.                          */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS on success, E4_FAILURE on a bad mode or write error.   */
/*-----------------------------------------------------------------------------*/
static int Cmd_Chmod(const char *pszImage, const char *pszMode,
                     const char *pszPath, bool bWrite)
{
    unsigned long ulMode;
    int           rc;

    /* Parse the mode in base 8 BEFORE mounting, so a typo fails instantly with no   */
    /* mount to undo.  Base 8 is the point: the shell writes modes in octal          */
    /* ("0644" = rw-r--r--), and reading "644" as decimal would set a nonsense       */
    /* permission word -- E4_ParseUint rejects anything that is not a clean octal    */
    /* number rather than letting strtoul salvage part of it.                        */
    if (!E4_ParseUint(pszMode, 8, &ulMode))
    {   /* The mode must be an octal number such as 0755. */
        Log_Err("invalid octal mode: %s", pszMode);
        return(E4_FAILURE);
    }

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    if (!bWrite)
    {   /* Dry run: report the intended mode and touch nothing. */
        printf("would chmod : %04lo  %s\n", ulMode, pszPath);
        printf("(dry run -- add --write to apply)\n");
        E4_Close(true);
        return(E4_SUCCESS);
    }

    /* ext4_chmod sets the permission bits; lwext4 PRESERVES the inode's file-type   */
    /* bits (verified against debugfs), so a value like 0644 changes the mode        */
    /* without ever reclassifying the entry's type.                                  */
    rc = ext4_chmod(pszPath, (uint32_t)ulMode);

    if (rc != 0)
    {   /* The path is missing or a write error occurred. */
        Log_Err("chmod %04lo %s failed: %s", ulMode, pszPath, strerror(rc));
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* The inode changed: its change time, as chmod(2) sets it.  Not the modify   */
    /* time -- the content is as it was.                                          */
    ext4_file_set_ctime(pszPath, E4_uiNow);

    /* Changed: report the new mode, then flush and unmount. */
    printf("chmod %04lo : %s\n", ulMode, pszPath);
    E4_Close(false);

    /* The mode was changed. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* Cmd_Chown:                                                                 */
/*                                                                            */
/* Change an entry's owner and group (dry run unless bWrite).  Both ids are   */
/* plain decimal numbers (ext inodes store numeric ids, not names).           */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszImage : the image file.                                             */
/*     pszUid   : the new owner uid as a decimal string.                      */
/*     pszGid   : the new group gid as a decimal string.                      */
/*     pszPath  : the path whose ownership to change.                         */
/*     bWrite   : true to apply, false for a dry run.                         */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on success, E4_FAILURE on a bad id or write error.    */
/*----------------------------------------------------------------------------*/
static int Cmd_Chown(const char *pszImage, const char *pszUid,
                     const char *pszGid, const char *pszPath, bool bWrite)
{
    unsigned long ulUid;
    unsigned long ulGid;
    int           rc;

    /* Parse BOTH ids in base 10 before mounting, so a bad id fails early.  They   */
    /* are numeric (not names like "root") because an offline image carries no     */
    /* passwd / group database to resolve a name against -- the inode stores the   */
    /* raw numeric uid / gid, and that is all this can set.                        */
    if (!E4_ParseUint(pszUid, 10, &ulUid) || !E4_ParseUint(pszGid, 10, &ulGid))
    {   /* Both ids must be plain decimal numbers. */
        Log_Err("invalid uid/gid: %s %s (both must be decimal)", pszUid, pszGid);
        return(E4_FAILURE);
    }

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    if (!bWrite)
    {   /* Dry run: report the intended ownership and touch nothing. */
        printf("would chown : %lu:%lu  %s\n", ulUid, ulGid, pszPath);
        printf("(dry run -- add --write to apply)\n");
        E4_Close(true);
        return(E4_SUCCESS);
    }

    /* ext4_chown writes the raw numeric uid and gid onto the inode; it changes    */
    /* neither the mode nor the type.  Both ids were validated as decimal above.   */
    rc = ext4_chown(pszPath, (uint32_t)ulUid, (uint32_t)ulGid);

    if (rc != 0)
    {   /* The path is missing or a write error occurred. */
        Log_Err("chown %lu:%lu %s failed: %s", ulUid, ulGid, pszPath, strerror(rc));
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* The inode changed: its change time, as chown(2) sets it. */
    ext4_file_set_ctime(pszPath, E4_uiNow);

    /* Changed: report the new owner / group, then flush and unmount. */
    printf("chown %lu:%lu : %s\n", ulUid, ulGid, pszPath);
    E4_Close(false);

    /* The ownership was changed. */
    return(E4_SUCCESS);
}

/*-----------------------------------------------------------------------------*/
/* Cmd_Rmdir:                                                                  */
/*                                                                             */
/* Remove a directory ONLY IF it is empty (dry run unless bWrite) -- the       */
/* guarded counterpart of rm-tree, which removes a directory AND everything    */
/* under it.  lwext4 has no empty-only primitive, so this scans the directory, */
/* refuses it unless the only entries are "." and "..", and then removes it    */
/* with ext4_dir_rm.                                                           */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszImage : the image file.                                              */
/*     pszPath  : the directory to remove.                                     */
/*     bWrite   : true to apply, false for a dry run.                          */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS on success, E4_FAILURE if it is missing, not a         */
/*           directory, not empty, or on a write error.                        */
/*-----------------------------------------------------------------------------*/
static int Cmd_Rmdir(const char *pszImage, const char *pszPath, bool bWrite)
{
    ext4_dir             dir;
    const ext4_direntry *pEnt;
    char                 szName[256];
    int                  rc;
    int                  nReal;

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    rc = ext4_dir_open(&dir, pszPath);
    if (rc != 0)
    {   /* Not a directory, or it does not exist. */
        Log_Err("cannot open directory %s: %s", pszPath, strerror(rc));
        E4_Close(!bWrite);
        return(E4_FAILURE);
    }

    /* Emptiness is checked BY HAND because lwext4 has no empty-only remove: its    */
    /* ext4_dir_rm (what rm-tree uses) would delete a non-empty directory and its   */
    /* whole contents.  So this scans the directory and counts any entry that is    */
    /* not "." or "..", and the nReal guard below is the ONLY thing that makes      */
    /* rmdir refuse a non-empty directory instead of behaving like rm-tree.         */
    nReal = 0;

    for (;;)
    {   /* One directory entry per iteration. */
        pEnt = ext4_dir_entry_next(&dir);

        if (pEnt == NULL)
        {   /* End of the directory: nReal now holds the real-child count. */
            break;
        }

        if (pEnt->inode == 0)
        {   /* A DEAD slot, not a child.  ext4 does not compact a directory when   */
            /* a name is removed: it zeroes the slot's inode field and folds the   */
            /* freed bytes into the preceding record, so a live directory is       */
            /* dotted with these holes.  The inode field -- not the name -- is     */
            /* the liveness test; counting a hole would make an empty directory    */
            /* look occupied and wrongly refuse the rmdir below.                   */
            continue;
        }

        /* ext4 names are COUNTED, not NUL-terminated, so pEnt->name runs        */
        /* straight on into the next record's bytes: copy it out and terminate   */
        /* it before strcmp, or strcmp would read past this entry into the       */
        /* following one and mis-judge the "." / ".." test.  name_length is a    */
        /* uint8_t (<= 255), so the copy is always inside szName[256].           */
        memcpy(szName, pEnt->name, pEnt->name_length);
        szName[pEnt->name_length] = '\0';

        if (strcmp(szName, ".") == 0 || strcmp(szName, "..") == 0)
        {   /* The self and parent links sit in every directory and are not what   */
            /* "non-empty" means, so they do not count toward nReal.               */
            continue;
        }

        /* A real child: this directory is not empty. */
        nReal++;
    }

    /* Done scanning; release the directory handle before acting on the count   */
    /* (last_err survives the close).                                           */
    ext4_dir_close(&dir);

    if (dir.last_err != EOK)
    {   /* The walk ended early: the directory's inode or one of its blocks could   */
        /* not be read, so the count below was taken from a PART of the directory   */
        /* -- and a directory judged empty on a partial read could be removed       */
        /* with entries still in it.  Refuse instead, on a dry run as well.         */
        Log_Err("cannot read directory %s in full: %s; not removed",
                pszPath, strerror(dir.last_err));
        E4_Close(!bWrite);
        return(E4_FAILURE);
    }

    if (nReal != 0)
    {   /* Not empty: refuse.  This nReal guard is the ONLY thing separating      */
        /* rmdir from rm-tree -- both end in the same ext4_dir_rm below, which    */
        /* deletes a directory AND its whole subtree, so without this check       */
        /* rmdir would silently behave like rm-tree and take content the          */
        /* operator never asked to lose.  The entry count is named in the error   */
        /* so they can see it was not in fact empty.                              */
        Log_Err("refusing to remove non-empty directory %s (%d entries; use rm-tree)",
                pszPath, nReal);
        E4_Close(!bWrite);
        return(E4_FAILURE);
    }

    if (!bWrite)
    {   /* Dry run: the directory is empty and would be removed. */
        printf("would remove empty directory : %s\n", pszPath);
        printf("(dry run -- add --write to apply)\n");
        E4_Close(true);
        return(E4_SUCCESS);
    }

    /* Emptiness is now proven, so ext4_dir_rm -- the SAME recursive primitive    */
    /* rm-tree uses -- can only take this one empty level here; the nReal guard   */
    /* above is what makes reusing it safe (lwext4 has no empty-only remove).     */
    rc = ext4_dir_rm(pszPath);

    if (rc != 0)
    {   /* The removal failed. */
        Log_Err("rmdir %s failed: %s", pszPath, strerror(rc));
        E4_Close(false);
        return(E4_FAILURE);
    }

    /* The directory that held it has changed: its times. */
    E4_TouchParent(pszPath);

    /* Removed: report it, then flush and unmount (this path was a write). */
    printf("removed empty directory : %s\n", pszPath);
    E4_Close(false);

    /* The empty directory was removed. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* E4_JoinPath:                                                               */
/*                                                                            */
/* Compose "<a><sep><b>" into a caller buffer, overflow-safe.  Used to build  */
/* the parallel image / host / relative paths as the tree walks of rdump and  */
/* rwrite descend, so a path that would not fit is rejected rather than       */
/* silently truncated (which _snprintf does on Windows).  An empty prefix     */
/* yields just the component, so the same call serves the top level, and a    */
/* prefix that already ends in the separator (the image root "/") gains no    */
/* second one.                                                                */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszOut  : the destination buffer.                                      */
/*     cchOut  : its size in bytes.                                           */
/*     pszA    : the prefix (may be "").                                      */
/*     pszB    : the component to append.                                     */
/*     chSep   : the separator to insert between them ('/' throughout).       */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on a composed path, E4_FAILURE if it would overflow.  */
/*----------------------------------------------------------------------------*/
static int E4_JoinPath(char *pszOut, size_t cchOut, const char *pszA,
                       const char *pszB, char chSep)
{
    size_t cchA;
    size_t cchB;

    /* Measure both parts once, up front, so the fit checks below are exact. */
    cchA = strlen(pszA);
    cchB = strlen(pszB);

    /* Two shapes: an empty prefix yields just the component (the top level of a   */
    /* tree walk), otherwise "<prefix><sep><component>".  BOTH check the fit       */
    /* before writing and REJECT an overflow rather than truncate -- because       */
    /* _snprintf on Windows truncates silently, and a truncated path in the        */
    /* rdump / rwrite descent would read or write the WRONG file; the same         */
    /* rejection also bounds an accidental directory cycle.                        */
    if (cchA == 0)
    {   /* No prefix: the result is just the component (plus its NUL). */
        if (cchB + 1 > cchOut)
        {   /* Even the bare component does not fit. */
            return(E4_FAILURE);
        }

        memcpy(pszOut, pszB, cchB + 1);
        return(E4_SUCCESS);
    }

    if (pszA[cchA - 1] == chSep)
    {   /* The prefix already ENDS in the separator -- the image root "/" is the    */
        /* case that matters.  Adding another would build "//name", which lwext4    */
        /* cannot open, so a whole-image `rdump /` (or an rwrite onto "/") failed   */
        /* on every child.  Drop the prefix's own separator from the length and     */
        /* let the one written below stand for it.                                  */
        cchA--;
    }

    if (cchA + 1 + cchB + 1 > cchOut)
    {   /* prefix + separator + component + NUL would overflow the buffer. */
        return(E4_FAILURE);
    }

    /* It fits: lay the prefix, the separator and the component down in order,   */
    /* then terminate by hand (no format string to mis-size).                    */
    memcpy(pszOut, pszA, cchA);
    pszOut[cchA] = chSep;
    memcpy(pszOut + cchA + 1, pszB, cchB);
    pszOut[cchA + 1 + cchB] = '\0';

    /* The composed path. */
    return(E4_SUCCESS);
}

/*------------------------------------------------------------------------------*/
/* E4_HostWiden:                                                                */
/*                                                                              */
/* Turn a UTF-8 host path into the WIDE extended-length form Windows needs to   */
/* reach a path longer than 260 characters: `\\?\C:\...` for a drive path, and  */
/* `\\?\UNC\server\share\...` for a UNC path.  Every host-side file primitive   */
/* below, and the image file itself, funnels through this -- a narrow fopen /   */
/* stat / opendir caps at MAX_PATH (260) and FAILS on a longer path, while the  */
/* wide \\?\ call reaches about 32767.                                          */
/*                                                                              */
/* Host paths are UTF-8 everywhere inside this tool (main converts the Unicode  */
/* command line to UTF-8, and E4_HostDirRead hands names back as UTF-8), so the */
/* decode here is exact for ANY Windows name.  UTF-8 is also what a Linux       */
/* filesystem stores, so a name crosses between the image and the host without  */
/* being re-encoded.  The path is then made absolute and backslash-normalised   */
/* by GetFullPathNameW (the \\?\ form forbids a forward slash and any `.` /     */
/* `..` segment), and the prefix is spliced on.  A path the caller ALREADY gave */
/* in the `\\?\` form, or a `\\.\` device path, is passed through as it is.     */
/*                                                                              */
/* Arguments:                                                                   */
/*     pszHost : the host path, UTF-8, absolute or relative.                    */
/*     pwOut   : receives the wide \\?\-prefixed absolute path.                 */
/*     cchOut  : capacity of pwOut, in wchar_t.                                 */
/*                                                                              */
/* Returns:                                                                     */
/*     int : E4_SUCCESS on a built path; E4_FAILURE if the path is not valid    */
/*           UTF-8, or the absolute path or the prefixed path would not fit.    */
/*------------------------------------------------------------------------------*/
static int E4_HostWiden(const char *pszHost, wchar_t *pwOut, size_t cchOut)
{
    /* STATIC, not on the stack, on purpose: 16 KiB each.  The tool is          */
    /* single-threaded and this routine never nests, so one copy serves every   */
    /* call -- and nothing of this size belongs on the thread's stack: the      */
    /* tree walks (E4_RdumpTree, E4_RwriteTree) call the host helpers for       */
    /* every entry, the compiler inlines them into the walk, and the walks      */
    /* keep each level's state on the heap so that depth costs the stack        */
    /* nothing.  The same holds for the host helpers' own wide buffers and      */
    /* the stream buffers of the per-file copies below.                         */
    static wchar_t wNarrow[E4_MAX_WPATH];
    static wchar_t wFull[E4_MAX_WPATH];
    DWORD          dwFull;
    int            iConv;

    /* STEP 1: UTF-8 -> wide.  MB_ERR_INVALID_CHARS makes a byte sequence that is   */
    /* not valid UTF-8 a FAILURE instead of a silent U+FFFD: a path quietly         */
    /* altered here would open a different file, or none.  The call returns 0 on    */
    /* that failure OR when the result would not fit wNarrow.                       */
    iConv = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pszHost, -1,
                                wNarrow, (int)E4_MAX_WPATH);
    if (iConv == 0)
    {   /* The path is not valid UTF-8, or did not fit the wide buffer. */
        return(E4_FAILURE);
    }

    /* STEP 2: resolve to an ABSOLUTE, backslash-normalised path.  The \\?\ form    */
    /* disables the Win32 path parser, so it tolerates no forward slash and no      */
    /* `.` / `..` -- GetFullPathNameW removes those AND turns a relative path       */
    /* (against the current directory) into the absolute one the prefix requires.   */
    /* It returns 0 on failure, or the length NEEDED (>= E4_MAX_WPATH) when wFull   */
    /* is too small; both are caught here.                                          */
    dwFull = GetFullPathNameW(wNarrow, (DWORD)E4_MAX_WPATH, wFull, NULL);
    if (dwFull == 0 || dwFull >= (DWORD)E4_MAX_WPATH)
    {   /* The absolute path could not be formed, or would not fit wFull. */
        return(E4_FAILURE);
    }

    /* STEP 3: prepend the extended-length prefix.  A UNC path "\\server\share"      */
    /* keeps one leading backslash and becomes "\\?\UNC\server\share"; any other     */
    /* (drive-letter) path simply gains the "\\?\" prefix.  _snwprintf is bounded    */
    /* by cchOut and does not always terminate on truncation, so a negative return   */
    /* (truncated) fails and the final slot is forced to NUL on the success path.    */
    /*                                                                               */
    /* THE ORDER OF THE TESTS MATTERS.  "\\?\C:\x" and "\\.\X:" also begin with      */
    /* two backslashes, and GetFullPathNameW hands both back unchanged; tested as    */
    /* a UNC path first, "\\?\C:\x" became "\\?\UNC\?\C:\x" -- a share named "?"     */
    /* on no server -- and every open of a path the user had typed in the            */
    /* extended form failed with "no such file".  So the already-prefixed forms      */
    /* are recognised FIRST and copied through.                                      */
    if (wFull[0] == L'\\' && wFull[1] == L'\\' &&
        (wFull[2] == L'?' || wFull[2] == L'.') && wFull[3] == L'\\')
    {   /* Already an extended-length or a device path: use it as it stands. */
        if (_snwprintf(pwOut, cchOut, L"%ls", wFull) < 0)
        {   /* The path did not fit the caller's buffer. */
            return(E4_FAILURE);
        }
    }
    else if (wFull[0] == L'\\' && wFull[1] == L'\\')
    {   /* A UNC path: drop its leading "\\" and splice in the UNC long-prefix. */
        if (_snwprintf(pwOut, cchOut, L"\\\\?\\UNC\\%ls", wFull + 2) < 0)
        {   /* The prefixed UNC path did not fit the caller's buffer. */
            return(E4_FAILURE);
        }
    }
    else
    {   /* A drive-letter path: the plain extended-length prefix suffices. */
        if (_snwprintf(pwOut, cchOut, L"\\\\?\\%ls", wFull) < 0)
        {   /* The prefixed path did not fit the caller's buffer. */
            return(E4_FAILURE);
        }
    }

    /* Force termination: _snwprintf leaves the buffer unterminated when the text    */
    /* exactly fills it, so the last slot is set to NUL after a non-truncated run.   */
    pwOut[cchOut - 1] = L'\0';

    /* The wide \\?\ path is ready for a _w* CRT call or FindFirstFileW. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* E4_HostFopen:                                                              */
/*                                                                            */
/* Long-path-capable replacement for fopen on a HOST file: widen the path to  */
/* the \\?\ form and open it with _wfopen.  Returns NULL with                 */
/* errno set on any failure, so every caller's existing `if (!pHost)` path is */
/* unchanged.                                                                 */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszHost : the UTF-8 host file path.                                    */
/*     pszMode : an fopen mode string (always a short ASCII literal here).    */
/*                                                                            */
/* Returns:                                                                   */
/*     FILE * : the open stream, or NULL (errno set) on failure.              */
/*----------------------------------------------------------------------------*/
static FILE *E4_HostFopen(const char *pszHost, const char *pszMode)
{
    /* Static, not on the stack: E4_HostWiden says why. */
    static wchar_t wPath[E4_MAX_WPATH];
    wchar_t wMode[8];
    int     i;

    /* Widen the path; a conversion failure is surfaced as a NULL open (errno) so   */
    /* the caller treats it exactly like any other open failure.                    */
    if (E4_HostWiden(pszHost, wPath, E4_MAX_WPATH) != E4_SUCCESS)
    {   /* The path could not be widened. */
        errno = EINVAL;
        return(NULL);
    }

    /* The mode is always a short ASCII literal ("rb" / "wb"), so it widens one   */
    /* char per slot with no code-page question; the buffer is sized for the      */
    /* longest mode this tool uses with room to spare.                            */
    for (i = 0; i < (int)(sizeof(wMode) / sizeof(wMode[0])) - 1 && pszMode[i] != '\0'; i++)
    {   /* One mode character per iteration. */
        wMode[i] = (wchar_t)pszMode[i];
    }
    wMode[i] = L'\0';

    /* _wfopen on the wide \\?\ path is the long-path-capable open and sets errno   */
    /* on failure exactly as fopen does.                                            */
    return(_wfopen(wPath, wMode));
}

/*------------------------------------------------------------------------------*/
/* E4_HostStat:                                                                 */
/*                                                                              */
/* Long-path-capable replacement for stat on a HOST path.  Uses                 */
/* _wstat64 (NOT _wstat) on purpose: its st_size is 64-bit, so a host file >=   */
/* 2 GiB reports its true size, and its times are 64-bit so they do not wrap in */
/* 2038 -- both of which the narrow 32-bit stat gets wrong.                     */
/*                                                                              */
/* Arguments:                                                                   */
/*     pszHost : the UTF-8 host path to query.                                  */
/*     pst     : receives the _stat64 result.                                   */
/*                                                                              */
/* Returns:                                                                     */
/*     int : 0 on success, -1 (errno set) on failure -- the stat convention.    */
/*------------------------------------------------------------------------------*/
static int E4_HostStat(const char *pszHost, struct _stat64 *pst)
{
    /* Static, not on the stack: E4_HostWiden says why. */
    static wchar_t wPath[E4_MAX_WPATH];

    /* Widen first; a conversion failure is reported as "not found" so the   */
    /* caller's existing stat-failed path runs.                              */
    if (E4_HostWiden(pszHost, wPath, E4_MAX_WPATH) != E4_SUCCESS)
    {   /* The path could not be widened. */
        errno = ENOENT;
        return(-1);
    }

    /* _wstat64 reaches a >260-char path the narrow stat cannot, and sets errno   */
    /* as stat does.                                                              */
    return(_wstat64(wPath, pst));
}

/*----------------------------------------------------------------------------*/
/* E4_HostMkdir:                                                              */
/*                                                                            */
/* Long-path-capable replacement for _mkdir on a HOST directory.              */
/* Preserves _mkdir's errno contract -- EEXIST in particular -- which callers */
/* rely on to tell "already there" (a tolerated merge) from a real failure.   */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszHost : the UTF-8 host directory path to create.                     */
/*                                                                            */
/* Returns:                                                                   */
/*     int : 0 on success, -1 (errno set, EEXIST when it already exists).     */
/*----------------------------------------------------------------------------*/
static int E4_HostMkdir(const char *pszHost)
{
    /* Static, not on the stack: E4_HostWiden says why. */
    static wchar_t wPath[E4_MAX_WPATH];

    /* Widen first; a conversion failure is reported as an invalid-argument fail. */
    if (E4_HostWiden(pszHost, wPath, E4_MAX_WPATH) != E4_SUCCESS)
    {   /* The path could not be widened. */
        errno = EINVAL;
        return(-1);
    }

    /* _wmkdir reaches a long parent path and sets errno (EEXIST et al.) exactly   */
    /* as _mkdir does.                                                             */
    return(_wmkdir(wPath));
}

/*------------------------------------------------------------------------------*/
/* E4_HostUtime:                                                                */
/*                                                                              */
/* Long-path-capable set of a HOST file's access + modify times,                */
/* the primitive behind the -t / --time option.  __utimbuf64 / _wutime64 carry  */
/* 64-bit times, matching E4_HostStat's 64-bit reads so a post-2038 time round- */
/* trips losslessly; only access + modify can be set (there is no portable      */
/* create-time set), which is why -t restores just those two.                   */
/*                                                                              */
/* Arguments:                                                                   */
/*     pszHost : the UTF-8 host file (or directory) path to stamp.              */
/*     atime   : access time, epoch seconds.                                    */
/*     mtime   : modify time, epoch seconds.                                    */
/*                                                                              */
/* Returns:                                                                     */
/*     int : 0 on success, -1 (errno set) on failure.                           */
/*------------------------------------------------------------------------------*/
static int E4_HostUtime(const char *pszHost, time_t atime, time_t mtime)
{
    /* Static, not on the stack: E4_HostWiden says why. */
    static wchar_t     wPath[E4_MAX_WPATH];
    struct __utimbuf64 ut;

    /* Widen first; a conversion failure is reported so the caller's best-effort   */
    /* warning fires rather than a wrong timestamp being claimed.                  */
    if (E4_HostWiden(pszHost, wPath, E4_MAX_WPATH) != E4_SUCCESS)
    {   /* The path could not be widened. */
        errno = EINVAL;
        return(-1);
    }

    /* Fill the 64-bit time pair and apply it; _wutime64 reaches a long path. */
    ut.actime  = (__time64_t)atime;
    ut.modtime = (__time64_t)mtime;

    /* The CRT's own result: 0, or -1 with errno set, as the caller expects. */
    return(_wutime64(wPath, &ut));
}

/*-----------------------------------------------------------------------------*/
/* E4_HostDirOpen:                                                             */
/*                                                                             */
/* Begin a long-path-capable walk of a HOST directory.  Widens                 */
/* the directory, appends the "\*" wildcard FindFirstFileW needs to enumerate  */
/* children, and primes the first entry.  FindFirstFileW is used rather than   */
/* MinGW's _wopendir because _wopendir has a long-path defect (it can silently */
/* read the CURRENT directory), whereas FindFirstFileW honours the \\?\ path.  */
/*                                                                             */
/* Arguments:                                                                  */
/*     pd         : the walk state to initialise.                              */
/*     pszHostDir : the UTF-8 host directory to enumerate.                     */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS with the first entry primed, E4_FAILURE otherwise      */
/*           (errno set for the caller's message).  On failure pd is left      */
/*           safe to pass to E4_HostDirClose.                                  */
/*-----------------------------------------------------------------------------*/
static int E4_HostDirOpen(struct E4_HostDir *pd, const char *pszHostDir)
{
    /* Static, not on the stack: E4_HostWiden says why. */
    static wchar_t wPath[E4_MAX_WPATH];
    size_t  cch;

    /* Default to "no handle" so a failed open still leaves a struct that   */
    /* E4_HostDirClose handles harmlessly.                                  */
    pd->hFind   = INVALID_HANDLE_VALUE;
    pd->bPrimed = false;

    /* Widen the directory path to the \\?\ form. */
    if (E4_HostWiden(pszHostDir, wPath, E4_MAX_WPATH) != E4_SUCCESS)
    {   /* The directory path could not be widened. */
        errno = EINVAL;
        return(E4_FAILURE);
    }

    /* Append "\*": FindFirstFileW enumerates a directory's CHILDREN by matching a   */
    /* wildcard inside it, not by naming the directory.  Guard the two extra chars   */
    /* against the buffer so the append never overruns.  A path that already ends    */
    /* in a backslash (a drive root "\\?\C:\") takes only the "*".                   */
    cch = wcslen(wPath);
    if (cch + 2 >= E4_MAX_WPATH)
    {   /* No room to append the "\*" search pattern. */
        errno = ENAMETOOLONG;
        return(E4_FAILURE);
    }

    if (cch > 0 && wPath[cch - 1] != L'\\')
    {   /* No trailing separator yet: add the one the pattern needs. */
        wPath[cch] = L'\\';
        cch++;
    }

    /* The wildcard itself, then the terminator, in the room proved above. */
    wPath[cch]     = L'*';
    wPath[cch + 1] = L'\0';

    /* Open the enumeration; its first result lands in pd->wfd and is handed out   */
    /* by the first E4_HostDirRead.                                                */
    pd->hFind = FindFirstFileW(wPath, &pd->wfd);
    if (pd->hFind == INVALID_HANDLE_VALUE)
    {   /* The directory could not be opened for enumeration.  FindFirstFileW    */
        /* reports through GetLastError, not errno, and the callers print        */
        /* strerror(errno) -- so the Win32 code is mapped here, or the message   */
        /* would show whatever an earlier, unrelated call had left in errno.     */
        if (GetLastError() == ERROR_ACCESS_DENIED)
        {   /* No permission to list the directory. */
            errno = EACCES;
        }
        else
        {   /* Missing, not a directory, or anything else: "no such directory". */
            errno = ENOENT;
        }

        return(E4_FAILURE);
    }

    /* The first entry is waiting in pd->wfd; it must be consumed before any   */
    /* FindNextFileW call, which bPrimed enforces.                             */
    pd->bPrimed = true;

    /* The walk is open with its first entry waiting. */
    return(E4_SUCCESS);
}

/*------------------------------------------------------------------------------*/
/* E4_HostDirRead:                                                              */
/*                                                                              */
/* Hand back the next HOST directory entry's name as UTF-8, the encoding every  */
/* path in this tool is carried in.  The primed FindFirstFileW entry is handed  */
/* out first; later calls advance with FindNextFileW.  "." and ".." are         */
/* returned like readdir -- the caller skips them.                              */
/*                                                                              */
/* UTF-8 holds every Windows name exactly, which is the point: the earlier      */
/* conversion to the ANSI code page did NOT fail on a name outside it, it       */
/* substituted a look-alike letter, so a second file's name collapsed onto a    */
/* neighbour's and that neighbour was imported twice while the real file was    */
/* silently left behind.  The one name that still cannot be handed back is one  */
/* that is not valid UTF-16 at all (a lone surrogate) or that would not fit the */
/* caller's buffer; that is reported as E4_HD_BADNAME, never skipped quietly,   */
/* so the caller can count it.                                                  */
/*                                                                              */
/* Arguments:                                                                   */
/*     pd         : the walk state from E4_HostDirOpen.                         */
/*     pszNameOut : receives the entry name, UTF-8, on E4_HD_ENTRY.             */
/*     cchOut     : capacity of pszNameOut, in bytes.                           */
/*                                                                              */
/* Returns:                                                                     */
/*     int : E4_HD_ENTRY with the name in pszNameOut; E4_HD_END when the        */
/*           directory is exhausted; E4_HD_BADNAME for an entry whose name      */
/*           cannot be handed back (the walk may continue); E4_HD_ERROR when    */
/*           the enumeration failed part-way (the walk is over, and the         */
/*           entries seen so far are NOT the whole directory).                  */
/*------------------------------------------------------------------------------*/
static int E4_HostDirRead(struct E4_HostDir *pd, char *pszNameOut, size_t cchOut)
{
    int iConv;

    if (pd->bPrimed)
    {   /* Hand out the entry FindFirstFileW primed; clear the flag so the next   */
        /* call advances.                                                         */
        pd->bPrimed = false;
    }
    else
    {   /* Advance.  A false return is the END only when the reason is "no more     */
        /* files"; any other reason is a read error part-way through, and calling   */
        /* that the end would report a truncated import as a complete one.          */
        if (!FindNextFileW(pd->hFind, &pd->wfd))
        {   /* Nothing more was read: decide which of the two it was. */
            if (GetLastError() == ERROR_NO_MORE_FILES)
            {   /* The clean end of the directory. */
                return(E4_HD_END);
            }

            /* Any other reason: the enumeration failed part-way, and the entries   */
            /* handed out so far are not the whole directory.                       */
            return(E4_HD_ERROR);
        }
    }

    /* Wide -> UTF-8.  WC_ERR_INVALID_CHARS makes an unpaired surrogate a FAILURE    */
    /* rather than a silent U+FFFD, for the same reason as in E4_HostWiden: a name   */
    /* altered in transit names a different file.  Zero is also what a name too      */
    /* long for the caller's buffer returns.                                         */
    iConv = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, pd->wfd.cFileName, -1,
                                pszNameOut, (int)cchOut, NULL, NULL);
    if (iConv == 0)
    {   /* The entry exists but its name cannot be handed back. */
        return(E4_HD_BADNAME);
    }

    /* The entry's name, exact, in pszNameOut. */
    return(E4_HD_ENTRY);
}

/*-------------------------------------------------------------------------------*/
/* E4_HostDirClose:                                                              */
/*                                                                               */
/* Release the Win32 search handle an E4_HostDir holds.  Idempotent: a struct    */
/* that never opened (hFind INVALID_HANDLE_VALUE) or an already-closed one is a  */
/* no-op, so callers may close on every exit path without tracking which failed. */
/*                                                                               */
/* Arguments:                                                                    */
/*     pd : the walk state to release.                                           */
/*                                                                               */
/* Returns:                                                                      */
/*     void : the handle is closed and marked none.                              */
/*-------------------------------------------------------------------------------*/
static void E4_HostDirClose(struct E4_HostDir *pd)
{
    if (pd->hFind != INVALID_HANDLE_VALUE)
    {   /* Release the search handle and mark the struct closed, so a second close   */
        /* is harmless.                                                              */
        FindClose(pd->hFind);
        pd->hFind = INVALID_HANDLE_VALUE;
    }
}

/*--------------------------------------------------------------------------------*/
/* E4_HostNameValid:                                                              */
/*                                                                                */
/* Whether the LAST component of a host destination path is a name Windows can    */
/* create as an ordinary file.  dump / rdump take a destination                   */
/* name from the user or from an image entry and must refuse a name that Windows  */
/* would either reject or SILENTLY REWRITE, rather than create a surprising file  */
/* or write to a device:                                                          */
/*                                                                                */
/*   - a DOS device name (CON / PRN / AUX / NUL / COM1-9 / LPT1-9), compared on   */
/*     the stem before any extension and case-insensitively, because Windows      */
/*     treats "con", "con.txt" and "CON" alike as the console DEVICE -- a file of */
/*     that name is not a file and writing it goes to the device;                 */
/*   - any character Windows forbids in a name (< > : " / \ | ? * or a control    */
/*     byte), which would fail or land somewhere unintended;                      */
/*   - a trailing '.' or ' ', which Windows STRIPS on creation, so "trail."       */
/*     becomes "trail" and a later lookup by the recorded name misses.            */
/*                                                                                */
/* Only the final component is checked: a device name is reserved in EVERY        */
/* directory, so "out\con" is as unsafe as "con", and the other rules are         */
/* per-name.  A final component of "." or "..", and a destination that is just    */
/* a drive ("X:"), name a folder that already exists and are let through.         */
/*                                                                                */
/* Arguments:                                                                     */
/*     pszPath : the host destination path (its last component is checked).       */
/*                                                                                */
/* Returns:                                                                       */
/*     bool : true if the final component is a safe, creatable file name.         */
/*--------------------------------------------------------------------------------*/
static bool E4_HostNameValid(const char *pszPath)
{
    static const char *apszReserved[] = {
        "CON",  "PRN",  "AUX",  "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    const char *pszName;
    const char *p;
    size_t      cchStem;
    size_t      i;

    /* Find the final component: the text after the last '/' or '\'.  Host paths   */
    /* are composed here with '/', but a user may type '\', so both separate.      */
    pszName = pszPath;
    for (p = pszPath; *p != '\0'; p++)
    {   /* One character per iteration, tracking the start of the last component. */
        if (*p == '/' || *p == '\\')
        {   /* A separator: the component restarts after it. */
            pszName = p + 1;
        }
    }

    if (pszName[0] == '\0')
    {   /* A trailing separator leaves no final name to create -- nothing to refuse. */
        return(true);
    }

    if (strcmp(pszName, ".") == 0 || strcmp(pszName, "..") == 0)
    {   /* "this folder" and "the parent folder" are not names to CREATE: they       */
        /* refer to a folder that exists, so `rdump <dir> .` and a destination       */
        /* ending in ".." are ordinary.  Without this the trailing-dot rule below    */
        /* refused both.  (An image entry is never one of these two: E4_CollectDir   */
        /* drops them before a name reaches here.)                                   */
        return(true);
    }

    if (pszName == pszPath && pszName[1] == ':' && pszName[2] == '\0' &&
        ((pszName[0] >= 'A' && pszName[0] <= 'Z') || (pszName[0] >= 'a' && pszName[0] <= 'z')))
    {   /* The whole destination is a bare drive, "X:" -- that drive's current      */
        /* folder, again something that exists rather than a name to create.  The   */
        /* colon rule below is about a colon INSIDE a file name.                    */
        return(true);
    }

    /* Reject forbidden characters and control bytes: either would make the create   */
    /* fail or redirect it.                                                          */
    for (p = pszName; *p != '\0'; p++)
    {   /* One character per iteration. */
        if ((unsigned char)*p < 0x20 ||
            *p == '<' || *p == '>' || *p == ':' || *p == '"' ||
            *p == '/' || *p == '\\' || *p == '|' || *p == '?' || *p == '*')
        {   /* An illegal character: this name cannot be created safely. */
            return(false);
        }
    }

    /* Reject a trailing '.' or ' ': Windows strips either on creation, which is   */
    /* a silent rename ("trail." -> "trail").                                      */
    cchStem = strlen(pszName);
    if (pszName[cchStem - 1] == '.' || pszName[cchStem - 1] == ' ')
    {   /* A trailing dot or space would be silently stripped. */
        return(false);
    }

    /* Measure the STEM -- the text up to the first '.' -- for the device-name   */
    /* test, since "con.txt" is the console device just as "con" is.             */
    cchStem = 0;
    while (pszName[cchStem] != '\0' && pszName[cchStem] != '.')
    {   /* One stem character per iteration, up to the first dot. */
        cchStem++;
    }

    for (i = 0; i < sizeof(apszReserved) / sizeof(apszReserved[0]); i++)
    {   /* One reserved device name per iteration. */
        if (strlen(apszReserved[i]) == cchStem &&
            _strnicmp(pszName, apszReserved[i], cchStem) == 0)
        {   /* The stem IS a reserved DOS device name. */
            return(false);
        }
    }

    /* The final component is a safe, creatable Windows file name. */
    return(true);
}

/*----------------------------------------------------------------------------*/
/* E4_HostDirEmpty:                                                           */
/*                                                                            */
/* Report whether a HOST (Windows) directory holds nothing but "." and "..".  */
/* rdump uses it to refuse extracting into a directory that already has       */
/* content unless --force is given, so an extract cannot quietly mix with     */
/* unrelated files.                                                           */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszHostDir : the host directory to test.                               */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true if empty (or unreadable is treated as NOT empty, the safe  */
/*            answer), false if it holds any real entry.                      */
/*----------------------------------------------------------------------------*/
static bool E4_HostDirEmpty(const char *pszHostDir)
{
    struct E4_HostDir hd;
    char              szName[E4_MAX_PATH];
    bool              bEmpty;
    int               iRead;

    if (E4_HostDirOpen(&hd, pszHostDir) != E4_SUCCESS)
    {   /* Cannot read it; treat as "not empty" so nothing is overwritten blind. */
        return(false);
    }

    /* Assume empty until a real entry (anything but "." / "..") is seen below. */
    bEmpty = true;

    for (;;)
    {   /* One directory entry per iteration. */
        iRead = E4_HostDirRead(&hd, szName, sizeof(szName));

        if (iRead == E4_HD_END)
        {   /* End of the directory with no real entry seen: it is empty. */
            break;
        }

        if (iRead != E4_HD_ENTRY)
        {   /* A read error, or an entry whose name could not be handed back.       */
            /* Either way the directory cannot be SHOWN to be empty, and the safe   */
            /* answer for the caller's "may I extract into this?" is no.            */
            bEmpty = false;
            break;
        }

        if (strcmp(szName, ".") == 0 || strcmp(szName, "..") == 0)
        {   /* The self / parent links do not count. */
            continue;
        }

        /* A real entry: the directory is not empty, so stop looking. */
        bEmpty = false;
        break;
    }

    /* Release the host directory handle and report the verdict. */
    E4_HostDirClose(&hd);

    /* true only when the walk reached the end without a real entry. */
    return(bEmpty);
}

/*----------------------------------------------------------------------------*/
/* E4_CollectDir:                                                             */
/*                                                                            */
/* Read every real entry of an image directory into a freshly allocated array */
/* (grown as needed, so any directory size is handled), skipping unused slots */
/* and the "." / ".." links, then close the directory.  The caller owns the   */
/* array and frees it.  Collecting first means the directory handle is closed */
/* before the caller descends into subdirectories or reads files, which keeps */
/* exactly one ext4_dir open at any moment.                                   */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszDir  : the image directory to collect.                              */
/*     ppEnts  : receives the malloc'd entry array (NULL when empty).         */
/*     pnEnts  : receives the entry count.                                    */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on a listed directory, E4_FAILURE on open / memory    */
/*           failure, or on a read that ended the walk early (with the        */
/*           reason reported).                                                */
/*----------------------------------------------------------------------------*/
static int E4_CollectDir(const char *pszDir, struct E4_DirEnt **ppEnts, int *pnEnts)
{
    ext4_dir             dir;
    const ext4_direntry *pEnt;
    struct E4_DirEnt    *pArr;
    struct E4_DirEnt    *pGrown;
    int                  nCap;
    int                  nUsed;
    int                  rc;

    /* Clear the out-parameters first, so every failure path below leaves the       */
    /* caller a safe empty result (no array, zero count) rather than a stale one.   */
    *ppEnts = NULL;
    *pnEnts = 0;

    rc = ext4_dir_open(&dir, pszDir);
    if (rc != 0)
    {   /* Not a directory, or it does not exist. */
        Log_Err("cannot open directory %s: %s", pszDir, strerror(rc));
        return(E4_FAILURE);
    }

    /* Start with no array: it is allocated lazily on the first kept entry and   */
    /* grown geometrically.  nCap is the current capacity, nUsed the number of   */
    /* entries filled so far.                                                    */
    pArr  = NULL;
    nCap  = 0;
    nUsed = 0;

    for (;;)
    {   /* One directory entry per iteration. */
        pEnt = ext4_dir_entry_next(&dir);

        if (pEnt == NULL)
        {   /* End of the directory. */
            break;
        }

        if (pEnt->inode == 0)
        {   /* A dead slot (deleted entry, inode field cleared); not a child. */
            continue;
        }

        /* Drop "." and ".." -- the extract walk must not descend into the self     */
        /* or parent link, which is how a walk would loop for ever or climb out     */
        /* of its own subtree.  Tested on the raw counted name (length + bytes),    */
        /* not strcmp, so no NUL-terminated copy is needed merely to screen them.   */
        if (pEnt->name_length == 1 && pEnt->name[0] == '.')
        {   /* The "." self link is not a child. */
            continue;
        }

        if (pEnt->name_length == 2 && pEnt->name[0] == '.' && pEnt->name[1] == '.')
        {   /* The ".." parent link is not a child. */
            continue;
        }

        if (nUsed == nCap)
        {   /* Grow the array GEOMETRICALLY (0 -> 128, then doubling) so a           */
            /* directory of any size is collected in O(log n) reallocs rather than   */
            /* one per entry.                                                        */
            if (nCap == 0)
            {   /* The first growth: from nothing to 128 entries. */
                nCap = 128;
            }
            else
            {   /* Every later growth doubles the capacity. */
                nCap = nCap * 2;
            }

            /* realloc into a SEPARATE pGrown so the old pArr is not leaked (and   */
            /* is freed) if the realloc fails.                                     */
            pGrown = (struct E4_DirEnt *)realloc(pArr, (size_t)nCap * sizeof(*pArr));

            if (pGrown == NULL)
            {   /* Out of memory collecting this directory. */
                Log_Err("out of memory listing %s", pszDir);
                free(pArr);
                ext4_dir_close(&dir);
                return(E4_FAILURE);
            }

            pArr = pGrown;
        }

        /* Record this entry: copy its counted name out and terminate it, store     */
        /* its directory-entry type (so the caller does not re-stat to learn it),   */
        /* and advance.  name_length is a uint8_t (<= 255), so the copy always      */
        /* fits szName[256].                                                        */
        memcpy(pArr[nUsed].szName, pEnt->name, pEnt->name_length);
        pArr[nUsed].szName[pEnt->name_length] = '\0';
        pArr[nUsed].iType = (int)pEnt->inode_type;
        nUsed++;
    }

    /* Collected: close the directory (last_err survives the close). */
    ext4_dir_close(&dir);

    if (dir.last_err != EOK)
    {   /* The walk ended early: the directory's inode or one of its blocks could    */
        /* not be read, so the names collected are NOT the directory.  Handing       */
        /* them over would extract or import a damaged directory as complete, so     */
        /* they are dropped and the level fails (the caller skips it and goes on).   */
        Log_Err("cannot read directory %s in full: %s", pszDir, strerror(dir.last_err));
        free(pArr);
        return(E4_FAILURE);
    }

    /* Hand the array and count back.  pArr stays NULL when the directory held   */
    /* no real entry, which the caller treats as an empty directory (and has     */
    /* nothing to free).                                                         */
    *ppEnts = pArr;
    *pnEnts = nUsed;

    /* The directory was collected (pArr is NULL when it held no real entry). */
    return(E4_SUCCESS);
}

/*-----------------------------------------------------------------------------*/
/* E4_ExtractFile:                                                             */
/*                                                                             */
/* Copy one image file's bytes OUT to a host file, verbatim (an extract must   */
/* be byte-exact, so no LF/CRLF translation -- unlike `cat --text`).  Streamed */
/* through a fixed buffer so size is no concern.                               */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszImgPath  : the file inside the image.                                */
/*     pszHostPath : the host file to create / overwrite.                      */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS on a complete copy, E4_FAILURE otherwise.              */
/*-----------------------------------------------------------------------------*/
static int E4_ExtractFile(const char *pszImgPath, const char *pszHostPath)
{
    ext4_file  f;
    FILE      *pHost;
    /* Static, not on the stack (64 KiB): E4_HostWiden says why. */
    static char abBuf[E4_IO_BUF];
    size_t     rcnt;
    int        rc;
    int        iRc;

    rc = ext4_fopen(&f, pszImgPath, "rb");
    if (rc != 0)
    {   /* The image file could not be opened for reading. */
        Log_Err("cannot read %s: %s", pszImgPath, strerror(rc));
        return(E4_FAILURE);
    }

    pHost = E4_HostFopen(pszHostPath, "wb");
    if (pHost == NULL)
    {   /* The host destination could not be created. */
        Log_Err("cannot create host file %s: %s", pszHostPath, strerror(errno));
        ext4_fclose(&f);
        return(E4_FAILURE);
    }

    /* Start optimistic; a read or write error in the loop flips it to failure. */
    iRc = E4_SUCCESS;

    for (;;)
    {   /* One read chunk per iteration, streamed so a file of any size copies   */
        /* through the one fixed buffer.                                         */

        /* Clear the count before each read (ext4_fread sets it on success). */
        rcnt = 0;

        /* ext4_fread signals an error through its RETURN CODE and reports a clean   */
        /* end-of-file as rc 0 with rcnt 0 -- unambiguous, UNLIKE the host-side      */
        /* fread in E4_ImportFile, where a 0 count needs ferror to tell EOF from a   */
        /* read error.  So here the rc is tested first, then a zero count is         */
        /* simply the end of the file.                                               */
        rc = ext4_fread(&f, abBuf, sizeof(abBuf), &rcnt);
        if (rc != 0)
        {   /* A read error on the image ends the copy. */
            Log_Err("read error on %s: %s", pszImgPath, strerror(rc));
            iRc = E4_FAILURE;
            break;
        }

        if (rcnt == 0)
        {   /* End of file: the extract is complete. */
            break;
        }

        /* Write verbatim: the extract is byte-exact (no LF/CRLF translation --    */
        /* that is cat's `--text` job), so a round-trip through dump / rdump and   */
        /* back by rwrite reproduces the original bytes.                           */
        if (fwrite(abBuf, 1, rcnt, pHost) != rcnt)
        {   /* The host write failed (a full disk, say). */
            Log_Err("write error on host file %s", pszHostPath);
            iRc = E4_FAILURE;
            break;
        }
    }

    /* Done streaming: close the host file and the in-image file. */
    fclose(pHost);
    ext4_fclose(&f);

    /* iRc carries the result of the streaming loop above. */
    return(iRc);
}

/*----------------------------------------------------------------------------*/
/* E4_ImportFile:                                                             */
/*                                                                            */
/* Copy one HOST file's bytes IN to an image file (the inverse of             */
/* E4_ExtractFile), creating or truncating the image file ("wb").  The        */
/* caller sets the mode / owner / mtime afterward; this moves only the bytes. */
/* A copy that fails part-way removes the partial image file again, so a      */
/* failure leaves no entry behind.                                            */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszHostPath : the host file to read.                                   */
/*     pszImgPath  : the image file to create / overwrite.                    */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on a complete copy, E4_FAILURE otherwise.             */
/*----------------------------------------------------------------------------*/
static int E4_ImportFile(const char *pszHostPath, const char *pszImgPath)
{
    FILE      *pHost;
    ext4_file  f;
    /* Static, not on the stack (64 KiB): E4_HostWiden says why. */
    static char abBuf[E4_IO_BUF];
    size_t     rcnt;
    size_t     wcnt;
    int        rc;
    int        iRc;

    pHost = E4_HostFopen(pszHostPath, "rb");
    if (pHost == NULL)
    {   /* The host source could not be opened. */
        Log_Err("cannot open host file %s: %s", pszHostPath, strerror(errno));
        return(E4_FAILURE);
    }

    rc = ext4_fopen(&f, pszImgPath, "wb");
    if (rc != 0)
    {   /* Parent missing, the name is a directory, or a write error. */
        Log_Err("cannot create %s: %s", pszImgPath, strerror(rc));
        fclose(pHost);
        return(E4_FAILURE);
    }

    /* Start optimistic; a read or write error in the loop flips it to failure. */
    iRc = E4_SUCCESS;

    for (;;)
    {   /* One host read chunk per iteration. */
        rcnt = fread(abBuf, 1, sizeof(abBuf), pHost);

        if (rcnt == 0)
        {   /* fread returns 0 for BOTH end-of-file and a read error, so ferror     */
            /* decides which -- the inverse of the image side's ext4_fread, which   */
            /* carries a separate error code.  Without this check a mid-file read   */
            /* error would read as a clean EOF and silently truncate the imported   */
            /* file into the image.                                                 */
            if (ferror(pHost))
            {   /* A genuine read error on the source. */
                Log_Err("read error on host file %s", pszHostPath);
                iRc = E4_FAILURE;
            }
            break;
        }

        /* Clear the written-count; ext4_fwrite reports how much it actually took. */
        wcnt = 0;

        rc = ext4_fwrite(&f, abBuf, rcnt, &wcnt);
        if (rc != 0 || wcnt != rcnt)
        {   /* The image is full or broken.  lwext4 reports a FULL filesystem as a   */
            /* short count with a ZERO status, so the two cases are worded apart     */
            /* rather than printing strerror(0), "No error".                         */
            if (rc != 0)
            {   /* lwext4 gave a reason. */
                Log_Err("write to %s failed: %s", pszImgPath, strerror(rc));
            }
            else
            {   /* A short count with no error code: no room left. */
                Log_Err("write to %s failed: the filesystem in the image is full", pszImgPath);
            }

            iRc = E4_FAILURE;
            break;
        }
    }

    /* Done streaming: close the in-image file and the host file. */
    ext4_fclose(&f);
    fclose(pHost);

    if (iRc != E4_SUCCESS)
    {   /* The copy stopped part-way: remove the partial entry.  Left in place it   */
        /* reads as a complete file under the right name, and on a full image it    */
        /* holds every block that was free -- so each LATER file of the same        */
        /* rwrite would fail too, each leaving an empty entry of its own.  With     */
        /* it gone, smaller files that follow can still be imported.                */
        rc = ext4_fremove(pszImgPath);

        if (rc != 0)
        {   /* It could not be removed; it is still there and incomplete. */
            Log_Err("warning: the partial file %s could not be removed: %s",
                    pszImgPath, strerror(rc));
        }
    }

    /* iRc carries the result of the streaming loop above. */
    return(iRc);
}

/*----------------------------------------------------------------------------*/
/* E4_SetHostTime:                                                            */
/*                                                                            */
/* Set a host file's access and modify times from inode epoch seconds (the    */
/* `-t` / `--time` option of dump / rdump).  Best-effort: a failure -- which  */
/* can happen on a directory on some hosts -- is reported but does not fail   */
/* the extract, since the data is already safely out.                         */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszHostPath : the host file (or directory) to stamp.                   */
/*     uiAtime     : access time, epoch seconds.                              */
/*     uiMtime     : modify time, epoch seconds.                              */
/*                                                                            */
/* Returns:                                                                   */
/*     void : the host time is set, or a warning is printed.                  */
/*----------------------------------------------------------------------------*/
static void E4_SetHostTime(const char *pszHostPath, uint32_t uiAtime, uint32_t uiMtime)
{
    /* Only access + modify can be set -- there is no portable set for the create    */
    /* time or ctime, which is why -t restores just those two.  The set goes         */
    /* through E4_HostUtime so it reaches a >260-char host path.  A                  */
    /* failure is WARNED, not propagated: the file's DATA is already extracted, so   */
    /* a missing timestamp is cosmetic, and a timestamp set on a Windows DIRECTORY   */
    /* fails on some CRTs -- failing the whole extract over that would be worse      */
    /* than a wrong mtime.                                                           */
    if (E4_HostUtime(pszHostPath, (time_t)uiAtime, (time_t)uiMtime) != 0)
    {   /* The host rejected the timestamp set; note it and carry on. */
        Log_Err("warning: could not set times on %s: %s",
                pszHostPath, strerror(errno));
    }
}

/*-----------------------------------------------------------------------------*/
/* E4_WriteMetaBlock:                                                          */
/*                                                                             */
/* Write one `key: value` metadata block for an entry to an open sidecar /     */
/* manifest stream -- the Unix facts NTFS cannot hold, so a later rwrite could */
/* restore them.  A regular file or directory gets path / type / mode (octal   */
/* permission bits) / uid / gid / the three times / size, read straight off    */
/* the raw inode exactly as `stat` does.  A SYMLINK is not materialised on     */
/* Windows, so it records path / type / target only (via ext4_readlink, which  */
/* does not follow the link), losing nothing of what the link WAS.             */
/*                                                                             */
/* Arguments:                                                                  */
/*     pMeta       : the open sidecar / manifest stream to append to.          */
/*     pszRecPath  : the path to record (in-image for dump, relative for       */
/*                   rdump).                                                   */
/*     iType       : the entry's EXT4_DE_* type (from its directory entry).    */
/*     pszImgPath  : the entry's full in-image path (to read the inode).       */
/*     sb          : the mounted superblock (for the size / mode accessors).   */
/*     bBlankBefore: true to emit a separating blank line first (manifest).    */
/*                                                                             */
/* Returns:                                                                    */
/*     void : one block is appended to pMeta.                                  */
/*-----------------------------------------------------------------------------*/
static void E4_WriteMetaBlock(FILE *pMeta, const char *pszRecPath, int iType,
                              const char *pszImgPath, struct ext4_sblock *sb,
                              bool bBlankBefore)
{
    struct ext4_inode inode;
    char              szTarget[E4_MAX_PATH];
    uint32_t          uiIno;
    uint32_t          uiMode;
    size_t            rcnt;
    int               rc;

    if (bBlankBefore)
    {   /* Separate consecutive manifest blocks with a blank line. */
        fputc('\n', pMeta);
    }

    /* Every block names the entry and its type first. */
    fprintf(pMeta, "path: %s\n", pszRecPath);
    fprintf(pMeta, "type: %s\n", E4_TypeName(iType));

    if (iType == EXT4_DE_SYMLINK)
    {   /* A symlink: record only where it pointed (its perm bits are not   */
        /* meaningful and it is never written to NTFS).                     */

        /* ext4_readlink reports the target length through rcnt; clear it first. */
        rcnt = 0;

        rc = ext4_readlink(pszImgPath, szTarget, sizeof(szTarget) - 1, &rcnt);
        if (rc == 0)
        {   /* Clamp defensively, NUL-terminate, and record the target. */
            if (rcnt >= sizeof(szTarget))
            {   /* Never index past the buffer end. */
                rcnt = sizeof(szTarget) - 1;
            }
            szTarget[rcnt] = '\0';
            fprintf(pMeta, "target: %s\n", szTarget);
        }
        else
        {   /* The target could not be read; say so rather than omit it. */
            fprintf(pMeta, "target: (unreadable: %s)\n", strerror(rc));
        }

        return;
    }

    /* A file / directory (or any other inode type): record the full metadata   */
    /* straight off the raw inode.                                              */
    uiIno = 0;
    memset(&inode, 0, sizeof(inode));

    rc = ext4_fill_raw_inode(pszImgPath, &uiIno, &inode);
    if (rc != 0)
    {   /* The inode could not be read; the type line still stands on its own. */
        fprintf(pMeta, "note: inode unreadable: %s\n", strerror(rc));
        return;
    }

    /* The full mode word, decoded with the superblock's help as `stat` does. */
    uiMode = ext4_inode_get_mode(sb, &inode);

    /* Record the PERMISSION bits only (low 12, type nibble masked off).  A later   */
    /* rwrite restores the mode with chmod, which sets perms and leaves the type    */
    /* to the freshly-created entry, so storing the full mode word (e.g. 0100644)   */
    /* would be misread; the type line above already carries the kind.              */
    fprintf(pMeta, "mode: %04lo\n",
            (unsigned long)(uiMode & ~((uint32_t)EXT4_INODE_MODE_TYPE_MASK)));
    fprintf(pMeta, "uid: %lu\n", (unsigned long)ext4_inode_get_uid(&inode));
    fprintf(pMeta, "gid: %lu\n", (unsigned long)ext4_inode_get_gid(&inode));
    fprintf(pMeta, "mtime: %lu\n",
            (unsigned long)ext4_inode_get_modif_time(&inode));
    fprintf(pMeta, "atime: %lu\n",
            (unsigned long)ext4_inode_get_access_time(&inode));
    fprintf(pMeta, "ctime: %lu\n",
            (unsigned long)ext4_inode_get_change_inode_time(&inode));
    fprintf(pMeta, "size: %llu\n",
            (unsigned long long)ext4_inode_get_size(sb, &inode));
}

/*----------------------------------------------------------------------------*/
/* E4_MetaTypeFromName:                                                       */
/*                                                                            */
/* The inverse of E4_TypeName: turn the label on a manifest's `type:` line    */
/* back into an EXT4_DE_* value.                                              */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszLabel : the text after "type: " (e.g. "file", "symlink").           */
/*                                                                            */
/* Returns:                                                                   */
/*     int : the EXT4_DE_* type; EXT4_DE_UNKNOWN for a label E4_TypeName does */
/*           not produce.                                                     */
/*----------------------------------------------------------------------------*/
static int E4_MetaTypeFromName(const char *pszLabel)
{
    static const int aiTypes[] = {
        EXT4_DE_REG_FILE, EXT4_DE_DIR, EXT4_DE_SYMLINK, EXT4_DE_CHRDEV,
        EXT4_DE_BLKDEV, EXT4_DE_FIFO, EXT4_DE_SOCK
    };
    size_t i;

    /* Ask E4_TypeName for each type's label rather than keeping a second list   */
    /* of the words here: the writer and the reader then cannot drift apart.     */
    for (i = 0; i < sizeof(aiTypes) / sizeof(aiTypes[0]); i++)
    {   /* One known entry type per iteration. */
        if (strcmp(pszLabel, E4_TypeName(aiTypes[i])) == 0)
        {   /* The label is this type's. */
            return(aiTypes[i]);
        }
    }

    /* Not a label this tool writes. */
    return(EXT4_DE_UNKNOWN);
}

/*----------------------------------------------------------------------------*/
/* E4_MetaCompare:                                                            */
/*                                                                            */
/* qsort comparator ordering manifest records by path, byte for byte.  The    */
/* same order (strcmp) is what E4_MetaFind's binary search assumes.           */
/*                                                                            */
/* Arguments:                                                                 */
/*     pvA : the first record (a struct E4_MetaRec).                          */
/*     pvB : the second record.                                               */
/*                                                                            */
/* Returns:                                                                   */
/*     int : negative, zero or positive as A's path sorts before, with or     */
/*           after B's.                                                       */
/*----------------------------------------------------------------------------*/
static int E4_MetaCompare(const void *pvA, const void *pvB)
{
    const struct E4_MetaRec *pA;
    const struct E4_MetaRec *pB;

    /* Give the untyped qsort arguments their record type. */
    pA = (const struct E4_MetaRec *)pvA;
    pB = (const struct E4_MetaRec *)pvB;

    /* Plain byte order of the two paths. */
    return(strcmp(pA->pszPath, pB->pszPath));
}

/*----------------------------------------------------------------------------*/
/* E4_MetaFree:                                                               */
/*                                                                            */
/* Release a loaded manifest.  Safe on a set that was never loaded or whose   */
/* load failed (both pointers NULL), so a caller may free on every exit.      */
/*                                                                            */
/* Arguments:                                                                 */
/*     pSet : the manifest to release.                                        */
/*                                                                            */
/* Returns:                                                                   */
/*     void : the text and the record array are freed and the set is empty.   */
/*----------------------------------------------------------------------------*/
static void E4_MetaFree(struct E4_MetaSet *pSet)
{
    /* free(NULL) is a no-op, so no tests are needed; the fields are cleared so   */
    /* a second call, or a stray use, meets an empty set rather than freed        */
    /* memory.                                                                    */
    free(pSet->pRecs);
    free(pSet->pszText);
    pSet->pRecs   = NULL;
    pSet->pszText = NULL;
    pSet->nRecs   = 0;
}

/*----------------------------------------------------------------------------*/
/* E4_MetaPathValid:                                                          */
/*                                                                            */
/* Whether a metadata record's `path:` value is one this tool wrote and can   */
/* use: a manifest's relative path of one or more names joined by "/"         */
/* ("name", "dir/name"), or a sidecar's absolute in-image path -- in either,  */
/* no name empty, "." or "..".  An ext4 name can never be one of those three, */
/* nor hold a "/", so no manifest or sidecar this tool wrote is refused here. */
/*                                                                            */
/* Why it matters: a manifest record's path is JOINED to the import's         */
/* destination to make the symbolic link it records (Cmd_Rwrite), and lwext4  */
/* resolves a ".." through the directory's own ".." entry -- so a ".." could  */
/* climb out of the tree, and a ".." inside a NEW link's path makes lwext4    */
/* create entries whose names contain the slash until the inodes run out.  A  */
/* "." or an empty name does not climb, but it names an entry under a second  */
/* spelling that nothing this tool wrote can contain.  Only the PATH is       */
/* judged: a link's `target:` text is Linux data and is restored as written,  */
/* however it climbs.                                                         */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszPath : the record's path: value.                                    */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true when every name of the path is non-empty and neither "."   */
/*            nor ".."; false otherwise (an empty value, and "/" alone, among */
/*            them).                                                          */
/*----------------------------------------------------------------------------*/
static bool E4_MetaPathValid(const char *pszPath)
{
    const char *pszComponent;
    size_t      cchComponent;

    /* An absolute path (a sidecar's) starts its names after the slash; a          */
    /* relative one (a manifest's) at the start.  An empty value, and "/" alone,   */
    /* then meet the empty-name test below at once.                                */
    pszComponent = pszPath;

    if (pszPath[0] == '/')
    {   /* Absolute: the first name follows the leading slash. */
        pszComponent = pszPath + 1;
    }

    for (;;)
    {   /* One name of the path per iteration. */
        cchComponent = strcspn(pszComponent, "/");

        if (cchComponent == 0 ||
            (cchComponent == 1 && pszComponent[0] == '.') ||
            (cchComponent == 2 && pszComponent[0] == '.' && pszComponent[1] == '.'))
        {   /* An empty name (an empty value, a doubled or trailing "/"), a ".",   */
            /* or a "..": not a path this tool writes.                             */
            return(false);
        }

        if (pszComponent[cchComponent] == '\0')
        {   /* That was the last name, and every name passed. */
            return(true);
        }

        /* Past the name and its "/", to the next one. */
        pszComponent += cchComponent + 1;
    }
}

/*-----------------------------------------------------------------------------*/
/* E4_MetaLoad:                                                                */
/*                                                                             */
/* Read a metadata manifest (`rdump -p`'s _ext4meta.txt) or a one-block        */
/* sidecar (`dump -p`'s <file>.ext4meta) into memory and cut it into records.  */
/* The format is the one E4_WriteMetaBlock writes: blocks of `key: value`      */
/* lines, each block opened by its `path:` line; a line starting `#` is a      */
/* comment and a blank line carries nothing.  A key this reader does not know  */
/* (`ctime`, `size`, `note`) is skipped, so a later writer may add lines.  A   */
/* value that does not parse as a number drops ONLY that value: the entry then */
/* takes the default for it, which is safer than refusing the whole import.  A */
/* `path:` this tool never writes (E4_MetaPathValid) refuses the WHOLE file:   */
/* -p is a restore from a file this tool wrote, and one holding such a path    */
/* was edited or made by something else.                                       */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszHostFile : the manifest or sidecar on the host.                      */
/*     pSet        : receives the loaded records (caller frees with            */
/*                   E4_MetaFree); left empty on failure.                      */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS when the file was read (even if it held no record);    */
/*           E4_FAILURE when it could not be opened, read or held in memory,   */
/*           or holds a record path this tool never writes (already reported). */
/*-----------------------------------------------------------------------------*/
static int E4_MetaLoad(const char *pszHostFile, struct E4_MetaSet *pSet)
{
    FILE              *pFile;
    long long          llSize;
    char              *pszLine;
    char              *pszNext;
    char              *pszValue;
    struct E4_MetaRec *pRec;
    unsigned long      ulVal;
    size_t             cbRead;
    int                nPaths;
    bool               bUid;
    bool               bGid;

    /* An empty set first, so every failure exit leaves one E4_MetaFree accepts. */
    pSet->pszText = NULL;
    pSet->pRecs   = NULL;
    pSet->nRecs   = 0;

    /* The file, through the long-path open like every host file; "rb" so the   */
    /* text is read byte for byte and a CRLF ending is seen (and cut) below.    */
    pFile = E4_HostFopen(pszHostFile, "rb");

    if (pFile == NULL)
    {   /* The manifest is missing or unreadable. */
        Log_Err("cannot open metadata file %s: %s", pszHostFile, strerror(errno));
        return(E4_FAILURE);
    }

    /* Measure it (64-bit calls, like every other host size here) and read the   */
    /* whole file into one buffer with room for a closing NUL.                   */
    llSize = -1;

    if (_fseeki64(pFile, 0, SEEK_END) == 0)
    {   /* At the end: the position is the length. */
        llSize = _ftelli64(pFile);
    }

    if (llSize < 0 || _fseeki64(pFile, 0, SEEK_SET) != 0)
    {   /* The file cannot be measured or rewound. */
        Log_Err("cannot read metadata file %s", pszHostFile);
        fclose(pFile);
        return(E4_FAILURE);
    }

    pSet->pszText = (char *)malloc((size_t)llSize + 1);

    if (pSet->pszText == NULL)
    {   /* Out of memory for the manifest's text. */
        Log_Err("out of memory reading metadata file %s", pszHostFile);
        fclose(pFile);
        return(E4_FAILURE);
    }

    cbRead = fread(pSet->pszText, 1, (size_t)llSize, pFile);
    fclose(pFile);

    if (cbRead != (size_t)llSize)
    {   /* A short read: do not parse half a manifest as though it were whole. */
        Log_Err("cannot read metadata file %s", pszHostFile);
        E4_MetaFree(pSet);
        return(E4_FAILURE);
    }

    /* The closing NUL, so the text can be walked as one string. */
    pSet->pszText[cbRead] = '\0';

    /* PASS 1: count the `path:` lines, which is the number of records, so the      */
    /* array is allocated once at its final size.  A line is a `path:` line only    */
    /* at the START of a line -- the text "path: " inside a value does not count.   */
    nPaths  = 0;
    pszLine = pSet->pszText;

    while (*pszLine != '\0')
    {   /* One line per iteration. */
        if (strncmp(pszLine, "path: ", 6) == 0)
        {   /* A block opens here. */
            nPaths++;
        }

        /* Step to the character after this line's '\n', or to the closing NUL. */
        pszNext = strchr(pszLine, '\n');

        if (pszNext == NULL)
        {   /* The last line has no line ending. */
            break;
        }

        pszLine = pszNext + 1;
    }

    if (nPaths == 0)
    {   /* No record at all: an empty manifest is legal, and everything then   */
        /* takes its defaults.                                                 */
        return(E4_SUCCESS);
    }

    /* The record array, one slot per `path:` line, zeroed so every "was it        */
    /* given" flag starts false and a block that omits a line takes the default.   */
    pSet->pRecs = (struct E4_MetaRec *)calloc((size_t)nPaths, sizeof(struct E4_MetaRec));

    if (pSet->pRecs == NULL)
    {   /* Out of memory for the records. */
        Log_Err("out of memory reading metadata file %s", pszHostFile);
        E4_MetaFree(pSet);
        return(E4_FAILURE);
    }

    /* PASS 2: cut the text into lines IN PLACE and fill the records.  pRec is     */
    /* the block being filled (NULL before the first `path:` line); bUid / bGid    */
    /* track the two halves of the owner, which counts only when both are there.   */
    pRec    = NULL;
    bUid    = false;
    bGid    = false;
    pszLine = pSet->pszText;

    while (pszLine != NULL && *pszLine != '\0')
    {   /* One line per iteration. */

        /* Find the end of this line and cut it there; remember where the next    */
        /* one starts BEFORE the cut destroys the '\n'.  A '\r' before the '\n'   */
        /* (a manifest that passed through a Windows editor) is cut off too.      */
        pszNext = strchr(pszLine, '\n');

        if (pszNext != NULL)
        {   /* Terminate the line and step past its ending. */
            *pszNext = '\0';

            if (pszNext > pszLine && pszNext[-1] == '\r')
            {   /* A CRLF ending: drop the CR as well. */
                pszNext[-1] = '\0';
            }

            pszNext++;
        }

        /* A line is `key: value`.  The value starts after the FIRST ": ", so a   */
        /* path or a link target that itself holds ": " is kept whole.            */
        pszValue = strstr(pszLine, ": ");

        if (pszLine[0] != '#' && pszValue != NULL)
        {   /* Not a comment, and it has a value: cut the key off and read it. */
            *pszValue = '\0';
            pszValue += 2;

            if (strcmp(pszLine, "path") == 0)
            {   /* A new block.  The count from pass 1 bounds the array: the same   */
                /* text is being walked, so nRecs cannot pass nPaths.               */
                if (!E4_MetaPathValid(pszValue))
                {   /* A path this tool never writes -- a ".." that could climb    */
                    /* out of the tree, a ".", an empty name.  The whole file is   */
                    /* refused (the header says why), before the image is open.    */
                    Log_Err("metadata file %s: a record path is not one this tool writes: %s",
                            pszHostFile, pszValue);
                    E4_MetaFree(pSet);
                    return(E4_FAILURE);
                }

                pRec          = &pSet->pRecs[pSet->nRecs];
                pRec->pszPath = pszValue;
                pRec->iType   = EXT4_DE_UNKNOWN;
                pSet->nRecs++;
                bUid = false;
                bGid = false;
            }
            else if (pRec != NULL)
            {   /* A line of the current block. */
                if (strcmp(pszLine, "type") == 0)
                {   /* The entry's kind. */
                    pRec->iType = E4_MetaTypeFromName(pszValue);
                }
                else if (strcmp(pszLine, "target") == 0)
                {   /* A symlink's stored text. */
                    pRec->pszTarget = pszValue;
                }
                else if (strcmp(pszLine, "mode") == 0)
                {   /* Permission bits, octal as written. */
                    if (E4_ParseUint(pszValue, 8, &ulVal))
                    {   /* A clean octal number. */
                        pRec->uiMode = (uint32_t)ulVal;
                        pRec->bMode  = true;
                    }
                }
                else if (strcmp(pszLine, "uid") == 0)
                {   /* The owner half of the pair. */
                    if (E4_ParseUint(pszValue, 10, &ulVal))
                    {   /* A clean number. */
                        pRec->uiUid = (uint32_t)ulVal;
                        bUid        = true;
                    }
                }
                else if (strcmp(pszLine, "gid") == 0)
                {   /* The group half of the pair. */
                    if (E4_ParseUint(pszValue, 10, &ulVal))
                    {   /* A clean number. */
                        pRec->uiGid = (uint32_t)ulVal;
                        bGid        = true;
                    }
                }
                else if (strcmp(pszLine, "mtime") == 0)
                {   /* Modify time, epoch seconds. */
                    if (E4_ParseUint(pszValue, 10, &ulVal))
                    {   /* A clean number. */
                        pRec->uiMtime = (uint32_t)ulVal;
                        pRec->bMtime  = true;
                    }
                }
                else if (strcmp(pszLine, "atime") == 0)
                {   /* Access time, epoch seconds. */
                    if (E4_ParseUint(pszValue, 10, &ulVal))
                    {   /* A clean number. */
                        pRec->uiAtime = (uint32_t)ulVal;
                        pRec->bAtime  = true;
                    }
                }

                /* The owner is applied as a pair (one chown call), so it is   */
                /* marked present only once both halves have been read.        */
                pRec->bOwner = (bUid && bGid);
            }
        }

        /* On to the next line (NULL after the last, which ends the loop). */
        pszLine = pszNext;
    }

    /* Sort by path for E4_MetaFind's binary search. */
    qsort(pSet->pRecs, (size_t)pSet->nRecs, sizeof(struct E4_MetaRec), E4_MetaCompare);

    /* The manifest is loaded. */
    return(E4_SUCCESS);
}

/*----------------------------------------------------------------------------*/
/* E4_MetaFind:                                                               */
/*                                                                            */
/* Find the manifest record for one entry by its path relative to the top of  */
/* the imported tree, and mark it used.                                       */
/*                                                                            */
/* Arguments:                                                                 */
/*     pSet   : the loaded manifest, or NULL when the import has none.        */
/*     pszRel : the entry's relative path, '/'-separated, as rdump wrote it.  */
/*                                                                            */
/* Returns:                                                                   */
/*     struct E4_MetaRec * : the record (now marked used), or NULL when there */
/*                           is no manifest or it has no record of that path. */
/*----------------------------------------------------------------------------*/
static struct E4_MetaRec *E4_MetaFind(struct E4_MetaSet *pSet, const char *pszRel)
{
    int iLow;
    int iHigh;
    int iMid;
    int iCmp;

    if (pSet == NULL)
    {   /* No manifest in this import: nothing to find. */
        return(NULL);
    }

    /* Binary search over the records E4_MetaLoad sorted by path.  [iLow, iHigh]   */
    /* is the range that can still hold the path.                                  */
    iLow  = 0;
    iHigh = pSet->nRecs - 1;

    while (iLow <= iHigh)
    {   /* One halving of the range per iteration. */
        iMid = iLow + (iHigh - iLow) / 2;
        iCmp = strcmp(pszRel, pSet->pRecs[iMid].pszPath);

        if (iCmp == 0)
        {   /* Found: mark it, so the end-of-import report can list what the   */
            /* manifest named and the host tree did not hold.                  */
            pSet->pRecs[iMid].bUsed = true;
            return(&pSet->pRecs[iMid]);
        }

        if (iCmp < 0)
        {   /* The path sorts before the middle record: keep the lower half. */
            iHigh = iMid - 1;
        }
        else
        {   /* It sorts after: keep the upper half. */
            iLow = iMid + 1;
        }
    }

    /* The manifest has no record of this path. */
    return(NULL);
}

/*-----------------------------------------------------------------------------*/
/* E4_ApplyImportMeta:                                                         */
/*                                                                             */
/* Stamp one freshly created image entry with its mode, owner and times.       */
/* Each value is taken from the FIRST source that has it, in this order:       */
/*                                                                             */
/*     1. the command line (--mode for files, --owner, --mtime);               */
/*     2. the entry's manifest record, when -p found one;                      */
/*     3. the default the caller passes in.                                    */
/*                                                                             */
/* The command line outranks the manifest because the operator typed it for    */
/* THIS run; the manifest records how the tree was, the option says how it     */
/* should be.  The access time follows the modify time unless a manifest       */
/* records one of its own.  The CHANGE time is always this run's moment: a     */
/* kernel sets it whenever an inode is written and lets nobody choose it, and  */
/* every entry this stamps was just created or just rewritten.                 */
/*                                                                             */
/* Arguments:                                                                  */
/*     pszImgPath : the entry inside the image (it must already exist).        */
/*     bIsDir     : true for an entry --mode must not reach -- a directory of  */
/*                  an imported tree (a file mode would make it untraversable) */
/*                  or a symlink (its mode is fixed).                          */
/*     pOpts      : the command line's metadata options.                       */
/*     pRec       : the entry's manifest record, or NULL.                      */
/*     uiDefMode  : mode when neither an option nor a record gives one.        */
/*     uiDefUid   : owner likewise.                                            */
/*     uiDefGid   : group likewise.                                            */
/*     uiDefMtime : modify time likewise (this run's moment, or the host       */
/*                  file's under --mtime host).                                */
/*                                                                             */
/* Returns:                                                                    */
/*     void : the entry is stamped.  The lwext4 return codes go unchecked: the */
/*            entry and its bytes are already in the image, and a failed       */
/*            stamp is not worth failing or undoing the import over.           */
/*-----------------------------------------------------------------------------*/
static void E4_ApplyImportMeta(const char *pszImgPath, bool bIsDir,
                               const struct E4_ImportOpts *pOpts,
                               const struct E4_MetaRec *pRec,
                               uint32_t uiDefMode, uint32_t uiDefUid,
                               uint32_t uiDefGid, uint32_t uiDefMtime)
{
    uint32_t uiMode;
    uint32_t uiUid;
    uint32_t uiGid;
    uint32_t uiMtime;

    /* Start from the defaults, then let each better source overwrite them. */
    uiMode  = uiDefMode;
    uiUid   = uiDefUid;
    uiGid   = uiDefGid;
    uiMtime = uiDefMtime;

    if (pRec != NULL)
    {   /* A manifest record: take whichever values it carries. */
        if (pRec->bMode)
        {   /* The recorded permission bits. */
            uiMode = pRec->uiMode;
        }

        if (pRec->bOwner)
        {   /* The recorded owner and group. */
            uiUid = pRec->uiUid;
            uiGid = pRec->uiGid;
        }

        if (pRec->bMtime)
        {   /* The recorded modify time. */
            uiMtime = pRec->uiMtime;
        }
    }

    if (pOpts->bMode && !bIsDir)
    {   /* --mode, files only (see the header). */
        uiMode = pOpts->uiMode;
    }

    if (pOpts->bOwner)
    {   /* --owner, for files and directories alike. */
        uiUid = pOpts->uiUid;
        uiGid = pOpts->uiGid;
    }

    if (pOpts->bMtime)
    {   /* --mtime, for files and directories alike. */
        uiMtime = pOpts->uiMtime;
    }

    /* Apply.  chmod keeps the inode's type bits and sets only the permissions. */
    ext4_chmod(pszImgPath, uiMode);
    ext4_chown(pszImgPath, uiUid, uiGid);
    ext4_file_set_mtime(pszImgPath, uiMtime);

    if (pRec != NULL && pRec->bAtime)
    {   /* A manifest knows the access time the entry had: restore it. */
        ext4_file_set_atime(pszImgPath, pRec->uiAtime);
    }
    else
    {   /* No record: the access time follows the modify time, so a new entry   */
        /* is not left with the 1970 access time lwext4 gives it.               */
        ext4_file_set_atime(pszImgPath, uiMtime);
    }

    /* The change time is this run's: the inode was just written (the header    */
    /* says why no source may choose it).  Set LAST, after the setters above,   */
    /* which do not touch it.                                                   */
    ext4_file_set_ctime(pszImgPath, E4_uiNow);
}

/*----------------------------------------------------------------------------*/
/* Cmd_Dump:                                                                  */
/*                                                                            */
/* Extract ONE file from the image to a named host file (read-only on the     */
/* image).  The source's type is learned from its PARENT directory entry, so  */
/* a symlink is recognised without being followed: a symlink's bytes are      */
/* skipped (it cannot be written to NTFS) and a directory is refused (rdump's */
/* job).  `-p` writes a `<hostfile>.ext4meta` sidecar with the Unix metadata; */
/* `-t` sets the host file's times from the inode.                            */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszImage    : the image file.                                          */
/*     pszImgFile  : the file inside the image to extract.                    */
/*     pszHostFile : the host file to write.                                  */
/*     bMeta       : true to write the metadata sidecar (-p).                 */
/*     bTimes      : true to set the host file's times from the inode (-t).   */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on success, E4_FAILURE otherwise.                     */
/*----------------------------------------------------------------------------*/
static int Cmd_Dump(const char *pszImage, const char *pszImgFile,
                    const char *pszHostFile, bool bMeta, bool bTimes)
{
    struct ext4_inode   inode;
    struct ext4_sblock *sb;
    char                szDir[E4_MAX_PATH];
    char                szMeta[E4_MAX_PATH];
    const char         *pszBase;
    FILE               *pMeta;
    uint32_t            uiIno;
    int                 iType;
    int                 iFind;
    int                 iRc;

    /* Refuse a host destination whose final component Windows cannot create        */
    /* as an ordinary file -- a reserved device name (con), an illegal character,   */
    /* or a trailing dot / space it would silently strip.  Checked BEFORE the       */
    /* mount so a bad name costs nothing and no device is written to.               */
    if (!E4_HostNameValid(pszHostFile))
    {   /* The host destination name is unsafe on Windows. */
        Log_Err("refusing Windows-illegal host destination name: %s", pszHostFile);
        return(E4_FAILURE);
    }

    if (E4_Open(pszImage, true) != E4_SUCCESS)
    {   /* Could not mount the image (already reported). */
        return(E4_FAILURE);
    }

    /* Confirm the source exists and learn its type from the PARENT directory     */
    /* entry, so a symlink is recognised without being followed.  A parent that   */
    /* cannot be read fails the command: nothing can be said about the name.      */
    iType = EXT4_DE_UNKNOWN;
    iFind = E4_FIND_ABSENT;

    if (E4_SplitParent(pszImgFile, szDir, sizeof(szDir), &pszBase) == E4_SUCCESS)
    {   /* The parent is known: ask it for the entry. */
        iFind = E4_FindInDir(szDir, pszBase, &iType);
    }

    if (iFind == E4_FIND_ERROR)
    {   /* The parent could not be read (already reported). */
        E4_Close(true);
        return(E4_FAILURE);
    }

    if (iFind == E4_FIND_ABSENT)
    {   /* Nothing at that path in the image (or the path could not be split). */
        Log_Err("not found in image: %s", pszImgFile);
        E4_Close(true);
        return(E4_FAILURE);
    }

    if (iType == EXT4_DE_DIR)
    {   /* dump extracts a single file; a tree is rdump's job. */
        Log_Err("%s is a directory -- use rdump for a tree", pszImgFile);
        E4_Close(true);
        return(E4_FAILURE);
    }

    /* Act on the ONE entry by type: dump handles a single file, so a symlink is     */
    /* skipped (no NTFS representation) yet still recorded under -p, and a regular   */
    /* file's bytes are extracted verbatim.  A directory was already refused above   */
    /* -- extracting a tree is rdump's job.  iRc starts SUCCESS and is flipped       */
    /* only by a sidecar failure below; the byte extract returns early on error.     */
    iRc = E4_SUCCESS;

    if (iType == EXT4_DE_SYMLINK)
    {   /* A symlink cannot be written to NTFS, so the bytes are skipped; -p   */
        /* still records it (type + target) so the extract loses nothing.      */
        Log_Err("skipping symlink (not represented on Windows): %s", pszImgFile);
    }
    else
    {   /* A regular file (or other non-dir inode): extract its bytes verbatim. */
        if (E4_ExtractFile(pszImgFile, pszHostFile) != E4_SUCCESS)
        {   /* The extraction failed (already reported). */
            E4_Close(true);
            return(E4_FAILURE);
        }

        /* The bytes are out: say so, naming both ends. */
        printf("dumped %s -> %s\n", pszImgFile, pszHostFile);
    }

    if (bMeta)
    {   /* -p: write the one-block sidecar "<hostfile>.ext4meta" beside it.         */
        /* Build the sidecar name "<hostfile>.ext4meta".  The fit is tested FIRST   */
        /* and a name that does not fit is refused: _snprintf truncates silently,   */
        /* and a truncated name would write the sidecar beside some OTHER file.     */
        /* An empty szMeta marks the refusal for the branch below.                  */
        szMeta[0] = '\0';

        if (strlen(pszHostFile) + sizeof(".ext4meta") <= sizeof(szMeta))
        {   /* It fits (sizeof the literal counts its NUL): compose it. */
            _snprintf(szMeta, sizeof(szMeta), "%s.ext4meta", pszHostFile);
            szMeta[sizeof(szMeta) - 1] = '\0';
        }

        /* Clear the superblock pointer; ext4_get_sblock fills it on success. */
        sb = NULL;

        if (szMeta[0] == '\0')
        {   /* The host path leaves no room for the sidecar's suffix. */
            Log_Err("host path too long for a sidecar: %s", pszHostFile);
            iRc = E4_FAILURE;
        }
        else if (ext4_get_sblock(E4_MP, &sb) != 0 || sb == NULL)
        {   /* Without the superblock the size / mode accessors cannot run. */
            Log_Err("cannot read superblock for sidecar");
            iRc = E4_FAILURE;
        }
        else
        {   /* Open the sidecar and write the single block for this path. */
            pMeta = E4_HostFopen(szMeta, "wb");

            if (pMeta == NULL)
            {   /* The sidecar could not be created. */
                Log_Err("cannot create sidecar %s: %s", szMeta, strerror(errno));
                iRc = E4_FAILURE;
            }
            else
            {   /* The sidecar's path: line is the in-image path (dump form). */
                E4_WriteMetaBlock(pMeta, pszImgFile, iType, pszImgFile, sb, false);
                fclose(pMeta);
                printf("wrote metadata sidecar : %s\n", szMeta);
            }
        }
    }

    /* -t stamps the extracted host file from the inode's times.  The              */
    /* `iType != EXT4_DE_SYMLINK` guard matters: a symlink produced no host file   */
    /* to stamp (its bytes were skipped above) and -p already recorded its         */
    /* target, so there is nothing here to timestamp.                              */
    if (bTimes && iType != EXT4_DE_SYMLINK)
    {   /* -t: stamp the host file with the inode's access / modify times. */
        uiIno = 0;
        memset(&inode, 0, sizeof(inode));

        if (ext4_fill_raw_inode(pszImgFile, &uiIno, &inode) == 0)
        {   /* Apply the inode's times to the extracted host file. */
            E4_SetHostTime(pszHostFile,
                           ext4_inode_get_access_time(&inode),
                           ext4_inode_get_modif_time(&inode));
        }
    }

    /* Everything done (the extract, the optional sidecar, the optional times);   */
    /* unmount the image.                                                         */
    E4_Close(true);

    /* iRc is SUCCESS unless the extract or the sidecar failed. */
    return(iRc);
}

/*------------------------------------------------------------------------------*/
/* E4_RdumpTree:                                                                */
/*                                                                              */
/* Extract an image directory TREE to the host, every level down, WITHOUT       */
/* recursion: the walk keeps an explicit stack of E4_ExtractFrame on the heap,  */
/* one frame per directory being worked, so a tree of any depth the image can   */
/* hold costs memory, never the thread's stack (a recursive walk's limit is the */
/* stack reserve divided by a frame the compiler sizes, and it fails as a       */
/* crash).  A directory's entries are COLLECTED and its handle closed when its  */
/* frame is pushed, so one ext4_dir is open at a time; then each entry is       */
/* acted on: a file is extracted, a directory is created on the host, recorded, */
/* and pushed to be walked, a symlink is skipped with a warning (it cannot be   */
/* written to NTFS), any other type is skipped.  With pMeta set, every entry    */
/* gets a block in the manifest (relative path), a directory's before its       */
/* children's; with bTimes, each extracted file is stamped from its inode, and  */
/* each directory at its pop -- after its children -- so its own writes do not  */
/* bump the time back.  A failed entry is counted and flagged and the walk goes */
/* on: a partial extract of everything readable beats aborting the whole tree   */
/* on one bad entry.  The image is only READ here.                              */
/*                                                                              */
/* Arguments:                                                                   */
/*     pszImgDir  : the image directory at the top of the tree.                 */
/*     pszHostDir : the matching host directory (already created).              */
/*     pszRel     : the top's path relative to the rdump root ("" from          */
/*                  Cmd_Rdump), used for the manifest's path: lines.            */
/*     pMeta      : the open manifest, or NULL when -p was not given.           */
/*     bTimes     : true to set host times from the inodes (-t).                */
/*     sb         : the mounted superblock (for the metadata accessors).        */
/*     pnDirs / pnFiles / pnSym / pnSkip : running counters.                    */
/*                                                                              */
/* Returns:                                                                     */
/*     int : E4_SUCCESS when the whole tree copied, E4_FAILURE if any entry     */
/*           failed, the top could not be listed, or a frame could not be       */
/*           allocated (the copy still continues over what it can).             */
/*------------------------------------------------------------------------------*/
static int E4_RdumpTree(const char *pszImgDir, const char *pszHostDir,
                        const char *pszRel, FILE *pMeta, bool bTimes,
                        struct ext4_sblock *sb, int *pnDirs, int *pnFiles,
                        int *pnSym, int *pnSkip)
{
    struct E4_ExtractFrame **ppStack;
    struct E4_ExtractFrame **ppGrown;
    struct E4_ExtractFrame  *pTop;
    struct E4_ExtractFrame  *pNew;
    struct E4_DirEnt        *pEnt;
    struct ext4_inode        inode;
    char                     szImg[E4_MAX_PATH];
    char                     szHost[E4_MAX_PATH];
    char                     szRel[E4_MAX_PATH];
    uint32_t                 uiIno;
    int                      nDepth;
    int                      nCap;
    int                      iRc;

    /* The top's three paths must fit a frame's buffers whole.  The copies below    */
    /* would truncate a longer one silently, and the walk would then run under a   */
    /* shortened name -- a host path that long names some other folder, found or   */
    /* not -- so an over-long top path is refused here with nothing extracted, as  */
    /* E4_JoinPath refuses every child's.  Tested first, before anything is taken.  */
    if (strlen(pszImgDir) >= E4_MAX_PATH || strlen(pszHostDir) >= E4_MAX_PATH ||
        strlen(pszRel) >= E4_MAX_PATH)
    {   /* A path of E4_MAX_PATH bytes or more: nothing to walk. */
        Log_Err("path too long (%d bytes or more), nothing extracted: %s -> %s",
                E4_MAX_PATH, pszImgDir, pszHostDir);
        return(E4_FAILURE);
    }

    /* The stack: an array of frame POINTERS, grown by doubling, so a frame         */
    /* never moves while the walk holds it.  The three path buffers above are the   */
    /* ONE set the walk composes a child's paths in; a child directory's frame      */
    /* takes its own copy of them when it is pushed.                                */
    nCap    = 64;
    nDepth  = 0;
    iRc     = E4_SUCCESS;
    ppStack = (struct E4_ExtractFrame **)malloc((size_t)nCap * sizeof(*ppStack));

    if (ppStack == NULL)
    {   /* Out of memory for the stack itself: nothing was extracted. */
        Log_Err("out of memory extracting %s", pszImgDir);
        return(E4_FAILURE);
    }

    /* The top directory's frame: its three paths as given, its entries collected   */
    /* now.  Its host directory is the caller's and is not stamped at the pop; a    */
    /* failure to list it ends the walk before it begins.  The three copies cannot  */
    /* truncate: the lengths were tested above.                                     */
    pNew = (struct E4_ExtractFrame *)calloc(1, sizeof(*pNew));

    if (pNew == NULL)
    {   /* Out of memory for the top's frame. */
        Log_Err("out of memory extracting %s", pszImgDir);
        free(ppStack);
        return(E4_FAILURE);
    }

    _snprintf(pNew->szImg, sizeof(pNew->szImg), "%s", pszImgDir);
    _snprintf(pNew->szHost, sizeof(pNew->szHost), "%s", pszHostDir);
    _snprintf(pNew->szRel, sizeof(pNew->szRel), "%s", pszRel);
    pNew->szImg[sizeof(pNew->szImg) - 1]   = '\0';
    pNew->szHost[sizeof(pNew->szHost) - 1] = '\0';
    pNew->szRel[sizeof(pNew->szRel) - 1]   = '\0';
    pNew->bStamp = false;
    pNew->iRc    = E4_SUCCESS;

    if (E4_CollectDir(pNew->szImg, &pNew->pEnts, &pNew->nEnts) != E4_SUCCESS)
    {   /* The top could not be listed (already reported): nothing to walk. */
        free(pNew);
        free(ppStack);
        return(E4_FAILURE);
    }

    /* The walk starts with the top on the stack. */
    ppStack[0] = pNew;
    nDepth     = 1;

    while (nDepth > 0)
    {   /* One entry of the top frame per iteration, or that frame's pop when   */
        /* its entries are done; the loop ends when the top directory pops.     */
        pTop = ppStack[nDepth - 1];

        if (pTop->iNext >= pTop->nEnts)
        {   /* Every entry of this directory is handled: pop it.  Its host      */
            /* directory is stamped HERE, after its children are written:       */
            /* writing an entry into a directory bumps that directory's own     */
            /* mtime, so a stamp before the children would be overwritten and   */
            /* leave a time newer than the inode's -- the symptom being a       */
            /* restored tree whose directory timestamps are all "now".          */
            if (pTop->bStamp)
            {   /* -t, on a directory below the top. */
                uiIno = 0;
                memset(&inode, 0, sizeof(inode));

                /* Apply the times only if the directory's inode could be read; a   */
                /* failure here is cosmetic (see E4_SetHostTime).                   */
                if (ext4_fill_raw_inode(pTop->szImg, &uiIno, &inode) == 0)
                {   /* Apply the image directory's times to the host directory. */
                    E4_SetHostTime(pTop->szHost,
                                   ext4_inode_get_access_time(&inode),
                                   ext4_inode_get_modif_time(&inode));
                }
            }

            /* The frame's result goes to its parent, or to the whole walk at the   */
            /* top: a failed child does not stop the walk, it only marks its        */
            /* parent failed (the partial-extract policy).                          */
            if (pTop->iRc != E4_SUCCESS)
            {   /* Something under this directory failed. */
                if (nDepth > 1)
                {   /* The parent carries it. */
                    ppStack[nDepth - 2]->iRc = E4_FAILURE;
                }
                else
                {   /* The top: the walk's own result. */
                    iRc = E4_FAILURE;
                }
            }

            /* Free the level's collected entries (free(NULL) is a no-op for an    */
            /* empty directory) and the frame, and go back to the parent, so the   */
            /* tree never holds more than one entry array per level in hand.       */
            free(pTop->pEnts);
            free(pTop);
            nDepth--;
            continue;
        }

        /* The next entry of this directory, taken now so a `continue` below   */
        /* never revisits it.                                                  */
        pEnt = &pTop->pEnts[pTop->iNext];
        pTop->iNext++;

        /* Build the three parallel paths; a path that would overflow the          */
        /* E4_MAX_PATH buffer is skipped rather than truncated (this also bounds   */
        /* any accidental cycle).                                                  */
        if (E4_JoinPath(szImg, sizeof(szImg), pTop->szImg, pEnt->szName, '/') != E4_SUCCESS ||
            E4_JoinPath(szHost, sizeof(szHost), pTop->szHost, pEnt->szName, '/') != E4_SUCCESS ||
            E4_JoinPath(szRel, sizeof(szRel), pTop->szRel, pEnt->szName, '/') != E4_SUCCESS)
        {   /* Path too long: skip this entry and flag the run. */
            Log_Err("path too long, skipping: %s/%s", pTop->szImg, pEnt->szName);
            (*pnSkip)++;
            pTop->iRc = E4_FAILURE;
            continue;
        }

        /* An image entry whose name Windows cannot create as a host file (a        */
        /* reserved device name, an illegal character, a trailing dot / space) is   */
        /* skipped rather than extracted to a surprising or device name.  This is   */
        /* the partial-extract policy -- the rest of the tree still comes out.      */
        /* The test sits AFTER the paths are built, not before, because the entry   */
        /* is still RECORDED under -p like every other skipped kind: the manifest   */
        /* is the inventory of what the tree held, and an entry left out of it      */
        /* for its name would be lost without trace.  A skipped directory is        */
        /* recorded as itself only; its children are not walked.                    */
        if (!E4_HostNameValid(pEnt->szName))
        {   /* Unsafe host name: skip this entry, count it, flag the run. */
            Log_Err("skipping entry with Windows-illegal host name: %s", szImg);
            (*pnSkip)++;
            pTop->iRc = E4_FAILURE;

            if (pMeta != NULL && (strchr(pEnt->szName, '\n') != NULL || strchr(pEnt->szName, '\r') != NULL))
            {   /* A name holding a line break cannot be carried by the manifest,    */
                /* whose `path:` value is one line: written as it is, the block      */
                /* would break at the line break and a later rwrite -p would read    */
                /* the part before it as another entry's name.  So it is left out,   */
                /* and said so, rather than recorded wrong.                          */
                Log_Err("not recorded in the manifest (its name holds a line break): %s", szImg);
            }
            else if (pMeta != NULL)
            {   /* Record its presence anyway, for a complete manifest. */
                E4_WriteMetaBlock(pMeta, szRel, pEnt->iType, szImg, sb, true);
            }

            continue;
        }

        if (pEnt->iType == EXT4_DE_DIR)
        {   /* A subdirectory.  The sequence is deliberate: create it on the host,    */
            /* record it in the manifest BEFORE its children, push it to be walked,   */
            /* and stamp its time at its pop, LAST.  Recording the parent ahead of    */
            /* its children is what lets a later rwrite replay the manifest           */
            /* top-down, every directory existing before anything is imported into    */
            /* it.                                                                    */

            /* Create the host directory.  An EEXIST is TOLERATED, not failed:      */
            /* Cmd_Rdump already created the top-level host directory, and a        */
            /* re-run (or a --force extract into a populated tree) can meet one     */
            /* that already exists -- that is a merge, not an error.  Any OTHER     */
            /* errno (missing parent, permission, disk full) skips this entry and   */
            /* flags the run, and the walk still continues over the rest.           */
            if (E4_HostMkdir(szHost) != 0 && errno != EEXIST)
            {   /* The host subdirectory could not be created. */
                Log_Err("cannot create host directory %s: %s", szHost, strerror(errno));
                (*pnSkip)++;
                pTop->iRc = E4_FAILURE;
                continue;
            }

            /* Count this directory toward the whole-tree total. */
            (*pnDirs)++;

            if (pMeta != NULL)
            {   /* Record the directory BEFORE descending, so the manifest lists   */
                /* a parent ahead of its children.                                 */
                E4_WriteMetaBlock(pMeta, szRel, EXT4_DE_DIR, szImg, sb, true);
            }

            /* The subdirectory's frame: its paths copied, its entries collected     */
            /* now.  A frame that cannot be allocated, or a directory that cannot    */
            /* be listed, counts as a failed entry -- stamped all the same when -t   */
            /* asks, since the host directory exists -- and the walk goes on.        */
            pNew = (struct E4_ExtractFrame *)calloc(1, sizeof(*pNew));

            if (pNew == NULL)
            {   /* Out of memory for the frame. */
                Log_Err("out of memory extracting %s", szImg);
                pTop->iRc = E4_FAILURE;
                continue;
            }

            memcpy(pNew->szImg, szImg, sizeof(szImg));
            memcpy(pNew->szHost, szHost, sizeof(szHost));
            memcpy(pNew->szRel, szRel, sizeof(szRel));
            pNew->bStamp = bTimes;
            pNew->iRc    = E4_SUCCESS;

            if (E4_CollectDir(pNew->szImg, &pNew->pEnts, &pNew->nEnts) != E4_SUCCESS)
            {   /* The subdirectory could not be listed (already reported): the   */
                /* frame is dropped and the entry counts as failed.               */
                pTop->iRc = E4_FAILURE;

                if (bTimes)
                {   /* Stamp it now: there will be no pop for it.  Only if its   */
                    /* inode could be read; a failure here is cosmetic.          */
                    uiIno = 0;
                    memset(&inode, 0, sizeof(inode));

                    if (ext4_fill_raw_inode(szImg, &uiIno, &inode) == 0)
                    {   /* Apply the image directory's times to the host directory. */
                        E4_SetHostTime(szHost,
                                       ext4_inode_get_access_time(&inode),
                                       ext4_inode_get_modif_time(&inode));
                    }
                }

                free(pNew);
                continue;
            }

            if (nDepth == nCap)
            {   /* The pointer array is full: double it (the frames themselves do   */
                /* not move).  A failed growth drops this subdirectory, frame and   */
                /* entries, as a failed entry, and the walk goes on.                */
                ppGrown = (struct E4_ExtractFrame **)realloc(ppStack, (size_t)nCap * 2 * sizeof(*ppStack));

                if (ppGrown == NULL)
                {   /* Out of memory for a deeper stack. */
                    Log_Err("out of memory extracting %s", szImg);
                    free(pNew->pEnts);
                    free(pNew);
                    pTop->iRc = E4_FAILURE;
                    continue;
                }

                ppStack = ppGrown;
                nCap    = nCap * 2;
            }

            /* Push: the next iteration works the subdirectory's entries. */
            ppStack[nDepth] = pNew;
            nDepth++;
        }
        else if (pEnt->iType == EXT4_DE_REG_FILE)
        {   /* A regular file: extract its bytes, then record and (optionally)       */
            /* stamp it.  A file is a leaf, so there is no frame and no post-order   */
            /* timestamp concern.                                                    */

            /* Extract first.  A failure is already reported, so skip-count this   */
            /* entry, flag the run, and move on (partial-extract policy).          */
            if (E4_ExtractFile(szImg, szHost) != E4_SUCCESS)
            {   /* The file failed (already reported); flag the run, continue. */
                (*pnSkip)++;
                pTop->iRc = E4_FAILURE;
                continue;
            }

            /* Count this file toward the whole-tree total. */
            (*pnFiles)++;

            if (pMeta != NULL)
            {   /* Record the file's Unix metadata (mode / uid / gid / times) so a   */
                /* later rwrite could restore what NTFS cannot hold.                 */
                E4_WriteMetaBlock(pMeta, szRel, EXT4_DE_REG_FILE, szImg, sb, true);
            }

            if (bTimes)
            {   /* Stamp the extracted host file from the inode's times. */
                uiIno = 0;
                memset(&inode, 0, sizeof(inode));

                /* Apply the times only if the file's inode could be read. */
                if (ext4_fill_raw_inode(szImg, &uiIno, &inode) == 0)
                {   /* Apply the image file's times to the host file. */
                    E4_SetHostTime(szHost,
                                   ext4_inode_get_access_time(&inode),
                                   ext4_inode_get_modif_time(&inode));
                }
            }
        }
        else if (pEnt->iType == EXT4_DE_SYMLINK)
        {   /* A symlink: it has no byte-faithful NTFS representation (a real        */
            /* Windows symlink needs privilege and would change its meaning), so     */
            /* its bytes are skipped with a warning -- but it is still RECORDED in   */
            /* the manifest (type + target), so -p loses nothing of what it was.     */
            Log_Err("skipping symlink (not represented on Windows): %s", szImg);

            /* Count it against the dedicated symlink tally, not the generic one. */
            (*pnSym)++;

            if (pMeta != NULL)
            {   /* Record it so the extract still loses nothing (type + target). */
                E4_WriteMetaBlock(pMeta, szRel, EXT4_DE_SYMLINK, szImg, sb, true);
            }
        }
        else
        {   /* A device node, FIFO or socket: there is nothing to extract to a     */
            /* host file, so skip it with a warning.  It is still recorded under   */
            /* -p so the manifest stays a complete inventory of the tree.          */
            Log_Err("skipping %s (%s): not a file, directory or symlink",
                    szImg, E4_TypeName(pEnt->iType));

            /* Count it among the generic skips. */
            (*pnSkip)++;

            if (pMeta != NULL)
            {   /* Record its presence anyway, for a complete manifest. */
                E4_WriteMetaBlock(pMeta, szRel, pEnt->iType, szImg, sb, true);
            }
        }
    }

    /* Every frame was popped and freed as its directory finished; only the   */
    /* pointer array is left.                                                 */
    free(ppStack);

    /* iRc is SUCCESS unless some entry failed above. */
    return(iRc);
}

/*----------------------------------------------------------------------------*/
/* Cmd_Rdump:                                                                 */
/*                                                                            */
/* Recursively extract an image directory TREE to a host directory (read-only */
/* on the image -- it has no --write).  The host destination is created if    */
/* absent; if it already exists it must be an empty directory unless --force  */
/* is given, so an extract never silently mixes with unrelated files.  `-p`   */
/* writes ONE manifest `<hostdir>/_ext4meta.txt` (a block per entry, relative */
/* paths); `-t` stamps each extracted file and directory from its inode.      */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszImage   : the image file.                                           */
/*     pszImgDir  : the directory inside the image to extract.                */
/*     pszHostDir : the host directory to extract into.                       */
/*     bMeta      : true to write the manifest (-p).                          */
/*     bTimes     : true to set host times from the inodes (-t).              */
/*     bForce     : true to extract into a non-empty host directory.          */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on a complete extract, E4_FAILURE otherwise.          */
/*----------------------------------------------------------------------------*/
static int Cmd_Rdump(const char *pszImage, const char *pszImgDir,
                     const char *pszHostDir, bool bMeta, bool bTimes, bool bForce)
{
    struct ext4_sblock *sb;
    struct _stat64      st;
    char                szMeta[E4_MAX_PATH];
    FILE               *pMeta;
    int                 nDirs;
    int                 nFiles;
    int                 nSym;
    int                 nSkip;
    int                 iRc;

    if (E4_Open(pszImage, true) != E4_SUCCESS)
    {   /* Could not mount the image (already reported). */
        return(E4_FAILURE);
    }

    /* Refuse a top-level host destination whose final component Windows       */
    /* cannot create as an ordinary file (a reserved device name, an illegal   */
    /* character, a trailing dot / space); per-entry names are checked in      */
    /* E4_RdumpTree as the tree is walked.                                     */
    if (!E4_HostNameValid(pszHostDir))
    {   /* The top-level host destination name is unsafe on Windows. */
        Log_Err("refusing Windows-illegal host destination name: %s", pszHostDir);
        E4_Close(true);
        return(E4_FAILURE);
    }

    /* Prepare the host destination: create it if absent, else require an empty   */
    /* directory unless --force.                                                  */
    if (E4_HostStat(pszHostDir, &st) == 0)
    {   /* The host path exists already. */
        if ((st.st_mode & S_IFMT) != S_IFDIR)
        {   /* A file is in the way of the destination directory. */
            Log_Err("host path %s exists and is not a directory", pszHostDir);
            E4_Close(true);
            return(E4_FAILURE);
        }

        if (!bForce && !E4_HostDirEmpty(pszHostDir))
        {   /* Refuse a non-empty destination without --force, so an extract never   */
            /* silently MIXES with unrelated files already there -- which would      */
            /* also poison a later rwrite of this directory back into an image, as   */
            /* that would re-import whatever had been mingled in.                    */
            Log_Err("host directory %s is not empty (use --force to extract into it)",
                    pszHostDir);
            E4_Close(true);
            return(E4_FAILURE);
        }
    }
    else
    {   /* The host directory does not exist: create it fresh.  A just-created   */
        /* directory is empty, so the --force / non-empty question above does    */
        /* not arise on this path.                                               */
        if (E4_HostMkdir(pszHostDir) != 0)
        {   /* The destination could not be created. */
            Log_Err("cannot create host directory %s: %s", pszHostDir, strerror(errno));
            E4_Close(true);
            return(E4_FAILURE);
        }
    }

    /* No manifest and no superblock unless -p asks for them just below.  Both     */
    /* stay NULL otherwise and are passed so to E4_RdumpTree, which then records   */
    /* nothing.                                                                    */
    pMeta = NULL;
    sb    = NULL;

    if (bMeta)
    {   /* -p: one manifest at the top of the host directory, and the superblock   */
        /* the metadata accessors need.                                            */
        if (E4_JoinPath(szMeta, sizeof(szMeta), pszHostDir, "_ext4meta.txt", '/') != E4_SUCCESS)
        {   /* The host path leaves no room for the manifest name. */
            Log_Err("host path too long for a manifest: %s", pszHostDir);
            E4_Close(true);
            return(E4_FAILURE);
        }

        if (ext4_get_sblock(E4_MP, &sb) != 0 || sb == NULL)
        {   /* The metadata blocks need the superblock. */
            Log_Err("cannot read superblock for manifest");
            E4_Close(true);
            return(E4_FAILURE);
        }

        pMeta = E4_HostFopen(szMeta, "wb");
        if (pMeta == NULL)
        {   /* The manifest could not be created. */
            Log_Err("cannot create manifest %s: %s", szMeta, strerror(errno));
            E4_Close(true);
            return(E4_FAILURE);
        }

        /* A human-readable banner heads the manifest; the per-entry blocks   */
        /* E4_WriteMetaBlock appends follow it.                               */
        fprintf(pMeta, "# ext4edit rdump metadata manifest -- one block per entry\n");
    }

    /* Whole-tree tallies that the walk threads through and the summary   */
    /* line prints at the end.                                            */
    nDirs  = 0;
    nFiles = 0;
    nSym   = 0;
    nSkip  = 0;

    /* Announce the extract before walking, so the output frames what follows. */
    printf("rdump %s -> %s\n", pszImgDir, pszHostDir);

    /* Walk the tree from the root (relative path "" at the top). */
    iRc = E4_RdumpTree(pszImgDir, pszHostDir, "", pMeta, bTimes, sb,
                       &nDirs, &nFiles, &nSym, &nSkip);

    if (pMeta != NULL)
    {   /* Close the finished manifest and name it for the operator. */
        fclose(pMeta);
        printf("wrote metadata manifest : %s\n", szMeta);
    }

    /* The whole tree is walked; unmount (rdump never writes the image). */
    E4_Close(true);

    /* Summary: the four tallies, so the operator sees at a glance how much came   */
    /* out and how much was skipped (symlinks and special files).                  */
    printf("%d dir(s), %d file(s) extracted; %d symlink(s) skipped, %d other skipped.\n",
           nDirs, nFiles, nSym, nSkip);

    /* iRc reflects whether every entry copied cleanly. */
    return(iRc);
}

/*------------------------------------------------------------------------------*/
/* E4_RwriteTree:                                                               */
/*                                                                              */
/* Import a HOST path -- a file, or a directory and everything under it -- into */
/* the image (dry run unless bWrite), WITHOUT recursion: the walk keeps an      */
/* explicit stack of E4_ImportFrame on the heap, one frame per host directory   */
/* being walked, so a tree of any depth costs memory, never the thread's stack  */
/* (a recursive walk's limit is the stack reserve divided by a frame the        */
/* compiler sizes, and it fails as a crash).  A host directory becomes an image */
/* directory (0755, uid=gid=0, this run's moment) whose children are imported   */
/* in turn; a host file becomes an image file (uiFileMode, uid=gid=0, this      */
/* run's moment, or the host's under --mtime host).  A host entry's kind is     */
/* read with stat, so a Windows symbolic link is followed to its target like    */
/* any other path -- the reparse point is deliberately NOT detected or          */
/* declined, since following it imports the target's bytes; an accidental       */
/* directory cycle is bounded by the E4_MAX_PATH guard in E4_JoinPath.          */
/* Anything that is neither a file nor a directory is skipped with a warning.   */
/* A failed entry is counted and flagged and the walk goes on: a partial import */
/* of everything readable beats aborting the whole tree on one bad entry.       */
/*                                                                              */
/* Arguments:                                                                   */
/*     pszHostPath : the host file / directory to import.                       */
/*     pszImgPath  : the destination path inside the image.                     */
/*     pszRel      : the top's path relative to the top of the import ("" from  */
/*                   Cmd_Rwrite), '/'-separated -- the key a manifest record is */
/*                   found by.                                                  */
/*     bWrite      : true to apply, false for a dry run.                        */
/*     pOpts       : the command line's --mode / --owner / --mtime.             */
/*     pMeta       : the loaded manifest (-p), or NULL for none.  With one,     */
/*                   each entry takes its recorded mode / owner / times, and    */
/*                   the manifest file itself is not imported.                  */
/*     pnDirs / pnFiles / pnSkip : running counters.                            */
/*                                                                              */
/* Returns:                                                                     */
/*     int : E4_SUCCESS when everything imported, E4_FAILURE if any part        */
/*           failed, a directory could not be read, or a frame could not be     */
/*           allocated (the import still continues over what it can).           */
/*------------------------------------------------------------------------------*/
static int E4_RwriteTree(const char *pszHostPath, const char *pszImgPath,
                         const char *pszRel, bool bWrite,
                         const struct E4_ImportOpts *pOpts,
                         struct E4_MetaSet *pMeta,
                         int *pnDirs, int *pnFiles, int *pnSkip)
{
    struct E4_ImportFrame **ppStack;
    struct E4_ImportFrame **ppGrown;
    struct E4_ImportFrame  *pTop;
    struct E4_ImportFrame  *pNew;
    struct _stat64          st;
    struct E4_MetaRec      *pRec;
    char                    szChildName[E4_MAX_NAME];
    char                    szHost[E4_MAX_PATH];
    char                    szImg[E4_MAX_PATH];
    char                    szRel[E4_MAX_PATH];
    uint32_t                uiMtime;
    uint32_t                uiShowMode;
    bool                    bPending;
    int                     nDepth;
    int                     nCap;
    int                     rc;
    int                     iRc;
    int                     iRead;

    /* The top's three paths must fit the path buffers whole.  The copies below     */
    /* would truncate a longer one silently, and the walk would then run under a    */
    /* shortened name -- a host path that long names some other folder, found or    */
    /* not -- so an over-long top path is refused here with nothing imported, as    */
    /* E4_JoinPath refuses every child's.  Tested first, before anything is taken.   */
    if (strlen(pszHostPath) >= E4_MAX_PATH || strlen(pszImgPath) >= E4_MAX_PATH ||
        strlen(pszRel) >= E4_MAX_PATH)
    {   /* A path of E4_MAX_PATH bytes or more: nothing to walk. */
        Log_Err("path too long (%d bytes or more), nothing imported: %s -> %s",
                E4_MAX_PATH, pszHostPath, pszImgPath);
        (*pnSkip)++;
        return(E4_FAILURE);
    }

    /* The stack: an array of frame POINTERS, grown by doubling, so a frame          */
    /* never moves while the walk holds it.  The three path buffers above are the    */
    /* ONE set an entry's paths are composed in; a directory's frame takes its own   */
    /* copy of them when it is pushed.                                               */
    nCap    = 64;
    nDepth  = 0;
    iRc     = E4_SUCCESS;
    ppStack = (struct E4_ImportFrame **)malloc((size_t)nCap * sizeof(*ppStack));

    if (ppStack == NULL)
    {   /* Out of memory for the stack itself: nothing was imported. */
        Log_Err("out of memory importing %s", pszHostPath);
        (*pnSkip)++;
        return(E4_FAILURE);
    }

    /* The walk works one PENDING entry at a time -- its host, image and relative    */
    /* paths in the three buffers -- and the first pending entry is the top itself   */
    /* as given (the three copies cannot truncate: the lengths were tested above).   */
    /* After it, each pending entry is the next child read from the directory frame  */
    /* on top of the stack.                                                          */
    _snprintf(szHost, sizeof(szHost), "%s", pszHostPath);
    _snprintf(szImg, sizeof(szImg), "%s", pszImgPath);
    _snprintf(szRel, sizeof(szRel), "%s", pszRel);
    szHost[sizeof(szHost) - 1] = '\0';
    szImg[sizeof(szImg) - 1]   = '\0';
    szRel[sizeof(szRel) - 1]   = '\0';
    bPending = true;

    for (;;)
    {   /* One pending entry per iteration, or -- with none pending -- one child   */
        /* read from the top frame, or that frame's pop when its children are      */
        /* done.  The loop ends when nothing is pending and no frame is left.      */

        if (!bPending)
        {   /* Nothing pending: the next child of the top directory, or its pop. */

            if (nDepth == 0)
            {   /* No directory is being walked and nothing is pending: done. */
                break;
            }

            pTop  = ppStack[nDepth - 1];
            iRead = E4_HostDirRead(&pTop->hd, szChildName, sizeof(szChildName));

            if (iRead == E4_HD_ERROR)
            {   /* The listing failed part-way: the children seen so far are NOT     */
                /* the whole directory, so the directory is marked failed and says   */
                /* so, and is finished as if its end had come.                       */
                Log_Err("error reading host directory %s; its import is incomplete", pTop->szHost);
                pTop->iRc = E4_FAILURE;
                iRead     = E4_HD_END;
            }

            if (iRead == E4_HD_END)
            {   /* End of the host directory: every child has been handled.  Pop     */
                /* it: release its handle, stamp it LAST -- after its children are   */
                /* in, so nothing done while filling it can disturb the times just   */
                /* set -- and hand its result to its parent (or to the whole walk    */
                /* at the top): a failed child does not stop the walk, it only       */
                /* marks its parent failed (the partial-import policy).              */
                E4_HostDirClose(&pTop->hd);

                if (bWrite)
                {   /* The default mode is 0755 and --mode does NOT apply             */
                    /* (E4_ApplyImportMeta): a directory must stay traversable, and   */
                    /* a file mode such as 0600 would make it impossible to descend   */
                    /* into.  A manifest record, when -p found one, supplies the      */
                    /* directory's real mode, owner and times.                        */
                    pRec = E4_MetaFind(pMeta, pTop->szRel);
                    E4_ApplyImportMeta(pTop->szImg, true, pOpts, pRec,
                                       (uint32_t)E4_IMPORT_DIR_MODE, 0, 0, pTop->uiMtime);
                }

                if (pTop->iRc != E4_SUCCESS)
                {   /* Something under this directory failed. */
                    if (nDepth > 1)
                    {   /* The parent carries it. */
                        ppStack[nDepth - 2]->iRc = E4_FAILURE;
                    }
                    else
                    {   /* The top: the walk's own result. */
                        iRc = E4_FAILURE;
                    }
                }

                /* The frame is finished with. */
                free(pTop);
                nDepth--;
                continue;
            }

            if (iRead == E4_HD_BADNAME)
            {   /* A child exists whose name cannot be carried (not valid UTF-16,   */
                /* or too long for a path here).  Count it and go on -- never       */
                /* skip it without a word.                                          */
                Log_Err("skipping a child of %s whose name cannot be represented", pTop->szHost);
                (*pnSkip)++;
                pTop->iRc = E4_FAILURE;
                continue;
            }

            if (strcmp(szChildName, ".") == 0 || strcmp(szChildName, "..") == 0)
            {   /* The iterator yields the self and parent links like readdir;     */
                /* walking either would re-import this directory or climb out of   */
                /* the subtree (and spin), so both are skipped.                    */
                continue;
            }

            if (pMeta != NULL && pTop->szRel[0] == '\0' &&
                strcmp(szChildName, E4_MANIFEST_NAME) == 0)
            {   /* The manifest itself, at the top of a tree being restored FROM   */
                /* it: it describes the tree and is not part of it, so it is not   */
                /* imported.  (Without -p it is an ordinary file and goes in.)     */
                continue;
            }

            /* Compose the child's parallel host, image and relative paths.  A       */
            /* path that would overflow E4_MAX_PATH is skipped, not truncated -- a   */
            /* truncated path would import to the WRONG place -- and that same       */
            /* length cap is what bounds an accidental directory cycle on the host   */
            /* side.                                                                 */
            if (E4_JoinPath(szHost, sizeof(szHost), pTop->szHost, szChildName, '/') != E4_SUCCESS ||
                E4_JoinPath(szImg, sizeof(szImg), pTop->szImg, szChildName, '/') != E4_SUCCESS ||
                E4_JoinPath(szRel, sizeof(szRel), pTop->szRel, szChildName, '/') != E4_SUCCESS)
            {   /* Path too long: skip this one child, count it, flag the run. */
                Log_Err("path too long, skipping: %s/%s", pTop->szHost, szChildName);
                (*pnSkip)++;
                pTop->iRc = E4_FAILURE;
                continue;
            }

            /* The child is the pending entry: the rest of this iteration imports   */
            /* it by its kind.                                                      */
            bPending = true;
        }

        /* The pending entry is taken: whatever happens to it below, the next        */
        /* iteration reads the next child.  Its failures are charged to the          */
        /* directory being walked, or to the whole walk when it is the top itself.   */
        bPending = false;

        /* stat (which FOLLOWS a symlink -- the reparse point is deliberately   */
        /* not detected or declined; see the header) decides the kind.  Only    */
        /* the mtime is carried across: Windows has no Unix uid / gid / mode,   */
        /* so every imported entry is given root-owned 0:0 and a default mode   */
        /* below (files uiFileMode, dirs 0755) -- the one-way cross-boundary    */
        /* mapping that rdump's `-p` sidecar exists to record the other way.    */
        if (E4_HostStat(szHost, &st) != 0)
        {   /* The host entry could not be examined; it is not imported. */
            Log_Err("cannot stat host path %s: %s", szHost, strerror(errno));
            (*pnSkip)++;

            if (nDepth > 0)
            {   /* A child: its directory is marked failed. */
                ppStack[nDepth - 1]->iRc = E4_FAILURE;
            }
            else
            {   /* The top itself: the walk's own result. */
                iRc = E4_FAILURE;
            }

            continue;
        }

        /* The time the entry will get: this run's moment, as a plain cp dates    */
        /* a copy -- or, under --mtime host, the host entry's own modify time     */
        /* (the cp -p form), captured now while the stat result is in hand.       */
        /* Nothing else of the host entry is carried: uid / gid / mode are        */
        /* assigned defaults below, since Windows has none.  Decided here, once   */
        /* per entry, so a directory (stamped at its pop) and a file stamp the    */
        /* same way.                                                              */
        uiMtime = E4_uiNow;

        if (pOpts->bMtimeHost)
        {   /* The host entry's own modify time, asked for. */
            uiMtime = (uint32_t)st.st_mtime;
        }

        if ((st.st_mode & S_IFMT) == S_IFDIR)
        {   /* A directory: create its image counterpart, open its host walk and   */
            /* push its frame; its children are the next pending entries, and it   */
            /* is stamped when it pops.                                            */

            if (!bWrite)
            {   /* Dry run: state the directory that WOULD be created and its fixed   */
                /* 0755 mode, and create nothing -- but still walk its children       */
                /* below, so the preview covers the whole subtree, not just its top   */
                /* level.  It is counted here, so the preview's tally matches what    */
                /* a clean real run reports.                                          */
                printf("  would mkdir : %s  (0%03o)\n", szImg, (unsigned)E4_IMPORT_DIR_MODE);
                (*pnDirs)++;
            }
            else
            {   /* Real write: create the image directory now; its import metadata   */
                /* comes at the pop.                                                 */
                rc = ext4_dir_mk(szImg);

                if (rc != 0 && rc != EEXIST)
                {   /* A real failure -- parent missing, the name taken by a         */
                    /* non-dir, or a write error -- drops this subtree.  An          */
                    /* EEXIST is NOT a failure and is deliberately allowed to        */
                    /* fall through: importing a host subtree ONTO an image tree     */
                    /* that already has that directory is a MERGE, so the            */
                    /* existing directory is reused and this entry's children        */
                    /* are imported into it rather than the whole import aborting.   */
                    Log_Err("mkdir %s failed: %s", szImg, strerror(rc));
                    (*pnSkip)++;

                    if (nDepth > 0)
                    {   /* A child: its directory is marked failed. */
                        ppStack[nDepth - 1]->iRc = E4_FAILURE;
                    }
                    else
                    {   /* The top itself. */
                        iRc = E4_FAILURE;
                    }

                    continue;
                }

                if (rc == 0)
                {   /* Brand new (not merged into): settle its block checksum      */
                    /* now, so a directory that stays empty is as clean as one     */
                    /* that gains children (E4_NewDirHeal says why).  A failure    */
                    /* is a write error on the image and drops this subtree like   */
                    /* a failed mkdir.                                             */
                    rc = E4_NewDirHeal(szImg);

                    if (rc != 0)
                    {   /* The new directory's block could not be rewritten. */
                        Log_Err("mkdir %s: created, but its block could not be settled: %s", szImg, strerror(rc));
                        (*pnSkip)++;

                        if (nDepth > 0)
                        {   /* A child: its directory is marked failed. */
                            ppStack[nDepth - 1]->iRc = E4_FAILURE;
                        }
                        else
                        {   /* The top itself. */
                            iRc = E4_FAILURE;
                        }

                        continue;
                    }
                }

                /* The directory exists in the image now (made, or merged into):    */
                /* count it.  Counting only here, after the fact, is what keeps     */
                /* the summary from reporting a directory that failed as one that   */
                /* was imported.                                                    */
                (*pnDirs)++;

                /* Report the created (or merged-into) directory.  Its mode, owner   */
                /* and times are stamped AFTER its children are imported, at its     */
                /* pop.                                                              */
                printf("  mkdir : %s\n", szImg);
            }

            /* The directory's frame: its paths copied, its stamp time kept, its    */
            /* host walk opened through the long-path iterator.  This runs for a    */
            /* dry run too, so the preview descends the whole tree.  A frame that   */
            /* cannot be allocated, or a directory that cannot be read, drops the   */
            /* subtree as a failed entry and the walk goes on.                      */
            pNew = (struct E4_ImportFrame *)calloc(1, sizeof(*pNew));

            if (pNew == NULL)
            {   /* Out of memory for the frame. */
                Log_Err("out of memory importing %s", szHost);
            }
            else if (E4_HostDirOpen(&pNew->hd, szHost) != E4_SUCCESS)
            {   /* The host directory could not be opened for reading. */
                Log_Err("cannot read host directory %s: %s", szHost, strerror(errno));
                free(pNew);
                pNew = NULL;
            }

            if (pNew != NULL && nDepth == nCap)
            {   /* The pointer array is full: double it (the frames themselves do    */
                /* not move).  A failed growth drops this subdirectory as a failed   */
                /* entry, its walk closed again.                                     */
                ppGrown = (struct E4_ImportFrame **)realloc(ppStack, (size_t)nCap * 2 * sizeof(*ppStack));

                if (ppGrown == NULL)
                {   /* Out of memory for a deeper stack. */
                    Log_Err("out of memory importing %s", szHost);
                    E4_HostDirClose(&pNew->hd);
                    free(pNew);
                    pNew = NULL;
                }
                else
                {   /* Room for more frames. */
                    ppStack = ppGrown;
                    nCap    = nCap * 2;
                }
            }

            if (pNew == NULL)
            {   /* The subtree is dropped (already reported): its directory, or   */
                /* the walk at the top, is marked failed.                         */
                if (nDepth > 0)
                {   /* A child: its directory is marked failed. */
                    ppStack[nDepth - 1]->iRc = E4_FAILURE;
                }
                else
                {   /* The top itself. */
                    iRc = E4_FAILURE;
                }

                continue;
            }

            memcpy(pNew->szHost, szHost, sizeof(szHost));
            memcpy(pNew->szImg, szImg, sizeof(szImg));
            memcpy(pNew->szRel, szRel, sizeof(szRel));
            pNew->uiMtime = uiMtime;
            pNew->iRc     = E4_SUCCESS;

            /* Push: the next iterations read this directory's children. */
            ppStack[nDepth] = pNew;
            nDepth++;
            continue;
        }

        if ((st.st_mode & S_IFMT) == S_IFREG)
        {   /* A regular file: copy its bytes into the image, then stamp the Unix   */
            /* metadata.  A file is a leaf: no frame.                               */

            if (!bWrite)
            {   /* Dry run: show the source byte count (st.st_size is the HOST      */
                /* file's length, already measured by the stat above, so no         */
                /* second probe is needed; it is 64-bit and printed as such, so     */
                /* a file of 2 GiB or more shows its true size) and the mode the    */
                /* real run would set, then touch nothing.  It is counted so the    */
                /* preview's tally matches what a clean real run reports.  The      */
                /* mode shown follows the real run's own precedence (the one        */
                /* E4_ApplyImportMeta holds): --mode first, then the file's         */
                /* manifest record under -p, then the default -- so the preview     */
                /* names the mode the real run would set, not merely the default.   */
                uiShowMode = (uint32_t)E4_IMPORT_FILE_MODE;
                pRec       = E4_MetaFind(pMeta, szRel);

                if (pRec != NULL && pRec->bMode)
                {   /* The manifest recorded this file's mode: the real run   */
                    /* restores it.                                           */
                    uiShowMode = pRec->uiMode;
                }

                if (pOpts->bMode)
                {   /* --mode was given: it outranks the record. */
                    uiShowMode = pOpts->uiMode;
                }

                printf("  would write : %s  (%lld bytes, mode 0%03o)\n",
                       szImg, (long long)st.st_size, (unsigned)uiShowMode);
                (*pnFiles)++;
                continue;
            }

            /* Move the BYTES first.  E4_ImportFile creates (or truncates) the     */
            /* image file and streams the host file into it; the metadata stamps   */
            /* below can only act on an inode that already exists, so this         */
            /* ordering is load-bearing, not incidental.  A failure here is        */
            /* already reported -- there is nothing to stamp on a file that did    */
            /* not get written.                                                    */
            if (E4_ImportFile(szHost, szImg) != E4_SUCCESS)
            {   /* The byte copy failed (already reported, and the partial file is    */
                /* gone).  It goes in the skip tally, NOT the imported one, and its   */
                /* directory (or the walk, for a lone file) is marked failed.         */
                (*pnSkip)++;

                if (nDepth > 0)
                {   /* A child: its directory is marked failed. */
                    ppStack[nDepth - 1]->iRc = E4_FAILURE;
                }
                else
                {   /* The top itself. */
                    iRc = E4_FAILURE;
                }

                continue;
            }

            /* The file is in the image, whole: count it as imported.  Counting    */
            /* only after the copy is what keeps a failed file out of "N file(s)   */
            /* imported".                                                          */
            (*pnFiles)++;

            /* Apply the cross-boundary metadata.  Windows carries no Unix uid /    */
            /* gid / mode, so by default the imported file gets 0644, owner         */
            /* root:root (0:0) and the time decided above.  A manifest record       */
            /* found under -p replaces those with what the file had when rdump -p   */
            /* extracted it, and --mode / --owner / --mtime outrank both            */
            /* (E4_ApplyImportMeta holds the order, and says why its return codes   */
            /* go unchecked).                                                       */
            pRec = E4_MetaFind(pMeta, szRel);
            E4_ApplyImportMeta(szImg, false, pOpts, pRec,
                               (uint32_t)E4_IMPORT_FILE_MODE, 0, 0, uiMtime);

            /* Report the imported file. */
            printf("  wrote : %s\n", szImg);
            continue;
        }

        /* Neither a regular file nor a directory.  stat FOLLOWS symlinks, so a   */
        /* host symlink has already been resolved to whatever it points at and    */
        /* never lands here; what reaches this point is a device node, FIFO,      */
        /* socket, or an entry stat could not classify.  None has a meaningful    */
        /* image representation this tool creates, so it is skipped with a        */
        /* warning rather than guessed at.  A skipped special file is not a       */
        /* failure of the import -- the rest of the tree is unaffected -- so      */
        /* nothing is marked failed.                                              */
        Log_Err("skipping host path (not a regular file or directory): %s", szHost);

        /* Count the skip for the summary. */
        (*pnSkip)++;
    }

    /* Every frame was popped and freed as its directory finished; only the   */
    /* pointer array is left.                                                 */
    free(ppStack);

    /* iRc is SUCCESS unless some entry failed above. */
    return(iRc);
}

/*----------------------------------------------------------------------------*/
/* Cmd_Rwrite:                                                                */
/*                                                                            */
/* Recursively IMPORT a host file or tree INTO the image (dry run unless      */
/* bWrite) -- the inverse of rdump, and the copy engine a later dual-pane GUI */
/* will share.  Imported files get uiFileMode (0644, or --mode); imported     */
/* directories get 0755; everything gets uid=gid=0 and this run's moment (or  */
/* the host mtime under --mtime host).  Every directory it creates has its    */
/* block checksum settled at once (E4_NewDirHeal), so an imported tree is     */
/* e2fsck-clean, empty directories included.                                  */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszImage    : the image file.                                          */
/*     pszHostPath : the host file / tree to import.                          */
/*     pszImgPath  : the destination path inside the image.                   */
/*     bWrite      : true to apply, false for a dry run.                      */
/*     pOpts       : the command line's --mode / --owner / --mtime.           */
/*     bMeta       : true to restore from the manifest rdump -p wrote (-p):   */
/*                   `<hostdir>\_ext4meta.txt` for a tree, or the sidecar     */
/*                   `<hostfile>.ext4meta` for a single file.  Each entry     */
/*                   then gets its recorded mode / owner / times, and the     */
/*                   symbolic links the manifest recorded are created again.  */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS on success, E4_FAILURE otherwise.                     */
/*----------------------------------------------------------------------------*/
static int Cmd_Rwrite(const char *pszImage, const char *pszHostPath,
                      const char *pszImgPath, bool bWrite,
                      const struct E4_ImportOpts *pOpts, bool bMeta)
{
    struct _stat64     st;
    struct E4_MetaSet  meta;
    struct E4_MetaSet *pMeta;
    char               szMeta[E4_MAX_PATH];
    char               szLink[E4_MAX_PATH];
    char               szDir[E4_MAX_PATH];
    const char        *pszBase;
    int                iType;
    bool               bExisted;
    int                nDirs;
    int                nFiles;
    int                nSkip;
    int                nLinks;
    int                nAbsent;
    int                iRc;
    int                rc;
    int                i;

    /* Check the host source exists before mounting, so a bad source path fails   */
    /* before the image is touched.                                               */
    if (E4_HostStat(pszHostPath, &st) != 0)
    {   /* The host source is missing or unreadable. */
        Log_Err("cannot stat host source %s: %s", pszHostPath, strerror(errno));
        return(E4_FAILURE);
    }

    /* No manifest unless -p asks for one; pMeta stays NULL then and the walk   */
    /* gives every entry its defaults.                                          */
    meta.pszText = NULL;
    meta.pRecs   = NULL;
    meta.nRecs   = 0;
    pMeta        = NULL;

    if (bMeta)
    {   /* -p: find and load the metadata file BEFORE the mount, so a missing    */
        /* one fails the command with the image untouched.  -p is a request to   */
        /* restore; quietly importing with defaults instead would look like a    */
        /* restore and not be one.                                               */
        if ((st.st_mode & S_IFMT) == S_IFDIR)
        {   /* A tree: the manifest sits at its top. */
            if (E4_JoinPath(szMeta, sizeof(szMeta), pszHostPath, E4_MANIFEST_NAME, '/') != E4_SUCCESS)
            {   /* The host path leaves no room for the manifest's name. */
                Log_Err("host path too long for a manifest: %s", pszHostPath);
                return(E4_FAILURE);
            }
        }
        else
        {   /* A single file: its sidecar sits beside it. */
            if (strlen(pszHostPath) + sizeof(E4_SIDECAR_SUFFIX) > sizeof(szMeta))
            {   /* The host path leaves no room for the sidecar's suffix. */
                Log_Err("host path too long for a sidecar: %s", pszHostPath);
                return(E4_FAILURE);
            }

            _snprintf(szMeta, sizeof(szMeta), "%s" E4_SIDECAR_SUFFIX, pszHostPath);
            szMeta[sizeof(szMeta) - 1] = '\0';
        }

        if (E4_MetaLoad(szMeta, &meta) != E4_SUCCESS)
        {   /* No usable metadata file (already reported). */
            return(E4_FAILURE);
        }

        if ((st.st_mode & S_IFMT) != S_IFDIR && meta.nRecs > 0)
        {   /* A sidecar's one block names the path the file had in the image it   */
            /* came from; the walk looks a single file up by the EMPTY relative    */
            /* path (it is the top of the import), so the record is re-keyed to    */
            /* that and any further blocks are ignored.                            */
            meta.pRecs[0].pszPath = "";
            meta.nRecs            = 1;
        }

        pMeta = &meta;
    }

    if (E4_Open(pszImage, !bWrite) != E4_SUCCESS)
    {   /* Mount failed (already reported). */
        return(E4_FAILURE);
    }

    if (bWrite)
    {   /* Announce a modifying run. */
        printf("rwrite (WRITE) %s -> %s:\n", pszHostPath, pszImgPath);
    }
    else
    {   /* Announce a dry run. */
        printf("rwrite (dry run -- add --write to apply) %s -> %s:\n",
               pszHostPath, pszImgPath);
    }

    /* Whole-tree tallies the walk fills in and the summary line prints. */
    nDirs  = 0;
    nFiles = 0;
    nSkip  = 0;

    /* Whether the destination's top entry is already there, learned BEFORE the   */
    /* walk makes it: only a NEW entry changes the directory that holds it, so    */
    /* this decides the parent's stamp below.  A merge onto an existing           */
    /* directory, or an overwrite of an existing file, leaves the parent alone.   */
    iType    = EXT4_DE_UNKNOWN;
    bExisted = false;

    if (bWrite &&
        E4_SplitParent(pszImgPath, szDir, sizeof(szDir), &pszBase) == E4_SUCCESS &&
        E4_FindInDir(szDir, pszBase, &iType) == E4_FIND_FOUND)
    {   /* Already there: the parent will not gain an entry. */
        bExisted = true;
    }

    /* Import (or, on a dry run, preview) the whole host tree, every level.  This   */
    /* is the shared copy engine the later dual-pane GUI is meant to drive, so it   */
    /* is kept self-contained and counter-driven rather than printing its own       */
    /* summary.                                                                     */
    iRc = E4_RwriteTree(pszHostPath, pszImgPath, "", bWrite, pOpts, pMeta,
                        &nDirs, &nFiles, &nSkip);

    if (bWrite && !bExisted)
    {   /* The top entry is new: the directory that holds it gained an entry,    */
        /* so its times are set as a kernel would.  The directories INSIDE the   */
        /* imported tree need nothing here: each is stamped after its children   */
        /* by the walk, which is the same moment or the recorded one.            */
        E4_TouchParent(pszImgPath);
    }

    /* What the manifest named and the host tree did not hold.  Every record an    */
    /* imported entry took was marked used by the walk; the rest are the entries   */
    /* rdump could not write to Windows.  A SYMBOLIC LINK among them is created    */
    /* again here from its recorded target -- that is what makes an extract and    */
    /* a re-import come back as the tree was.  Anything else (a device node, a     */
    /* fifo, a socket, an entry whose name Windows cannot hold) cannot be          */
    /* rebuilt from the manifest alone and is listed, so the operator knows what   */
    /* the image now lacks.  The records are sorted by path, so a link's parent    */
    /* directory has always been dealt with before the link.                       */
    nLinks  = 0;
    nAbsent = 0;

    if (pMeta != NULL && bWrite)
    {   /* A real run with a manifest: sweep the unused records. */
        for (i = 0; i < pMeta->nRecs; i++)
        {   /* One manifest record per iteration. */
            if (pMeta->pRecs[i].bUsed)
            {   /* An imported entry took this record; nothing more to do. */
                continue;
            }

            if (pMeta->pRecs[i].iType != EXT4_DE_SYMLINK || pMeta->pRecs[i].pszTarget == NULL)
            {   /* Not a link with a target: it cannot be rebuilt.  Say so. */
                Log_Err("in the manifest but not on the host, not restored: %s",
                        pMeta->pRecs[i].pszPath);
                nAbsent++;
                continue;
            }

            if (pMeta->pRecs[i].pszPath[0] == '/')
            {   /* An absolute path: a sidecar's form, naming a place in the image   */
                /* the file came FROM, not a place in this tree -- it cannot be      */
                /* joined to the destination.  (A sidecar's one record is re-keyed   */
                /* to "" above and taken by the file itself; this is a tree          */
                /* manifest that holds one, which this tool never writes.)           */
                Log_Err("in the manifest with an absolute path, not restored: %s",
                        pMeta->pRecs[i].pszPath);
                nAbsent++;
                continue;
            }

            if (E4_JoinPath(szLink, sizeof(szLink), pszImgPath, pMeta->pRecs[i].pszPath, '/') != E4_SUCCESS)
            {   /* The link's image path does not fit. */
                Log_Err("path too long, symlink not restored: %s", pMeta->pRecs[i].pszPath);
                nAbsent++;
                iRc = E4_FAILURE;
                continue;
            }

            /* ext4_fsymlink(TARGET, PATH): the target text is stored verbatim. */
            rc = ext4_fsymlink(pMeta->pRecs[i].pszTarget, szLink);

            if (rc != 0)
            {   /* The name is taken, the parent is missing, or a write error. */
                Log_Err("symlink %s -> %s not restored: %s",
                        szLink, pMeta->pRecs[i].pszTarget, strerror(rc));
                nAbsent++;
                iRc = E4_FAILURE;
                continue;
            }

            /* Stamp the link as Cmd_Symlink stamps one: mode 0777 as Linux keeps   */
            /* every symlink (--mode left aside, as for a directory), owner and     */
            /* times from --owner / --mtime or the defaults -- a symlink record     */
            /* holds only its path, type and target, so the record itself gives     */
            /* nothing here.  The directory that holds the link is NOT touched:     */
            /* the walk stamped it from the manifest's record, and a restore is     */
            /* meant to reproduce the recorded tree, not to date the directory      */
            /* by the restore's own moment.                                         */
            E4_ApplyImportMeta(szLink, true, pOpts, &pMeta->pRecs[i], 0777, 0, 0, E4_uiNow);

            printf("  symlink : %s -> %s\n", szLink, pMeta->pRecs[i].pszTarget);
            nLinks++;
        }

        /* The restore's own tally, apart from the import's. */
        printf("%d symlink(s) restored from the manifest; %d manifest entr%s not restored.\n",
               nLinks, nAbsent, (nAbsent == 1) ? "y" : "ies");
    }

    /* The manifest's records are no longer needed. */
    E4_MetaFree(&meta);

    /* Flush (only when this was a write) and unmount, matching the mount mode. */
    E4_Close(!bWrite);

    /* Summary: directories and files imported (or "to import" on a dry run),       */
    /* plus the count of everything that did NOT go in -- special files,            */
    /* over-long paths, and files or directories whose import failed.  The two      */
    /* tallies never overlap: an entry is counted as imported only once it is in.   */
    printf("%d dir(s), %d file(s) %s; %d skipped or failed.\n",
           nDirs, nFiles, bWrite ? "imported" : "to import", nSkip);

    if (!bWrite && iRc == E4_SUCCESS)
    {   /* Nudge toward the real run after a clean dry run. */
        printf("(dry run -- add --write to apply; back up the image first)\n");
    }

    /* iRc reflects whether every entry imported / previewed cleanly. */
    return(iRc);
}

/*------------------------------------------------------------------------------*/
/* Util_Usage:                                                                  */
/*                                                                              */
/* Print the one usage / help block to stderr (not stdout, so the help still    */
/* reads while a cat / dump is redirected to a file).  Shown on --help, on a    */
/* run with too few arguments, and on an unknown command.  Every command and    */
/* option it documents, broken out one per line:                                */
/*                                                                              */
/*   READ-ONLY on the image (dump / rdump also write host files):               */
/*     ls <dir>                 list one directory's entries.                   */
/*     cat <path> [--text]      stream a file to stdout (byte-exact).           */
/*       --text                 translate LF to CRLF for a Windows editor.      */
/*     stat <path>              print one path's inode facts.                   */
/*     readlink <path>          print a symlink's target, without following.    */
/*     dump <imgfile> <hostfile>  extract ONE file to the host.                 */
/*       -p                     also write a <hostfile>.ext4meta sidecar.       */
/*       -t / --time            set the host file's times from the inode.       */
/*     rdump <imgdir> <hostdir>   recursively extract a TREE to the host.       */
/*       --force                extract into a non-empty host directory.        */
/*       -p                     write one _ext4meta.txt manifest for the tree.  */
/*       -t / --time            set host times from the inodes.                 */
/*                                                                              */
/*   EDITING (a DRY RUN unless --write is given):                               */
/*     rm <path>                remove one file or symlink.                     */
/*     rmdir <dir>              remove an EMPTY directory only.                 */
/*     rm-tree <dir>            remove a directory and everything under it.     */
/*     mkdir <dir>              create one directory level (parent must exist). */
/*     write <path> <hostfile>  copy a host file INTO the image at <path>.      */
/*     rwrite <hostpath> <imgpath>  recursively IMPORT a host file / tree.      */
/*     symlink <target> <link>  create a symbolic link holding <target>.        */
/*       (the four creating commands take:)                                     */
/*       -p                     restore from the dump -p / rdump -p metadata    */
/*                              (write and rwrite).                             */
/*       --mode <octal>         permission bits for imported files and a        */
/*                              mkdir'd directory (else 0644 / 0755).           */
/*       --owner <uid>:<gid>    numeric owner and group (else 0:0).             */
/*       --mtime <seconds|now|host>  modify time: a number, this run's moment   */
/*                              (the default), or the host file's own.          */
/*     ln <target> <link>       create a hard link to an existing inode.        */
/*     mv <src> <dst>           rename / move an entry within the image.        */
/*     chmod <octal> <path>     set an entry's permission bits.                 */
/*     chown <uid> <gid> <path> set an entry's numeric owner / group.           */
/*                                                                              */
/*   GLOBAL:                                                                    */
/*     --write                  apply an editing command (otherwise a dry run). */
/*     --help                   print this block and exit.                      */
/*     --version                print the version and exit.                     */
/*                                                                              */
/* Arguments:                                                                   */
/*     None.                                                                    */
/*                                                                              */
/* Returns:                                                                     */
/*     void : writes the usage block to stderr.                                 */
/*------------------------------------------------------------------------------*/
static void Util_Usage(void)
{
    /* One fprintf of the whole block, to stderr for the reason the header gives   */
    /* -- the help survives stdout being redirected to an image file.  The         */
    /* trailing notes are part of the help on purpose: they warn that a POSIX-     */
    /* style shell rewrites a leading-slash image path, and that a real write      */
    /* wants a backup and an fsck first.                                           */
    fprintf(stderr,
        "ext4edit " EXT4EDIT_VERSION " -- list and edit files in an ext2/3/4 image\n"
        "\n"
        "Usage:\n"
        "  ext4edit <image> <command> [args] [--write]\n"
        "  ext4edit --help | --version\n"
        "\n"
        "  <image> : an ext2/3/4 image FILE (e.g. a partition image carved from a dump).\n"
        "\n"
        "Read-only on the image (dump / rdump also create host files):\n"
        "  ls <dir>             list a directory.\n"
        "  cat <path> [--text]  write a file's bytes to stdout (--text: LF -> CRLF).\n"
        "  stat <path>          show inode facts (type, mode, size, uid/gid, times).\n"
        "  readlink <path>      print a symlink's target.\n"
        "  dump <imgfile> <hostfile> [-p] [-t]\n"
        "                       extract ONE file to the host (-p: .ext4meta metadata\n"
        "                       sidecar; -t: set the host file's times; a symlink is\n"
        "                       skipped, recorded under -p).\n"
        "  rdump <imgdir> <hostdir> [--force] [-p] [-t]\n"
        "                       recursively extract a TREE to the host (--force: allow\n"
        "                       a non-empty host dir; -p: one _ext4meta.txt manifest;\n"
        "                       -t: set host times; symlinks skipped, recorded under -p).\n"
        "\n"
        "Editing commands (DRY RUN unless --write is given):\n"
        "  rm <path>                 remove one file / link.\n"
        "  rmdir <dir>               remove an EMPTY directory (refuses non-empty).\n"
        "  rm-tree <dir>             remove a directory and everything under it.\n"
        "  mkdir <dir>               create a directory (the parent must exist): 0755,\n"
        "                            uid=gid=0, dated now.\n"
        "  write <path> <hostfile>   copy a host file INTO the image at <path> (a new\n"
        "                            file: 0644, uid=gid=0, dated now; an existing\n"
        "                            one keeps its mode and owner).\n"
        "  rwrite <hostpath> <imgpath>\n"
        "                            recursively IMPORT a host file / tree INTO the\n"
        "                            image (files 0644, dirs 0755, uid=gid=0, dated\n"
        "                            now; host symlinks are followed).\n"
        "  symlink <target> <link>   create a symbolic link (0777, uid=gid=0, dated now).\n"
        "  options of the four creating commands above:\n"
        "    -p                      restore mode / owner / times (and, for rwrite,\n"
        "                            symlinks) from what dump -p / rdump -p recorded\n"
        "                            (write and rwrite).\n"
        "    --mode <octal>          permission bits for imported files, or for the\n"
        "                            directory mkdir makes.\n"
        "    --owner <uid>:<gid>     numeric owner and group.\n"
        "    --mtime <seconds|now|host>\n"
        "                            modify time: UTC epoch seconds; now (the\n"
        "                            default); or host, keeping the host file's own.\n"
        "  ln <target> <link>        create a hard link.\n"
        "  mv <src> <dst>            rename / move an entry.\n"
        "  chmod <octal> <path>      change permission bits (e.g. 0644).\n"
        "  chown <uid> <gid> <path>  change owner / group (numeric ids).\n"
        "\n"
        "Paths inside the image are absolute (start with /), with no . or ..\n"
        "component; a trailing / is dropped.  Run from cmd.exe or PowerShell: a\n"
        "POSIX-style shell rewrites a leading-slash path before the program sees\n"
        "it.  Times are kept as a kernel keeps them: a change sets the\n"
        "inode's change time, and an entry made, removed or renamed sets its\n"
        "directory's times.\n"
        "ALWAYS image the disk first (dd4win) and fsck the result afterward.\n");
}

/*----------------------------------------------------------------------------*/
/* Arg_HasFlag:                                                               */
/*                                                                            */
/* Return true if a given flag token (e.g. "--write", "--text", "-p", "-t",   */
/* "--force") appears anywhere in the argument vector.  Flags are             */
/* position-independent.                                                      */
/*                                                                            */
/* Arguments:                                                                 */
/*     argc    : argument count.                                              */
/*     argv    : argument vector.                                             */
/*     pszFlag : the flag token to look for.                                  */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true when the flag is present, false otherwise.                 */
/*----------------------------------------------------------------------------*/
static bool Arg_HasFlag(int argc, char **argv, const char *pszFlag)
{
    int i;

    for (i = 1; i < argc; i++)
    {   /* One argument per iteration. */
        if (strcmp(argv[i], pszFlag) == 0)
        {   /* The flag is present. */
            return(true);
        }
    }

    /* The flag was not present in the argument vector. */
    return(false);
}

/*----------------------------------------------------------------------------*/
/* Arg_GetOpt:                                                                */
/*                                                                            */
/* Find an option that takes a VALUE (for example "--mode 0644") and hand     */
/* back the following argument.  Like Arg_HasFlag the search is               */
/* position-independent, so the option may sit before or after the            */
/* positionals.                                                               */
/*                                                                            */
/* Arguments:                                                                 */
/*     argc    : argument count.                                              */
/*     argv    : argument vector.                                             */
/*     pszOpt  : the option token to look for (for example "--mode").         */
/*     ppszVal : receives a pointer to the value argument when found.         */
/*                                                                            */
/* Returns:                                                                   */
/*     bool : true and sets *ppszVal when the option is present with a value  */
/*            after it; false otherwise.                                      */
/*----------------------------------------------------------------------------*/
static bool Arg_GetOpt(int argc, char **argv, const char *pszOpt,
                      const char **ppszVal)
{
    int i;

    /* Stop one short of the end: the option needs a following value argument. */
    for (i = 1; i < argc - 1; i++)
    {   /* One argument per iteration. */
        if (strcmp(argv[i], pszOpt) == 0)
        {   /* Found it; the next argument is its value. */
            *ppszVal = argv[i + 1];
            return(true);
        }
    }

    /* The option was not present, or had no value argument after it. */
    return(false);
}

/*----------------------------------------------------------------------------*/
/* Arg_Utf8Args:                                                              */
/*                                                                            */
/* Build the argument vector from the process's UNICODE command line, each    */
/* argument converted to UTF-8.  The argv the C runtime hands to main is not  */
/* used, for two reasons.  It is in the ANSI code page, which cannot hold     */
/* every Windows file name -- a name outside it arrives with its letters      */
/* replaced.  And the MinGW runtime EXPANDS WILDCARDS in it: an argument      */
/* holding `?` or `*` that happens to match host files is rewritten before    */
/* main runs, so `\\?\C:\dir\file` (the `?` matched) arrived mangled, and an  */
/* image path holding a star could be replaced by host file names.            */
/* CommandLineToArgvW does neither.                                           */
/*                                                                            */
/* Arguments:                                                                 */
/*     pnArgs : receives the argument count (the program name included).      */
/*                                                                            */
/* Returns:                                                                   */
/*     char ** : a malloc'd array of malloc'd UTF-8 strings, kept for the     */
/*               life of the process (the paths in it are used to the end);   */
/*               NULL on failure, with *pnArgs set to 0.                      */
/*----------------------------------------------------------------------------*/
static char **Arg_Utf8Args(int *pnArgs)
{
    wchar_t **ppwArgs;
    char    **ppszArgs;
    int       nArgs;
    int       cb;
    int       i;

    /* Nothing yet, so every failure exit below leaves a safe zero count. */
    *pnArgs = 0;
    nArgs   = 0;

    /* Split the wide command line the way the shell-facing Windows API does. */
    ppwArgs = CommandLineToArgvW(GetCommandLineW(), &nArgs);

    if (ppwArgs == NULL || nArgs < 1)
    {   /* The command line could not be split. */
        return(NULL);
    }

    ppszArgs = (char **)malloc((size_t)nArgs * sizeof(char *));

    if (ppszArgs == NULL)
    {   /* Out of memory for the vector itself. */
        LocalFree(ppwArgs);
        return(NULL);
    }

    for (i = 0; i < nArgs; i++)
    {   /* One argument per iteration: measure, allocate, convert. */

        /* First call sizes the UTF-8 form, NUL included (-1 = NUL-terminated        */
        /* input).  WC_ERR_INVALID_CHARS refuses an unpaired surrogate instead of    */
        /* altering it -- an argument is a path, and an altered path names another   */
        /* file.                                                                     */
        cb = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ppwArgs[i], -1,
                                 NULL, 0, NULL, NULL);

        if (cb <= 0)
        {   /* This argument is not valid UTF-16. */
            LocalFree(ppwArgs);
            return(NULL);
        }

        ppszArgs[i] = (char *)malloc((size_t)cb);

        if (ppszArgs[i] == NULL)
        {   /* Out of memory for this argument. */
            LocalFree(ppwArgs);
            return(NULL);
        }

        /* Second call converts into the buffer just sized for it. */
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ppwArgs[i], -1,
                                ppszArgs[i], cb, NULL, NULL) <= 0)
        {   /* Should not fail after the sizing call; never trust it. */
            LocalFree(ppwArgs);
            return(NULL);
        }
    }

    /* The wide vector is one LocalAlloc block owned by us; release it. */
    LocalFree(ppwArgs);

    /* The UTF-8 vector and its count.  (On the failure exits above the strings   */
    /* already converted are not freed: the caller exits the process at once.)    */
    *pnArgs = nArgs;
    return(ppszArgs);
}

/*-----------------------------------------------------------------------------*/
/* Arg_ImportOpts:                                                             */
/*                                                                             */
/* Read the metadata options the creating commands -- `write`, `rwrite`,       */
/* `mkdir`, `symlink` -- share off the command line:                           */
/*                                                                             */
/*     --mode <octal>       permission bits for imported files (and mkdir's    */
/*                          directory)                                         */
/*     --owner <uid>:<gid>  numeric owner and group for everything created     */
/*     --mtime <seconds>    modify time, UTC epoch seconds; or the word `now`  */
/*                          (this run's moment, also the default when the      */
/*                          option is absent); or `host`, which keeps each     */
/*                          host file's own modify time (write and rwrite)     */
/*                                                                             */
/* Each is strictly parsed -- a value that is not a clean number is an error,  */
/* never read as far as it goes -- because it is written straight onto inodes. */
/*                                                                             */
/* Arguments:                                                                  */
/*     argc  : argument count.                                                 */
/*     argv  : argument vector.                                                */
/*     pOpts : receives the options; every "was it given" flag is false for    */
/*             an option that is absent.                                       */
/*                                                                             */
/* Returns:                                                                    */
/*     bool : true when every option present parsed cleanly; false (already    */
/*            reported) when one did not.                                      */
/*-----------------------------------------------------------------------------*/
static bool Arg_ImportOpts(int argc, char **argv, struct E4_ImportOpts *pOpts)
{
    const char   *pszVal;
    const char   *pszColon;
    char          szUid[32];
    unsigned long ulA;
    unsigned long ulB;
    size_t        cchUid;

    /* Nothing given until an option is found. */
    memset(pOpts, 0, sizeof(*pOpts));
    pszVal = NULL;

    if (Arg_GetOpt(argc, argv, "--mode", &pszVal))
    {   /* --mode: octal, as the shell writes a mode. */
        if (!E4_ParseUint(pszVal, 8, &ulA))
        {   /* The mode must be an octal number such as 0600. */
            Log_Err("invalid octal mode: %s", pszVal);
            return(false);
        }

        pOpts->uiMode = (uint32_t)ulA;
        pOpts->bMode  = true;
    }

    if (Arg_GetOpt(argc, argv, "--owner", &pszVal))
    {   /* --owner: two decimal ids joined by ONE colon.  The uid half is copied   */
        /* out so the strict parser sees a terminated token; a half that is        */
        /* missing, too long or not a number voids the option.                     */
        pszColon = strchr(pszVal, ':');
        cchUid   = 0;

        if (pszColon != NULL)
        {   /* A colon was found: the uid is the text before it. */
            cchUid = (size_t)(pszColon - pszVal);
        }

        if (pszColon == NULL || cchUid == 0 || cchUid >= sizeof(szUid))
        {   /* No colon, or nothing (or far too much) before it. */
            Log_Err("invalid --owner (want <uid>:<gid>, both decimal): %s", pszVal);
            return(false);
        }

        memcpy(szUid, pszVal, cchUid);
        szUid[cchUid] = '\0';

        if (!E4_ParseUint(szUid, 10, &ulA) || !E4_ParseUint(pszColon + 1, 10, &ulB))
        {   /* One of the two ids is not a plain decimal number. */
            Log_Err("invalid --owner (want <uid>:<gid>, both decimal): %s", pszVal);
            return(false);
        }

        pOpts->uiUid  = (uint32_t)ulA;
        pOpts->uiGid  = (uint32_t)ulB;
        pOpts->bOwner = true;
    }

    if (Arg_GetOpt(argc, argv, "--mtime", &pszVal))
    {   /* --mtime: epoch seconds; `now`, this run's one moment (the default when   */
        /* the option is absent, so naming it changes nothing but says so); or      */
        /* `host`, which is not a time at all but a choice of source, flagged       */
        /* apart so the creating command reads each host file's own time.           */
        if (strcmp(pszVal, "host") == 0)
        {   /* Keep the host file's time (the cp -p form). */
            pOpts->bMtimeHost = true;
        }
        else if (strcmp(pszVal, "now") == 0)
        {   /* This run's moment, read once at the start. */
            pOpts->uiMtime = E4_uiNow;
            pOpts->bMtime  = true;
        }
        else
        {   /* A number of seconds since 1970, UTC. */
            if (!E4_ParseUint(pszVal, 10, &ulA))
            {   /* Not a plain decimal number. */
                Log_Err("invalid --mtime (want epoch seconds, now, or host): %s", pszVal);
                return(false);
            }

            pOpts->uiMtime = (uint32_t)ulA;
            pOpts->bMtime  = true;
        }
    }

    /* Every option present was valid. */
    return(true);
}

/*----------------------------------------------------------------------------*/
/* Arg_ImagePath:                                                             */
/*                                                                            */
/* Hold an IMAGE path from the command line to the one spelling lwext4 is     */
/* given: absolute, and -- but for the root "/" itself -- with no empty, "."  */
/* or ".." component.  A single trailing "/" is dropped in place ("/etc/" is  */
/* what a person types for a directory), so the argument string is edited;    */
/* everything else is refused with the reason.                                */
/*                                                                            */
/* lwext4 resolves "." and ".." through the directories' own entries, so      */
/* "/d/./f" and "/d/x/../f" would name "/d/f" under other spellings -- and a  */
/* CREATING call handed a ".." inside a path does worse: it makes entries     */
/* whose names contain the slash until the inodes run out.  An empty LAST     */
/* component is subtler: lwext4's walk ends on it, so "/d/" opens "/d" with   */
/* success while the entry's own name is left empty, and a rename from it     */
/* (mv /d/ /e) links the directory under the new name and then fails to       */
/* unlink the old, leaving it with two parents.  Refusing these at the edge,  */
/* once, covers every command -- and keeps the text tests that compare paths  */
/* (Cmd_Mv's into-itself test) exact, one spelling per entry.                 */
/*                                                                            */
/* Arguments:                                                                 */
/*     pszPath : the argument (the tool's own UTF-8 copy, so it may be        */
/*               shortened in place).                                         */
/*                                                                            */
/* Returns:                                                                   */
/*     int : E4_SUCCESS when the path is usable (a trailing "/" now dropped); */
/*           E4_FAILURE, reported, otherwise.                                 */
/*----------------------------------------------------------------------------*/
static int Arg_ImagePath(char *pszPath)
{
    const char *pszComponent;
    size_t      cchPath;
    size_t      cchComponent;

    if (pszPath[0] != '/')
    {   /* Relative, or empty: lwext4 finds the mount by matching the start of   */
        /* the path against "/", so a relative path matches no mount and comes   */
        /* back as "not found" -- naming a missing file when the fault is the    */
        /* argument's spelling.                                                  */
        Log_Err("image path must start with /: %s", pszPath);
        return(E4_FAILURE);
    }

    /* One trailing "/" is dropped, never more: "/d//" loses one and is then   */
    /* refused below for the empty component it still has.  The root alone     */
    /* keeps its slash -- it IS the slash.                                     */
    cchPath = strlen(pszPath);

    if (cchPath > 1 && pszPath[cchPath - 1] == '/')
    {   /* A directory typed with its slash: the same entry without it. */
        pszPath[cchPath - 1] = '\0';
    }

    if (pszPath[1] == '\0')
    {   /* The root, "/": valid, and the only path that ends in "/". */
        return(E4_SUCCESS);
    }

    /* Each component in turn: the text after a "/" up to the next "/" or the   */
    /* end.  Every "/" must be followed by a component, so the loop runs at     */
    /* least once and ends only at the end of the string.                       */
    pszComponent = pszPath + 1;

    for (;;)
    {   /* One component per iteration: the one that starts here. */
        cchComponent = strcspn(pszComponent, "/");

        if (cchComponent == 0)
        {   /* "//" inside the path, or a second trailing "/": an empty name. */
            Log_Err("image path has an empty component: %s", pszPath);
            return(E4_FAILURE);
        }

        if ((cchComponent == 1 && pszComponent[0] == '.') ||
            (cchComponent == 2 && pszComponent[0] == '.' && pszComponent[1] == '.'))
        {   /* "." or "..": another spelling of an entry (the header says what   */
            /* lwext4 makes of them).                                            */
            Log_Err("image path must not contain a . or .. component: %s", pszPath);
            return(E4_FAILURE);
        }

        if (pszComponent[cchComponent] == '\0')
        {   /* That was the last component, and every one passed. */
            return(E4_SUCCESS);
        }

        /* Past this component and its "/", to the next. */
        pszComponent += cchComponent + 1;
    }
}

/*----------------------------------------------------------------------------*/
/* E4_ConsoleRestore:                                                         */
/*                                                                            */
/* Put the console's output code page back to what it was when the program    */
/* started.  Registered with atexit by E4_ConsoleUtf8, so it runs on every    */
/* return from main.  The code page belongs to the CONSOLE, not the process:  */
/* left changed, it would stay changed for the user's shell after this exits. */
/*                                                                            */
/* Arguments:                                                                 */
/*     None.                                                                  */
/*                                                                            */
/* Returns:                                                                   */
/*     void : the console's output code page is restored, if one was saved.   */
/*----------------------------------------------------------------------------*/
static void E4_ConsoleRestore(void)
{
    if (E4_uiOldConsoleCp != 0)
    {   /* A code page was saved at start: push any text still buffered out   */
        /* under UTF-8 first, then put the old page back.                     */
        fflush(stdout);
        fflush(stderr);
        SetConsoleOutputCP(E4_uiOldConsoleCp);
        E4_uiOldConsoleCp = 0;
    }
}

/*-----------------------------------------------------------------------------*/
/* E4_ConsoleUtf8:                                                             */
/*                                                                             */
/* Make the console show this tool's output correctly.  Every name it prints   */
/* -- host paths, image entries -- is UTF-8, and a console left on its default */
/* code page would show any letter outside ASCII as two or three wrong ones.   */
/* The console is switched to UTF-8 for the run and switched back at exit.     */
/* Output redirected to a file or a pipe is not touched by this: the bytes     */
/* written there are UTF-8 either way.                                         */
/*                                                                             */
/* Arguments:                                                                  */
/*     None.                                                                   */
/*                                                                             */
/* Returns:                                                                    */
/*     void : the console (if there is one) is on UTF-8, and the restore is    */
/*            registered.                                                      */
/*-----------------------------------------------------------------------------*/
static void E4_ConsoleUtf8(void)
{
    UINT uiNow;

    /* 0 means the process has no console at all (fully detached); then there   */
    /* is nothing to switch and nothing to restore.                             */
    uiNow = GetConsoleOutputCP();

    if (uiNow != 0 && uiNow != CP_UTF8)
    {   /* A console on some other code page: remember it, switch, and make   */
        /* sure it is put back however main returns.                          */
        if (SetConsoleOutputCP(CP_UTF8))
        {   /* Switched; arm the restore. */
            E4_uiOldConsoleCp = uiNow;
            atexit(E4_ConsoleRestore);
        }
    }
}

/*-----------------------------------------------------------------------------*/
/* main:                                                                       */
/*                                                                             */
/* Parse the command line and dispatch to one Cmd_ handler.  The arguments are */
/* taken from the Unicode command line as UTF-8 (Arg_Utf8Args), not from the   */
/* runtime's argv.  --help and                                                 */
/* --version exit immediately; otherwise argv[1] is the image, argv[2] is the  */
/* command (ls / rm / cat ...), and the rest are collected into positionals    */
/* with any flag stripped out, so a flag may appear in any position.           */
/*                                                                             */
/* The dispatch is a flat strcmp chain, NOT a function-pointer table, on       */
/* purpose: the command set is small and fixed, and a flat chain keeps each    */
/* command's usage string, its argument-count rule and its Cmd_ call ADJACENT  */
/* and greppable, where a table would scatter those three across the file.     */
/* main validates that enough positionals are PRESENT and that each IMAGE path */
/* among them is well formed (Arg_ImagePath, which also drops a trailing "/"); */
/* every other path, mode and existence check belongs to the Cmd_ handler, so  */
/* adding a command here is one branch carrying its own count rule, the names  */
/* of its image-path positionals, and nothing else.                            */
/*                                                                             */
/* Arguments:                                                                  */
/*     argc : the runtime's argument count; replaced by Arg_Utf8Args' count.   */
/*     argv : the runtime's argument vector (ANSI, wildcards expanded);        */
/*            replaced by Arg_Utf8Args' UTF-8 vector before anything reads it. */
/*                                                                             */
/* Returns:                                                                    */
/*     int : E4_SUCCESS (0) on success, E4_FAILURE (1) on any error.           */
/*-----------------------------------------------------------------------------*/
int main(int argc, char **argv)
{
    const char   *pszImage;
    const char   *pszCmd;
    char         *apszArg[E4_MAX_POSARGS];
    struct E4_ImportOpts opts;
    int           nArg;
    bool          bWrite;
    bool          bSkipNext;
    int           i;

    /* Console first, so even the error just below prints readably. */
    E4_ConsoleUtf8();

    /* The clock, once: every stamp of this run is this moment (the global says   */
    /* why one moment and not a fresh read per entry).                            */
    E4_uiNow = (uint32_t)time(NULL);

    /* Replace the runtime's argv with the UTF-8 one built from the Unicode        */
    /* command line (Arg_Utf8Args says why).  Everything below -- and every path   */
    /* handed to a Cmd_ handler -- reads only this vector.                         */
    argv = Arg_Utf8Args(&argc);

    if (argv == NULL)
    {   /* The command line could not be read as Unicode text. */
        Log_Err("cannot read the command line");
        return(E4_FAILURE);
    }

    if (argc >= 2 && strcmp(argv[1], "--help") == 0)
    {   /* --help: usage to stderr, success. */
        Util_Usage();
        return(E4_SUCCESS);
    }

    if (argc >= 2 && strcmp(argv[1], "--version") == 0)
    {   /* --version: the bare version to stdout. */
        printf("ext4edit %s\n", EXT4EDIT_VERSION);
        return(E4_SUCCESS);
    }

    if (argc < 3)
    {   /* Need at least an image and a command. */
        Util_Usage();
        return(E4_FAILURE);
    }

    pszImage = argv[1];
    pszCmd   = argv[2];
    bWrite   = Arg_HasFlag(argc, argv, "--write");

    /* Collect the command's positional arguments -- everything after the   */
    /* command that is not a flag -- so a flag may appear in any position   */
    /* without shifting the positionals.                                    */
    nArg      = 0;
    bSkipNext = false;

    for (i = 3; i < argc; i++)
    {   /* One post-command argument per iteration. */
        if (bSkipNext)
        {   /* This argument is a preceding option's value (--mode's); skip it. */
            bSkipNext = false;
            continue;
        }

        if (strcmp(argv[i], "--write") == 0 || strcmp(argv[i], "--text") == 0 ||
            strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "-t") == 0 ||
            strcmp(argv[i], "--time") == 0 || strcmp(argv[i], "--force") == 0)
        {   /* A known boolean flag, not a positional (Arg_HasFlag reads them). */
            continue;
        }

        if (strcmp(argv[i], "--mode") == 0 || strcmp(argv[i], "--owner") == 0 ||
            strcmp(argv[i], "--mtime") == 0)
        {   /* A value-bearing option: skip it AND its value (Arg_GetOpt reads it). */
            bSkipNext = true;
            continue;
        }

        if (nArg < E4_MAX_POSARGS)
        {   /* Record this positional argument. */
            apszArg[nArg] = argv[i];
            nArg++;
        }
    }

    /* The read-only commands first (rmdir sits among them as found; it is an     */
    /* editing command like those below and takes bWrite the same way).  Each     */
    /* branch is the same shape: confirm the command's positionals are present,   */
    /* refuse an image path Arg_ImagePath will not hold, then hand off to its     */
    /* Cmd_ handler, which does the real work and owns every other check.  The    */
    /* order of the branches is the order the usage text lists the commands; a    */
    /* name is matched whole, so "rm" never catches "rm-tree" or "rmdir".         */

    if (strcmp(pszCmd, "ls") == 0)
    {   /* ls: list one directory in the image to stdout (read-only). */
        if (nArg < 1)
        {   /* Missing the directory. */
            Log_Err("ls needs a directory: ext4edit <image> ls <dir>");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Ls(pszImage, apszArg[0]));
    }

    if (strcmp(pszCmd, "cat") == 0)
    {   /* cat: stream a file's bytes to stdout; --text turns LF into CRLF. */
        if (nArg < 1)
        {   /* Missing the path. */
            Log_Err("cat needs a path: ext4edit <image> cat <path> [--text]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Cat(pszImage, apszArg[0], Arg_HasFlag(argc, argv, "--text")));
    }

    if (strcmp(pszCmd, "rmdir") == 0)
    {   /* rmdir: remove an EMPTY directory only (refuses a non-empty one). */
        if (nArg < 1)
        {   /* Missing the directory. */
            Log_Err("rmdir needs a directory: ext4edit <image> rmdir <dir> [--write]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Rmdir(pszImage, apszArg[0], bWrite));
    }

    if (strcmp(pszCmd, "stat") == 0)
    {   /* stat: print one path's inode facts (read-only). */
        if (nArg < 1)
        {   /* Missing the path. */
            Log_Err("stat needs a path: ext4edit <image> stat <path>");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Stat(pszImage, apszArg[0]));
    }

    if (strcmp(pszCmd, "readlink") == 0)
    {   /* readlink: print a symlink's target without following it. */
        if (nArg < 1)
        {   /* Missing the path. */
            Log_Err("readlink needs a path: ext4edit <image> readlink <path>");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Readlink(pszImage, apszArg[0]));
    }

    if (strcmp(pszCmd, "dump") == 0)
    {   /* dump: extract ONE file to the host; -p metadata sidecar, -t times. */
        if (nArg < 2)
        {   /* Missing the image file or the host destination. */
            Log_Err("dump needs an image path and a host file: ext4edit <image> dump <imgfile> <hostfile> [-p] [-t]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; -t and --time are one flag spelled two ways.  The handler's   */
        /* status is the program's exit code.                                      */
        return(Cmd_Dump(pszImage, apszArg[0], apszArg[1],
                        Arg_HasFlag(argc, argv, "-p"),
                        Arg_HasFlag(argc, argv, "-t") || Arg_HasFlag(argc, argv, "--time")));
    }

    if (strcmp(pszCmd, "rdump") == 0)
    {   /* rdump: recursively extract a TREE; --force, -p manifest, -t times. */
        if (nArg < 2)
        {   /* Missing the image directory or the host destination. */
            Log_Err("rdump needs an image dir and a host dir: ext4edit <image> rdump <imgdir> <hostdir> [--force] [-p] [-t]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; -t and --time are one flag spelled two ways.  The handler's   */
        /* status is the program's exit code.                                      */
        return(Cmd_Rdump(pszImage, apszArg[0], apszArg[1],
                         Arg_HasFlag(argc, argv, "-p"),
                         Arg_HasFlag(argc, argv, "-t") || Arg_HasFlag(argc, argv, "--time"),
                         Arg_HasFlag(argc, argv, "--force")));
    }

    /* Editing commands next.  Each is a DRY RUN unless --write is present: the     */
    /* one bWrite flag read above is threaded into each handler, which drives its   */
    /* mount mode (read-only for a preview, read-write for the real change).        */

    if (strcmp(pszCmd, "rm") == 0)
    {   /* rm: remove one file or symlink. */
        if (nArg < 1)
        {   /* Missing the path. */
            Log_Err("rm needs a path: ext4edit <image> rm <path> [--write]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Rm(pszImage, apszArg[0], bWrite));
    }

    if (strcmp(pszCmd, "rm-tree") == 0)
    {   /* rm-tree: remove a directory and everything under it. */
        if (nArg < 1)
        {   /* Missing the directory. */
            Log_Err("rm-tree needs a directory: ext4edit <image> rm-tree <dir> [--write]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_RmTree(pszImage, apszArg[0], bWrite));
    }

    if (strcmp(pszCmd, "mkdir") == 0)
    {   /* mkdir: create one directory level (the parent must already exist). */
        if (nArg < 1)
        {   /* Missing the directory. */
            Log_Err("mkdir needs a directory: ext4edit <image> mkdir <dir> [--mode <octal>] [--owner <uid>:<gid>] [--mtime <seconds|now>] [--write]");
            return(E4_FAILURE);
        }

        /* The new directory's own mode, owner and time, from --mode / --owner /   */
        /* --mtime; the handler fills in what is not given.  The options are       */
        /* parsed before the path is checked, so a malformed option is reported    */
        /* ahead of a bad path; no reason for that order is recorded.              */
        if (!Arg_ImportOpts(argc, argv, &opts))
        {   /* A metadata option did not parse (already reported). */
            return(E4_FAILURE);
        }

        if (opts.bMtimeHost)
        {   /* --mtime host names a host file's own time, and mkdir has no host   */
            /* file: refused, rather than silently taken as "now".                */
            Log_Err("--mtime host has no meaning for mkdir: there is no host file");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Mkdir(pszImage, apszArg[0], bWrite, &opts));
    }

    if (strcmp(pszCmd, "write") == 0)
    {   /* write: copy a host file's bytes INTO the image at <path>. */
        if (nArg < 2)
        {   /* Missing the destination path or the host source file. */
            Log_Err("write needs a path and a host file: ext4edit <image> write <path> <hostfile> [-p] [--mode <octal>] [--owner <uid>:<gid>] [--mtime <seconds|now|host>] [--write]");
            return(E4_FAILURE);
        }

        /* The written file's mode, owner and time, from --mode / --owner /      */
        /* --mtime (with -p, the sidecar supplies what the command line does     */
        /* not); the handler fills in the rest.  Parsed before the path check,   */
        /* as for mkdir.                                                         */
        if (!Arg_ImportOpts(argc, argv, &opts))
        {   /* A metadata option did not parse (already reported). */
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Write(pszImage, apszArg[0], apszArg[1], bWrite, &opts,
                         Arg_HasFlag(argc, argv, "-p")));
    }

    if (strcmp(pszCmd, "rwrite") == 0)
    {   /* rwrite: recursively import a host file / tree INTO the image. */
        if (nArg < 2)
        {   /* Missing the host source or the image destination. */
            Log_Err("rwrite needs a host path and an image path: ext4edit <image> rwrite <hostpath> <imgpath> [-p] [--mode <octal>] [--owner <uid>:<gid>] [--mtime <seconds|now|host>] [--write]");
            return(E4_FAILURE);
        }

        /* --mode / --owner / --mtime; the defaults for what is not given (files   */
        /* 0644, directories 0755, root:root, this run's moment) are applied per   */
        /* entry by the import itself.                                             */
        if (!Arg_ImportOpts(argc, argv, &opts))
        {   /* A metadata option did not parse (already reported). */
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[1]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the image path is the SECOND positional here (host source   */
        /* first, as cp orders them).  The handler's status is the exit code.    */
        return(Cmd_Rwrite(pszImage, apszArg[0], apszArg[1], bWrite, &opts,
                          Arg_HasFlag(argc, argv, "-p")));
    }

    if (strcmp(pszCmd, "ln") == 0)
    {   /* ln: create a hard link (a second name for an existing inode). */
        if (nArg < 2)
        {   /* Missing the target or the link name. */
            Log_Err("ln needs a target and a link: ext4edit <image> ln <target> <link> [--write]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS || Arg_ImagePath(apszArg[1]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Ln(pszImage, apszArg[0], apszArg[1], bWrite));
    }

    if (strcmp(pszCmd, "symlink") == 0)
    {   /* symlink: create a symbolic link holding the given target text. */
        if (nArg < 2)
        {   /* Missing the target or the link name. */
            Log_Err("symlink needs a target and a link: ext4edit <image> symlink <target> <link> [--owner <uid>:<gid>] [--mtime <seconds|now>] [--write]");
            return(E4_FAILURE);
        }

        /* The link's owner and time, from --owner / --mtime (the usage line   */
        /* above names no --mode: the handler gives every symlink the fixed    */
        /* 0777 Linux keeps).  Parsed before the path check, as for mkdir.     */
        if (!Arg_ImportOpts(argc, argv, &opts))
        {   /* A metadata option did not parse (already reported). */
            return(E4_FAILURE);
        }

        if (opts.bMtimeHost)
        {   /* --mtime host names a host file's own time, and a symlink has no   */
            /* host file: refused, rather than silently taken as "now".          */
            Log_Err("--mtime host has no meaning for symlink: there is no host file");
            return(E4_FAILURE);
        }

        /* Only the LINK is an image path here: the target is text stored in   */
        /* the link as given, relative or absolute, existing or not.           */
        if (Arg_ImagePath(apszArg[1]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Symlink(pszImage, apszArg[0], apszArg[1], bWrite, &opts));
    }

    if (strcmp(pszCmd, "mv") == 0)
    {   /* mv: rename / move an entry within the image. */
        if (nArg < 2)
        {   /* Missing the source or the destination. */
            Log_Err("mv needs a source and a destination: ext4edit <image> mv <src> <dst> [--write]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[0]) != E4_SUCCESS || Arg_ImagePath(apszArg[1]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the handler's status is the program's exit code. */
        return(Cmd_Mv(pszImage, apszArg[0], apszArg[1], bWrite));
    }

    if (strcmp(pszCmd, "chmod") == 0)
    {   /* chmod: set an entry's permission bits (octal mode). */
        if (nArg < 2)
        {   /* Missing the mode or the path. */
            Log_Err("chmod needs a mode and a path: ext4edit <image> chmod <octal> <path> [--write]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[1]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the mode text is validated by the handler, not here.  Its   */
        /* status is the program's exit code.                                    */
        return(Cmd_Chmod(pszImage, apszArg[0], apszArg[1], bWrite));
    }

    if (strcmp(pszCmd, "chown") == 0)
    {   /* chown: set an entry's numeric owner and group. */
        if (nArg < 3)
        {   /* Missing the uid, gid, or path. */
            Log_Err("chown needs uid, gid and a path: ext4edit <image> chown <uid> <gid> <path> [--write]");
            return(E4_FAILURE);
        }

        if (Arg_ImagePath(apszArg[2]) != E4_SUCCESS)
        {   /* Not a usable image path (already reported). */
            return(E4_FAILURE);
        }

        /* Hand off; the two id texts are validated by the handler, not here.   */
        /* Its status is the program's exit code.                               */
        return(Cmd_Chown(pszImage, apszArg[0], apszArg[1], apszArg[2], bWrite));
    }

    /* Anything else is an unknown command: name it, then print the usage text    */
    /* so a mistyped command shows the list it should have come from, and fail.   */
    Log_Err("unknown command: %s", pszCmd);
    Util_Usage();
    return(E4_FAILURE);
}
