@echo off
rem ---------------------------------------------------------------------------
rem build.bat -- build ext4edit.exe (native 64-bit Windows, static) with the
rem WinLibs MinGW-w64 gcc, from cmd.exe or PowerShell: .\build.bat
rem
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright (c) 2026 B. C. Services
rem
rem The only tool needed is gcc on PATH (the WinLibs MinGW-w64 build, UCRT
rem runtime).  Everything is named relative to this script's own folder, so
rem it runs from anywhere; the objects go to build\obj and the executable
rem beside this script.
rem
rem The bundled lwext4 (lwext4\, upstream BSD) is NOT as published: it carries
rem numbered fixes and additions, each marked where it sits ("ext4edit patch
rem N") and each explained with its diff in LWEXT4_UPSTREAM.md beside this
rem script.  Two of them add whole files of ours -- src\ext4_inline.c and
rem src\ext4_casefold.c with their headers -- and one brings e2fsprogs'
rem Unicode folding in lwext4\unicode\, compiled through the stand-in headers
rem beside it, which is why that folder is on the include path.  lwext4 and
rem the folding code are compiled with no -Wall, because they are not held
rem to this project's warning bar.  Only our own ext4edit.c is compiled with
rem -Wall -Wextra.
rem
rem EVERY SOURCE IS NAMED BELOW, ONE CALL PER FILE -- there is no wildcard.
rem A file in lwext4\ that is not listed is not part of the program, and a
rem listed file that goes missing stops the build.  lwext4's own
rem blockdev\ext4_filedev.c is deliberately NOT built: ext4edit.c carries its
rem own image block device.  Verify a build by the executable's timestamp,
rem never by this script's last line.
rem ---------------------------------------------------------------------------
setlocal
pushd "%~dp0"

rem The lwext4 settings: the built-in defaults (no CMake-generated header),
rem lwext4's own stdout debug chatter off so the listings stay clean, and a
rem 4096-block cache in place of lwext4's default of 8 (sized for a
rem microcontroller): blocks are found through a tree and allocated only as
rem they are used, so the figure costs nothing but memory -- at most 4096
rem times the block size, 16 MiB with 4 KiB blocks -- and an rdump or rwrite
rem of a large tree, which touches far more than 8 metadata blocks at a time,
rem no longer evicts and re-reads (and, writing, flushes) them over and over.
set CFG=-DCONFIG_USE_DEFAULT_CFG=1 -DCONFIG_DEBUG_PRINTF=0 -DCONFIG_BLOCK_DEV_CACHE_SIZE=4096
set INC=-Ilwext4\include -Ilwext4\unicode
set OBJS=

if not exist build\obj mkdir build\obj
if errorlevel 1 goto :fail

rem The lwext4 sources, one call each (all in lwext4\src\).  ext4_casefold
rem and ext4_inline are the two files of ours that the casefold and
rem inline_data patches add.
call :lwext4 ext4
if errorlevel 1 goto :fail
call :lwext4 ext4_balloc
if errorlevel 1 goto :fail
call :lwext4 ext4_bcache
if errorlevel 1 goto :fail
call :lwext4 ext4_bitmap
if errorlevel 1 goto :fail
call :lwext4 ext4_block_group
if errorlevel 1 goto :fail
call :lwext4 ext4_blockdev
if errorlevel 1 goto :fail
call :lwext4 ext4_casefold
if errorlevel 1 goto :fail
call :lwext4 ext4_crc32
if errorlevel 1 goto :fail
call :lwext4 ext4_debug
if errorlevel 1 goto :fail
call :lwext4 ext4_dir
if errorlevel 1 goto :fail
call :lwext4 ext4_dir_idx
if errorlevel 1 goto :fail
call :lwext4 ext4_extent
if errorlevel 1 goto :fail
call :lwext4 ext4_fs
if errorlevel 1 goto :fail
call :lwext4 ext4_hash
if errorlevel 1 goto :fail
call :lwext4 ext4_ialloc
if errorlevel 1 goto :fail
call :lwext4 ext4_inline
if errorlevel 1 goto :fail
call :lwext4 ext4_inode
if errorlevel 1 goto :fail
call :lwext4 ext4_journal
if errorlevel 1 goto :fail
call :lwext4 ext4_mbr
if errorlevel 1 goto :fail
call :lwext4 ext4_mkfs
if errorlevel 1 goto :fail
call :lwext4 ext4_super
if errorlevel 1 goto :fail
call :lwext4 ext4_trans
if errorlevel 1 goto :fail
call :lwext4 ext4_xattr
if errorlevel 1 goto :fail

rem The Unicode folding the casefold patch brings (in lwext4\unicode\):
rem e2fsprogs' nls_utf8.c mirrored verbatim, compiled as the library is --
rem with lwext4's settings and not to our warning bar, since it is e2fsprogs'
rem code -- through the stand-in headers beside it, so no e2fsprogs build is
rem needed.
gcc -c -O2 -std=gnu99 %CFG% %INC% -o build\obj\nls_utf8.o lwext4\unicode\nls_utf8.c
if errorlevel 1 goto :fail
set OBJS=%OBJS% build\obj\nls_utf8.o

rem Our wrapper, to the project's warning bar.
gcc -c -O2 -std=gnu99 -Wall -Wextra %CFG% %INC% -o build\obj\ext4edit.o ext4edit.c
if errorlevel 1 goto :fail
set OBJS=%OBJS% build\obj\ext4edit.o

rem Link one static exe from exactly the objects compiled above -- never
rem whatever happens to be lying in the object folder.  The stack is set to
rem 64 MiB (the Windows default is 1 MiB) as headroom only: rdump and rwrite
rem walk a tree as loops over heap-held frames, one per directory level, so
rem the depth of a tree costs the stack nothing, and nothing in the program
rem needs more than the default.  This is address space reserved, not memory
rem used; only what is actually reached is committed.
gcc -static -Wl,--stack,67108864 -o ext4edit.exe %OBJS%
if errorlevel 1 goto :fail

echo built: ext4edit.exe
popd
exit /b 0

rem One lwext4 source: compile it with the library's settings and add its
rem object to the link list.  %1 is the file's name without its .c.
:lwext4
gcc -c -O2 -std=gnu99 %CFG% %INC% -o build\obj\%1.o lwext4\src\%1.c
if errorlevel 1 exit /b 1
set OBJS=%OBJS% build\obj\%1.o
exit /b 0

rem Any step failed: gcc has already said which; leave the tree as it is.
:fail
echo BUILD FAILED
popd
exit /b 1
