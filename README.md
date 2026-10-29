# ext4edit -- list and edit files in an ext2/3/4 image

`ext4edit` is a small Windows command-line tool that lists and edits files in
an **ext2/3/4 image FILE** -- for example a partition image carved out of a raw
disk dump -- with no Linux host and no untrusted Windows ext4 driver. It reads
(`ls`, `cat`, `stat`, `readlink`), edits (`rm`, `rmdir`, `rm-tree`, `mkdir`,
`write`, `ln`, `symlink`, `mv`, `chmod`, `chown`), and copies whole trees between
the image and the Windows filesystem -- OUT with `dump` / `rdump`, IN with
`rwrite` -- all in userspace.

It is built on the bundled **lwext4** library (BSD, in `lwext4/`, carrying
fifty-one numbered fixes and additions -- see "Bundled lwext4" below) wrapped
in one C file, and builds to a single static `.exe` with the WinLibs MinGW-w64
gcc. Because it operates on an image FILE (not a `\\.\` device), it does **not**
need Administrator.

Every editing command is a **DRY RUN unless `--write`** is given, so you see
exactly what would change before anything is touched. **Always keep a backup of
the image before writing, and run `e2fsck` on the result afterward** -- this
writes a real filesystem.

## Usage

```
ext4edit <image> <command> [args] [options] [--write]
ext4edit --help
ext4edit --version
```

`<image>` is an ext2/3/4 image FILE. Paths inside the image are absolute (start
with `/`). Options and flags may appear in any position after the command. With
no arguments, or an unknown command, `ext4edit` prints its usage to stderr and
exits non-zero, so a bare run is never silent.

## Command reference

### Global

| Argument | Meaning |
|---|---|
| `<image>` | the ext2/3/4 image file; a long path or a `\\?\` path is accepted as it is |
| `--write` | apply an editing command; without it the command is a DRY RUN that prints what it would do and changes nothing (the image is mounted read-only) |
| `--help` | print the usage block to stderr and exit 0 |
| `--version` | print `ext4edit <version>` to stdout and exit 0 |

### Read-only commands (`dump` / `rdump` also create host files)

| Command | Meaning |
|---|---|
| `ls <dir>` | list a directory: one line per entry with its type and name (`.` and `..` shown) |
| `cat <path> [--text]` | write a file's bytes to stdout, binary-exact; `--text` translates LF to CRLF for a Windows editor |
| `stat <path>` | show inode facts: inode number, type, mode, link count, uid/gid, size, blocks, times |
| `readlink <path>` | print a symlink's stored target without following it |
| `dump <imgfile> <hostfile> [-p] [-t]` | extract ONE file to the host. `-p`: write a `<hostfile>.ext4meta` metadata sidecar; `-t` (or `--time`): set the host file's times from the inode. A symlink is skipped (recorded under `-p`); a directory is refused -- use `rdump`. |
| `rdump <imgdir> <hostdir> [--force] [-p] [-t]` | recursively extract a TREE to the host. Refuses a non-empty `<hostdir>` unless `--force`. `-p`: write one `<hostdir>/_ext4meta.txt` manifest; `-t` (or `--time`): set host file times. Symlinks, device nodes, fifos and sockets are skipped (recorded under `-p`). |

### Editing commands (DRY RUN unless `--write`)

| Command | Meaning |
|---|---|
| `rm <path>` | remove one file or link; a path that is not there is reported as `absent` and is a success |
| `rmdir <dir>` | remove an EMPTY directory (refuses a non-empty one; use `rm-tree`) |
| `rm-tree <dir>` | remove a directory and everything under it |
| `mkdir <dir>` | create ONE directory level (the parent must already exist): `0755`, `uid=gid=0`, dated now |
| `write <path> <hostfile>` | copy a host (Windows) file INTO the image at `<path>`. A new file gets `0644`, `uid=gid=0` and is dated now; a file that is overwritten keeps its mode and owner. |
| `rwrite <hostpath> <imgpath>` | recursively IMPORT a host file / tree INTO the image. New files get `0644`, directories `0755`, `uid=gid=0`, all dated now; host symlinks are followed. Note the order: host source first, image destination second. |
| `symlink <target> <link>` | create a symbolic link holding `<target>` as text (never validated): `0777` (as Linux keeps every symlink), `uid=gid=0`, dated now |
| `ln <target> <link>` | create a hard link: a second name for an existing file's inode |
| `mv <src> <dst>` | rename / move an entry (a directory is never moved into its own subtree) |
| `chmod <octal> <path>` | change permission bits (e.g. `0644`); the type bits are kept |
| `chown <uid> <gid> <path>` | change owner / group (numeric ids) |

### Options of the four creating commands (`write`, `rwrite`, `mkdir`, `symlink`)

| Option | Meaning |
|---|---|
| `-p` | (`write`, `rwrite`) restore from the metadata `dump -p` / `rdump -p` recorded: each entry gets its recorded mode, owner, group, modify and access times, and `rwrite` creates the recorded symbolic links again. `write -p` reads `<hostfile>.ext4meta`; `rwrite -p` reads `<hostpath>\_ext4meta.txt` (which is then not imported itself). A missing metadata file is an error. |
| `--mode <octal>` | permission bits for imported FILES (an imported tree's directories stay `0755`), or for the directory `mkdir` makes; a symlink's mode is always `0777` |
| `--owner <uid>:<gid>` | numeric owner and group for everything created |
| `--mtime <seconds>`, `--mtime now`, `--mtime host` | modify time for everything created: a UTC epoch number; `now`, this run's moment (the default, so naming it changes nothing); or `host`, which keeps each host file's own modify time (`write` and `rwrite` only, what `cp -p` does; `mkdir` and `symlink` have no host file and refuse it) |

An option given on the command line outranks the recorded metadata, which
outranks the defaults. "Now" is read from the clock ONCE per run, so every
entry of one `rwrite` carries the same timestamp.

### Exit code

`0` on success and `1` on any failure -- including an `rm` whose path was found
but could not be removed, an `rdump` / `rwrite` in which any entry was skipped or
failed, any command whose directory could not be read in full, a bad option, or
an image that could not be mounted.

## Images, names and paths

- **A read-only command never needs write access to the image.** `ls`, `cat`,
  `stat`, `readlink`, `dump`, `rdump` and every dry run open the image file
  read-only, so a write-protected image, or one on read-only media, can be
  read. Nothing is written to it.
- **Two filesystems are refused for writing**, and still read: one whose
  journal is on a separate device (an external journal, which this tool cannot
  reach), and a `fast_commit` filesystem that was not cleanly unmounted (its
  journal may hold fast commits only the Linux driver can replay; mount it on
  Linux once).
- **An image path is held to one spelling.** It must start with `/`; a
  trailing `/` is dropped (`ls /etc/` lists `/etc`); an empty, `.` or `..`
  component is refused. lwext4 would read `/d/x/../f` as `/d/f`, create
  nonsense entries from a `..` handed to `symlink`, and leave a directory with
  two parents after `mv /d/ /e`, so none of them reaches it. The same rule
  binds the `path:` lines of a manifest read with `-p`.
- **A directory that cannot be read in full fails the command.** A listing,
  an extract, an emptiness test or an existence check that lwext4 could not
  finish -- a damaged inode or block -- is reported as such, never as a
  complete (or empty) directory.
- **Times are kept as a kernel keeps them.** Anything created is dated now
  (modify and access time) unless `--mtime` says otherwise; every change --
  a create, an overwrite, `chmod`, `chown`, `mv`, `ln` -- sets the inode's
  change time to now, which no option moves; and a directory that gains,
  loses or renames an entry gets now as its modify and change times. lwext4
  alone would date everything 1970 and leave directories untouched. The one
  exception is a `-p` restore, whose directories keep the recorded times even
  as the recorded symlinks are created in them.
- **Names are Unicode.** The command line is read as Unicode and every name is
  carried as UTF-8, which is what a Linux filesystem stores. A host file with
  any Windows name is imported under that same name, and an image entry is
  extracted under its own name.
- **Long paths work**, for the image file as for anything else: a path may be
  up to 4095 bytes of UTF-8, which is Linux's own limit, so no path an image
  can hold is too long. A host path given in the `\\?\` form is accepted as it
  is.
- **`*` and `?` in an argument are taken literally.** They are not expanded
  against the host's files.
- **A name Windows cannot hold is never created.** `dump` / `rdump` refuse a
  destination whose last component is a reserved device name (`CON`, `NUL`,
  `COM1` ...), holds an illegal character, or ends in a dot or a space, and
  `rdump` skips an image entry with such a name -- recording it in the `-p`
  manifest like every other skipped entry.
- **A write that runs out of room leaves nothing behind.** `write` / `rwrite`
  report "the filesystem in the image is full" and remove the partial file, so
  an `rwrite` carries on with the files that do fit.
- **A damaged image does not hang the tool.** An internal check of the
  bundled library that fails is reported once at the end of the run, with a
  hint to check the image with `e2fsck`, and the command fails.

## Extract and import -- crossing the Windows boundary

`dump` / `rdump` copy OUT of the image; `rwrite` copies IN. Because NTFS cannot
hold a Unix file's mode, owner or symlink nature, the two directions handle that
metadata explicitly:

- **Extract (`dump` / `rdump`).** File bytes are written out verbatim. With
  **`-p`** the Unix metadata NTFS cannot keep is written as plain text beside the
  data -- `dump -p` writes a per-file sidecar `<hostfile>.ext4meta`; `rdump -p`
  writes one manifest `<hostdir>/_ext4meta.txt` with a block per entry. With
  **`-t`** the host FILE's access / modify times are set from the inode (a host
  directory's own mtime is not set -- the C runtime cannot stamp a directory on
  Windows -- but `-p` still records it). A **symlink** cannot be created on NTFS,
  so it is skipped with a warning and, under `-p`, recorded as `type: symlink`
  with its `target:`.
- **Import (`rwrite`).** Windows files carry no Unix mode / owner, so imported
  files get **`0644`** (or `--mode <octal>`), directories get **`0755`** (so
  they stay traversable -- `--mode` applies to files only), everything gets
  **`uid=gid=0`** (or `--owner`), and every entry is **dated now** (or
  `--mtime <seconds>`; `--mtime host` copies each host file's own modify time
  instead). A host symlink is followed to its target, so the target's bytes
  are imported.
- **Import with `-p` -- the round trip.** `rdump <dir> <host> -p` followed by
  `rwrite <host> <dir> -p --write` puts the tree back as it was: every file and
  directory gets the mode, owner, group and times the manifest recorded, and
  the symbolic links are created again from their recorded targets. What the
  manifest names but cannot rebuild -- a device node, a fifo, a socket, an
  entry whose name Windows could not hold -- is listed as not restored. A
  restored symbolic link has its target but not its original owner or time.

The `-p` block is `key: value` lines: `path`, `type`, `mode` (octal permission
bits), `uid`, `gid`, `mtime` / `atime` / `ctime` (epoch seconds) and `size`; a
symlink block is `path`, `type: symlink` and `target`. In an `rdump` manifest the
`path` is relative to `<hostdir>` and blocks are separated by a blank line.

## Note on paths in a POSIX-style shell

A POSIX-style shell on Windows rewrites a leading-slash argument such as
`/etc/network` into a Windows path before the program sees it. Run ext4edit
from **cmd.exe or PowerShell**, where an argument reaches the program as
typed.

## Examples

All from cmd.exe or PowerShell. Every editing example below is a dry run
without `--write`; the ones shown with it change the image.

Reading:

```
ext4edit image.img ls /                              # list the root
ext4edit image.img ls /etc                           # list a directory
ext4edit image.img cat /etc/fstab                    # a file's bytes, as they are
ext4edit image.img cat /etc/fstab --text > fstab.txt # as CRLF text, into a host file
ext4edit image.img stat /etc/hostname                # inode facts for one entry
ext4edit image.img readlink /etc/localtime           # a symlink's target
```

Extracting to the host:

```
ext4edit image.img dump /etc/hostname out.txt              # one file
ext4edit image.img dump /etc/hostname out.txt -p -t        # plus its metadata sidecar and times
ext4edit image.img rdump /etc C:\tmp\etc -p                # a tree, with a manifest
ext4edit image.img rdump /etc C:\tmp\etc -p -t --force     # into a host dir that is not empty
ext4edit image.img rdump / C:\tmp\whole -p                 # the whole image
```

Importing from the host:

```
ext4edit image.img write /etc/hostname C:\tmp\hostname.txt --write          # one file (0644, root, now)
ext4edit image.img write /etc/hostname C:\tmp\hostname.txt --mode 0600 --owner 1000:1000 --write
ext4edit image.img write /etc/hostname out.txt -p --write                   # restored from out.txt.ext4meta
ext4edit image.img rwrite C:\tmp\etc /etc --write                           # a tree (files 0644, dirs 0755)
ext4edit image.img rwrite C:\tmp\etc /etc -p --write                        # the round trip: modes, owners, times, symlinks restored
ext4edit image.img rwrite C:\tmp\etc /etc --mtime host --write              # keep each host file's own modify time
ext4edit image.img rwrite C:\tmp\etc /etc --mtime 1700000000 --write        # one fixed modify time for everything
```

Editing:

```
ext4edit image.img rm /tmp/stale                     # DRY RUN: shows what would go
ext4edit image.img rm /tmp/stale --write             # remove one file
ext4edit image.img rmdir /var/empty --write          # remove an EMPTY directory
ext4edit image.img rm-tree /var/cache --write        # remove a directory and its contents
ext4edit image.img mkdir /opt/app --write            # a new directory (0755, root, now)
ext4edit image.img mkdir /opt/app --mode 0700 --owner 1000:1000 --write
ext4edit image.img symlink /usr/bin/busybox /bin/sh --write      # a symbolic link
ext4edit image.img ln /etc/hostname /etc/hostname.bak --write    # a hard link
ext4edit image.img mv /etc/hostname /etc/hostname.old --write    # rename / move
ext4edit image.img chmod 0644 /etc/hostname --write
ext4edit image.img chown 0 0 /etc/hostname --write
```

Afterwards, on the Linux side (or in WSL):

```
e2fsck -fn image.img       # check without changing
e2fsck -fy image.img       # and repair
```

## Building

### Tools required

One tool builds the program: the compiler. Nothing else is needed -- no shell
beyond `cmd.exe`, no CMake, no Visual Studio, no MSYS2 packages, no library
installs (lwext4 is bundled as source).

| Tool | What it is for | How to install it |
|---|---|---|
| **WinLibs MinGW-w64 GCC** (64-bit, UCRT runtime, POSIX threads) | the C compiler and linker; the only compiler the build is held to | `winget install BrechtSanders.WinLibs.POSIX.UCRT`, then put its `mingw64\bin` folder on `PATH` -- winget places it under `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin`. Or download the "UCRT runtime, POSIX threads" 64-bit zip from `https://winlibs.com`, extract it anywhere, and put its `mingw64\bin` on `PATH`. Check with `gcc --version`: it names `MinGW-W64 x86_64-ucrt-posix-seh`; the build here uses release 15.2.0. |

The MSYS2 toolchains, Cygwin's gcc and Visual C++ are not used: the source is
built as one static native executable with the WinLibs compiler, so no runtime
DLL has to travel with it.

### The build

From `cmd.exe` or PowerShell, in this folder, with `gcc` on `PATH`:

```
.\build.bat
```

(The `.\` is what PowerShell requires for a script in the current folder, and
what `cmd.exe` requires on a machine set not to search the current folder for
programs; it is never wrong.) `build.bat` does the following, and every source
file is named in it, one line each -- there is no wildcard -- so a file that is
not listed is not part of the program and a listed file that goes missing
stops the build with `BUILD FAILED`:

1. Changes to its own folder, so it runs from anywhere, and makes `build\obj\`.
2. Compiles the bundled lwext4 -- the 23 sources listed in the script under
   `lwext4/src/`, including the two files our patches add (`ext4_inline.c`,
   `ext4_casefold.c`) -- and e2fsprogs' Unicode folding (`lwext4/unicode/nls_utf8.c`),
   each with `gcc -c -O2 -std=gnu99`, the lwext4 settings
   `-DCONFIG_USE_DEFAULT_CFG=1 -DCONFIG_DEBUG_PRINTF=0 -DCONFIG_BLOCK_DEV_CACHE_SIZE=4096`
   (the built-in configuration, so no CMake; lwext4's debug chatter off; a
   4096-block cache in place of the default 8), and the include path
   `-Ilwext4/include -Ilwext4/unicode`. These are compiled as the library is,
   with no `-Wall`: they are not held to this program's warning bar.
   lwext4's own `blockdev/ext4_filedev.c` is deliberately not built.
3. Compiles `ext4edit.c` with `-Wall -Wextra` added. It builds with no warning.
4. Links everything into one static `ext4edit.exe` (`gcc -static`) with a
   64 MiB stack (`-Wl,--stack,67108864`), headroom only: the tree walks of
   `rdump` and `rwrite` are loops over heap-held frames, one per directory
   level, so a tree's depth costs the stack nothing. The objects go to
   `build/obj/`, the executable to this folder.

The result imports nothing but the Windows system DLLs and the Universal C
Runtime (part of Windows 10 and later; installable on older versions), so no
compiler DLL travels with it. Verify a build by the executable's timestamp,
never by the script's last line. There is no `clean` target: delete `build/` and
`ext4edit.exe` by hand.

### The documents

`README.html` and `LWEXT4_UPSTREAM.html` are rendered from the Markdown files
beside them by a Python 3 script kept with the development tooling (it needs
the `markdown` package: `pip install --user markdown`). The HTML is shipped so
that nothing is needed to read the documents; edit the `.md` and re-render.
The build does not need Python, or anything else: the compiler alone.

## Bundled lwext4

`lwext4/` is upstream `gkostka/lwext4` v1.0.0 (commit `890484c`), BSD-licensed,
(c) Grzegorz Kostka: its `src/`, `include/` and `blockdev/` folders, plus a
`unicode/` folder of ours. Its BSD terms are retained in every file header and
in `LICENSE`. **It is not as published: it carries fifty-two numbered fixes
and additions**, each marked where it sits as `ext4edit patch N` (so
`grep -rn "ext4edit patch" lwext4` finds every site). **Every patch is
documented in [`LWEXT4_UPSTREAM.md`](LWEXT4_UPSTREAM.md)** (also rendered as
`LWEXT4_UPSTREAM.html`): a one-line table of all fifty-two at a glance, then
each one's section -- the defect, how it was shown, the fix -- with its
unified diff against v1.0.0 and where upstream's master stands on it; the
defects found and not fixed; and upstream's open issues that are not
defects. That document is the reference for the library; this one only says
what the patches add up to: the tool mounts images made with
`metadata_csum_seed`, `largedir`, `inline_data` and `casefold`, and a damaged
image no longer hangs it -- every assertion that data could reach is an error
return, and an internal check that still fails is reported at the end of the
run.

`blockdev/ext4_filedev.c` is bundled but **not built**: `ext4edit.c` carries
its own image block device, which opens the image read-only for a read-only
command, takes a long or Unicode path, and measures a file over 2 GiB
correctly.

## Licence

**GPL-2.0-or-later** (see `LICENSE` and `GPL-2.0.txt`) for the wrapper, this
document, the build script and the files our patches add to lwext4; the
bundled lwext4 keeps its BSD licence on its own files, and `lwext4/unicode/`
holds e2fsprogs' Unicode folding under the GNU GPL. Until version 1.2.0 the
wrapper was dual MIT OR GPL-2.0-or-later; the added files are GPL, which the
MIT option could not cover.

## See also

`dd4win` (in `..\dd4win\`), which images a disk or card to the file this tool
edits and writes it back.
