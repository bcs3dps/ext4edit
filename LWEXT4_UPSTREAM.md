# lwext4 -- defects found, and the patches that fix them

This document lists every defect found in **lwext4**
(`https://github.com/gkostka/lwext4`, Grzegorz Kostka and contributors,
BSD licence) while it was used as the ext2 / ext3 / ext4 library of
this program, and every change made to lwext4's own sources to fix them. Each
patch is explained -- the defect, how it was shown, the fix -- and given as
a unified diff against the upstream release it was made on.

- **Part 1** gives the patches, one section each, with its diff.
- **Part 2** lists defects found and verified -- by a test, or by reading
  the code where a test cannot reach them -- that are NOT fixed here.
- **Part 3** lists upstream's open issues that are not lwext4 defects.

---

## The base: release v1.0.0, and what master already has

The copy was taken from upstream's **v1.0.0 release tag** (commit
`890484c`, 2016-02-09). Every diff below is against v1.0.0, and every
patch applies to it: each patch's hunks were checked with `git apply
--check` against a clean v1.0.0 tree, one patch at a time, and all 50
apply; patch 52, added on 2026-10-28, was made against this copy and its
hunk is given against v1.0.0's own line numbers.

Upstream's **master** branch is 150 commits later (`58bcf89`,
2022-09-22, the newest). All 112 of those commits that touch the library
(`src/`, `include/`) were read against these patches, so each patch section
says where master stands:

- **fixed in master** -- a master commit fixes the same defect (named); the
  patch is not needed on master;
- **partly fixed in master** -- a master commit fixes part of it, and what
  is left is said;
- **not fixed in master** -- master still has the defect, checked in
  master's own code.

Each section also says whether its hunks apply to master as they stand
(`git apply --check`), only with fuzz (`patch -F2 --dry-run`, every hunk
placed), or how many hunks fail there and would need adapting, because
master's code has moved.

One more fact about master for anyone taking these into it: master commit
`b1a34c5` relicensed master's xattr and extent code as GPL.

Upstream has been quiet: the newest pull request reviewed is #176
(2026-10-16).

---

## How to read the patches

- Each hunk is unified diff with two lines of context, lwext4's own paths
  (`a/src/ext4.c`, `b/src/ext4.c`), and the function it is in after the
  second `@@`. The upstream line numbers are v1.0.0's; the changed-side
  numbers are those of v1.0.0 with only that patch applied.
- The changed code is marked in the source with its patch number
  ("ext4edit patch N"), so `grep "ext4edit patch"` over the copy finds
  every site; the comments in the hunks are the patch's own.
- A hunk that carries the changes of two patches is shown once, under the
  first, and named under the other ("Also this patch's ...").
- A few patches build on others, and compile only with them: patches 7 and 8
  need the files they add (below); patch 9 matters only once patches 3, 5,
  7 or 8 widen the features lwext4 mounts; patch 41 calls patch 38's
  recount; patch 45's revoke cancel looks in patch 51's revoke tree, and the
  hunk shown under patch 46 also defines that tree's functions.
- **Shown** means a test showed the defect -- a test image made by
  e2fsprogs (`mke2fs`, `debugfs`), the operation run through lwext4, and the
  result judged by e2fsprogs (`e2fsck -fn`, `debugfs`) -- failing before the
  patch and passing after it. **By reading** means the fault was found and
  fixed from the code alone, no test being able to reach it.
- Patch 11 is not in this list: it changed e2fsprogs, not lwext4.

**Files added.** Patches 7 and 8 add whole files, which are not reproduced
here -- they are in this program's source tree, beside lwext4's own, under
`lwext4/`:

| File | Lines | Patch | Licence |
|---|---|---|---|
| `include/ext4_inline.h`, `src/ext4_inline.c` | 165, 896 | 7 | GPL-2.0-or-later (ours) |
| `include/ext4_casefold.h`, `src/ext4_casefold.c` | 155, 212 | 8 | GPL-2.0-or-later (ours) |
| `unicode/nls_utf8.c`, `unicode/utf8n.h`, `unicode/utf8data.h` | 1 015, 120, 4 109 | 8 | e2fsprogs 1.47.4's Unicode folding, mirrored verbatim bar one exported wrapper (`ext2fs_casefold`, marked); GNU GPL, as their headers state |
| `unicode/config.h`, `unicode/ext2_fs.h`, `unicode/ext2fs.h`, `unicode/ext2fsP.h` | 23, 34, 117, 58 | 8 | GPL-2.0-or-later (ours): stand-ins that let `nls_utf8.c` build without e2fsprogs |

None of these is covered by lwext4's BSD licence.

---

## The patches at a glance

| Patch | The defect | Found | Master |
|---|---|---|---|
| 1 | out of space appending to an extent file never returns | shown | fixed (`ab4ac25`) |
| 2 | a failed write leaves the write-back count raised | shown | not fixed |
| 3 | `metadata_csum_seed` refused | shown | not fixed |
| 4 | a refused mount keeps its mount point; the next is a phantom, its unmount crashes | shown | fixed (`ce13c49`) |
| 5 | `largedir` refused; a split index node gets the wrong limit on metadata_csum | shown | not fixed |
| 6 | a read-only session dirties block groups; a failed block get crashes the next release | shown | not fixed |
| 7 | `inline_data` refused (a feature added) | shown | not fixed |
| 8 | `casefold` refused (a feature added) | shown | not fixed |
| 9 | the formatter stamps every feature lwext4 can mount | shown | not fixed (latent) |
| 10 | `fast_commit` not named (one define) | -- | not fixed |
| 12 | a symlink of 60 bytes or more is written over the start of the filesystem | shown | partly (`c5f8d13`) |
| 13 | removing a tree frees a file with another name outside it | shown | not fixed |
| 14 | a moved small directory's block checksum is left wrong | shown | not fixed |
| 15 | a sparse file's holes are read from block 0 | shown | partly |
| 16 | creating anything hangs on a group whose free count is wrong | shown | not fixed |
| 17 | `ext4_dir_rm` puts the wrong reference on an error | reading | not fixed |
| 18 | `ext4_fremove` leaves write-back on after a failed truncate | reading | not fixed |
| 19 | a failed get or put in the path walk releases twice | reading | not fixed |
| 20 | a failed write returns EOK and is committed | reading | not fixed |
| 21 | a checksummed journal's commit and tags are written in the wrong byte order | shown | not fixed |
| 22 | transaction IDs restart at 1 at every journal start | shown | partly (`1a96ef0`) |
| 23 | meta_bg groups read their descriptors from the wrong block | shown | not fixed |
| 24 | a fast symlink with an xattr block is taken for a slow one | shown | not fixed |
| 25 | only the low 16 bits of a uid / gid are used | shown | not fixed |
| 26 | an all-zero directory hash seed is used as given | shown | not fixed |
| 27 | the superblock's geometry is not bounded | shown | not fixed |
| 28 | listing a directory of size 0 reads through a NULL pointer | shown | not fixed |
| 29 | the xattr block number's high half is shifted by 16 | reading | not fixed |
| 30 | a failed htree walk leaks its blocks; the add-entry caller releases wild | reading | not fixed |
| 31 | removing a file frees a SHARED xattr block | shown | not fixed |
| 32 | extent headers are not bounded; the root is never checked | shown | not fixed |
| 33 | the formatter's inodes per group is not a multiple of 8 | shown | not fixed |
| 34 | a damaged xattr set releases the inode's block twice | shown | fixed (`18ddc30`) |
| 35 | the directory iterator cannot report a read error | shown | not fixed |
| 36 | an assertion spins for ever, and data reaches several | shown | not fixed |
| 37 | a multi-block free neither revokes nor invalidates its blocks | shown | fixed (`9dc5fd9`, `faa76ad`, `47bbb86`) |
| 38 | an aborted transaction leaves the superblock's free counts lowered | shown | not fixed |
| 39 | listing a directory over-reads its block's buffer | shown | fixed (`a84a1b6`) |
| 40 | a damaged journal superblock crashes a writable mount | shown | fixed (`dd3f008`) |
| 41 | a journal replay leaves the superblock's free counts stale | shown | fixed (`2e93de2`) |
| 42 | the journal's escape flag is mishandled; a replay destroys the superblock | shown | fixed (`384424b`, `fb6fd61`) |
| 43 | on 1 KiB blocks a replay's superblock is overwritten by the pre-replay one | shown | not fixed |
| 44 | writing attributes back corrupts a SHARED xattr block | shown | fixed (`5372f03`, `9fdd0b7`, `278e8d4`) |
| 45 | a freed block goes unrevoked; a revoked block reused as metadata is lost | reading | fixed, bar one case (`8281a97`, `321eba3`) |
| 46 | transaction IDs are compared without their wrap past 2^32 | shown | fixed (`bc6e18d`, `ea294e2`) |
| 47 | a new inode is not zeroed; it inherits the old file's attributes | shown | fixed (`8163730`) |
| 48 | a creating open's transaction nests inside its caller's | reading | fixed (`9b38aef`) |
| 49 | a failed bitmap write leaks a group descriptor reference | reading | fixed (`2248e43`) |
| 50 | a missing path level is hashed and named with the rest of the path | shown | fixed (`1aa9740`) |
| 51 | a transaction's revokes are a list (a cost, not a wrong result) | -- | master's shape |
| 52 | a name of exactly 255 bytes cannot be reached: the path walk stops one byte short | shown | not fixed |

---

## Part 1 -- the patches

### Patch 1 -- out of space while appending to an extent file never returns

**Where:** `src/ext4_fs.c`, `ext4_fs_append_inode_dblk`, the extent branch.

**The defect:** the result of `ext4_extent_get_blocks` was not looked at
before `ext4_assert(*fblock)`. On a full filesystem the call fails, the
block is 0, the assertion fails -- and lwext4's default `ext4_assert`
prints and then loops for ever. Had it returned, the inode's size would also
have been grown for a block never allocated. ext2 and ext3 (no extents)
return ENOSPC as they should.

**Shown:** a 20 MiB file written into a 16 MiB ext4 image spun at 100 % CPU
until killed, at 1 and 4 KiB blocks (a debugger showed `ext4_fwrite` ->
`ext4_fs_append_inode_dblk`). Patched, the write fails with ENOSPC, and
images filled to their last block are `e2fsck -fn` clean.

**The fix:** the failure is returned before the assertion and before the
size is grown. (The same hunk carries patch 36's change to that assertion.)

**Upstream:** fixed in master by `ab4ac25` (2016-02-18). Its hunk applies
to master only with fuzz, and is not needed there.

<!-- hunks 1 -->
```diff
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -1632,6 +1632,21 @@ int ext4_fs_append_inode_dblk(struct ext4_inode_ref *inode_ref,
 						&current_fsblk, true, NULL);
 
+		/* ext4edit patch 1: report the failure.  As published this
+		 * went straight on to the assertion below, which fails when
+		 * no block could be allocated (a full filesystem) -- and a
+		 * failed ext4_assert loops for ever -- and then grew the
+		 * inode's size for a block that was never allocated. */
+		if (rc != EOK)
+			return rc;
+
 		*fblock = current_fsblk;
-		ext4_assert(*fblock);
+		/* ext4edit patch 36: a successful allocating lookup that     */
+		/* still names block 0 means the extent code misbehaved (or    */
+		/* damage confused it) -- EIO, and the size is NOT grown, as   */
+		/* patch 1 orders for a failed allocation; upstream asserts.   */
+		if (!*fblock) {
+			/* Block 0: refused, the size left as it was. */
+			return EIO;
+		}
 
 		ext4_inode_set_size(inode_ref->inode, inode_size + block_size);
```
<!-- /hunks -->

### Patch 2 -- a failed write leaves the device's write-back count raised

**Where:** `src/ext4.c`, `ext4_fwrite`.

**The defect:** `ext4_fwrite` starts write-back cache mode near its top,
and two failure exits -- appending a block fails (ENOSPC), or a block-map
initialisation fails -- jump past the matching stop. The block device's
`cache_write_back` count stays one too high for as long as the device
object lives, so a later write-back "off" no longer flushes anything.

**Shown:** in one process, after a write failed for lack of space, an image
then formatted with `ext4_mkfs` reached the disk WITHOUT its root directory
(`e2fsck`: "Root inode is not a directory"), because the formatter's own
write-back "off" no longer flushed its cache. Patched, the format is clean.

**The fix:** both exits stop write-back mode before they leave.

**Upstream:** not fixed in master; applies to master.

<!-- hunks 2 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -1883,6 +1883,11 @@ int ext4_fwrite(ext4_file *f, const void *buf, size_t size, size_t *wcnt)
 				r = ext4_fs_init_inode_dblk_idx(&ref, iblk_idx,
 								&fblk);
-				if (r != EOK)
+				if (r != EOK) {
+					/* ext4edit patch 2: this exit, too,
+					 * must stop write back cache mode. */
+					ext4_block_cache_write_back(
+					    f->mp->fs.bdev, 0);
 					goto Finish;
+				}
 			} else {
 				rr = ext4_fs_append_inode_dblk(&ref, &fblk,
@@ -1928,4 +1933,9 @@ int ext4_fwrite(ext4_file *f, const void *buf, size_t size, size_t *wcnt)
 			 * should be updated.*/
 			r = rr;
+			/* ext4edit patch 2: stop write back cache mode on
+			 * this exit too.  As published, the goto skipped the
+			 * matching stop below and left the device's
+			 * cache_write_back count one too high for good. */
+			ext4_block_cache_write_back(f->mp->fs.bdev, 0);
 			goto out_fsize;
 		}
```
<!-- /hunks -->

### Patch 3 -- `metadata_csum_seed` is refused

**Where:** `include/ext4_types.h` (the superblock struct, the supported
set), `include/ext4_super.h`, and every metadata checksum seed in
`ext4_balloc.c`, `ext4_dir.c`, `ext4_dir_idx.c`, `ext4_extent.c`,
`ext4_fs.c` (two), `ext4_ialloc.c`, `ext4_xattr.c`.

**The defect:** e2fsprogs 1.43 onward sets the incompatible feature
`metadata_csum_seed` (0x2000, which lwext4 names `BG_USE_META_CSUM`) by
default with `metadata_csum`, so every recent image -- current Raspberry Pi
OS among them -- carries it. lwext4 lists it as unsupported, so the mount
fails with ENOTSUP. With the feature, every metadata checksum is seeded from
the superblock's stored `s_checksum_seed` (offset 0x270) rather than from
crc32c of the uuid, and lwext4's struct had that word inside its padding.

**Shown:** a Raspberry Pi OS root partition, refused before, mounted with
the patch, was browsed and its files read; the ordinary test images stayed
`e2fsck` clean.

**The fix:** the superblock gains `prj_quota_inum` and `checksum_seed`
(out of the padding, same size); `EXT4_FINCOM_CSUM_SEED` joins the
supported set; a new `ext4_sb_csum_seed()` returns the stored seed when the
feature is set and crc32c(uuid) otherwise, and replaces the eight seed
computations. The journal's own checksums (the jbd2 superblock's uuid) are
untouched.

**Upstream:** not fixed in master. 2 of its 12 hunks need adapting to
master; the rest apply.

<!-- hunks 3 -->
```diff
--- a/include/ext4_super.h
+++ b/include/ext4_super.h
@@ -50,4 +50,5 @@ extern "C" {
 #include "ext4_types.h"
 #include "ext4_misc.h"
+#include "ext4_crc32.h"	/* ext4edit patch 3: for ext4_sb_csum_seed */
 
 /**@brief   Blocks count get stored in superblock.
@@ -135,4 +136,17 @@ static inline bool ext4_sb_feature_incom(struct ext4_sblock *s, uint32_t v)
 {
 	return to_le32(s->features_incompatible) & v;
+}
+
+/**@brief   ext4edit patch 3: the seed every metadata checksum starts from.
+ *          With metadata_csum_seed set it is the superblock's stored seed
+ *          (so the uuid can change without rewriting every checksum);
+ *          otherwise it is crc32c of the uuid, as before.
+ * @param   s superblock descriptor
+ * @return  the checksum seed*/
+static inline uint32_t ext4_sb_csum_seed(struct ext4_sblock *s)
+{
+	if (ext4_sb_feature_incom(s, EXT4_FINCOM_CSUM_SEED))
+		return to_le32(s->checksum_seed);
+	return ext4_crc32c(EXT4_CRC32_INIT, s->uuid, sizeof(s->uuid));
 }
 
--- a/include/ext4_types.h
+++ b/include/ext4_types.h
@@ -175,5 +175,18 @@ struct ext4_sblock {
 	uint8_t  encrypt_pw_salt[16];	/* Salt used for string2key algorithm */
 	uint32_t lpf_ino;		/* Location of the lost+found inode */
-	uint32_t padding[100];	/* Padding to the end of the block */
+	uint32_t prj_quota_inum;	/* ext4edit patch 3: inode for tracking project quota (was the first word of padding) */
+	uint32_t checksum_seed;	/* ext4edit patch 3: crc32c(uuid) when metadata_csum_seed is set (was the second word of padding) */
+	uint8_t wtime_hi;	/* ext4edit patch 8: the high byte of write_time (these eight bytes were the third and fourth words of padding) */
+	uint8_t mtime_hi;	/* ext4edit patch 8: the high byte of mount_time */
+	uint8_t mkfs_time_hi;	/* ext4edit patch 8: the high byte of mkfs_time */
+	uint8_t lastcheck_hi;	/* ext4edit patch 8: the high byte of last_check_time */
+	uint8_t first_error_time_hi;	/* ext4edit patch 8: the high byte of first_error_time */
+	uint8_t last_error_time_hi;	/* ext4edit patch 8: the high byte of last_error_time */
+	uint8_t first_error_errcode;	/* ext4edit patch 8: the error code of the first error */
+	uint8_t last_error_errcode;	/* ext4edit patch 8: the error code of the last error */
+	uint16_t encoding;	/* ext4edit patch 8: filename charset encoding, EXT4_ENC_* (was the fifth word of padding, with the flags) */
+	uint16_t encoding_flags;	/* ext4edit patch 8: filename charset encoding flags, EXT4_ENC_STRICT_MODE_FL */
+	uint32_t orphan_file_inum;	/* ext4edit patch 8: inode of the orphan file (was the sixth word of padding) */
+	uint32_t padding[94];	/* Padding to the end of the block (98 words before patch 8) */
 	uint32_t checksum;		/* crc32c(superblock) */
 };
@@ -279,8 +292,27 @@ struct ext4_sblock {
 #define EXT4_SUPPORTED_FCOM (EXT4_FCOM_DIR_INDEX)
 
+/* ext4edit patch 3: metadata_csum_seed (0x2000, the bit lwext4 names         */
+/* BG_USE_META_CSUM) is supported -- the checksum seed is then read from the  */
+/* superblock's checksum_seed field instead of being derived from the uuid,   */
+/* see ext4_sb_csum_seed in ext4_super.h.  Current e2fsprogs sets it by       */
+/* default, so every recent image (Raspberry Pi OS among them) carries it.    */
+#define EXT4_FINCOM_CSUM_SEED EXT4_FINCOM_BG_USE_META_CSUM
+
+/* ext4edit patch 5: largedir (0x4000) is supported -- an htree may be three   */
+/* levels deep (indirect_levels up to 2, EXT4_HTREE_LEVEL in ext4_dir_idx.h)   */
+/* and a directory's size uses size_hi like a file's.  e2fsprogs 1.44 onward   */
+/* offers it, and a directory that outgrows two levels needs it.               */
 #define EXT4_SUPPORTED_FINCOM                              \
 	(EXT4_FINCOM_FILETYPE | EXT4_FINCOM_META_BG |      \
 	 EXT4_FINCOM_EXTENTS | EXT4_FINCOM_FLEX_BG |       \
-	 EXT4_FINCOM_64BIT)
+	 EXT4_FINCOM_64BIT | EXT4_FINCOM_CSUM_SEED |       \
+	 EXT4_FINCOM_LARGEDIR | EXT4_FINCOM_INLINE_DATA |  \
+	 EXT4_FINCOM_CASEFOLD)
+/* ext4edit patch 7: inline_data (0x8000) is supported -- see ext4_inline.h; */
+/* the inode flag that marks inline content is EXT4_INODE_FLAG_INLINE_DATA.  */
+/* ext4edit patch 8: casefold (0x20000) is supported -- see ext4_casefold.h; */
+/* the superblock's encoding must be one the mirrored tables fold, which     */
+/* ext4_fs_check_features tests, and EXT4_INODE_FLAG_CASEFOLD marks each     */
+/* case-insensitive directory.                                               */
 
 #define EXT4_SUPPORTED_FRO_COM                             \
--- a/src/ext4_balloc.c
+++ b/src/ext4_balloc.c
@@ -93,6 +93,5 @@ static uint32_t ext4_balloc_bitmap_csum(struct ext4_sblock *sb,
 
 		/* First calculate crc32 checksum against fs uuid */
-		checksum = ext4_crc32c(EXT4_CRC32_INIT, sb->uuid,
-				sizeof(sb->uuid));
+		checksum = ext4_sb_csum_seed(sb); /* ext4edit patch 3 */
 		/* Then calculate crc32 checksum against block_group_desc */
 		checksum = ext4_crc32c(checksum, bitmap, blocks_per_group / 8);
--- a/src/ext4_dir.c
+++ b/src/ext4_dir.c
@@ -87,5 +87,5 @@ static uint32_t ext4_dir_csum(struct ext4_inode_ref *inode_ref,
 
 	/* First calculate crc32 checksum against fs uuid */
-	csum = ext4_crc32c(EXT4_CRC32_INIT, sb->uuid, sizeof(sb->uuid));
+	csum = ext4_sb_csum_seed(sb); /* ext4edit patch 3 */
 	/* Then calculate crc32 checksum against inode number
 	 * and inode generation */
--- a/src/ext4_dir_idx.c
+++ b/src/ext4_dir_idx.c
@@ -228,5 +228,5 @@ static uint32_t ext4_dir_dx_checksum(struct ext4_inode_ref *inode_ref, void *de,
 		t->checksum = 0;
 		/* First calculate crc32 checksum against fs uuid */
-		csum = ext4_crc32c(EXT4_CRC32_INIT, sb->uuid, sizeof(sb->uuid));
+		csum = ext4_sb_csum_seed(sb); /* ext4edit patch 3 */
 		/* Then calculate crc32 checksum against inode number
 		 * and inode generation */
--- a/src/ext4_extent.c
+++ b/src/ext4_extent.c
@@ -325,6 +325,5 @@ static uint32_t ext4_ext_block_csum(struct ext4_inode_ref *inode_ref,
 			to_le32(ext4_inode_get_generation(inode_ref->inode));
 		/* First calculate crc32 checksum against fs uuid */
-		checksum = ext4_crc32c(EXT4_CRC32_INIT, sb->uuid,
-				sizeof(sb->uuid));
+		checksum = ext4_sb_csum_seed(sb); /* ext4edit patch 3 */
 		/* Then calculate crc32 checksum against inode number
 		 * and inode generation */
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -508,6 +508,5 @@ static uint16_t ext4_fs_bg_checksum(struct ext4_sblock *sb, uint32_t bgid,
 
 		/* First calculate crc32 checksum against fs uuid */
-		checksum = ext4_crc32c(EXT4_CRC32_INIT, sb->uuid,
-				sizeof(sb->uuid));
+		checksum = ext4_sb_csum_seed(sb); /* ext4edit patch 3 */
 		/* Then calculate crc32 checksum against bgid */
 		checksum = ext4_crc32c(checksum, &le32_bgid, sizeof(bgid));
@@ -670,6 +669,5 @@ static uint32_t ext4_fs_inode_checksum(struct ext4_inode_ref *inode_ref)
 
 		/* First calculate crc32 checksum against fs uuid */
-		checksum = ext4_crc32c(EXT4_CRC32_INIT, sb->uuid,
-				       sizeof(sb->uuid));
+		checksum = ext4_sb_csum_seed(sb); /* ext4edit patch 3 */
 		/* Then calculate crc32 checksum against inode number
 		 * and inode generation */
--- a/src/ext4_ialloc.c
+++ b/src/ext4_ialloc.c
@@ -101,5 +101,5 @@ static uint32_t ext4_ialloc_bitmap_csum(struct ext4_sblock *sb,	void *bitmap)
 
 		/* First calculate crc32 checksum against fs uuid */
-		csum = ext4_crc32c(EXT4_CRC32_INIT, sb->uuid, sizeof(sb->uuid));
+		csum = ext4_sb_csum_seed(sb); /* ext4edit patch 3 */
 		/* Then calculate crc32 checksum against inode bitmap */
 		csum = ext4_crc32c(csum, bitmap, (inodes_per_group + 7) / 8);
--- a/src/ext4_xattr.c
+++ b/src/ext4_xattr.c
@@ -134,6 +134,5 @@ ext4_xattr_block_checksum(struct ext4_inode_ref *inode_ref,
 		header->h_checksum = 0;
 		/* First calculate crc32 checksum against fs uuid */
-		checksum = ext4_crc32c(EXT4_CRC32_INIT, sb->uuid,
-				sizeof(sb->uuid));
+		checksum = ext4_sb_csum_seed(sb); /* ext4edit patch 3 */
 		/* Then calculate crc32 checksum block number */
 		checksum = ext4_crc32c(checksum, &le64_blocknr,
```
<!-- /hunks -->

### Patch 4 -- a refused mount keeps its mount point

**Where:** `src/ext4.c`, `ext4_mount` (every failure exit after the mount
point is claimed) and `ext4_umount`.

**The defect:** `ext4_mount` claims a slot -- `mounted = 1` and the name --
before it opens the device and reads the superblock. When a later step
fails (not an ext filesystem, a damaged superblock, an unsupported
feature) it returns with the slot still claimed. The next `ext4_mount` of
the same name finds the slot and returns EOK at once, opening nothing: every
read then fails with EIO, and `ext4_umount` calls
`ext4_bcache_cleanup(NULL)` and crashes. The block-size-mismatch exit also
left the device open and a dynamic cache allocated.

**Shown:** a zero-filled file mounted twice: the second mount "succeeded"
and the unmount crashed with an access violation. Patched, both mounts are
refused, and a good image then mounts, lists and unmounts.

**The fix:** each failure exit gives the mount point back (`mounted = 0`,
the name cleared); the size-mismatch exit also releases the cache and
closes the device; `ext4_umount` refuses (ENODEV) a mount point that is not
mounted or has no cache, so a future slip is an error, not a crash.

**Upstream:** fixed in master by `ce13c49`. 1 of its 4 hunks fails on
master, and it is not needed there.

<!-- hunks 4 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -378,15 +378,42 @@ int ext4_mount(const char *dev_name, const char *mount_point,
 
 	r = ext4_block_init(bd);
-	if (r != EOK)
+	if (r != EOK) {
+		/* ext4edit patch 4: a failed mount must give the mount point
+		 * back.  As published, every failure below this line returned
+		 * with mp->mounted still set and the name still in place, so
+		 * the NEXT ext4_mount of the same mount point matched the loop
+		 * above and returned EOK without opening the device, reading
+		 * the superblock or binding a cache: a phantom mount whose
+		 * reads fail with EIO and whose ext4_umount dereferences the
+		 * NULL cache and crashes the process.  Reached through the GUI
+		 * by one refused open followed by another on the same path. */
+		mp->mounted = 0;
+		mp->name[0] = '\0';
 		return r;
+	}
 
 	r = ext4_fs_init(&mp->fs, bd, read_only);
 	if (r != EOK) {
 		ext4_block_fini(bd);
+		/* ext4edit patch 4: give the mount point back (see above). */
+		mp->mounted = 0;
+		mp->name[0] = '\0';
 		return r;
 	}
 
 	bsize = ext4_sb_get_block_size(&mp->fs.sb);
-	ext4_block_set_lb_size(bd, bsize);
+	/* ext4edit patch 36: a refused size releases as the later failures */
+	/* below do -- the device closed, the mount point given back, and   */
+	/* NOT ext4_fs_fini, which would write the superblock.  A guard: the */
+	/* superblock bounds of patches 24-29 refuse such a block size       */
+	/* before this point, so data cannot reach it.                       */
+	r = ext4_block_set_lb_size(bd, bsize);
+	if (r != EOK) {
+		/* Refused: the device closed and the mount point given back. */
+		ext4_block_fini(bd);
+		mp->mounted = 0;
+		mp->name[0] = '\0';
+		return r;
+	}
 
 	mp->cache_dynamic = 0;
@@ -402,10 +429,24 @@ int ext4_mount(const char *dev_name, const char *mount_point,
 			free(bc);
 			ext4_block_fini(bd);
+			/* ext4edit patch 4: give the mount point back (see above). */
+			mp->mounted = 0;
+			mp->name[0] = '\0';
 			return r;
 		}
 	}
 
-	if (bsize != bc->itemsize)
+	if (bsize != bc->itemsize) {
+		/* ext4edit patch 4: as published this exit returned with the
+		 * device still open and a dynamic cache still allocated, as
+		 * well as the mount point still claimed.  Undo all three. */
+		if (mp->cache_dynamic) {
+			ext4_bcache_fini_dynamic(bc);
+			free(bc);
+		}
+		ext4_block_fini(bd);
+		mp->mounted = 0;
+		mp->name[0] = '\0';
 		return ENOTSUP;
+	}
 
 	/*Bind block cache to block device*/
@@ -418,4 +459,7 @@ int ext4_mount(const char *dev_name, const char *mount_point,
 			free(bc);
 		}
+		/* ext4edit patch 4: give the mount point back (see above). */
+		mp->mounted = 0;
+		mp->name[0] = '\0';
 		return r;
 	}
@@ -440,4 +484,12 @@ int ext4_umount(const char *mount_point)
 
 	if (!mp)
+		return ENODEV;
+
+	/* ext4edit patch 4: a mount point that is not mounted, or whose
+	 * block device never had a cache bound, is not one this function can
+	 * take down -- as published it went on to ext4_bcache_cleanup(NULL)
+	 * and crashed.  With the mount-side patch this can no longer be
+	 * reached; the guard keeps a future slip an error, not a crash. */
+	if (!mp->mounted || !mp->fs.bdev || !mp->fs.bdev->bc)
 		return ENODEV;
 
```
<!-- /hunks -->

### Patch 5 -- `largedir` is refused, and a split index node gets the wrong limit on metadata_csum

**Where:** `include/ext4_types.h`, `include/ext4_dir_idx.h`,
`src/ext4_inode.c` (`ext4_inode_get_size`), `src/ext4_dir_idx.c`
(`ext4_dir_hinfo_init`, both path arrays, `ext4_dir_dx_split_index`,
`ext4_dir_dx_add_entry`).

**The defect:** two. (1) `largedir` (0x4000) allows an htree of three
levels and a directory size that uses `size_hi`. lwext4 refused the
feature, and its depth check, its two-element path arrays and its split
code (which handled only a full root and a full level-one node, ENOSPC
otherwise) could not read or grow such a tree. (2) A defect WITHOUT
largedir, on every metadata_csum filesystem: `ext4_dir_dx_split_index`
gave a new index node the limit (block - 8) / 8, forgetting the 8-byte
checksum tail, while `ext4_dir_dx_get_leaf` checks every node against
(block - 16) / 8 -- so the first index node lwext4 split was refused by
lwext4 itself on the next walk (it then clears the directory's index flag
and searches linearly), and the Linux driver reports "dx entry: limit
mismatch". One error exit of `ext4_dir_dx_add_entry` also released a block
it had never taken.

**Shown:** lwext4 filled an empty large_dir image's directory with 70 000
entries of 250-byte names; `e2fsck -fn` found the three-level tree clean.
After `e2fsck -D` re-indexed it, lwext4 read it back (lookups at both ends,
a missing name, the full listing), added 3 000 directories through index
splits at depth, removed 1 500, and `e2fsck` was clean again.

**The fix:** largedir joins the supported set; `EXT4_HTREE_LEVEL_COMPAT`
(2) and `EXT4_HTREE_LEVEL` (3) and `ext4_dir_dx_htree_level()` as the Linux
driver has them size the depth check and both path arrays; a directory's
size takes `size_hi` with largedir; `ext4_dir_dx_split_index` is rewritten
on the driver's method -- walk up from the full leaf index node to the
nearest ancestor with room, split the node below it, or add a level under
the root when all are full and the tree may grow (ENOSPC before anything is
allocated when it may not) -- and returns EAGAIN, so
`ext4_dir_dx_add_entry` releases its path and walks the tree again. A new
node's limit allows for the checksum tail.

**Upstream:** not fixed in master -- master still has no largedir, and its
split still computes the limit before subtracting the tail. 1 of its 9
hunks needs adapting to master.

<!-- hunks 5 -->
```diff
--- a/include/ext4_dir_idx.h
+++ b/include/ext4_dir_idx.h
@@ -64,4 +64,12 @@ struct ext4_dir_idx_block {
 #define EXT4_DIR_DX_INIT_BCNT 2
 
+/* ext4edit patch 5: how deep an htree may be, as the Linux driver counts it
+ * -- the root plus the index levels below it.  Without largedir the tree is
+ * the root and one level of index nodes (indirect_levels 0 or 1); with it a
+ * second level of index nodes is allowed (indirect_levels up to 2).  The
+ * path arrays in ext4_dir_idx.c are sized for the larger figure. */
+#define EXT4_HTREE_LEVEL_COMPAT 2
+#define EXT4_HTREE_LEVEL 3
+
 
 /**@brief Initialize index structure of new directory.
--- a/src/ext4_dir_idx.c
+++ b/src/ext4_dir_idx.c
@@ -202,7 +202,38 @@ struct ext4_dx_sort_entry {
 };
 
+/**@brief ext4edit patch 5: how many levels an htree on this filesystem may
+ *        have, the root counted -- the Linux driver's ext4_dir_htree_level.
+ * @param sb Superblock
+ * @return EXT4_HTREE_LEVEL with largedir, EXT4_HTREE_LEVEL_COMPAT without
+ */
+static uint8_t ext4_dir_dx_htree_level(struct ext4_sblock *sb)
+{
+	if (ext4_sb_feature_incom(sb, EXT4_FINCOM_LARGEDIR))
+		return EXT4_HTREE_LEVEL;
+
+	return EXT4_HTREE_LEVEL_COMPAT;
+}
+
 static int ext4_dir_dx_hash_string(struct ext4_hash_info *hinfo, int len,
 				   const char *name)
 {
+	/* ext4edit patch 8: in a casefolded directory the hash is taken over
+	 * the name's FOLDED form, as the Linux driver's ext4fs_dirhash takes
+	 * it; a name that cannot be folded (not valid UTF-8, or too long once
+	 * folded) is hashed raw, as the driver hashes it.  Every hash of this
+	 * directory's names -- the lookup, the insert, and the re-hash of each
+	 * entry when a leaf is split -- comes through here, so all agree. */
+	if (hinfo->casefold) {
+		char folded[EXT4_CASEFOLD_FOLD_MAX];
+		int n;
+
+		n = ext4_casefold_fold(hinfo->casefold, name, (size_t)len,
+				       folded, sizeof(folded));
+		if (n >= 0)
+			return ext2_htree_hash(folded, n, hinfo->seed,
+					       hinfo->hash_version,
+					       &hinfo->hash, &hinfo->minor_hash);
+	}
+
 	return ext2_htree_hash(name, len, hinfo->seed, hinfo->hash_version,
 			       &hinfo->hash, &hinfo->minor_hash);
@@ -489,6 +520,8 @@ static int ext4_dir_hinfo_init(struct ext4_hash_info *hinfo,
 		return EXT4_ERR_BAD_DX_DIR;
 
-	/* Check indirect levels */
-	if (root->info.indirect_levels > 1)
+	/* Check indirect levels.  ext4edit patch 5: the limit depends on
+	 * largedir -- one level of index nodes below the root without it, two
+	 * with it (the Linux driver's ext4_dir_htree_level). */
+	if (root->info.indirect_levels >= ext4_dir_dx_htree_level(sb))
 		return EXT4_ERR_BAD_DX_DIR;
 
@@ -749,8 +782,9 @@ int ext4_dir_dx_find_entry(struct ext4_dir_search_result *result,
 
 	/*
-	 * Hardcoded number 2 means maximum height of index tree,
-	 * specified in the Linux driver.
+	 * The path from the root to the leaf index node: as deep as the
+	 * deepest tree the driver accepts (ext4edit patch 5: three with
+	 * largedir, where this said "hardcoded number 2").
 	 */
-	struct ext4_dir_idx_block dx_blocks[2];
+	struct ext4_dir_idx_block dx_blocks[EXT4_HTREE_LEVEL];
 	struct ext4_dir_idx_block *dx_block;
 	struct ext4_dir_idx_block *tmp;
@@ -1108,169 +1142,169 @@ static int ext4_dir_dx_split_data(struct ext4_inode_ref *inode_ref,
 }
 
-/**@brief  Split index node and maybe some parent nodes in the tree hierarchy.
- * @param inode_ref Directory i-node
- * @param dx_blocks Array with path from root to leaf node
- * @param dx_block  Leaf block to be split if needed
- * @return Error code
+/**@brief  Make room in the leaf index node for one more entry.
+ *
+ * ext4edit patch 5: rewritten for a tree of any depth the driver accepts
+ * (three levels with largedir).  As published it handled two shapes only --
+ * a full root, or a full level-one node whose parent is the root -- and
+ * returned ENOSPC for everything else; it also sized a new node's limit
+ * WITHOUT the checksum tail on a metadata_csum filesystem, so every node it
+ * split was refused by ext4_dir_dx_get_leaf (and by the Linux driver, "limit
+ * mismatch") the next time it was walked.
+ *
+ * The method is the Linux driver's: from the full leaf index node walk up to
+ * the nearest ancestor with room.  If one exists, split the node below it in
+ * two and hand the right half's first hash to that ancestor.  If none does --
+ * every node up to the root is full -- push the root's entries down into a
+ * new node so the tree gains a level, unless it is already as deep as the
+ * filesystem allows.  Either change alters the path the caller walked, so
+ * the caller is told to walk it again rather than being handed a patched-up
+ * path.
+ *
+ * @param ino_ref   Directory i-node
+ * @param dx_blks   Array with the path from the root to the leaf index node
+ * @param dxb       The leaf index node (the last element of the path)
+ * @return EOK when the leaf index node already has room (the path is
+ *         unchanged); EAGAIN when the tree changed shape and the caller must
+ *         release the path and walk it again; ENOSPC when every node up to
+ *         the root is full and the tree is as deep as it may be; another
+ *         error code on failure.
  */
 static int
 ext4_dir_dx_split_index(struct ext4_inode_ref *ino_ref,
 			struct ext4_dir_idx_block *dx_blks,
-			struct ext4_dir_idx_block *dxb,
-			struct ext4_dir_idx_block **new_dx_block)
+			struct ext4_dir_idx_block *dxb)
 {
 	struct ext4_sblock *sb = &ino_ref->fs->sb;
+	struct ext4_dir_idx_block *at;
+	struct ext4_dir_idx_climit *climit;
+	struct ext4_dir_idx_root *root;
+	struct ext4_dir_idx_node *new_node;
+	struct ext4_dir_idx_entry *new_en;
 	struct ext4_dir_idx_entry *e;
+	struct ext4_block b;
+	ext4_fsblk_t new_fblk;
+	uint32_t new_iblk;
+	uint32_t block_size;
+	uint32_t entry_space;
+	uint32_t node_limit;
+	uint32_t count;
+	uint32_t count_left;
+	uint32_t count_right;
+	uint32_t hash_right;
 	int r;
 
-	uint32_t block_size = ext4_sb_get_block_size(&ino_ref->fs->sb);
-	uint32_t entry_space = block_size - sizeof(struct ext4_fake_dir_entry);
-	uint32_t node_limit =  entry_space / sizeof(struct ext4_dir_idx_entry);
+	/* Room in the leaf index node: nothing to do. */
+	climit = (struct ext4_dir_idx_climit *)dxb->entries;
+	if (ext4_dir_dx_climit_get_count(climit) <
+	    ext4_dir_dx_climit_get_limit(climit))
+		return EOK;
 
-	bool meta_csum = ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM);
+	/* The limit of a NEW index node: a block less the fake directory
+	 * entry that heads it and, with metadata_csum, less the tail that
+	 * holds its checksum -- the same figure ext4_dir_dx_get_leaf checks
+	 * every node against. */
+	block_size = ext4_sb_get_block_size(sb);
+	entry_space = block_size - sizeof(struct ext4_fake_dir_entry);
+	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM))
+		entry_space -= sizeof(struct ext4_dir_idx_tail);
+	node_limit = entry_space / sizeof(struct ext4_dir_idx_entry);
 
-	if (dxb == dx_blks)
-		e = ((struct ext4_dir_idx_root *)dxb->b.data)->en;
-	else
-		e = ((struct ext4_dir_idx_node *)dxb->b.data)->entries;
+	/* Walk up from the full leaf index node to the nearest ancestor with
+	 * room; `at` ends on the node that will be split, or on the root when
+	 * everything above is full as well. */
+	at = dxb;
+	while (at > dx_blks) {
+		climit = (struct ext4_dir_idx_climit *)(at - 1)->entries;
+		if (ext4_dir_dx_climit_get_count(climit) <
+		    ext4_dir_dx_climit_get_limit(climit))
+			break;
 
-	struct ext4_dir_idx_climit *climit = (struct ext4_dir_idx_climit *)e;
-
-	uint16_t leaf_limit = ext4_dir_dx_climit_get_limit(climit);
-	uint16_t leaf_count = ext4_dir_dx_climit_get_count(climit);
-
-	/* Check if is necessary to split index block */
-	if (leaf_limit == leaf_count) {
-		struct ext4_dir_idx_entry *ren;
-		ptrdiff_t levels = dxb - dx_blks;
-
-		ren = ((struct ext4_dir_idx_root *)dx_blks[0].b.data)->en;
-		struct ext4_dir_idx_climit *rclimit = (void *)ren;
-		uint16_t root_limit = ext4_dir_dx_climit_get_limit(rclimit);
-		uint16_t root_count = ext4_dir_dx_climit_get_count(rclimit);
-
-
-		/* Linux limitation */
-		if ((levels > 0) && (root_limit == root_count))
-			return ENOSPC;
-
-		/* Add new block to directory */
-		ext4_fsblk_t new_fblk;
-		uint32_t new_iblk;
-		r = ext4_fs_append_inode_dblk(ino_ref, &new_fblk, &new_iblk);
-		if (r != EOK)
-			return r;
-
-		/* load new block */
-		struct ext4_block b;
-		r = ext4_trans_block_get_noread(ino_ref->fs->bdev, &b, new_fblk);
-		if (r != EOK)
-			return r;
-
-		struct ext4_dir_idx_node *new_node = (void *)b.data;
-		struct ext4_dir_idx_entry *new_en = new_node->entries;
-
-		memset(&new_node->fake, 0, sizeof(struct ext4_fake_dir_entry));
-		new_node->fake.entry_length = block_size;
-
-		/* Split leaf node */
-		if (levels > 0) {
-			uint32_t count_left = leaf_count / 2;
-			uint32_t count_right = leaf_count - count_left;
-			uint32_t hash_right;
-			size_t sz;
-
-			struct ext4_dir_idx_climit *left_climit;
-			struct ext4_dir_idx_climit *right_climit;
-
-			hash_right = ext4_dir_dx_entry_get_hash(e + count_left);
-			/* Copy data to new node */
-			sz = count_right * sizeof(struct ext4_dir_idx_entry);
-			memcpy(new_en, e + count_left, sz);
-
-			/* Initialize new node */
-			left_climit = (struct ext4_dir_idx_climit *)e;
-			right_climit = (struct ext4_dir_idx_climit *)new_en;
-
-			ext4_dir_dx_climit_set_count(left_climit, count_left);
-			ext4_dir_dx_climit_set_count(right_climit, count_right);
-
-			if (meta_csum)
-				entry_space -= sizeof(struct ext4_dir_idx_tail);
-
-			ext4_dir_dx_climit_set_limit(right_climit, node_limit);
-
-			/* Which index block is target for new entry */
-			uint32_t position_index =
-			    (dxb->position - dxb->entries);
-			if (position_index >= count_left) {
-				ext4_dir_set_dx_csum(
-						ino_ref,
-						(struct ext4_dir_en *)
-						dxb->b.data);
-				ext4_trans_set_block_dirty(dxb->b.buf);
-
-				struct ext4_block block_tmp = dxb->b;
-
-				dxb->b = b;
-
-				dxb->position =
-				    new_en + position_index - count_left;
-				dxb->entries = new_en;
-
-				b = block_tmp;
-			}
-
-			/* Finally insert new entry */
-			ext4_dir_dx_insert_entry(ino_ref, dx_blks, hash_right,
-						 new_iblk);
-			ext4_dir_set_dx_csum(ino_ref, (void*)dx_blks[0].b.data);
-			ext4_dir_set_dx_csum(ino_ref, (void*)dx_blks[1].b.data);
-			ext4_trans_set_block_dirty(dx_blks[0].b.buf);
-			ext4_trans_set_block_dirty(dx_blks[1].b.buf);
-
-			ext4_dir_set_dx_csum(ino_ref, (void *)b.data);
-			ext4_trans_set_block_dirty(b.buf);
-			return ext4_block_set(ino_ref->fs->bdev, &b);
-		} else {
-			size_t sz;
-			/* Copy data from root to child block */
-			sz = leaf_count * sizeof(struct ext4_dir_idx_entry);
-			memcpy(new_en, e, sz);
-
-			struct ext4_dir_idx_climit *new_climit = (void*)new_en;
-			if (meta_csum)
-				entry_space -= sizeof(struct ext4_dir_idx_tail);
-
-			ext4_dir_dx_climit_set_limit(new_climit, node_limit);
-
-			/* Set values in root node */
-			struct ext4_dir_idx_climit *new_root_climit = (void *)e;
-
-			ext4_dir_dx_climit_set_count(new_root_climit, 1);
-			ext4_dir_dx_entry_set_block(e, new_iblk);
-
-			struct ext4_dir_idx_root *r = (void *)dx_blks[0].b.data;
-			r->info.indirect_levels = 1;
-
-			/* Add new entry to the path */
-			dxb = dx_blks + 1;
-			dxb->position = dx_blks->position - e + new_en;
-			dxb->entries = new_en;
-			dxb->b = b;
-			*new_dx_block = dxb;
-
-			ext4_dir_set_dx_csum(ino_ref, (void*)dx_blks[0].b.data);
-			ext4_dir_set_dx_csum(ino_ref, (void*)dx_blks[1].b.data);
-			ext4_trans_set_block_dirty(dx_blks[0].b.buf);
-			ext4_trans_set_block_dirty(dx_blks[1].b.buf);
-		}
+		at--;
 	}
 
-	return EOK;
+	root = (struct ext4_dir_idx_root *)dx_blks[0].b.data;
+
+	/* Full to the root and already as deep as allowed: the directory
+	 * cannot take the entry.  Decided before any block is appended, so a
+	 * refusal leaves the directory exactly as it was. */
+	if (at == dx_blks &&
+	    root->info.indirect_levels + 1 >= ext4_dir_dx_htree_level(sb))
+		return ENOSPC;
+
+	/* One new block: the right half of a split node, or the node the
+	 * root's entries move down into. */
+	r = ext4_fs_append_inode_dblk(ino_ref, &new_fblk, &new_iblk);
+	if (r != EOK)
+		return r;
+
+	r = ext4_trans_block_get_noread(ino_ref->fs->bdev, &b, new_fblk);
+	if (r != EOK)
+		return r;
+
+	new_node = (struct ext4_dir_idx_node *)b.data;
+	new_en = new_node->entries;
+	memset(&new_node->fake, 0, sizeof(struct ext4_fake_dir_entry));
+	new_node->fake.entry_length = block_size;
+
+	if (at == dx_blks) {
+		/* Add a level: every entry of the root moves to the new node,
+		 * and the root keeps one entry pointing at it.  The count rides
+		 * along in the copied first entry; only the limit is rewritten,
+		 * since a node holds more entries than the root does. */
+		e = root->en;
+		climit = (struct ext4_dir_idx_climit *)e;
+		count = ext4_dir_dx_climit_get_count(climit);
+		memcpy(new_en, e, count * sizeof(struct ext4_dir_idx_entry));
+		ext4_dir_dx_climit_set_limit((struct ext4_dir_idx_climit *)new_en,
+					     node_limit);
+
+		ext4_dir_dx_climit_set_count(climit, 1);
+		ext4_dir_dx_entry_set_block(e, new_iblk);
+		root->info.indirect_levels++;
+
+		ext4_dir_set_dx_csum(ino_ref, (void *)dx_blks[0].b.data);
+		ext4_trans_set_block_dirty(dx_blks[0].b.buf);
+	} else {
+		/* Split the node at `at`: the upper half of its entries moves to
+		 * the new node, whose first entry becomes that node's catch-all
+		 * (its hash slot now holds the limit and count), and the hash it
+		 * carried goes to the parent as the new node's lower bound. */
+		e = at->entries;
+		climit = (struct ext4_dir_idx_climit *)e;
+		count = ext4_dir_dx_climit_get_count(climit);
+		count_left = count / 2;
+		count_right = count - count_left;
+		hash_right = ext4_dir_dx_entry_get_hash(e + count_left);
+
+		memcpy(new_en, e + count_left,
+		       count_right * sizeof(struct ext4_dir_idx_entry));
+		ext4_dir_dx_climit_set_count(climit, count_left);
+		ext4_dir_dx_climit_set_count((struct ext4_dir_idx_climit *)new_en,
+					     count_right);
+		ext4_dir_dx_climit_set_limit((struct ext4_dir_idx_climit *)new_en,
+					     node_limit);
+
+		ext4_dir_set_dx_csum(ino_ref, (void *)at->b.data);
+		ext4_trans_set_block_dirty(at->b.buf);
+
+		/* The parent has room (that is how `at` was chosen); the entry
+		 * goes in right after the one that points at `at`'s block.  This
+		 * writes the parent's checksum and marks it dirty. */
+		ext4_dir_dx_insert_entry(ino_ref, at - 1, hash_right, new_iblk);
+	}
+
+	ext4_dir_set_dx_csum(ino_ref, (void *)b.data);
+	ext4_trans_set_block_dirty(b.buf);
+	r = ext4_block_set(ino_ref->fs->bdev, &b);
+	if (r != EOK)
+		return r;
+
+	/* The tree changed shape under the caller's path. */
+	return EAGAIN;
 }
 
 int ext4_dir_dx_add_entry(struct ext4_inode_ref *parent,
-			  struct ext4_inode_ref *child, const char *name)
+			  struct ext4_inode_ref *child, const char *name,
+			  uint32_t name_len)
 {
 	int rc2 = EOK;
@@ -1308,16 +1342,50 @@ int ext4_dir_dx_add_entry(struct ext4_inode_ref *parent,
 
 	/*
-	 * Hardcoded number 2 means maximum height of index
-	 * tree defined in Linux.
+	 * The path from the root to the leaf index node: as deep as the
+	 * deepest tree the driver accepts (ext4edit patch 5: three with
+	 * largedir, where this said "hardcoded number 2").
 	 */
-	struct ext4_dir_idx_block dx_blks[2];
+	struct ext4_dir_idx_block dx_blks[EXT4_HTREE_LEVEL];
 	struct ext4_dir_idx_block *dx_blk;
 	struct ext4_dir_idx_block *dx_it;
 
+again:
 	r = ext4_dir_dx_get_leaf(&hinfo, parent, &root_blk, &dx_blk, dx_blks);
 	if (r != EOK) {
-		r = EXT4_ERR_BAD_DX_DIR;
+		/* ext4edit patch 30 (upstream #133): the failed walk has
+		 * released every block it held; dx_blk was never set, so the
+		 * release_index loop below must not run. */
+		return EXT4_ERR_BAD_DX_DIR;
+	}
+
+	/*
+	 * ext4edit patch 5: make room in the leaf index node BEFORE the
+	 * leaf data block is looked up.  A split or a new level changes the
+	 * shape of the tree, and the path just walked no longer describes
+	 * it, so EAGAIN means: put the path back and walk it again from the
+	 * root, as the Linux driver's "restart" does.  As published the
+	 * split ran after the leaf was looked up and, on failure, released a
+	 * target block that had never been taken.
+	 */
+	r = ext4_dir_dx_split_index(parent, dx_blks, dx_blk);
+	if (r == EAGAIN) {
+		dx_it = dx_blks;
+		while (dx_it <= dx_blk) {
+			r = ext4_block_set(fs->bdev, &dx_it->b);
+			if (r != EOK)
+				return r;
+
+			dx_it++;
+		}
+
+		r = ext4_trans_block_get(fs->bdev, &root_blk, rblock_addr);
+		if (r != EOK)
+			return r;
+
+		goto again;
+	}
+
+	if (r != EOK)
 		goto release_index;
-	}
 
 	/* Try to insert to existing data block */
@@ -1328,12 +1396,4 @@ int ext4_dir_dx_add_entry(struct ext4_inode_ref *parent,
 	if (r != EOK)
 		goto release_index;
-
-	/*
-	 * Check if there is needed to split index node
-	 * (and recursively also parent nodes)
-	 */
-	r = ext4_dir_dx_split_index(parent, dx_blks, dx_blk, &dx_blk);
-	if (r != EOK)
-		goto release_target_index;
 
 	struct ext4_block target_block;
--- a/src/ext4_inode.c
+++ b/src/ext4_inode.c
@@ -100,6 +100,11 @@ uint64_t ext4_inode_get_size(struct ext4_sblock *sb, struct ext4_inode *inode)
 	uint64_t v = to_le32(inode->size_lo);
 
+	/* ext4edit patch 5: with largedir a DIRECTORY's size is 64-bit too
+	 * (the Linux driver's ext4_isize); without it size_hi of a directory
+	 * is the ancient dir_acl field and must be ignored, as before. */
 	if ((ext4_get32(sb, rev_level) > 0) &&
-	    (ext4_inode_is_type(sb, inode, EXT4_INODE_MODE_FILE)))
+	    (ext4_inode_is_type(sb, inode, EXT4_INODE_MODE_FILE) ||
+	     (ext4_sb_feature_incom(sb, EXT4_FINCOM_LARGEDIR) &&
+	      ext4_inode_is_type(sb, inode, EXT4_INODE_MODE_DIRECTORY))))
 		v |= ((uint64_t)to_le32(inode->size_hi)) << 32;
 
```

- Also this patch's: the hunk of `include/ext4_types.h` at upstream line 279, shown under patch 3.
<!-- /hunks -->

### Patch 6 -- a read-only session dirties block groups, and a failed block get crashes the next release

**Where:** `src/ext4_fs.c` (`ext4_fs_get_block_group_ref`),
`src/ext4_blockdev.c` (`ext4_block_get_noread`), `src/ext4_extent.c`
(`read_extent_tree_block`).

**The defect:** `ext4_fs_get_block_group_ref` initialises an uninitialised
group's bitmaps (and zeroes its inode table) the first time the group is
touched -- on a READ-ONLY mount too -- leaving the bitmap and descriptor
blocks dirty in the cache, where a read-only device can never flush them.
Once they fill the cache, a block get has to shake it, the flush fails, and
`ext4_block_get_noread` had already set the caller's `lb_id` with no
buffer; `read_extent_tree_block` then released "what it got" on the
strength of `lb_id`, and `ext4_block_set`'s assertion on the missing buffer
spins for ever.

**Shown:** a read-only session that looked at files in two uninitialised
groups, then looked up names through a three-level directory with an
extent index block, spun at 100 % CPU on the fifth lookup (found with a
debugger). Patched, the lookups pass.

**The fix:** a read-only filesystem returns before the lazy
initialisation (the bitmaps are only for allocation); a failed get clears
`lb_id`, `buf` and `data`; `read_extent_tree_block` releases a block only
when it holds a buffer.

**Upstream:** not fixed in master; applies to master.

<!-- hunks 6 -->
```diff
--- a/src/ext4_blockdev.c
+++ b/src/ext4_blockdev.c
@@ -225,14 +225,27 @@ int ext4_block_get_noread(struct ext4_blockdev *bdev, struct ext4_block *b,
 	r = ext4_block_cache_shake(bdev);
 	if (r != EOK)
-		return r;
+		goto fail;
 
 	r = ext4_bcache_alloc(bdev->bc, b, &is_new);
 	if (r != EOK)
-		return r;
+		goto fail;
 
-	if (!b->data)
-		return ENOMEM;
+	if (!b->data) {
+		r = ENOMEM;
+		goto fail;
+	}
 
 	return EOK;
+
+fail:
+	/* ext4edit patch 6: a failed get hands back a block that holds
+	 * NOTHING.  As published lb_id was left set with buf NULL, and a
+	 * caller that releases "whatever it got" (read_extent_tree_block
+	 * tested lb_id) then called ext4_block_set on it, whose assertion on
+	 * the missing buffer loops for ever. */
+	b->lb_id = 0;
+	b->buf = NULL;
+	b->data = NULL;
+	return r;
 }
 
--- a/src/ext4_extent.c
+++ b/src/ext4_extent.c
@@ -454,5 +454,8 @@ static int read_extent_tree_block(struct ext4_inode_ref *inode_ref,
 	return EOK;
 errout:
-	if (bh->lb_id)
+	/* ext4edit patch 6: release the block only when one is actually
+	 * held -- the buffer says so; the block number alone did not, since a
+	 * failed get used to leave it set (see ext4_block_get_noread). */
+	if (bh->buf)
 		ext4_block_set(inode_ref->fs->bdev, bh);
 
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -598,4 +598,17 @@ int ext4_fs_get_block_group_ref(struct ext4_fs *fs, uint32_t bgid,
 	}
 
+	/* ext4edit patch 6: a READ-ONLY session leaves an uninitialised
+	 * group as it is.  The bitmaps it would build here are consulted only
+	 * by allocation, which a read-only session never does, and as
+	 * published the initialisation wrote the bitmap block and this
+	 * descriptor block into the cache as DIRTY -- which a read-only block
+	 * device can never flush.  Once enough of those sat in the cache (two
+	 * groups were enough, with the default eight buffers), the next block
+	 * get had to shake the cache, the flush failed with EROFS, and the
+	 * session died in a looping assertion (see ext4_block_get_noread).
+	 * The Linux driver likewise initialises nothing on a read-only mount. */
+	if (fs->read_only)
+		return EOK;
+
 	if (ext4_bg_has_flag(bg, EXT4_BLOCK_GROUP_BLOCK_UNINIT)) {
 		rc = ext4_fs_init_block_bitmap(ref);
```
<!-- /hunks -->

### Patch 7 -- `inline_data` is refused (a feature added)

**Where:** the new `include/ext4_inline.h` and `src/ext4_inline.c`, with
hooks in `ext4_types.h`, `ext4_dir.h`, `ext4_dir.c`, `ext4.c` and
`ext4_fs.c`.

**The defect:** `mke2fs -O inline_data` keeps small files and directories
inside the inode -- 60 bytes in `i_block`, the rest in the inode body's
"system.data" attribute. lwext4 refuses the feature; were it allowed
without this patch, every block-map walk would read `i_block` as block
numbers: a read would return garbage and a remove would FREE whatever
blocks the content's bytes named.

**The design:** inline content is read in place; an inline directory is
laid out in memory as the one block it would become, so the lookup,
listing and emptiness code run over it unchanged; any write first converts
the inode to a block-backed one, as the Linux driver does when inline
content outgrows the inode. Nothing is ever written inline. The block is
allocated and filled while the inode is still inline, and attached only
then, so a full filesystem or an I/O error cannot leave the inode emptied.

**Shown:** an image made with `mke2fs -O inline_data -I 256` holding files
of 0, 10, 60 and 120 bytes and a three-file directory: sizes, lookups, the
listing and exact reads; then a rename, a create and a mkdir in the
directory, the 120-byte file overwritten with 200 bytes, the 60-byte file
removed -- `e2fsck -fn` clean, the untouched file still inline. Damaged
inline inodes were refused rather than overrun: a size of 2^64 - 1 no
longer wraps the buffer's allocation, an inline directory's entries are
held to the block, and a name added to an inline directory on a full
filesystem leaves its entries whole.

**The fix:** as designed above. Dropping an inline inode whose
"system.data" is missing is allowed (lwext4's removal answers ENOENT for
it, not ENODATA), so a damaged inode can be removed.

**Upstream:** not fixed in master (master lists inline_data among the
features to implement). 1 of its 21 hunks needs adapting to master.

<!-- hunks 7 -->
```diff
--- a/include/ext4_dir.h
+++ b/include/ext4_dir.h
@@ -60,4 +60,5 @@ struct ext4_dir_iter {
 	uint64_t curr_off;
 	struct ext4_dir_en *curr;
+	uint8_t *inline_blk;	/* ext4edit patch 7: an inline directory laid out as one block (malloc'd), else NULL */
 };
 
@@ -65,4 +66,5 @@ struct ext4_dir_search_result {
 	struct ext4_block block;
 	struct ext4_dir_en *dentry;
+	uint8_t *inline_blk;	/* ext4edit patch 7: the laid-out block the entry was found in (malloc'd), else NULL */
 };
 
--- a/include/ext4_types.h
+++ b/include/ext4_types.h
@@ -461,4 +461,6 @@ struct ext4_inode {
 #define EXT4_INODE_FLAG_EA_INODE 0x00200000  /* Inode used for large EA */
 #define EXT4_INODE_FLAG_EOFBLOCKS 0x00400000 /* Blocks allocated beyond EOF */
+#define EXT4_INODE_FLAG_INLINE_DATA 0x10000000 /* ext4edit patch 7: content is inline (i_block and system.data) */
+#define EXT4_INODE_FLAG_CASEFOLD 0x40000000 /* ext4edit patch 8: a case-insensitive directory (names hashed and compared folded) */
 #define EXT4_INODE_FLAG_RESERVED 0x80000000  /* reserved for ext4 lib */
 
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -47,4 +47,6 @@
 #include "ext4_fs.h"
 #include "ext4_dir.h"
+#include "ext4_inline.h"
+#include "ext4_casefold.h"	/* ext4edit patch 8 */
 #include "ext4_inode.h"
 #include "ext4_super.h"
@@ -250,4 +252,11 @@ static int ext4_link(struct ext4_mountpoint *mp, struct ext4_inode_ref *parent,
 		idx = ext4_inode_has_flag(ch->inode, EXT4_INODE_FLAG_INDEX);
 		struct ext4_dir_search_result res;
+
+		/* ext4edit patch 7: ".." is rewritten in place below, which
+		 * an inline directory does not even store -- convert first. */
+		r = ext4_inline_convert(ch);
+		if (r != EOK)
+			return r;
+
 		if (!idx) {
 			r = ext4_dir_find_entry(&res, ch, "..", strlen(".."));
@@ -1699,4 +1708,29 @@ int ext4_fread(ext4_file *f, void *buf, size_t size, size_t *rcnt)
 
 		r = EOK;
+		goto Finish;
+	}
+
+	/* ext4edit patch 7: an inline file's content comes out of the inode,
+	 * not from blocks; `size` is already clamped to what is left. */
+	if (ext4_inline_has(sb, ref.inode)) {
+		uint8_t *inline_data;
+		size_t inline_size;
+
+		r = ext4_inline_read(&ref, &inline_data, &inline_size);
+		if (r != EOK)
+			goto Finish;
+
+		if (f->fpos < inline_size) {
+			size_t len = size;
+			if (len > inline_size - f->fpos)
+				len = inline_size - f->fpos;
+
+			memcpy(buf, inline_data + f->fpos, len);
+			f->fpos += len;
+			if (rcnt)
+				*rcnt = len;
+		}
+
+		free(inline_data);
 		goto Finish;
 	}
@@ -1834,4 +1868,10 @@ int ext4_fwrite(ext4_file *f, const void *buf, size_t size, size_t *wcnt)
 		return r;
 	}
+
+	/* ext4edit patch 7: a write goes to blocks, so an inline file gets
+	 * its block first (its content and size are kept). */
+	r = ext4_inline_convert(&ref);
+	if (r != EOK)
+		goto Finish;
 
 	/*Sync file size*/
--- a/src/ext4_dir.c
+++ b/src/ext4_dir.c
@@ -48,4 +48,6 @@
 #include "ext4_trans.h"
 #include "ext4_dir.h"
+#include "ext4_inline.h"
+#include "ext4_casefold.h"	/* ext4edit patch 8 */
 #include "ext4_dir_idx.h"
 #include "ext4_crc32.h"
@@ -54,4 +56,5 @@
 
 #include <string.h>
+#include <stdlib.h>	/* ext4edit patch 7: free() of a laid-out inline directory */
 
 /****************************************************************************/
@@ -206,4 +209,30 @@ static int ext4_dir_iterator_seek(struct ext4_dir_iter *it, uint64_t pos)
 	/* The iterator is not valid until we seek to the desired position */
 	it->curr = NULL;
+
+	/* ext4edit patch 7: an inline directory is served from the block it
+	 * would become, laid out once per iterator and freed by _fini.  It is
+	 * one block at logical 0 with no cache block behind it (lb_id stays 0,
+	 * so nothing below ever tries to release it), and its end is the block
+	 * size, not the inode's inline size. */
+	if (ext4_inline_has(sb, inode)) {
+		uint32_t inline_bsize = ext4_sb_get_block_size(sb);
+
+		if (!it->inline_blk) {
+			r = ext4_inline_dir_block(it->inode_ref, &it->inline_blk);
+			if (r != EOK)
+				return r;
+		}
+
+		if (pos >= inline_bsize) {
+			it->curr_off = pos;
+			return EOK;
+		}
+
+		it->curr_blk.lb_id = 0;
+		it->curr_blk.buf = NULL;
+		it->curr_blk.data = it->inline_blk;
+		it->curr_off = pos;
+		return ext4_dir_iterator_set(it, inline_bsize);
+	}
 
 	/* Are we at the end? */
@@ -264,4 +293,5 @@ int ext4_dir_iterator_init(struct ext4_dir_iter *it,
 	it->curr_off = 0;
 	it->curr_blk.lb_id = 0;
+	it->inline_blk = NULL;	/* ext4edit patch 7 */
 
 	return ext4_dir_iterator_seek(it, pos);
@@ -291,4 +321,11 @@ int ext4_dir_iterator_fini(struct ext4_dir_iter *it)
 	it->curr = 0;
 
+	/* ext4edit patch 7: the laid-out inline directory, if one was made. */
+	if (it->inline_blk) {
+		free(it->inline_blk);
+		it->inline_blk = NULL;
+		it->curr_blk.data = NULL;
+	}
+
 	if (it->curr_blk.lb_id)
 		return ext4_block_set(it->inode_ref->fs->bdev, &it->curr_blk);
@@ -441,7 +478,42 @@ int ext4_dir_find_entry(struct ext4_dir_search_result *result,
 	struct ext4_sblock *sb = &parent->fs->sb;
 
+	/* ext4edit patch 8: the folding table when this directory is
+	 * casefolded, NULL otherwise; the index search reads the same thing
+	 * for itself, the two linear searches below take it from here. */
+	const struct ext2fs_nls_table *casefold = ext4_casefold_dir(parent);
+
 	/* Entry clear */
 	result->block.lb_id = 0;
 	result->dentry = NULL;
+	result->inline_blk = NULL;
+
+	/* ext4edit patch 7: an inline directory is searched in the block it
+	 * would become, which the result then owns (ext4_dir_destroy_result
+	 * frees it).  The entry found points into that memory, so a caller
+	 * that would WRITE through it converts the directory first (see
+	 * ext4_dir_remove_entry and ext4_link). */
+	if (ext4_inline_has(sb, parent->inode)) {
+		struct ext4_block fake;
+		struct ext4_dir_en *de;
+		uint8_t *blk;
+
+		r = ext4_inline_dir_block(parent, &blk);
+		if (r != EOK)
+			return r;
+
+		memset(&fake, 0, sizeof(fake));
+		fake.data = blk;
+		r = ext4_dir_find_in_block(&fake, sb, casefold, name_len, name,
+					   &de);
+		if (r == EOK) {
+			result->block = fake;
+			result->dentry = de;
+			result->inline_blk = blk;
+			return EOK;
+		}
+
+		free(blk);
+		return r;
+	}
 
 #if CONFIG_DIR_INDEX_ENABLE
@@ -521,15 +593,70 @@ int ext4_dir_remove_entry(struct ext4_inode_ref *parent, const char *name,
 		return ENOTDIR;
 
-	/* Try to find entry */
-	struct ext4_dir_search_result result;
-	int rc = ext4_dir_find_entry(&result, parent, name, name_len);
+	/* ext4edit patch 7: the entry is invalidated in place below, which
+	 * must land in a real block, so an inline directory converts first. */
+	int rc = ext4_inline_convert(parent);
 	if (rc != EOK)
 		return rc;
 
-	/* Invalidate entry */
-	ext4_dir_en_set_inode(result.dentry, 0);
+	/* Try to find entry */
+	struct ext4_dir_search_result result;
+	rc = ext4_dir_find_entry(&result, parent, name, name_len);
+	if (rc != EOK)
+		return rc;
 
 	/* Store entry position in block */
 	uint32_t pos = (uint8_t *)result.dentry - result.block.data;
+
+	/* ext4edit patch 36: the predecessor is FOUND, and the walk to it    */
+	/* checked, BEFORE the entry is touched.  Upstream invalidates the    */
+	/* entry first and then walks by each rec_len unchecked: a 0 never    */
+	/* advances (a spin for ever with no assertion to see it), and a      */
+	/* length that steps over the entry fires the assertion after the     */
+	/* block is already half changed.  Here a walk that does not land     */
+	/* exactly on the entry is damage -- EIO, the block released          */
+	/* unchanged.                                                         */
+	uint32_t block_size;
+	uint32_t offset;
+	struct ext4_dir_en *tmp_de;
+	uint16_t de_len;
+
+	/* The walk's state, before it starts: no predecessor yet. */
+	block_size = ext4_sb_get_block_size(&parent->fs->sb);
+	offset = 0;
+	tmp_de = NULL;
+	de_len = 0;
+
+	if (pos != 0) {
+		/* Not the first entry: walk to its direct predecessor. */
+		tmp_de = (void *)result.block.data;
+		de_len = ext4_dir_en_get_entry_len(tmp_de);
+
+		while (1) {
+			/* One entry before the removed one per iteration. */
+			if (ext4_dir_en_len_bad(de_len, offset, block_size)) {
+				/* Damaged: the walk stops; refused below. */
+				break;
+			}
+			if (offset + de_len >= pos) {
+				/* This one reaches the removed entry. */
+				break;
+			}
+
+			/* Step to the next entry. */
+			offset += de_len;
+			tmp_de = (void *)(result.block.data + offset);
+			de_len = ext4_dir_en_get_entry_len(tmp_de);
+		}
+
+		if (ext4_dir_en_len_bad(de_len, offset, block_size) ||
+		    (offset + de_len != pos)) {
+			/* The walk did not land on the entry: damage. */
+			ext4_dir_destroy_result(parent, &result);
+			return EIO;
+		}
+	}
+
+	/* Invalidate entry */
+	ext4_dir_en_set_inode(result.dentry, 0);
 
 	/*
@@ -686,4 +813,14 @@ int ext4_dir_destroy_result(struct ext4_inode_ref *parent,
 			    struct ext4_dir_search_result *result)
 {
+	/* ext4edit patch 7: a result from an inline directory holds the
+	 * laid-out block, not a cache block. */
+	if (result->inline_blk) {
+		free(result->inline_blk);
+		result->inline_blk = NULL;
+		result->block.data = NULL;
+		result->dentry = NULL;
+		return EOK;
+	}
+
 	if (result->block.lb_id)
 		return ext4_block_set(parent->fs->bdev, &result->block);
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -47,4 +47,6 @@
 #include "ext4_trans.h"
 #include "ext4_fs.h"
+#include "ext4_inline.h"
+#include "ext4_casefold.h"	/* ext4edit patch 8 */
 #include "ext4_blockdev.h"
 #include "ext4_super.h"
@@ -924,4 +926,10 @@ int ext4_fs_free_inode(struct ext4_inode_ref *inode_ref)
 	uint32_t suboff;
 	int rc;
+
+	/* ext4edit patch 7: an inline inode's i_block is content, not block
+	 * pointers; a truncate to 0 has normally dropped the content already,
+	 * and if not there is nothing of it to free from the block bitmaps. */
+	if (ext4_inline_has(&fs->sb, inode_ref->inode))
+		goto finish;
 #if CONFIG_EXTENT_ENABLE
 	/* For extents must be data block destroyed by other way */
@@ -1193,4 +1201,17 @@ int ext4_fs_truncate_inode(struct ext4_inode_ref *inode_ref, uint64_t new_size)
 
 		return EOK;
+	}
+
+	/* ext4edit patch 7: an inline inode has no blocks to release -- and
+	 * i_block is its CONTENT, which the indirect path below would read as
+	 * block numbers and free.  To nothing: drop the inline storage.  To
+	 * something smaller: give it a block first, then shorten that. */
+	if (ext4_inline_has(sb, inode_ref->inode)) {
+		if (new_size == 0)
+			return ext4_inline_drop(inode_ref);
+
+		r = ext4_inline_convert(inode_ref);
+		if (r != EOK)
+			return r;
 	}
 
@@ -1332,4 +1353,10 @@ static int ext4_fs_get_inode_dblk_idx_internal(struct ext4_inode_ref *inode_ref,
 	}
 
+	/* ext4edit patch 7: an inline inode has no block map at all; every
+	 * path that reads or writes one goes through ext4_inline.c first, so
+	 * arriving here is a caller that forgot to. */
+	if (ext4_inline_has(&fs->sb, inode_ref->inode))
+		return ENOTSUP;
+
 	ext4_fsblk_t current_block;
 
```

- Also this patch's: the hunk of `include/ext4_types.h` at upstream line 279, shown under patch 3.
- Also this patch's: the hunk of `src/ext4_dir.c` at upstream line 336, shown under patch 36.
<!-- /hunks -->

### Patch 8 -- `casefold` is refused (a feature added)

**Where:** the new `include/ext4_casefold.h`, `src/ext4_casefold.c` and
`unicode/` (above), with hooks in `ext4_types.h`, `ext4_hash.h`,
`ext4_hash.c`, `ext4_dir_idx.c`, `ext4_dir.h`, `ext4_dir.c`, `ext4_fs.c`
and `ext4.c`.

**The defect:** on a `mke2fs -O casefold` filesystem (Linux 5.2 onward) a
directory with the casefold flag is case-insensitive: the kernel hashes
and compares its names by their FOLDED form (Unicode NFD and full case
folding under the superblock's encoding, UTF-8 12.1), while the entries
keep their bytes. lwext4 refuses the feature; were it allowed without this
patch, a lookup by another case would miss, and an entry added to an
indexed casefolded directory would be hashed by its raw bytes and placed
where the kernel never looks.

**The design:** the folding is e2fsprogs' own code and tables, so the
forms are the kernel's; the new module decides when to fold (the flag on
the directory, the feature and a known encoding on the filesystem) and
compares (the looked-up name folded once per block, each entry folded and
compared byte for byte; a name that is not valid UTF-8 is hashed and
compared raw, the kernel's non-strict fallback). A directory made inside a
casefolded one inherits the flag. A filesystem with an unknown encoding is
refused.

**Shown:** an image with a casefolded `/ci` and an ordinary `/plain`:
lookups by other cases (STRASSE finding the sharp s), the root and `/plain`
still case-sensitive, stored spellings listed; then 300 links put through
`/ci`'s htree and found by any case, creates onto other cases refused, a
rename and a remove by another case. `e2fsck -fn` is CLEAN on the index
lwext4 built -- it checks every entry of a casefolded htree against the
folded hash, so that is the proof the hashes are the kernel's.

**The fix:** as designed above. The superblock gains `encoding` /
`encoding_flags` (with the high time bytes and `orphan_file_inum` before
them, out of the padding); the htree hash takes a folded name up to 4096
bytes, where it capped at 255.

**Upstream:** not fixed in master. 1 of its 26 hunks needs adapting to
master.

<!-- hunks 8 -->
```diff
--- a/include/ext4_dir.h
+++ b/include/ext4_dir.h
@@ -264,7 +264,13 @@ int ext4_dir_try_insert_entry(struct ext4_sblock *sb,
 			      uint32_t name_len);
 
+/* ext4edit patch 8: the folding table is e2fsprogs' type (see ext4_casefold.h). */
+struct ext2fs_nls_table;
+
 /**@brief Try to find entry in block by name.
  * @param block     Block containing entries
  * @param sb        Superblock
+ * @param casefold  ext4edit patch 8: the directory's folding table when it
+ *                  is casefolded (names compared by their folded forms), or
+ *                  NULL for a byte-for-byte compare
  * @param name_len  Length of entry name
  * @param name      Name of entry to be found
@@ -273,4 +279,5 @@ int ext4_dir_try_insert_entry(struct ext4_sblock *sb,
  */
 int ext4_dir_find_in_block(struct ext4_block *block, struct ext4_sblock *sb,
+			   const struct ext2fs_nls_table *casefold,
 			   size_t name_len, const char *name,
 			   struct ext4_dir_en **res_entry);
--- a/include/ext4_hash.h
+++ b/include/ext4_hash.h
@@ -46,4 +46,7 @@ extern "C" {
 #include <stdint.h>
 
+/* ext4edit patch 8: the folding table is e2fsprogs' type (see ext4_casefold.h). */
+struct ext2fs_nls_table;
+
 struct ext4_hash_info {
 	uint32_t hash;
@@ -51,4 +54,5 @@ struct ext4_hash_info {
 	uint32_t hash_version;
 	const uint32_t *seed;
+	const struct ext2fs_nls_table *casefold;	/* ext4edit patch 8: the directory's folding table, or NULL when its names hash as they are */
 };
 
--- a/include/ext4_types.h
+++ b/include/ext4_types.h
@@ -250,4 +250,5 @@ struct ext4_sblock {
 #define EXT4_FINCOM_LARGEDIR 0x4000	 /* >2GB or 3-lvl htree */
 #define EXT4_FINCOM_INLINE_DATA 0x8000      /* data in inode */
+#define EXT4_FINCOM_CASEFOLD 0x20000	 /* ext4edit patch 8: case-insensitive directories (the inode flag EXT4_INODE_FLAG_CASEFOLD marks each) */
 
 /*
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -959,4 +959,12 @@ static int ext4_generic_open2(ext4_file *f, const char *path, int flags,
 				break;
 
+			/* ext4edit patch 8: a DIRECTORY made inside a casefolded
+			 * directory is casefolded too, as the Linux driver makes it
+			 * (EXT4_CASEFOLD_FL is in EXT4_FL_INHERITED, and only a
+			 * directory keeps it); a file or link made there is not. */
+			if ((!is_goal || ftype == EXT4_DE_DIR) &&
+			    ext4_casefold_dir(&ref))
+				ext4_inode_set_flag(child_ref.inode,
+						    EXT4_INODE_FLAG_CASEFOLD);
 
 			/*Link with root dir.*/
--- a/src/ext4_dir.c
+++ b/src/ext4_dir.c
@@ -496,5 +496,6 @@ int ext4_dir_find_entry(struct ext4_dir_search_result *result,
 		/* Try to find entry in block */
 		struct ext4_dir_en *res_entry;
-		r = ext4_dir_find_in_block(&b, sb, name_len, name, &res_entry);
+		r = ext4_dir_find_in_block(&b, sb, casefold, name_len, name,
+					   &res_entry);
 		if (r == EOK) {
 			result->block = b;
@@ -641,4 +642,5 @@ int ext4_dir_try_insert_entry(struct ext4_sblock *sb,
 
 int ext4_dir_find_in_block(struct ext4_block *block, struct ext4_sblock *sb,
+			   const struct ext2fs_nls_table *casefold,
 			   size_t name_len, const char *name,
 			   struct ext4_dir_en **res_entry)
@@ -650,16 +652,49 @@ int ext4_dir_find_in_block(struct ext4_block *block, struct ext4_sblock *sb,
 	uint8_t *addr_limit = block->data + ext4_sb_get_block_size(sb);
 
+	/* ext4edit patch 8: in a casefolded directory the looked-up name is
+	 * folded ONCE here, and each entry is compared against that form by
+	 * ext4_casefold_match (which folds the entry).  A name that cannot be
+	 * folded is compared raw, as the Linux driver compares it on a
+	 * non-strict filesystem; the buffer is the driver's PATH_MAX. */
+	char folded_buf[EXT4_CASEFOLD_FOLD_MAX];
+	const char *folded = NULL;
+	size_t folded_len = 0;
+	if (casefold) {
+		int n = ext4_casefold_fold(casefold, name, name_len, folded_buf,
+					   sizeof(folded_buf));
+		if (n >= 0) {
+			folded = folded_buf;
+			folded_len = (size_t)n;
+		}
+	}
+
 	/* Walk through the block and check entries */
 	while ((uint8_t *)de < addr_limit) {
-		/* Termination condition */
-		if ((uint8_t *)de + name_len > addr_limit)
+		/* Termination condition (ext4edit patch 8: a folded match can
+		 * have a different length, so the raw-length bound applies only
+		 * to the byte compare; the entry's header must fit either way) */
+		if ((uint8_t *)de + sizeof(struct ext4_fake_dir_entry) > addr_limit)
+			break;
+		if (!casefold && (uint8_t *)de + name_len > addr_limit)
 			break;
 
 		/* Valid entry - check it */
 		if (ext4_dir_en_get_inode(de) != 0) {
-			/* For more efficient compare only lengths firstly*/
 			uint16_t el = ext4_dir_en_get_name_len(sb, de);
-			if (el == name_len) {
-				/* Compare names */
+			if (casefold) {
+				/* ext4edit patch 8: compare the folded forms -- of
+				 * a name that lies inside the block */
+				if ((uint8_t *)de->name + el > addr_limit)
+					break;
+				if (ext4_casefold_match(casefold, name, name_len,
+							folded, folded_len,
+							(const char *)de->name,
+							el)) {
+					*res_entry = de;
+					return EOK;
+				}
+			} else if (el == name_len) {
+				/* For more efficient compare only lengths firstly,
+				 * then the names */
 				if (memcmp(name, de->name, name_len) == 0) {
 					*res_entry = de;
--- a/src/ext4_dir_idx.c
+++ b/src/ext4_dir_idx.c
@@ -50,4 +50,5 @@
 #include "ext4_crc32.h"
 #include "ext4_hash.h"
+#include "ext4_casefold.h"	/* ext4edit patch 8 */
 
 #include <string.h>
@@ -466,4 +467,6 @@ int ext4_dir_dx_init(struct ext4_inode_ref *dir, struct ext4_inode_ref *parent)
 /**@brief Initialize hash info structure necessary for index operations.
  * @param hinfo      Pointer to hinfo to be initialized
+ * @param dir        The directory the index belongs to (ext4edit patch 8:
+ *                   whether its names are hashed folded is read from it)
  * @param root_block Root block (number 0) of index
  * @param sb         Pointer to superblock
@@ -473,4 +476,5 @@ int ext4_dir_dx_init(struct ext4_inode_ref *dir, struct ext4_inode_ref *parent)
  */
 static int ext4_dir_hinfo_init(struct ext4_hash_info *hinfo,
+			       struct ext4_inode_ref *dir,
 			       struct ext4_block *root_block,
 			       struct ext4_sblock *sb, size_t name_len,
@@ -478,4 +482,9 @@ static int ext4_dir_hinfo_init(struct ext4_hash_info *hinfo,
 {
 	struct ext4_dir_idx_root *root;
+
+	/* ext4edit patch 8: a casefolded directory hashes its names folded;
+	 * set before the hash below, and carried into every later hash of this
+	 * directory's names (a split re-hashes through a copy of hinfo). */
+	hinfo->casefold = ext4_casefold_dir(dir);
 
 	root = (struct ext4_dir_idx_root *)root_block->data;
@@ -742,5 +751,6 @@ int ext4_dir_dx_find_entry(struct ext4_dir_search_result *result,
 	/* Initialize hash info (compute hash value) */
 	struct ext4_hash_info hinfo;
-	rc = ext4_dir_hinfo_init(&hinfo, &root_block, &fs->sb, name_len, name);
+	rc = ext4_dir_hinfo_init(&hinfo, inode_ref, &root_block, &fs->sb,
+				 name_len, name);
 	if (rc != EOK) {
 		ext4_block_set(fs->bdev, &root_block);
@@ -788,7 +798,9 @@ int ext4_dir_dx_find_entry(struct ext4_dir_search_result *result,
 		}
 
-		/* Linear search inside block */
+		/* Linear search inside block (ext4edit patch 8: compared folded
+		 * in a casefolded directory, the table hinfo already holds) */
 		struct ext4_dir_en *de;
-		rc = ext4_dir_find_in_block(&b, &fs->sb, name_len, name, &de);
+		rc = ext4_dir_find_in_block(&b, &fs->sb, hinfo.casefold,
+					    name_len, name, &de);
 
 		/* Found => return it */
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -233,4 +233,16 @@ int ext4_fs_check_features(struct ext4_fs *fs, bool *read_only)
 				"sblock has unsupported features incompatible:\n");
 		ext4_fs_debug_features_inc(v);
+		return ENOTSUP;
+	}
+
+	/* ext4edit patch 8: casefold is supported only for an encoding the
+	 * mirrored folding tables know (UTF-8 12.1, the only one ever
+	 * defined).  Under any other, lookups folded by the wrong tables would
+	 * miss names the Linux driver finds and would hash new entries where
+	 * the driver would not look, so such a filesystem is refused. */
+	if (ext4_sb_feature_incom(&fs->sb, EXT4_FINCOM_CASEFOLD) &&
+	    !ext4_casefold_table(&fs->sb)) {
+		ext4_dbg(DEBUG_FS, DBG_ERROR
+				"sblock has casefold with an unknown encoding\n");
 		return ENOTSUP;
 	}
--- a/src/ext4_hash.c
+++ b/src/ext4_hash.c
@@ -259,5 +259,8 @@ int ext2_htree_hash(const char *name, int len, const uint32_t *hash_seed,
 		return (-1);
 
-	if (len < 1 || len > 255)
+	/* ext4edit patch 8: a FOLDED name (ext4_casefold.h) may be longer than
+	 * 255 bytes, or empty, and the Linux driver hashes it all the same; the
+	 * cap is the folding buffer's size, where it was the raw name's limit. */
+	if (len < 0 || len > 4096)
 		goto error;
 
```

- Also this patch's: the hunk of `include/ext4_types.h` at upstream line 175, shown under patch 3.
- Also this patch's: the hunk of `include/ext4_types.h` at upstream line 279, shown under patch 3.
- Also this patch's: the hunk of `include/ext4_types.h` at upstream line 461, shown under patch 7.
- Also this patch's: the hunk of `src/ext4.c` at upstream line 47, shown under patch 7.
- Also this patch's: the hunk of `src/ext4_dir.c` at upstream line 48, shown under patch 7.
- Also this patch's: the hunk of `src/ext4_dir.c` at upstream line 441, shown under patch 7.
- Also this patch's: the hunk of `src/ext4_dir_idx.c` at upstream line 202, shown under patch 5.
- Also this patch's: the hunk of `src/ext4_dir_idx.c` at upstream line 1299, shown under patch 50.
- Also this patch's: the hunk of `src/ext4_fs.c` at upstream line 47, shown under patch 7.
<!-- /hunks -->

### Patch 9 -- the formatter stamps every feature lwext4 can mount

**Where:** `src/ext4_mkfs.c`, `ext4_mkfs`, the ext4 case.

**The defect:** a new ext4 filesystem's incompatible features were
`EXT4_SUPPORTED_FINCOM` -- the set lwext4 can MOUNT, which in v1.0.0
happens to equal what the formatter lays out. Any change that widens the
supported set (patches 3, 5, 7, 8) widens what a fresh image is stamped
with: `csum_seed` without `metadata_csum` is a superblock e2fsck reports,
and casefold would promise an encoding the superblock does not carry. The
set also outgrows the formatter's 16-bit field, which gcc's overflow
warning is how this was noticed.

**Shown:** the formatter's 1-40 MiB sweep, every image `e2fsck -fn` clean.

**The fix:** the ext4 case names what the formatter lays out: `filetype`,
`extents`, `64bit`.

**Upstream:** not fixed in master (latent there, as in v1.0.0); applies to
master.

<!-- hunks 9 -->
```diff
--- a/src/ext4_mkfs.c
+++ b/src/ext4_mkfs.c
@@ -671,5 +671,16 @@ int ext4_mkfs(struct ext4_fs *fs, struct ext4_blockdev *bd,
 		info->feat_compat = EXT4_SUPPORTED_FCOM;
 		info->feat_ro_compat = EXT4_SUPPORTED_FRO_COM;
-		info->feat_incompat = EXT4_SUPPORTED_FINCOM;
+		/* ext4edit patch 9: a NEW ext4 filesystem gets the incompatible
+		 * features this formatter actually lays out -- filetype, extents,
+		 * 64bit -- and no longer the whole set the library can MOUNT.  As
+		 * published the two were one macro, so every feature a later patch
+		 * taught the library to read (csum_seed, largedir, inline_data,
+		 * casefold) was silently stamped on every fresh image: harmless
+		 * for most, but csum_seed without metadata_csum is a superblock
+		 * e2fsck reports, and casefold would promise an encoding the
+		 * superblock does not carry.  The set is also wider than the
+		 * 16-bit field here, which is how this was noticed. */
+		info->feat_incompat = EXT4_FINCOM_FILETYPE | EXT4_FINCOM_EXTENTS |
+				      EXT4_FINCOM_64BIT;
 		break;
 	}
```
<!-- /hunks -->

### Patch 10 -- `fast_commit` is not named

**Where:** `include/ext4_types.h`.

**What it is:** one define, `EXT4_FCOM_FAST_COMMIT` (0x0400), so a caller
can see the compatible feature and refuse to write after an unclean
shutdown. lwext4 neither honours nor replays the journal's fast-commit area
-- that defect is NOT fixed here (Part 2).

**Upstream:** not in master; applies to master.

<!-- hunks 10 -->
```diff
--- a/include/ext4_types.h
+++ b/include/ext4_types.h
@@ -218,4 +218,5 @@ struct ext4_sblock {
 #define EXT4_FCOM_RESIZE_INODE 0x0010
 #define EXT4_FCOM_DIR_INDEX 0x0020
+#define EXT4_FCOM_FAST_COMMIT 0x0400	/* ext4edit patch 10: the journal may hold fast commits, which this library cannot replay */
 
 /*
```
<!-- /hunks -->

### Patch 12 -- a symlink of 60 bytes or more is written over the start of the filesystem

**Where:** `src/ext4.c`, `ext4_fsymlink_set` and `ext4_fsymlink`.

**The defect:** four in one path. (1) A target too long for the inode gets
a data block allocated -- and is then written with
`ext4_block_writebytes(bdev, 0, buf, size)`: at byte 0 of the FILESYSTEM.
The boot area is overwritten every time, and past 1 KiB the superblock;
the link's own block keeps whatever it held, so the link reads back empty
and `e2fsck` calls it invalid. (2) The length test was `size >
block_size`, so a target exactly one block long was accepted, which
`e2fsck` calls invalid. (3) That test ran after the inode and its entry
were created, so a refused link was left behind empty; and the failure
exits after write-back mode started skipped its stop (patch 2's shape).
(4) A link over a name already taken was not refused: over a link its text
was silently replaced, over a file or directory the answer was ENOENT.
`symlink(2)` refuses all three with EEXIST.

**Shown:** ext4 at 4 and 1 KiB, ext3 at 2 KiB and ext2 at 1 KiB, links of
59, 60 and 1024 bytes, a block less one, and a whole block, copies of the
long ones, and links over a file, a directory and a link. Unpatched, every
long link read back as 0 bytes, the boot area changed on all four images,
the target text sat at byte 2048 of the 4 KiB image, the whole-block link
was accepted, and `e2fsck -fn` reported every long link invalid. Patched,
all pass, `e2fsck -fn` is clean and `debugfs` reads every link back
identical.

**The fix:** `ext4_fsymlink` refuses a target of a block or more (EINVAL)
and a name that exists (EEXIST) before it creates anything;
`ext4_fsymlink_set` tests `size >= block_size` and writes the text into the
allocated block -- the whole block, the rest zeroed, DIRECTLY to the
device, as `ext4_fwrite` writes file data -- and every failure leaves
through the write-back stop. Not through the block cache: `ext4_fread`,
which `ext4_readlink` uses, reads data blocks directly, so a block still
dirty in the cache reads back as what the disk held (with a large cache,
every long link made in a session came back empty).

**Upstream:** partly fixed in master: `c5f8d13` writes the target into the
link's own block (1), leaving the rest of that block as it was; (2), (3)
and (4) remain. 2 of its 3 hunks need adapting to master.

<!-- hunks 12 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -2266,5 +2266,8 @@ static int ext4_fsymlink_set(ext4_file *f, const void *buf, uint32_t size)
 	/*Sync file size*/
 	block_size = ext4_sb_get_block_size(&f->mp->fs.sb);
-	if (size > block_size) {
+	/* ext4edit patch 12: a target as long as a block is refused too,
+	 * not only a longer one -- e2fsck calls a link whose size is a
+	 * block or more invalid. */
+	if (size >= block_size) {
 		r = EINVAL;
 		goto Finish;
@@ -2285,15 +2288,40 @@ static int ext4_fsymlink_set(ext4_file *f, const void *buf, uint32_t size)
 		ext4_inode_clear_flag(ref.inode, EXT4_INODE_FLAG_EXTENTS);
 	} else {
+		uint8_t *block_buf;
+
 		ext4_fs_inode_blocks_init(&f->mp->fs, &ref);
 		r = ext4_fs_append_inode_dblk(&ref, &fblock, &sblock);
+		/* ext4edit patch 12: every failure from here on leaves through
+		 * Stop_wb, so the write-back mode started above is ended (as
+		 * published they jumped past it, leaving the device's
+		 * write-back count raised -- the shape of patch 2). */
 		if (r != EOK)
-			goto Finish;
+			goto Stop_wb;
 
-		r = ext4_block_writebytes(f->mp->fs.bdev, 0, buf, size);
-		if (r != EOK)
-			goto Finish;
+		/* ext4edit patch 12: as published the text was written with
+		 * ext4_block_writebytes at byte 0 of the FILESYSTEM -- over the
+		 * boot area and, past 1 KiB, the superblock -- while the block
+		 * just allocated kept what it held, so every link of 60 bytes
+		 * or more read back empty and e2fsck called it invalid.  The
+		 * text now goes into that block, whole, with the rest of the
+		 * block zeroed (e2fsck requires the text to end at i_size).
+		 * It is written DIRECTLY to the device, as ext4_fwrite writes a
+		 * file's data, and NOT through the block cache: ext4_fread --
+		 * which ext4_readlink uses -- reads data blocks directly, so a
+		 * block left dirty in the cache would read back as what the
+		 * disk held until the cache flushed (a large cache shows it:
+		 * a link copied in the same session came back empty). */
+		block_buf = calloc(1, block_size);
+		if (!block_buf) {
+			r = ENOMEM;
+			goto Stop_wb;
+		}
 
+		memcpy(block_buf, buf, size);
+		r = ext4_blocks_set_direct(f->mp->fs.bdev, block_buf, fblock, 1);
+		free(block_buf);
 	}
 
+Stop_wb:
 	/*Stop write back cache mode*/
 	ext4_block_cache_write_back(f->mp->fs.bdev, 0);
@@ -2327,10 +2355,37 @@ int ext4_fsymlink(const char *target, const char *path)
 		return EROFS;
 
+	/* ext4edit patch 12: a target that cannot fit is refused HERE,
+	 * before anything is made -- ext4_fsymlink_set's own test comes
+	 * after ext4_generic_open2 has created the inode and its entry,
+	 * and on a filesystem with no journal no abort takes those back,
+	 * so a refused link would be left behind empty, and e2fsck calls
+	 * an empty link invalid. */
+	if (strlen(target) >= ext4_sb_get_block_size(&mp->fs.sb))
+		return EINVAL;
+
 	filetype = EXT4_DE_SYMLINK;
 
+	/* ext4edit patch 48 (upstream master 9b38aef): ONE transaction      */
+	/* covers the inode, its entry and the target, now that the creating */
+	/* open below opens none of its own (before, its stop committed this */
+	/* one and the target was written with none).  It sits inside the   */
+	/* write-back window, as master orders it, so the commit comes        */
+	/* before write-back ends.                                            */
 	EXT4_MP_LOCK(mp);
+	ext4_block_cache_write_back(mp->fs.bdev, 1);
 	ext4_trans_start(mp);
 
-	ext4_block_cache_write_back(mp->fs.bdev, 1);
+	/* ext4edit patch 12: a name already taken is refused with EEXIST,
+	 * as symlink(2) refuses it.  ext4_generic_open2 has no O_EXCL: as
+	 * published, an existing LINK was opened and its text replaced,
+	 * and an existing file or directory came back as ENOENT ("no such
+	 * file") because its type differed.  An open of any type that
+	 * succeeds is the test; it creates nothing. */
+	r = ext4_generic_open2(&f, path, O_RDONLY, EXT4_DE_UNKNOWN, NULL, NULL);
+	if (r == EOK) {
+		r = EEXIST;
+		goto Finish;
+	}
+
 	r = ext4_generic_open2(&f, path, O_RDWR|O_CREAT, filetype, NULL, NULL);
 	if (r == EOK)
```
<!-- /hunks -->

### Patch 13 -- removing a directory tree frees a file that still has another name outside it

**Where:** `src/ext4.c`, `ext4_dir_rm`, the per-entry loop.

**The defect:** every non-directory in the tree was truncated, its link
count forced to 0 and its inode freed, whatever its link count. A hard link
from outside the tree is left naming a freed inode: the file reads as
empty, and `e2fsck` reports "has deleted/unused inode".

**Shown:** ext3 at 4, 2 and 1 KiB and ext4 at 4 KiB, a file in a tree with a
hard link outside it, the tree removed: unpatched, `e2fsck` failed on all
four; patched, all clean and the outside name intact.

**The fix:** as `ext4_fremove` does it -- truncate only when this is the
file's last name, let `ext4_unlink` take one link away, and free the inode
only when none is left. A directory is still removed whole.

**Upstream:** pull request #148 (open). Not fixed in master; applies to
master.

<!-- hunks 13 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -2751,8 +2751,19 @@ int ext4_dir_rm(const char *path)
 						child.inode);
 
-				/* Truncate */
-				if (inode_type != EXT4_INODE_MODE_DIRECTORY)
-					r = ext4_trunc_inode(mp, child.index, 0);
-				else
+				/* ext4edit patch 13 (upstream PR #148): as
+				 * published every non-directory was truncated
+				 * and freed here whatever its link count, so a
+				 * file in the tree with another name OUTSIDE it
+				 * was left as a freed inode under that name.  Now
+				 * as ext4_fremove does it: a file is truncated
+				 * only when this is its last name, ext4_unlink
+				 * takes one link away, and the inode is freed
+				 * only when none is left. */
+				if (inode_type != EXT4_INODE_MODE_DIRECTORY) {
+					if (ext4_inode_get_links_cnt(child.inode) == 1)
+						r = ext4_trunc_inode(mp, child.index, 0);
+					else
+						r = EOK;
+				} else
 					r = ext4_trunc_dir(mp, &act, &child);
 
@@ -2772,13 +2783,19 @@ int ext4_dir_rm(const char *path)
 				}
 
-				ext4_inode_set_del_time(child.inode, -1L);
-				ext4_inode_set_links_cnt(child.inode, 0);
-				child.dirty = true;
+				/* ext4edit patch 13: a directory goes
+				 * whole; a file only when it has no name left. */
+				if (inode_type == EXT4_INODE_MODE_DIRECTORY ||
+				    !ext4_inode_get_links_cnt(child.inode)) {
+					ext4_inode_set_del_time(child.inode, -1L);
+					ext4_inode_set_links_cnt(child.inode, 0);
+					child.dirty = true;
 
-				r = ext4_fs_free_inode(&child);
-				if (r != EOK) {
-					ext4_fs_put_inode_ref(&child);
-					goto End;
-				}
+					r = ext4_fs_free_inode(&child);
+					if (r != EOK) {
+						ext4_fs_put_inode_ref(&child);
+						goto End;
+					}
+				} else
+					child.dirty = true;
 
 				r = ext4_fs_put_inode_ref(&child);
```
<!-- /hunks -->

### Patch 14 -- a moved small directory's block checksum is left wrong

**Where:** `src/ext4.c`, `ext4_link`, the branch that rewrites ".." in a
directory without an htree index.

**The defect:** ".." is rewritten in place and the block marked dirty, but
its checksum is not recomputed. lwext4 makes its own new directories
indexed, but Linux and e2fsprogs leave small directories unindexed, so
moving ANY small directory Linux made, on a metadata_csum filesystem, left
a block `e2fsck` reports as failing its checksum and Linux will not read.

**Shown:** ext4 images at 1 and 4 KiB made by `mke2fs -d` with 40 small
directories, each moved to a new parent: unpatched, `e2fsck` reported the
checksum error on every moved directory; patched, clean.

**The fix:** `ext4_dir_set_csum` before the block is marked dirty.

**Upstream:** pull request #132 (open). Not fixed in master; applies to
master.

<!-- hunks 14 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -256,4 +256,10 @@ static int ext4_link(struct ext4_mountpoint *mp, struct ext4_inode_ref *parent,
 
 			ext4_dir_en_set_inode(res.dentry, parent->index);
+			/* ext4edit patch 14 (upstream PR #132): the block's
+			 * checksum is recomputed after ".." is rewritten -- as
+			 * published it was not, so on metadata_csum every
+			 * non-indexed directory moved to a new parent (any
+			 * small directory Linux made) failed its checksum. */
+			ext4_dir_set_csum(ch, (void *)res.block.data);
 			ext4_trans_set_block_dirty(res.block.buf);
 			r = ext4_dir_destroy_result(ch, &res);
```
<!-- /hunks -->

### Patch 15 -- a sparse file's holes are read from block 0

**Where:** `src/ext4.c`, `ext4_fread`, the whole-block loop and the
trailing partial block.

**The defect:** the loop gathered runs of consecutive physical blocks and
never tested for 0, which is how a hole is recorded: a hole was read from
block 0 of the filesystem, and the runs after it were mis-tracked. The
trailing block had the same fault. Reading a sparse file gives wrong
content.

**Shown:** a file of 4 KiB of A, an 8 KiB hole, 4 KiB of B and a hole to
32 KiB (`mke2fs -d` keeps the holes), read back at 1 and 4 KiB blocks:
unpatched wrong, patched exact.

**The fix:** each block is looked up; a hole is a block of zeros; a real
block starts a run of consecutive ones, read directly in one call as
before; the tail reads zeros for a hole too.

**Upstream:** pull request #103, issue #101. Partly fixed in master: its
leading partial block reads a hole as zeros, while the whole-block loop and
the tail still read block 0. 2 of its 3 hunks need adapting to master.

<!-- hunks 15 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -1733,29 +1733,44 @@ int ext4_fread(ext4_file *f, void *buf, size_t size, size_t *rcnt)
 	}
 
+	/* ext4edit patch 15 (upstream PR #103): whole blocks are read one
+	 * RUN at a time, and a HOLE (a block whose number is 0) reads as
+	 * zeros.  As published the loop never tested for 0: a hole was read
+	 * from physical block 0 of the filesystem, and the runs after one were
+	 * mis-tracked, so a sparse file read back wrong. */
 	fblock_start = 0;
 	fblock_count = 0;
 	while (size >= block_size) {
-		while (iblock_idx < iblock_last) {
-			r = ext4_fs_get_inode_dblk_idx(&ref, iblock_idx,
-						       &fblock, true);
-			if (r != EOK)
-				goto Finish;
-
-			iblock_idx++;
-
-			if (!fblock_start)
-				fblock_start = fblock;
-
-			if ((fblock_start + fblock_count) != fblock)
-				break;
-
-			fblock_count++;
-		}
-
-		r = ext4_blocks_get_direct(f->mp->fs.bdev, u8_buf, fblock_start,
-					   fblock_count);
+		r = ext4_fs_get_inode_dblk_idx(&ref, iblock_idx, &fblock, true);
 		if (r != EOK)
 			goto Finish;
 
+		if (fblock == 0) {
+			/* A hole: one block of zeros. */
+			memset(u8_buf, 0, block_size);
+			fblock_count = 1;
+		} else {
+			/* A run of blocks that follow each other on disk. */
+			fblock_start = fblock;
+			fblock_count = 1;
+			while ((uint64_t)(fblock_count + 1) * block_size <= size) {
+				r = ext4_fs_get_inode_dblk_idx(&ref,
+						iblock_idx + fblock_count,
+						&fblock, true);
+				if (r != EOK)
+					goto Finish;
+
+				if (fblock != fblock_start + fblock_count)
+					break;
+
+				fblock_count++;
+			}
+
+			r = ext4_blocks_get_direct(f->mp->fs.bdev, u8_buf,
+						   fblock_start, fblock_count);
+			if (r != EOK)
+				goto Finish;
+		}
+
+		iblock_idx += fblock_count;
 		size -= block_size * fblock_count;
 		u8_buf += block_size * fblock_count;
@@ -1764,7 +1779,4 @@ int ext4_fread(ext4_file *f, void *buf, size_t size, size_t *rcnt)
 		if (rcnt)
 			*rcnt += block_size * fblock_count;
-
-		fblock_start = fblock;
-		fblock_count = 1;
 	}
 
@@ -1775,8 +1787,13 @@ int ext4_fread(ext4_file *f, void *buf, size_t size, size_t *rcnt)
 			goto Finish;
 
-		off = fblock * block_size;
-		r = ext4_block_readbytes(f->mp->fs.bdev, off, u8_buf, size);
-		if (r != EOK)
-			goto Finish;
+		/* ext4edit patch 15: a trailing hole reads as zeros too. */
+		if (fblock == 0) {
+			memset(u8_buf, 0, size);
+		} else {
+			off = fblock * block_size;
+			r = ext4_block_readbytes(f->mp->fs.bdev, off, u8_buf, size);
+			if (r != EOK)
+				goto Finish;
+		}
 
 		f->fpos += size;
```
<!-- /hunks -->

### Patch 16 -- creating anything hangs when a group's free-inode count is wrong

**Where:** `src/ext4_ialloc.c`, `ext4_ialloc_alloc_inode`.

**The defect:** when a group's free count claims inodes its bitmap does not
have, the branch does `continue` without `++bgid`, so the same group is
tried for ever -- exactly what a damaged or interrupted filesystem has.

**Shown:** `mke2fs -t ext4 -b 1024 -N 16` with every inode used, then
`debugfs` setting group 0's free inode count to 2 (checksum recalculated):
one mkdir hung until killed; patched, it fails with ENOSPC at once.

**The fix:** `++bgid` before that `continue`.

**Upstream:** pull request #106, issue #50. Not fixed in master; applies
to master.

<!-- hunks 16 -->
```diff
--- a/src/ext4_ialloc.c
+++ b/src/ext4_ialloc.c
@@ -298,4 +298,10 @@ int ext4_ialloc_alloc_inode(struct ext4_fs *fs, uint32_t *idx, bool is_dir)
 					return rc;
 
+				/* ext4edit patch 16 (upstream PR #106): on to the
+				 * next group.  As published this `continue` kept
+				 * bgid, so a group whose free count claims inodes
+				 * its bitmap does not have was retried for ever --
+				 * every create on such a filesystem hung. */
+				++bgid;
 				continue;
 			}
```
<!-- /hunks -->

### Patch 17 -- `ext4_dir_rm` puts the wrong reference on an error

**Where:** `src/ext4.c`, `ext4_dir_rm`.

**The defect:** when loading the directory being removed fails at the last
unlink, the error path put `act` -- never filled -- and left `parent`
held. By reading: no test reaches it without injecting an I/O failure.

**The fix:** put `parent`.

**Upstream:** pull request #158 (open). Not fixed in master; applies to
master.

<!-- hunks 17 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -2830,5 +2830,9 @@ int ext4_dir_rm(const char *path)
 				&act);
 		if (r != EOK) {
-			ext4_fs_put_inode_ref(&act);
+			/* ext4edit patch 17 (upstream PR #158): release the
+			 * PARENT, which was loaded -- as published this put
+			 * `act`, which the failed call never filled, and left
+			 * the parent held. */
+			ext4_fs_put_inode_ref(&parent);
 			goto Finish;
 		}
```
<!-- /hunks -->

### Patch 18 -- `ext4_fremove` leaves write-back on after a failed truncation

**Where:** `src/ext4.c`, `ext4_fremove`.

**The defect:** a failed truncation returned with the write-back mode it
had just started still on (patch 2's shape). By reading.

**The fix:** write-back is stopped before that return.

**Upstream:** pull request #160 (open). Not fixed in master; applies to
master.

<!-- hunks 18 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -1433,4 +1433,9 @@ int ext4_fremove(const char *path)
 		r = ext4_trunc_inode(mp, child.index, 0);
 		if (r != EOK) {
+			/* ext4edit patch 18 (upstream PR #160): end the
+			 * write-back mode started above before returning -- as
+			 * published this exit left the device's write-back count
+			 * raised (the shape of patch 2). */
+			ext4_block_cache_write_back(mp->fs.bdev, 0);
 			ext4_fs_put_inode_ref(&parent);
 			ext4_fs_put_inode_ref(&child);
```
<!-- /hunks -->

### Patch 19 -- a failed get or put in the path walk releases twice

**Where:** `src/ext4.c`, `ext4_generic_open2` and `ext4_create_hardlink`.

**The defect:** a failed put or get while walking a path broke out to an
exit that puts `ref` again: a second put, or a put of a reference never
filled. By reading.

**The fix:** both exits return at once.

**Upstream:** pull request #156 (open). Not fixed in master; applies to
master.

<!-- hunks 19 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -1011,11 +1011,15 @@ static int ext4_generic_open2(ext4_file *f, const char *path, int flags,
 		}
 
+		/* ext4edit patch 19 (upstream PR #156): these two exits
+		 * RETURN rather than break -- the error path after the loop
+		 * puts `ref` again, which after a failed put is a second put,
+		 * and after a failed get is a put of a ref never filled. */
 		r = ext4_fs_put_inode_ref(&ref);
 		if (r != EOK)
-			break;
+			return r;
 
 		r = ext4_fs_get_inode_ref(fs, next_inode, &ref);
 		if (r != EOK)
-			break;
+			return r;
 
 		if (is_goal)
@@ -1165,11 +1169,15 @@ static int ext4_create_hardlink(const char *path,
 		}
 
+		/* ext4edit patch 19 (upstream PR #156): these two exits
+		 * RETURN rather than break -- the error path after the loop
+		 * puts `ref` again, which after a failed put is a second put,
+		 * and after a failed get is a put of a ref never filled. */
 		r = ext4_fs_put_inode_ref(&ref);
 		if (r != EOK)
-			break;
+			return r;
 
 		r = ext4_fs_get_inode_ref(fs, next_inode, &ref);
 		if (r != EOK)
-			break;
+			return r;
 
 		if (is_goal)
```
<!-- /hunks -->

### Patch 20 -- a failed write returns EOK and is committed

**Where:** `src/ext4.c`, `ext4_fwrite`, at `Finish:`.

**The defect:** the inode put's result was assigned over `r`, so a write
that failed -- an I/O error, a full filesystem -- returned EOK and its
transaction was committed. By reading; the full-filesystem case then
passes images filled to their last block, `e2fsck -fn` clean.

**The fix:** the first failure is the one returned (and the transaction
aborted).

**Upstream:** pull request #150 (open). Not fixed in master; applies to
master.

<!-- hunks 20 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -1970,5 +1970,13 @@ int ext4_fwrite(ext4_file *f, const void *buf, size_t size, size_t *wcnt)
 
 Finish:
-	r = ext4_fs_put_inode_ref(&ref);
+	/* ext4edit patch 20 (upstream PR #150): the FIRST failure is the
+	 * one returned -- as published the put's result was assigned over
+	 * r, so a write that failed (an I/O error, a full filesystem)
+	 * returned EOK and was committed. */
+	{
+		int r_put = ext4_fs_put_inode_ref(&ref);
+		if (r == EOK)
+			r = r_put;
+	}
 
 	if (r != EOK)
```
<!-- /hunks -->

### Patch 21 -- a checksummed journal's commit blocks and tags are written in the wrong byte order

**Where:** `src/ext4_journal.c`: `jbd_commit_csum_set`, `jbd_block_csum`,
`jbd_block_tag_csum_set`.

**The defect:** on a journal with checksum v2 or v3 -- which the Linux
driver turns on for every metadata_csum filesystem, so nearly every modern
one -- the commit block's checksum was stored in host order (jbd2, and
lwext4's own verifier, read it big-endian), the tag checksum hashed the
sequence number in host order, and a v2 tag kept the wrong 16 bits. After a
crash between a commit and its checkpoint, e2fsck and Linux report "Journal
transaction N was corrupt, replay was aborted", and lwext4's own recovery
rejects its own commit block, replays nothing, and clears needs_recovery.

**Shown:** an ext4 image whose journal was switched to checksum v3
(`debugfs`, `jo -c -v 3`), written by lwext4 and then set back to a pending
replay of its last transaction, with one directory block zeroed in place:
unpatched, `e2fsck` "transaction 1 was corrupt" and lwext4's replay left
the directory corrupt; patched, both replays restore it and the image is
clean. A control without metadata_csum passes either way.

**The fix:** `to_be32` on the commit checksum; the sequence hashed as
`to_be32(sequence)`; a v2 tag `to_be16((uint16_t)checksum)`.

**Upstream:** pull request #164 (open). Not fixed in master; applies to
master. Beside it, NOT changed: the v1 checksum branch (Part 2).

<!-- hunks 21 -->
```diff
--- a/src/ext4_journal.c
+++ b/src/ext4_journal.c
@@ -275,5 +275,12 @@ static void jbd_commit_csum_set(struct jbd_fs *jbd_fs,
 	header->chksum_type = 0;
 	header->chksum_size = 0;
-	header->chksum[0] = jbd_commit_csum(jbd_fs, header);
+	/* ext4edit patch 21 (upstream #164): stored BIG-endian, as jbd2
+	 * stores it and as jbd_verify_commit_csum below reads it.  As
+	 * published it was stored in host order, so on a little-endian host
+	 * every commit block of a checksummed journal failed verification:
+	 * after a crash e2fsck and Linux called the transaction corrupt and
+	 * aborted the replay, and lwext4's own recovery silently replayed
+	 * nothing and cleared needs_recovery. */
+	header->chksum[0] = to_be32(jbd_commit_csum(jbd_fs, header));
 }
 
@@ -305,9 +312,14 @@ static uint32_t jbd_block_csum(struct jbd_fs *jbd_fs, const void *buf,
 	if (jbd_has_csum(&jbd_fs->sb)) {
 		uint32_t block_size = jbd_get32(&jbd_fs->sb, blocksize);
+		/* ext4edit patch 21 (upstream #164): the sequence number is
+		 * hashed as it lies on disk, BIG-endian, as jbd2 hashes it; as
+		 * published it was hashed in host order, so no data-block tag
+		 * this journal wrote matched what a replayer computes. */
+		uint32_t be_sequence = to_be32(sequence);
 		/* First calculate crc32c checksum against fs uuid */
 		checksum = ext4_crc32c(EXT4_CRC32_INIT, jbd_fs->sb.uuid,
 				       sizeof(jbd_fs->sb.uuid));
 		/* Then calculate crc32c checksum against sequence no. */
-		checksum = ext4_crc32c(checksum, &sequence,
+		checksum = ext4_crc32c(checksum, &be_sequence,
 				sizeof(uint32_t));
 		/* Calculate crc32c checksum against tho whole block */
@@ -336,5 +348,8 @@ static void jbd_block_tag_csum_set(struct jbd_fs *jbd_fs, void *__tag,
 	if (ver == 2) {
 		struct jbd_block_tag *tag = __tag;
-		tag->checksum = (uint16_t)to_be32(checksum);
+		/* ext4edit patch 21 (upstream #164): the LOW 16 bits,
+		 * big-endian, as jbd2 stores a v2 tag; as published the cast
+		 * after the byte swap kept the wrong half. */
+		tag->checksum = to_be16((uint16_t)checksum);
 	} else {
 		struct jbd_block_tag3 *tag = __tag;
```
<!-- /hunks -->

### Patch 22 -- transaction IDs restart at 1 at every journal start

**Where:** `src/ext4_journal.c`: `jbd_journal_start`, `jbd_journal_stop`,
`jbd_recover`.

**The defect:** every journal start wrote the log from its first block with
IDs 1, 2, 3 ..., over the previous cycle's blocks; a stop wrote the
journal superblock's sequence as 0; a recovery left the sequence where it
was. After a crash right after a commit, a replay could run on into an OLD
cycle's transaction that carried the next expected ID and began where the
next transaction would have -- stale metadata applied over newer. Repeating
the same kind of change makes equal-sized transactions, so the alignment is
not far-fetched. (On a checksummed journal patch 21's defect hid this:
nothing was replayed at all.)

**Shown:** with patch 21's run, the journal sequence after a session was 0
unpatched and continued across cycles patched, and the replays pass with
continued IDs.

**The fix:** a start takes its first ID from the superblock's sequence (1
when 0); a stop writes the next unused ID as the sequence; a recovery
writes the last replayed ID + 1 -- as jbd2 does.

**Upstream:** pull request #114, issue #86 (the PR's hunks do not apply to
v1.0.0 as they stand). Partly fixed in master: `1a96ef0` takes a start's ID
from the sequence + 1, and the recovery records the last ID -- but master's
stop still writes the sequence as 0, so after every clean stop the next
start is back at ID 1, and the defect stands. 1 of its 3 hunks needs
adapting to master.

<!-- hunks 22 -->
```diff
--- a/src/ext4_journal.c
+++ b/src/ext4_journal.c
@@ -1198,4 +1198,10 @@ int jbd_recover(struct jbd_fs *jbd_fs)
 				   features_incompatible);
 		jbd_set32(&jbd_fs->sb, start, 0);
+		/* ext4edit patch 22 (upstream #114): the next session's
+		 * transactions take IDs AFTER the ones just replayed, as jbd2
+		 * continues after a recovery; as published the sequence was
+		 * left where it was, so the replayed transactions -- still in
+		 * the log -- could be matched again by later IDs. */
+		jbd_set32(&jbd_fs->sb, sequence, info.last_trans_id + 1);
 		features_incompatible &= ~EXT4_FINCOM_RECOVER;
 		ext4_set32(&jbd_fs->inode_ref.fs->sb,
@@ -1242,6 +1248,16 @@ int jbd_journal_start(struct jbd_fs *jbd_fs,
 	journal->start = journal->first;
 	journal->last = journal->first;
-	journal->trans_id = 1;
-	journal->alloc_trans_id = 1;
+	/* ext4edit patch 22 (upstream #114): IDs continue from the
+	 * journal superblock's sequence, as jbd2's do, instead of restarting
+	 * at 1 at every start.  A start writes over the log from its first
+	 * block each time, and the blocks of earlier cycles stay behind it:
+	 * with IDs restarting, a replay after a crash could run on into an
+	 * old cycle's transaction that happened to carry the next expected
+	 * ID and lie where the next transaction would begin, and apply its
+	 * stale metadata over newer.  Continued IDs never match them. */
+	journal->trans_id = jbd_get32(&jbd_fs->sb, sequence);
+	if (journal->trans_id == 0)
+		journal->trans_id = 1;
+	journal->alloc_trans_id = journal->trans_id;
 
 	journal->block_size = jbd_get32(&jbd_fs->sb, blocksize);
@@ -1398,5 +1414,9 @@ int jbd_journal_stop(struct jbd_journal *journal)
 
 	journal->start = 0;
-	journal->trans_id = 0;
+	/* ext4edit patch 22 (upstream #114): the superblock's sequence is
+	 * the NEXT ID to use, so the next start -- here or the Linux
+	 * driver's mount -- carries on after this session's transactions;
+	 * as published it was written as 0. */
+	journal->trans_id = journal->alloc_trans_id;
 	jbd_journal_write_sb(journal);
 	return jbd_write_sb(journal->jbd_fs);
```
<!-- /hunks -->

### Patch 23 -- meta_bg groups read their descriptors from the wrong block

**Where:** `src/ext4_fs.c`, `ext4_fs_get_descriptor_block`.

**The defect:** with meta_bg, a meta-group's descriptor block lives in its
FIRST group (after that group's superblock backup, if any); lwext4 looked
in group `bgid` itself, so every other group's bitmaps and counts were read
from, and written to, some other block.

**Shown:** a 64 MiB ext4 made with `-b 1024 -O meta_bg,^resize_inode`
(eight groups, one meta-group): unpatched, a copy that overwrote a file
lost it, and `e2fsck` found "Group 1 block(s) in use but group is marked
BLOCK_UNINIT"; patched, clean.

**The fix:** compute from group `dsc_id * dsc_per_block`, as the kernel's
`descriptor_loc()` does.

**Upstream:** pull request #166 (open). Not fixed in master; applies to
master.

<!-- hunks 23 -->
```diff
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -479,8 +479,17 @@ static ext4_fsblk_t ext4_fs_get_descriptor_block(struct ext4_sblock *s,
 		return ext4_get32(s, first_data_block) + dsc_id + 1;
 
-	if (ext4_sb_is_super_in_bg(s, bgid))
+	/* ext4edit patch 23 (upstream #166): with meta_bg, the descriptor
+	 * block for a whole meta-group (dsc_per_block groups) lives in the
+	 * FIRST group of that meta-group, after its superblock backup if it
+	 * has one -- the kernel's descriptor_loc().  As published it was
+	 * looked for in group bgid itself, which is right only for that first
+	 * group: every other group read its descriptor from some other block,
+	 * so their bitmaps and counts were wrong and writes corrupted them. */
+	uint32_t first_bg_of_meta = dsc_id * dsc_per_block;
+
+	if (ext4_sb_is_super_in_bg(s, first_bg_of_meta))
 		has_super = 1;
 
-	return (has_super + ext4_fs_first_bg_block_no(s, bgid));
+	return (has_super + ext4_fs_first_bg_block_no(s, first_bg_of_meta));
 }
 
```
<!-- /hunks -->

### Patch 24 -- a fast symlink with an xattr block is taken for a slow one

**Where:** a new `ext4_inode_is_fast_symlink` in `src/ext4_inode.c`
(declared in `include/ext4_inode.h`), used by `ext4_fread` and
`ext4_fs_truncate_inode`.

**The defect:** lwext4 called a symlink fast only when its block count was
0, so a fast symlink with an xattr BLOCK (SELinux labels on 128-byte
inodes, say) was taken for a slow one: readlink failed, and REMOVING it
hung, walking the target text as a block map.

**Shown:** such a symlink: unpatched, the read failed and the remove hung;
patched, both work and `e2fsck` is clean.

**The fix:** the Linux driver's rule -- the block count less the xattr
block (one cluster with bigalloc) is 0.

**Upstream:** issue #108. Not fixed in master (it still tests a block
count of 0). 1 of its 4 hunks needs adapting to master.

<!-- hunks 24 -->
```diff
--- a/include/ext4_inode.h
+++ b/include/ext4_inode.h
@@ -320,4 +320,13 @@ bool ext4_inode_can_truncate(struct ext4_sblock *sb, struct ext4_inode *inode);
 bool ext4_inode_can_truncate(struct ext4_sblock *sb, struct ext4_inode *inode);
 
+/**@brief Check whether a symlink keeps its target in the inode (ext4edit
+ *        patch 24, upstream #108): its block count less any xattr block is 0.
+ * @param sb    Superblock
+ * @param inode I-node
+ * @return true for a fast symlink, false for a slow one or not a symlink
+ */
+bool ext4_inode_is_fast_symlink(struct ext4_sblock *sb,
+				struct ext4_inode *inode);
+
 /**@brief Get extent header from the root of the extent tree.
  * @param inode I-node to get extent header from
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -1683,7 +1683,8 @@ int ext4_fread(ext4_file *f, void *buf, size_t size, size_t *rcnt)
 	/*If the size of symlink is smaller than 60 bytes*/
 	bool softlink;
-	softlink = ext4_inode_is_type(sb, ref.inode, EXT4_INODE_MODE_SOFTLINK);
-	if (softlink && f->fsize < sizeof(ref.inode->blocks)
-		     && !ext4_inode_get_blocks_count(sb, ref.inode)) {
+	/* ext4edit patch 24 (upstream #108): a fast symlink may have an
+	 * xattr block, which a block count of zero would not allow. */
+	softlink = ext4_inode_is_fast_symlink(sb, ref.inode);
+	if (softlink) {
 
 		char *content = (char *)ref.inode->blocks;
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -1183,7 +1183,9 @@ int ext4_fs_truncate_inode(struct ext4_inode_ref *inode_ref, uint64_t new_size)
 
 	bool v;
-	v = ext4_inode_is_type(sb, inode_ref->inode, EXT4_INODE_MODE_SOFTLINK);
-	if (v && old_size < sizeof(inode_ref->inode->blocks) &&
-	    !ext4_inode_get_blocks_count(sb, inode_ref->inode)) {
+	/* ext4edit patch 24 (upstream #108): a fast symlink with an xattr
+	 * block is still fast -- its i_block is the target text, never a
+	 * block map to walk and free. */
+	v = ext4_inode_is_fast_symlink(sb, inode_ref->inode);
+	if (v) {
 		char *content = (char *)inode_ref->inode->blocks + new_size;
 		memset(content, 0,
--- a/src/ext4_inode.c
+++ b/src/ext4_inode.c
@@ -366,4 +366,37 @@ bool ext4_inode_can_truncate(struct ext4_sblock *sb, struct ext4_inode *inode)
 }
 
+/* ext4edit patch 24 (upstream #108): whether a symlink keeps its target
+ * in the inode (a "fast" symlink) rather than in a data block.  As the
+ * Linux driver decides it: a symlink whose block count, LESS the xattr
+ * block it may have, is zero.  As published lwext4 tested for a block
+ * count of zero, so a fast symlink that also had an xattr block (SELinux
+ * labels on small inodes, say) was taken for a slow one: its target text
+ * was read as a block map -- readlink failed -- and removing it walked
+ * that "map", hanging or freeing blocks named by the target's bytes. */
+bool ext4_inode_is_fast_symlink(struct ext4_sblock *sb,
+				struct ext4_inode *inode)
+{
+	uint64_t ea_sectors = 0;
+	uint32_t ea_bytes;
+
+	if (!ext4_inode_is_type(sb, inode, EXT4_INODE_MODE_SOFTLINK))
+		return false;
+
+	/* The inode holds at most sizeof(blocks) bytes of target. */
+	if (ext4_inode_get_size(sb, inode) >= sizeof(inode->blocks))
+		return false;
+
+	/* An xattr block counts one block -- one CLUSTER with bigalloc --
+	 * in i_blocks, which is kept in 512-byte sectors. */
+	if (ext4_inode_get_file_acl(inode, sb)) {
+		ea_bytes = ext4_sb_get_block_size(sb);
+		if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_BIGALLOC))
+			ea_bytes = 1024 << to_le32(sb->log_cluster_size);
+		ea_sectors = ea_bytes >> 9;
+	}
+
+	return ext4_inode_get_blocks_count(sb, inode) == ea_sectors;
+}
+
 struct ext4_extent_header *
 ext4_inode_get_extent_header(struct ext4_inode *inode)
```
<!-- /hunks -->

### Patch 25 -- only the low 16 bits of a uid / gid are used

**Where:** `src/ext4_inode.c`, `ext4_inode_get_uid` / `set_uid` /
`get_gid` / `set_gid`.

**The defect:** the high halves (in `osd2`, at the same offset in the Linux
and Hurd layouts) were ignored: uid 100000 read as 34464, gid 65536 read as
0 -- root -- and setting 70000 stored 4464.

**Shown:** an image with those ids: unpatched all four wrong, patched right.

**The fix:** both halves read and written.

**Upstream:** issue #135. Not fixed in master; applies to master.

<!-- hunks 25 -->
```diff
--- a/src/ext4_inode.c
+++ b/src/ext4_inode.c
@@ -86,12 +86,20 @@ void ext4_inode_set_mode(struct ext4_sblock *sb, struct ext4_inode *inode,
 }
 
+/* ext4edit patch 25 (upstream #135): an owner or group id is 32 bits, its
+ * low half in i_uid / i_gid and its HIGH half in osd2 (at the same offset in
+ * the Linux and Hurd layouts), as the Linux driver keeps it.  As published
+ * only the low 16 bits were read and written: uid 100000 read as 34464,
+ * gid 65536 as 0 -- root -- and setting 70000 stored 4464. */
 uint32_t ext4_inode_get_uid(struct ext4_inode *inode)
 {
-	return to_le32(inode->uid);
+	return (uint32_t)to_le16(inode->uid) |
+	       ((uint32_t)to_le16(inode->osd2.linux2.uid_high) << 16);
 }
 
 void ext4_inode_set_uid(struct ext4_inode *inode, uint32_t uid)
 {
-	inode->uid = to_le32(uid);
+	/* ext4edit patch 25: both halves (see ext4_inode_get_uid). */
+	inode->uid = to_le16((uint16_t)uid);
+	inode->osd2.linux2.uid_high = to_le16((uint16_t)(uid >> 16));
 }
 
@@ -176,9 +184,13 @@ uint32_t ext4_inode_get_gid(struct ext4_inode *inode)
 uint32_t ext4_inode_get_gid(struct ext4_inode *inode)
 {
-	return to_le32(inode->gid);
+	/* ext4edit patch 25: both halves (see ext4_inode_get_uid). */
+	return (uint32_t)to_le16(inode->gid) |
+	       ((uint32_t)to_le16(inode->osd2.linux2.gid_high) << 16);
 }
 void ext4_inode_set_gid(struct ext4_inode *inode, uint32_t gid)
 {
-	inode->gid = to_le32(gid);
+	/* ext4edit patch 25: both halves (see ext4_inode_get_uid). */
+	inode->gid = to_le16((uint16_t)gid);
+	inode->osd2.linux2.gid_high = to_le16((uint16_t)(gid >> 16));
 }
 
```
<!-- /hunks -->

### Patch 26 -- an all-zero directory hash seed is used as given

**Where:** `src/ext4_hash.c`, `ext2_htree_hash`.

**The defect:** an all-zero seed means "no seed" to the Linux driver and
e2fsprogs (the default constants are used); lwext4 used the zeros. On such
a filesystem names Linux had filed were not found, and names lwext4 added
broke the hash order.

**Shown:** an image with a zero seed: unpatched, a name Linux filed was not
found and `e2fsck` reported a bad max hash; patched, found and clean.

**The fix:** a seed of all zeros is not copied in.

**Upstream:** issue #135. Not fixed in master; applies to master.

<!-- hunks 26 -->
```diff
--- a/src/ext4_hash.c
+++ b/src/ext4_hash.c
@@ -267,5 +267,12 @@ int ext2_htree_hash(const char *name, int len, const uint32_t *hash_seed,
 	hash[3] = 0x10325476;
 
-	if (hash_seed)
+	/* ext4edit patch 26 (upstream #135): an ALL-ZERO seed means "no
+	 * seed" -- the defaults above -- as the Linux driver and e2fsprogs
+	 * read it.  As published a zero seed was copied in, so on such a
+	 * filesystem lwext4 hashed names differently from Linux: names Linux
+	 * had filed were not found, and names lwext4 added went where Linux
+	 * would not look. */
+	if (hash_seed && (hash_seed[0] | hash_seed[1] |
+			  hash_seed[2] | hash_seed[3]) != 0)
 		memcpy(hash, hash_seed, sizeof(hash));
 
```
<!-- /hunks -->

### Patch 27 -- the superblock's geometry is not bounded

**Where:** `src/ext4_super.c`, `ext4_sb_check`.

**The defect:** the block size, 64bit descriptor size, blocks (or clusters)
and inodes per group, inode size and first data block were not checked, so
a damaged or crafted image opened -- and then divided by zero (#174, #90)
or overran its bitmaps (#93, part of #172).

**Shown:** two damaged superblocks (an impossible descriptor size, an
impossible group size) opened unpatched; patched, both are refused.

**The fix:** the Linux driver's limits: the block size at most 64 KiB;
64bit descriptors at least 64 bytes and a power of two; blocks (clusters
with bigalloc) and inodes per group at most one bitmap block's worth; the
inode size at most a block and a power of two; the first data block inside
the filesystem.

**Upstream:** issues #165, #111, #96, #174, #93, #90. Not fixed in master;
applies to master.

<!-- hunks 27 -->
```diff
--- a/src/ext4_super.c
+++ b/src/ext4_super.c
@@ -158,4 +158,45 @@ bool ext4_sb_check(struct ext4_sblock *s)
 		return false;
 
+	/* ext4edit patch 27 (upstream #165, #111 / #96): the limits the
+	 * Linux driver puts on a superblock before it trusts its geometry.
+	 * As published none of these was checked: a damaged or crafted image
+	 * gave a block size of 0 or of gigabytes (a division by zero, or a
+	 * cache of gigabyte blocks), descriptors too small for 64bit, or
+	 * groups larger than one bitmap block can describe -- and opened. */
+	uint32_t log_bs = ext4_get32(s, log_block_size);
+	if (log_bs > 6)		/* 1 KiB << 6: 64 KiB, the largest block */
+		return false;
+
+	uint32_t bsize = 1024u << log_bs;
+
+	if (ext4_sb_feature_incom(s, EXT4_FINCOM_64BIT)) {
+		/* 64bit descriptors are at least 64 bytes, a power of two
+		 * (the raw field: ext4_sb_get_desc_size rounds 0 up). */
+		uint16_t raw = ext4_get16(s, desc_size);
+		if (raw < EXT4_MAX_BLOCK_GROUP_DESCRIPTOR_SIZE ||
+		    (raw & (raw - 1)) != 0)
+			return false;
+	}
+
+	/* One bitmap block describes a group: bsize * 8 clusters (blocks
+	 * without bigalloc) and bsize * 8 inodes. */
+	if (ext4_sb_feature_ro_com(s, EXT4_FRO_COM_BIGALLOC)) {
+		if (ext4_get32(s, frags_per_group) > bsize * 8)
+			return false;
+	} else if (ext4_get32(s, blocks_per_group) > bsize * 8) {
+		return false;
+	}
+
+	if (ext4_get32(s, inodes_per_group) > bsize * 8)
+		return false;
+
+	/* An inode fits in a block, and its size is a power of two. */
+	uint16_t isize = ext4_get16(s, inode_size);
+	if (isize > bsize || (isize & (isize - 1)) != 0)
+		return false;
+
+	if (ext4_get32(s, first_data_block) >= ext4_sb_get_blocks_cnt(s))
+		return false;
+
 	if (!ext4_sb_verify_csum(s))
 		return false;
```
<!-- /hunks -->

### Patch 28 -- listing a directory of size 0 reads through a NULL pointer

**Where:** `src/ext4.c`, `ext4_dir_entry_next`.

**The defect:** the iterator succeeds with no entry when its position is at
or past the directory's size -- a directory whose size field is 0, say --
and the copy then read through a NULL pointer.

**Shown:** such a directory: unpatched, the listing crashed; patched, it
ends empty.

**The fix:** no entry ends the listing.

**Upstream:** pull request #112, issue #89. Not fixed in master (`a84a1b6`
rewrote the copy but still reads through the entry untested). Its one hunk
needs adapting to master.

<!-- hunks 28 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -2966,14 +2966,55 @@ const ext4_direntry *ext4_dir_entry_next(ext4_dir *d)
 	r = ext4_dir_iterator_init(&it, &dir, d->next_off);
 	if (r != EOK) {
+		d->last_err = r;
+		d->next_off = EXT4_DIR_ENTRY_OFFSET_TERM;
 		ext4_fs_put_inode_ref(&dir);
 		goto Finish;
 	}
 
-	memcpy(&d->de, it.curr, sizeof(ext4_direntry));
+	/* ext4edit patch 28 (upstream #112): the iterator succeeds with
+	 * no entry when the position is at or past the directory's size --
+	 * a directory whose size field is 0, say -- and the copy below read
+	 * through a NULL pointer, crashing the program.  No entry is the end
+	 * of the listing. */
+	if (!it.curr) {
+		d->next_off = EXT4_DIR_ENTRY_OFFSET_TERM;
+		ext4_dir_iterator_fini(&it);
+		ext4_fs_put_inode_ref(&dir);
+		goto Finish;
+	}
+
+	/* ext4edit patch 39: the entry is copied by its CONTENT length -- its */
+	/* 8-byte header and its name -- never by the size of ext4_direntry.   */
+	/* That size is 263 bytes (a 255-byte name), so copying it whole from  */
+	/* an entry that starts in a block's last 262 bytes read past the end  */
+	/* of the block's buffer: a heap over-read that crashed the program    */
+	/* only when the buffer happened to end at an unmapped page.  The      */
+	/* name length is taken through ext4_dir_en_get_name_len, as the       */
+	/* iterator's check took it (ext4_dir_iterator_set has already proved  */
+	/* the header and the name lie inside the entry and the block), and    */
+	/* held to the 255 bytes de.name holds.  The rest of d->de is zeroed,  */
+	/* so no byte of an earlier entry, or of the block's slack, is left    */
+	/* in it.  The record length still arrives as de.entry_length.         */
+	name_len = ext4_dir_en_get_name_len(&d->f.mp->fs.sb, it.curr);
+	if (name_len > sizeof(d->de.name)) {
+		/* Longer than any name ext4 allows: only what de.name holds. */
+		name_len = sizeof(d->de.name);
+	}
+
+	/* The cleared entry, then the header and the name over it. */
+	memset(&d->de, 0, sizeof(ext4_direntry));
+	memcpy(&d->de, it.curr, 8 + (size_t)name_len);
 	de = &d->de;
 
-	ext4_dir_iterator_next(&it);
+	/* ext4edit patch 35: as published the result of advancing was
+	 * ignored, so a block that could not be read ended the walk as if
+	 * it were the directory's end.  Now its error is kept and the walk
+	 * stops; the entry already copied above is still returned, and the
+	 * caller sees the error once its loop has ended. */
+	r = ext4_dir_iterator_next(&it);
+	if (r != EOK)
+		d->last_err = r;
 
-	d->next_off = it.curr ? it.curr_off : EXT4_DIR_ENTRY_OFFSET_TERM;
+	d->next_off = (r == EOK && it.curr) ? it.curr_off : EXT4_DIR_ENTRY_OFFSET_TERM;
 
 	ext4_dir_iterator_fini(&it);
```
<!-- /hunks -->

### Patch 29 -- the xattr block number's high half is shifted by 16

**Where:** `src/ext4_inode.c`, `ext4_inode_get_file_acl`.

**The defect:** the high half (bits 32-47) was shifted by 16, so on a
filesystem of 2^32 blocks or more an xattr block above that was looked for
in the wrong place. By reading. (The upstream pull request's own fix shifts
a 32-bit value by 32, which is undefined.)

**The fix:** widened to 64 bits before a shift of 32, as
`ext4_inode_set_file_acl` stores it.

**Upstream:** issue #88. Not fixed in master; applies to master.

<!-- hunks 29 -->
```diff
--- a/src/ext4_inode.c
+++ b/src/ext4_inode.c
@@ -286,6 +286,10 @@ uint64_t ext4_inode_get_file_acl(struct ext4_inode *inode,
 	uint64_t v = to_le32(inode->file_acl_lo);
 
+	/* ext4edit patch 29 (upstream #88): the high half is bits 32-47,
+	 * as ext4_inode_set_file_acl stores it -- widened to 64 bits BEFORE
+	 * the shift (the upstream PR shifts a 32-bit value by 32, which is
+	 * undefined).  As published it was shifted by 16. */
 	if (ext4_get32(sb, creator_os) == EXT4_SUPERBLOCK_OS_LINUX)
-		v |= (uint32_t)to_le16(inode->osd2.linux2.file_acl_high) << 16;
+		v |= (uint64_t)to_le16(inode->osd2.linux2.file_acl_high) << 32;
 
 	return v;
```
<!-- /hunks -->

### Patch 30 -- a failed htree walk leaks its blocks, and the add-entry caller releases wild

**Where:** `src/ext4_dir_idx.c`: `ext4_dir_dx_get_leaf`, its two callers,
and a new `ext4_dir_dx_release_walk`.

**The defect:** a failed walk (a damaged index node, an I/O error)
returned holding every level it had loaded; the lookup caller released one
and leaked the rest, and the add-entry caller released `dx_blks` up to a
`dx_blk` the walk sets only on success: a loop over stack garbage.

**Shown:** an indexed directory whose index root count was zeroed, then a
mkdir and a lookup in it: it did not crash unpatched in this build (the
undefined pointer happened to stop the loop), so the fix is by reading;
patched, both behave and a 3-level, 71 500-entry directory still checks
clean.

**The fix:** the walk releases everything it holds on every failure exit,
and the callers release nothing after a failed walk.

**Upstream:** pull request #133 (open). Not fixed in master; applies to
master.

<!-- hunks 30 -->
```diff
--- a/src/ext4_dir_idx.c
+++ b/src/ext4_dir_idx.c
@@ -533,4 +533,26 @@ static int ext4_dir_hinfo_init(struct ext4_hash_info *hinfo,
  * @return Standard error code
  */
+/* ext4edit patch 30 (upstream #133): release what a failed index walk
+ * holds -- dx_blocks from the first up to (not including) end, then extra
+ * when it is given -- so that ext4_dir_dx_get_leaf owns every block on
+ * failure, the root included, and its callers release nothing.  As
+ * published a failed walk left its blocks held (the lookup caller released
+ * one, leaking the rest), and the add-entry caller then released from
+ * dx_blks up to a dx_blk the walk had never set: a wild loop over stack
+ * garbage -- a crash -- on a damaged indexed directory or an I/O error. */
+static void ext4_dir_dx_release_walk(struct ext4_inode_ref *inode_ref,
+				     struct ext4_dir_idx_block *dx_blocks,
+				     struct ext4_dir_idx_block *end,
+				     struct ext4_block *extra)
+{
+	struct ext4_dir_idx_block *it;
+
+	for (it = dx_blocks; it < end; it++)
+		ext4_block_set(inode_ref->fs->bdev, &it->b);
+
+	if (extra)
+		ext4_block_set(inode_ref->fs->bdev, extra);
+}
+
 static int ext4_dir_dx_get_leaf(struct ext4_hash_info *hinfo,
 				struct ext4_inode_ref *inode_ref,
@@ -565,6 +587,11 @@ static int ext4_dir_dx_get_leaf(struct ext4_hash_info *hinfo,
 	while (true) {
 		uint16_t cnt = ext4_dir_dx_climit_get_count((void *)entries);
-		if ((cnt == 0) || (cnt > limit))
+		if ((cnt == 0) || (cnt > limit)) {
+			/* ext4edit patch 30: the levels above, and this
+			 * block, not yet recorded in dx_blocks. */
+			ext4_dir_dx_release_walk(inode_ref, dx_blocks,
+						 tmp_dx_blk, tmp_blk);
 			return EXT4_ERR_BAD_DX_DIR;
+		}
 
 		/* Do binary search in every node */
@@ -599,10 +626,20 @@ static int ext4_dir_dx_get_leaf(struct ext4_hash_info *hinfo,
 
 		r = ext4_fs_get_inode_dblk_idx(inode_ref, n_blk, &fblk, false);
-		if (r != EOK)
+		if (r != EOK) {
+			/* ext4edit patch 30: every level so far, this one
+			 * included (tmp_blk is its recorded copy). */
+			ext4_dir_dx_release_walk(inode_ref, dx_blocks,
+						 tmp_dx_blk + 1, NULL);
 			return r;
+		}
 
 		r = ext4_trans_block_get(inode_ref->fs->bdev, tmp_blk, fblk);
-		if (r != EOK)
+		if (r != EOK) {
+			/* ext4edit patch 30: as above; the failed get may
+			 * have cleared tmp_blk, but dx_blocks has the copy. */
+			ext4_dir_dx_release_walk(inode_ref, dx_blocks,
+						 tmp_dx_blk + 1, NULL);
 			return r;
+		}
 
 		entries = ((struct ext4_dir_idx_node *)tmp_blk->data)->entries;
@@ -617,4 +654,7 @@ static int ext4_dir_dx_get_leaf(struct ext4_hash_info *hinfo,
 		if (limit != entry_space) {
 			ext4_block_set(inode_ref->fs->bdev, tmp_blk);
+			/* ext4edit patch 30: and every level above it. */
+			ext4_dir_dx_release_walk(inode_ref, dx_blocks,
+						 tmp_dx_blk + 1, NULL);
 			return EXT4_ERR_BAD_DX_DIR;
 		}
@@ -759,5 +799,6 @@ int ext4_dir_dx_find_entry(struct ext4_dir_search_result *result,
 				  dx_blocks);
 	if (rc != EOK) {
-		ext4_block_set(fs->bdev, &root_block);
+		/* ext4edit patch 30: the failed walk has released every
+		 * block it held, the root included. */
 		return EXT4_ERR_BAD_DX_DIR;
 	}
```

- Also this patch's: the hunk of `src/ext4_dir_idx.c` at upstream line 1308, shown under patch 5.
<!-- /hunks -->

### Patch 31 -- removing a file frees a SHARED xattr block

**Where:** `src/ext4_fs.c`, `ext4_fs_free_inode`, and a new
`ext4_xattr_block_drop_ref` in `src/ext4_xattr.c` (declared in
`include/ext4_xattr.h`).

**The defect:** the Linux driver gives inodes with identical attributes one
xattr block and counts them in `h_refcount`; lwext4 freed the block
whatever the count, so it was reused while other files still pointed at it.

**Shown:** 128-byte inodes, `/a`'s xattr block shared with `/b`
(`h_refcount` 2), `/a` removed: unpatched, `e2fsck` "Extended attribute
block has reference count 2, should be 1"; patched, clean, `/b` keeps the
block with a count of 1.

**The fix:** a shared block has its count lowered and is re-signed; only
the last user's departure frees it.

**Upstream:** no issue found. Not fixed in master (its inode free still
frees the block unconditionally); applies to master with fuzz.

<!-- hunks 31 -->
```diff
--- a/include/ext4_xattr.h
+++ b/include/ext4_xattr.h
@@ -107,4 +107,14 @@ const char *ext4_get_xattr_name_prefix(uint8_t name_index,
 				       size_t *ret_prefix_len);
 
+/**@brief Drop one inode's reference to an xattr block (ext4edit patch 31):
+ *        a SHARED block (h_refcount above 1) has its count lowered and is
+ *        re-signed; only an unshared one may be freed by the caller.
+ * @param inode_ref The inode giving up the block
+ * @param blk       The xattr block
+ * @param free_it   Set true when the caller must free the block
+ * @return Standard error code */
+int ext4_xattr_block_drop_ref(struct ext4_inode_ref *inode_ref,
+			      ext4_fsblk_t blk, bool *free_it);
+
 #ifdef __cplusplus
 }
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -56,4 +56,5 @@
 #include "ext4_ialloc.h"
 #include "ext4_extent.h"
+#include "ext4_xattr.h"	/* ext4edit patch 31 */
 
 #include <string.h>
@@ -1040,7 +1041,17 @@ int ext4_fs_free_inode(struct ext4_inode_ref *inode_ref)
 	    ext4_inode_get_file_acl(inode_ref->inode, &fs->sb);
 	if (xattr_block) {
-		int rc = ext4_balloc_free_block(inode_ref, xattr_block);
+		/* ext4edit patch 31: a shared block keeps its other users;
+		 * only this inode's reference goes (ext4_xattr.c). */
+		bool free_it;
+		int rc = ext4_xattr_block_drop_ref(inode_ref, xattr_block,
+						   &free_it);
 		if (rc != EOK)
 			return rc;
+
+		if (free_it) {
+			rc = ext4_balloc_free_block(inode_ref, xattr_block);
+			if (rc != EOK)
+				return rc;
+		}
 
 		ext4_inode_set_file_acl(inode_ref->inode, &fs->sb, 0);
--- a/src/ext4_xattr.c
+++ b/src/ext4_xattr.c
@@ -162,4 +162,40 @@ ext4_xattr_set_block_checksum(struct ext4_inode_ref *inode_ref,
 	header->h_checksum =
 		ext4_xattr_block_checksum(inode_ref, blocknr, header);
+}
+
+/* ext4edit patch 31: an xattr block may be SHARED -- the Linux driver
+ * gives every inode with identical attributes the same block and counts
+ * them in h_refcount.  When an inode goes, its reference is dropped: a
+ * shared block keeps its other users (the count lowered, the block
+ * re-signed), and only the last user's departure frees it.  As published
+ * ext4_fs_free_inode freed the block whatever the count, so removing one
+ * such file freed a block other files still used: it was reused for data,
+ * and their attributes became whatever was written there. */
+int ext4_xattr_block_drop_ref(struct ext4_inode_ref *inode_ref,
+			      ext4_fsblk_t blk, bool *free_it)
+{
+	struct ext4_block block;
+	struct ext4_xattr_header *header;
+	uint32_t refs;
+	int r;
+
+	*free_it = true;
+
+	r = ext4_trans_block_get(inode_ref->fs->bdev, &block, blk);
+	if (r != EOK)
+		return r;
+
+	header = EXT4_XATTR_BHDR(&block);
+	refs = to_le32(header->h_refcount);
+
+	/* Shared: one fewer user, and nothing to free. */
+	if (to_le32(header->h_magic) == EXT4_XATTR_MAGIC && refs > 1) {
+		header->h_refcount = to_le32(refs - 1);
+		ext4_xattr_set_block_checksum(inode_ref, blk, header);
+		ext4_trans_set_block_dirty(block.buf);
+		*free_it = false;
+	}
+
+	return ext4_block_set(inode_ref->fs->bdev, &block);
 }
 
```
<!-- /hunks -->

### Patch 32 -- extent headers are not bounded, and the root header is never checked

**Where:** `src/ext4_extent.c`: `ext4_ext_check` and the start of
`ext4_find_extent`.

**The defect:** `eh_max` was never compared with what the node can hold,
the depth never bounded, and the root header in the inode never checked,
so a damaged or crafted header sent the searches past their buffers; and an
extent block failing its checksum was used anyway.

**Shown:** a file whose root header claims 255 entries of a maximum of
65 535: reading it is refused, read-only and writable, with the file beside
it still whole -- on the patched library only.

**The fix:** as the Linux driver's `__ext4_ext_check`: `eh_max` at most
what the node holds, depth at most 5, the root checked at the start of
every `ext4_find_extent` (it has no checksum tail). A failed extent-block
checksum REFUSES the block on a writable mount and is only logged on a
read-only one, so a damaged card stays readable for rescue.

**Upstream:** pull request #107, issues #100, #91. Not fixed in master;
applies to master with fuzz.

<!-- hunks 32 -->
```diff
--- a/src/ext4_extent.c
+++ b/src/ext4_extent.c
@@ -416,7 +416,23 @@ static int ext4_ext_check(struct ext4_inode_ref *inode_ref,
 		goto corrupted;
 	}
+	/* ext4edit patch 32 (upstream #107): as the Linux driver's
+	 * __ext4_ext_check -- eh_max no larger than the node can hold (4 in
+	 * the inode, (block - 12) / 12 in a block), and no deeper than 5
+	 * levels.  As published neither was bounded, so a damaged or crafted
+	 * header sent the searches past the end of the node. */
+	if (to_le16(eh->max_entries_count) >
+	    ext4_ext_max_entries(inode_ref, depth)) {
+		error_msg = "eh_max too large for the node";
+		goto corrupted;
+	}
+	if (depth > 5) {
+		error_msg = "tree deeper than 5 levels";
+		goto corrupted;
+	}
 
+	/* The root (pblk 0, in the inode) has no tail and no checksum. */
 	tail = find_ext4_extent_tail(eh);
-	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
+	if (pblk != 0 &&
+	    ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
 		if (tail->et_checksum != to_le32(ext4_ext_block_csum(inode_ref, eh))) {
 			ext4_dbg(DEBUG_EXTENT,
@@ -425,4 +441,12 @@ static int ext4_ext_check(struct ext4_inode_ref *inode_ref,
 				 pblk);
 
+			/* ext4edit patch 32: a session that may WRITE
+			 * refuses the block, as Linux does; read-only, the
+			 * block is still read (a damaged card stays
+			 * readable for rescue), as published. */
+			if (!inode_ref->fs->read_only) {
+				error_msg = "extent block checksum failed";
+				goto corrupted;
+			}
 		}
 	}
@@ -529,4 +553,13 @@ static int ext4_find_extent(struct ext4_inode_ref *inode_ref, ext4_lblk_t block,
 	eh = ext_inode_hdr(inode_ref->inode);
 	depth = ext_depth(inode_ref->inode);
+
+	/* ext4edit patch 32 (upstream #107): the ROOT header, in the inode,
+	 * is checked as the Linux driver checks it when it reads the inode;
+	 * as published only the tree blocks below it were, and a bad root
+	 * eh_max or depth went straight into the search and the path
+	 * allocation below. */
+	ret = ext4_ext_check(inode_ref, eh, depth, 0);
+	if (ret != EOK)
+		return ret;
 
 	if (path) {
```
<!-- /hunks -->

### Patch 33 -- the formatter's inodes per group is not a multiple of 8

**Where:** `src/ext4_mkfs.c`, `compute_inodes_per_group`.

**The defect:** the count was aligned only to the inodes one table block
holds -- 4 at 1 KiB blocks of 256-byte inodes -- so it could be a multiple
of 4 and not of 8. The last byte of each group's inode bitmap then holds
inodes and padding together, and e2fsck reports "Padding at end of inode
bitmap is not set" on a filesystem nothing has touched since it was made.

**Shown:** the 1-40 MiB sweep at 1 KiB blocks failed at 17, 20, 23, 33, 36
and 38 MiB, and 100 MiB (1972 per group), for every type; patched, every
image is clean.

**The fix:** round down to a multiple of 8 (at least 8), as e2fsprogs'
formatter does.

**Upstream:** no issue found. Not fixed in master; applies to master.

<!-- hunks 33 -->
```diff
--- a/src/ext4_mkfs.c
+++ b/src/ext4_mkfs.c
@@ -116,4 +116,20 @@ static uint32_t compute_inodes_per_group(struct ext4_mkfs_info *info)
 	uint32_t inodes = EXT4_DIV_ROUND_UP(info->inodes, block_groups);
 	inodes = EXT4_ALIGN(inodes, (info->block_size / info->inode_size));
+
+	/* ext4edit patch 33: the count is also made a multiple of 8, rounding
+	 * DOWN, as e2fsprogs' formatter does ("needed to simplify the bitmap
+	 * splicing code").  Aligned only to the inodes in one table block,
+	 * 1 KiB blocks of 256-byte inodes allow multiples of 4: a 100 MiB
+	 * filesystem got 1972, so the last byte of every group's inode bitmap
+	 * held four inodes and four padding bits, and e2fsck -fn rejects the
+	 * fresh filesystem ("Padding at end of inode bitmap is not set").  A
+	 * multiple of 8 is still a whole number of 2-, 4- and 8-inode table
+	 * blocks; for 16 or more per block the alignment above already gave
+	 * one, so nothing changes there.  The table's block count follows
+	 * from this figure (create_fs_aux_info), so it shrinks with it. */
+	inodes &= ~(uint32_t)7;
+
+	if (inodes < 8)
+		inodes = 8;
 
 	/* After properly rounding up the number of inodes/group,
```
<!-- /hunks -->

### Patch 34 -- a damaged xattr set releases the inode's block twice

**Where:** `src/ext4_xattr.c`, `ext4_fs_get_xattr_ref`.

**The defect:** when parsing the attribute set fails, the error path
released `inode_ref->block` -- the inode's own block, which the caller
releases again -- instead of the xattr block it had just taken. The second
release fails the block cache's assertion, which spins for ever.

**Shown:** an inline file whose 300-byte attribute is in a block whose
first entry's value offset was set to 0xFFFF: unpatched, the read hung;
patched, the read is refused and the session goes on.

**The fix:** release `&ref->block`.

**Upstream:** fixed in master by `18ddc30`. Its one hunk fails on master,
and it is not needed there.

<!-- hunks 34 -->
```diff
--- a/src/ext4_xattr.c
+++ b/src/ext4_xattr.c
@@ -856,6 +856,14 @@ int ext4_fs_get_xattr_ref(struct ext4_fs *fs, struct ext4_inode_ref *inode_ref,
 	if (rc != EOK) {
 		ext4_xattr_purge_items(ref);
+		/* ext4edit patch 34: release the xattr block this reference
+		 * took above.  As published this released inode_ref->block --
+		 * the INODE's block, which the caller still holds and releases
+		 * at its own put -- so a damaged attribute set released the
+		 * inode's block twice (the block cache's assertion, which
+		 * loops for ever) and the xattr block never.  block_loaded is
+		 * cleared below, so ext4_fs_put_xattr_ref, if called, does not
+		 * release it a second time. */
 		if (xattr_block)
-			ext4_block_set(fs->bdev, &inode_ref->block);
+			ext4_block_set(fs->bdev, &ref->block);
 
 		ref->block_loaded = false;
```
<!-- /hunks -->

### Patch 35 -- the directory iterator cannot report a read error

**Where:** `include/ext4.h` (`ext4_dir.last_err`), `src/ext4.c`
(`ext4_dir_open`, `ext4_dir_entry_next`).

**The defect:** `ext4_dir_entry_next` returned NULL both at the end and when
the directory's inode or a block could not be read, and ignored
`ext4_dir_iterator_next`'s result -- so a damaged directory listed as
complete, or as empty.

**Shown:** a directory whose extent points past the filesystem: unpatched,
it listed as empty with success; patched, the listing fails, and the
directory beside it lists all its entries.

**The fix:** the failures set `d->last_err` and end the walk; the entry
already copied is still returned; `ext4_dir_open` clears it. A caller
checks it once its loop has ended.

**Upstream:** no issue found. Not fixed in master (it still ignores the
iterator's result). 2 of its 4 hunks need adapting to master.

<!-- hunks 35 -->
```diff
--- a/include/ext4.h
+++ b/include/ext4.h
@@ -105,4 +105,13 @@ typedef struct ext4_dir {
 	/**@brief   Next entry offset*/
 	uint64_t next_off;
+	/* ext4edit patch 35: why the walk ended early.  ext4_dir_entry_next
+	 * returns NULL both at the end of the directory and when the
+	 * directory's inode or one of its blocks cannot be read, so a
+	 * caller could not tell a complete listing from a cut-short one.
+	 * EOK while the walk is sound (ext4_dir_open sets it); the error of
+	 * the read that failed once one has -- set when the walk stops, and
+	 * kept until the directory is opened again, so a caller checks it
+	 * once its loop has ended. */
+	int last_err;
 } ext4_dir;
 
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -2934,4 +2934,6 @@ int ext4_dir_open(ext4_dir *d, const char *path)
 	r = ext4_generic_open(&d->f, path, "r", false, 0, 0);
 	d->next_off = 0;
+	/* ext4edit patch 35: a fresh walk has no failure yet. */
+	d->last_err = EOK;
 	EXT4_MP_UNLOCK(mp);
 	return r;
@@ -2959,6 +2961,11 @@ const ext4_direntry *ext4_dir_entry_next(ext4_dir *d)
 	}
 
+	/* ext4edit patch 35: a failure to load the directory's inode or to
+	 * read the block at the walk's position ends the walk and is kept
+	 * in d->last_err, so the caller can tell it from the end. */
 	r = ext4_fs_get_inode_ref(&d->f.mp->fs, d->f.inode, &dir);
 	if (r != EOK) {
+		d->last_err = r;
+		d->next_off = EXT4_DIR_ENTRY_OFFSET_TERM;
 		goto Finish;
 	}
```

- Also this patch's: the hunk of `src/ext4.c` at upstream line 2966, shown under patch 28.
<!-- /hunks -->

### Patch 36 -- an assertion spins for ever, and data reaches several

**Where:** `include/ext4_debug.h`, `src/ext4_debug.c` (the macro), and the
assertion sites in `ext4_fs.c`, `ext4.c`, `ext4_dir.h` / `ext4_dir.c`,
`ext4_dir_idx.c`, `ext4_extent.c`, `ext4_xattr.c`, `ext4_bcache.c`,
`ext4_blockdev.h` / `ext4_blockdev.c`, `ext4_mkfs.c`, `ext4_journal.h` /
`ext4_journal.c`.

**The defect:** lwext4's own `ext4_assert` prints and loops `while (1)`,
and data reaches it. `ext4_fs_correspond_inode_mode` knew only directory,
file and symlink, so ANY path walk through a folder holding a device node,
a FIFO or a socket (OpenWrt's 0,0 whiteouts, a running system's sockets)
spun; `ext4_dir_write_entry` did the same when renaming one, and on an
oversize length; a directory with a HOLE spun in the extent lookup, and on
a block-mapped directory every reader read block 0 as a directory block and
the add-entry scan WROTE a name into it; every walk over a directory block
stepped by an unchecked `rec_len`, and a 0 never advanced. The other sites
guard a caller's arguments, lwext4's own bookkeeping, or I/O and memory
failures in the journal -- each also a spin.

**Shown:** an image with device nodes, a FIFO, a socket and a whiteout:
listing hung unpatched; patched, list, stat, rename and remove work,
`e2fsck` clean. A directory with a hole: hung unpatched; patched, refused,
block 0 untouched. Entries with a record length of 0: hung unpatched;
patched, every walk refused, none spins.

**The fix:** every assertion is an error return its callers handle (EINVAL
for a broken precondition, EIO for damage, ENOMEM), the calls that were
inside assertions are made outside them and checked; both type maps cover
all seven kinds, and an entry that does not say is resolved from the inode;
a new `ext4_dir_en_len_bad` refuses a `rec_len` under 12, off the 4-byte
step or past the block in every walk; a lookup that may not return a hole
refuses one (EIO); `ext4_dir_write_entry`, `ext4_block_set_lb_size`,
`jbd_add_revoke_block_tags` and `jbd_build_revoke_tree` return int; the
journal gains a sticky fault (a checkpoint that cannot read or allocate
stops, and the journal stop returns the fault with needs_recovery left set,
so the transactions are replayed later); the macro records its site
(`ext4_assert_record` / `_last` / `_clear`) and continues. Fixed on the
way: a failed htree insert after a leaf split reported success; a damaged
block's lookup went on to the next block; the in-block insert cut its
neighbour before writing.

**Upstream:** no issue found. Not fixed in master (the same macro, the same
three-type maps). 17 of its 94 hunks need adapting to master.

<!-- hunks 36 -->
```diff
--- a/include/ext4_blockdev.h
+++ b/include/ext4_blockdev.h
@@ -183,5 +183,9 @@ int ext4_block_flush_lba(struct ext4_blockdev *bdev, uint64_t lba);
  * @param   lb_size logical block size (in bytes)
  * @return  standard error code*/
-void ext4_block_set_lb_size(struct ext4_blockdev *bdev, uint32_t lb_bsize);
+/* ext4edit patch 36: returns int -- EINVAL, and the size left as it was,   */
+/* for a size that is 0 or not a multiple of the physical block size (and   */
+/* for a device whose physical block size is 0).                            */
+/* Upstream's is void and asserts on such a size.                           */
+int ext4_block_set_lb_size(struct ext4_blockdev *bdev, uint32_t lb_bsize);
 
 /**@brief   Block get function (through cache, don't read).
--- a/include/ext4_debug.h
+++ b/include/ext4_debug.h
@@ -161,4 +161,12 @@ uint32_t ext4_dmask_get(void);
 #endif
 
+/* ext4edit patch 36: where a failed ext4_assert is recorded (ext4_debug.c).  */
+/* _record keeps the FIRST site since the last _clear; _last gives it (NULL:  */
+/* none) with its line; the engine reads, reports and clears it with each     */
+/* failure it records.                                                        */
+void ext4_assert_record(const char *file, int line);
+const char *ext4_assert_last(int *line);
+void ext4_assert_clear(void);
+
 #if CONFIG_DEBUG_ASSERT
 /**@brief   Debug assertion.*/
@@ -166,11 +174,18 @@ uint32_t ext4_dmask_get(void);
 #include <stdio.h>
 
+/* ext4edit patch 36: a failed assertion is RECORDED and execution goes on;   */
+/* it never spins.  Upstream's own form printed and then looped `while (1)`   */
+/* for ever, so one assertion reached by unusual or damaged data -- a device  */
+/* node in a listed folder, say -- would hang the engine for good and the     */
+/* window with it.  Every assertion lwext4 had is an error return its         */
+/* callers handle; this form is the backstop for any an upstream change       */
+/* brings later, and the engine adds its site to the next failure's detail    */
+/* line so it is seen.  The do / while (0) makes the macro one statement      */
+/* wherever it is used.                                                       */
 #define ext4_assert(_v)                                                        \
 	do {                                                                   \
 		if (!(_v)) {                                                   \
-			printf("assertion failed:\nfile: %s\nline: %d\n",      \
-			       __FILE__, __LINE__);                            \
-			       while (1)				       \
-				       ;				       \
+			/* The condition failed: note the site, carry on. */  \
+			ext4_assert_record(__FILE__, __LINE__);                \
 		}                                                              \
 	} while (0)
--- a/include/ext4_dir.h
+++ b/include/ext4_dir.h
@@ -114,4 +114,37 @@ static inline void ext4_dir_en_set_entry_len(struct ext4_dir_en *de, uint16_t l)
 {
 	de->entry_len = to_le16(l);
+}
+
+/* ext4edit patch 36: the smallest record a directory entry can have. */
+#define EXT4_DIR_EN_MIN_LEN 12	// 8-byte header + a 1-4 byte name, rounded to 4 (e2fsprogs: ext2fs_dir_rec_len(1, 0))
+
+/*----------------------------------------------------------------------------*/
+/* ext4_dir_en_len_bad:                                                       */
+/*                                                                            */
+/* ext4edit patch 36.  Whether an entry's record length, read from disk, is   */
+/* one no walk may step by.  Every loop over a directory block advances by   */
+/* the entry's own rec_len; a 0 never advances (the walk spins for ever on   */
+/* the same entry), a length of 1 to 11 lands inside the next entry's        */
+/* header, a length that is not a multiple of 4 breaks the alignment every   */
+/* entry relies on, and a length running past the block reads (or writes)    */
+/* beyond the buffer.  e2fsck's pass 2 (e2fsprogs 1.47.4) reports all four   */
+/* as a corrupted directory; every walk that calls this treats the block as  */
+/* damaged and returns an error rather than step.                            */
+/*                                                                            */
+/* Arguments:                                                                 */
+/*     len   : the entry's rec_len.                                           */
+/*     off   : the entry's offset from the start of the block.               */
+/*     limit : the bytes the walk may cover (the block size, or less when    */
+/*             a checksum tail closes the block).                            */
+/*                                                                            */
+/* Returns:                                                                   */
+/*     bool : true when the length is unusable -- the block is damaged.      */
+/*----------------------------------------------------------------------------*/
+static inline bool ext4_dir_en_len_bad(uint32_t len, uint32_t off,
+				       uint32_t limit)
+{
+	/* Too short, off the 4-byte step, or past the end. */
+	return (len < EXT4_DIR_EN_MIN_LEN) || ((len % 4) != 0) ||
+	       (off + len > limit);
 }
 
@@ -208,4 +241,9 @@ int ext4_dir_iterator_fini(struct ext4_dir_iter *it);
 int ext4_dir_iterator_fini(struct ext4_dir_iter *it);
 
+/* ext4edit patch 36: the directory-entry type of an inode, all seven kinds  */
+/* (EXT4_DE_UNKNOWN for a mode that is none of them); see ext4_dir.c.        */
+uint8_t ext4_dir_inode_entry_type(struct ext4_sblock *sb,
+				  struct ext4_inode *inode);
+
 /**@brief Write directory entry to concrete data block.
  * @param sb        Superblock
@@ -216,7 +254,10 @@ int ext4_dir_iterator_fini(struct ext4_dir_iter *it);
  * @param name_len  Length of entry name
  */
-void ext4_dir_write_entry(struct ext4_sblock *sb, struct ext4_dir_en *en,
-			  uint16_t entry_len, struct ext4_inode_ref *child,
-			  const char *name, size_t name_len);
+/* ext4edit patch 36: returns an error code -- EOK, or EINVAL with nothing   */
+/* written for an entry longer than a block or a child of no known type.     */
+/* Upstream's is void and asserts on both.                                   */
+int ext4_dir_write_entry(struct ext4_sblock *sb, struct ext4_dir_en *en,
+			 uint16_t entry_len, struct ext4_inode_ref *child,
+			 const char *name, size_t name_len);
 
 /**@brief Add new entry to the directory.
--- a/include/ext4_journal.h
+++ b/include/ext4_journal.h
@@ -111,4 +111,13 @@ struct jbd_journal {
 
 	struct jbd_fs *jbd_fs;
+
+	/* ext4edit patch 36: the STICKY journal fault.  Set by a checkpoint   */
+	/* that could not read a journal block or get the memory it needed,   */
+	/* or by a transaction's teardown that could not get the blocks it    */
+	/* copies, where upstream asserts (a spin for ever).  Set, it stops   */
+	/* every later checkpoint and commit, and the journal stop returns it */
+	/* WITHOUT clearing needs_recovery, so the transactions not yet       */
+	/* checkpointed stay in the journal for the next replay.              */
+	int fault;	// EOK, or the error (EIO, ENOMEM, or the block layer's) that faulted the journal; cleared only by the next jbd_journal_start
 };
 
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -116,5 +116,10 @@ int ext4_device_register(struct ext4_blockdev *bd, struct ext4_bcache *bc,
 {
 	uint32_t i;
-	ext4_assert(bd && dev_name);
+	/* ext4edit patch 36: a missing device or name is refused, where  */
+	/* upstream asserts.                                              */
+	if (!bd || !dev_name) {
+		/* Nothing to register: refused, nothing changed. */
+		return EINVAL;
+	}
 
 	for (i = 0; i < CONFIG_EXT4_BLOCKDEVS_COUNT; ++i) {
@@ -337,5 +342,4 @@ int ext4_mount(const char *dev_name, const char *mount_point,
 	       bool read_only)
 {
-	ext4_assert(mount_point && dev_name);
 	int r;
 	int i;
@@ -345,4 +349,11 @@ int ext4_mount(const char *dev_name, const char *mount_point,
 	struct ext4_bcache *bc = 0;
 	struct ext4_mountpoint *mp = 0;
+
+	/* ext4edit patch 36: a missing name is refused, where upstream  */
+	/* asserts.                                                      */
+	if (!mount_point || !dev_name) {
+		/* Nothing to mount: refused before anything is claimed. */
+		return EINVAL;
+	}
 
 	if (mount_point[strlen(mount_point) - 1] != '/')
@@ -980,13 +991,27 @@ static int ext4_generic_open2(ext4_file *f, const char *path, int flags,
 
 		next_inode = ext4_dir_en_get_inode(result.dentry);
+		/* ext4edit patch 36: the entry's own type when it names one    */
+		/* (all seven map), else -- no filetype feature, or an entry    */
+		/* whose type byte is UNKNOWN or out of range -- the child      */
+		/* inode's mode, read.  Upstream asserts (a spin for ever) on   */
+		/* every type but directory, file and symlink.  A failed read   */
+		/* releases the search result before leaving, which upstream's  */
+		/* no-filetype branch does not (a cache block held for ever).   */
+		imode = 0;
 		if (ext4_sb_feature_incom(sb, EXT4_FINCOM_FILETYPE)) {
 			uint8_t t;
 			t = ext4_dir_en_get_inode_type(sb, result.dentry);
 			imode = ext4_fs_correspond_inode_mode(t);
-		} else {
+		}
+		if (imode == 0) {
+			/* The entry does not say: ask the inode. */
 			struct ext4_inode_ref child_ref;
 			r = ext4_fs_get_inode_ref(fs, next_inode, &child_ref);
-			if (r != EOK)
+			if (r != EOK) {
+				/* Unreadable: the search result released, */
+				/* and the walk ends with the error.        */
+				ext4_dir_destroy_result(&ref, &result);
 				break;
+			}
 
 			imode = ext4_inode_type(sb, child_ref.inode);
@@ -1142,13 +1167,23 @@ static int ext4_create_hardlink(const char *path,
 
 		next_inode = result.dentry->inode;
+		/* ext4edit patch 36: as in ext4_generic_open2 -- the entry's */
+		/* type when it names one, else the child inode's mode; the     */
+		/* search result released on a failed read.                     */
+		inode_mode = 0;
 		if (ext4_sb_feature_incom(sb, EXT4_FINCOM_FILETYPE)) {
 			uint8_t t;
 			t = ext4_dir_en_get_inode_type(sb, result.dentry);
 			inode_mode = ext4_fs_correspond_inode_mode(t);
-		} else {
+		}
+		if (inode_mode == 0) {
+			/* The entry does not say: ask the inode. */
 			struct ext4_inode_ref child_ref;
 			r = ext4_fs_get_inode_ref(fs, next_inode, &child_ref);
-			if (r != EOK)
+			if (r != EOK) {
+				/* Unreadable: the search result released, */
+				/* and the walk ends with the error.        */
+				ext4_dir_destroy_result(&ref, &result);
 				break;
+			}
 
 			inode_mode = ext4_inode_type(sb, child_ref.inode);
@@ -1555,5 +1590,10 @@ int ext4_fclose(ext4_file *f)
 int ext4_fclose(ext4_file *f)
 {
-	ext4_assert(f && f->mp);
+	/* ext4edit patch 36: a file never opened is refused, where  */
+	/* upstream asserts.                                         */
+	if (!f || !f->mp) {
+		/* No file, or not open: refused, nothing touched. */
+		return EINVAL;
+	}
 
 	f->mp = 0;
@@ -1612,5 +1652,10 @@ int ext4_ftruncate(ext4_file *f, uint64_t size)
 {
 	int r;
-	ext4_assert(f && f->mp);
+	/* ext4edit patch 36: a file never opened is refused, where  */
+	/* upstream asserts.                                         */
+	if (!f || !f->mp) {
+		/* No file, or not open: refused, nothing touched. */
+		return EINVAL;
+	}
 
 	if (f->mp->fs.read_only)
@@ -1648,5 +1693,10 @@ int ext4_fread(ext4_file *f, void *buf, size_t size, size_t *rcnt)
 	struct ext4_inode_ref ref;
 
-	ext4_assert(f && f->mp);
+	/* ext4edit patch 36: a file never opened is refused, where  */
+	/* upstream asserts.                                         */
+	if (!f || !f->mp) {
+		/* No file, or not open: refused, nothing touched. */
+		return EINVAL;
+	}
 
 	if (f->flags & O_WRONLY)
@@ -1808,5 +1858,10 @@ int ext4_fwrite(ext4_file *f, const void *buf, size_t size, size_t *wcnt)
 	int r, rr = EOK;
 
-	ext4_assert(f && f->mp);
+	/* ext4edit patch 36: a file never opened is refused, where  */
+	/* upstream asserts.                                         */
+	if (!f || !f->mp) {
+		/* No file, or not open: refused, nothing touched. */
+		return EINVAL;
+	}
 
 	if (f->mp->fs.read_only)
@@ -2255,5 +2310,10 @@ static int ext4_fsymlink_set(ext4_file *f, const void *buf, uint32_t size)
 	int r;
 
-	ext4_assert(f && f->mp);
+	/* ext4edit patch 36: a file never opened is refused, where  */
+	/* upstream asserts.                                         */
+	if (!f || !f->mp) {
+		/* No file, or not open: refused, nothing touched. */
+		return EINVAL;
+	}
 
 	if (!size)
--- a/src/ext4_bcache.c
+++ b/src/ext4_bcache.c
@@ -70,5 +70,10 @@ int ext4_bcache_init_dynamic(struct ext4_bcache *bc, uint32_t cnt,
 			     uint32_t itemsize)
 {
-	ext4_assert(bc && cnt && itemsize);
+	/* ext4edit patch 36: a missing cache or a zero size is refused,  */
+	/* where upstream asserts.                                        */
+	if (!bc || !cnt || !itemsize) {
+		/* Nothing to set up: refused, nothing touched. */
+		return EINVAL;
+	}
 
 	memset(bc, 0, sizeof(struct ext4_bcache));
@@ -262,16 +267,36 @@ int ext4_bcache_free(struct ext4_bcache *bc, struct ext4_block *b)
 int ext4_bcache_free(struct ext4_bcache *bc, struct ext4_block *b)
 {
-	struct ext4_buf *buf = b->buf;
+	struct ext4_buf *buf;
 
-	ext4_assert(bc && b);
+	/* ext4edit patch 36: each of these four tests is an assertion in     */
+	/* upstream (a spin for ever).  Every one means a block RELEASED      */
+	/* TWICE or never got -- a lwext4 bug on an error path, not data --   */
+	/* so each is refused with EINVAL and the buffer is left untouched: a */
+	/* double release must not drive the reference count below zero and  */
+	/* free a block still in use.  (b is tested before it is read;        */
+	/* upstream reads b->buf first.)                                      */
+	if (!bc || !b) {
+		/* No cache or no block: refused. */
+		return EINVAL;
+	}
 
 	/*Check if valid.*/
-	ext4_assert(b->lb_id);
+	if (!b->lb_id) {
+		/* A block never got: refused. */
+		return EINVAL;
+	}
 
 	/*Block should have a valid pointer to ext4_buf.*/
-	ext4_assert(buf);
+	buf = b->buf;
+	if (!buf) {
+		/* No buffer behind it: refused. */
+		return EINVAL;
+	}
 
 	/*Check if someone don't try free unreferenced block cache.*/
-	ext4_assert(buf->refctr);
+	if (!buf->refctr) {
+		/* Already released: refused, the count left at zero. */
+		return EINVAL;
+	}
 
 	/*Just decrease reference counter*/
--- a/src/ext4_blockdev.c
+++ b/src/ext4_blockdev.c
@@ -48,18 +48,29 @@ static void ext4_bdif_lock(struct ext4_blockdev *bdev)
 static void ext4_bdif_lock(struct ext4_blockdev *bdev)
 {
+	int r;
+
 	if (!bdev->bdif->lock)
 		return;
 
-	int r = bdev->bdif->lock(bdev);
-	ext4_assert(r == EOK);
+	/* ext4edit patch 36: the result is not asserted (upstream spins for  */
+	/* ever on a failed lock).  The engine installs no lock callbacks     */
+	/* (bfe_blockdev.c's static instance passes 0), so this line is never */
+	/* reached there; a callback that CAN fail must report through the    */
+	/* I/O call it guards, since this function has no way to return it -- */
+	/* the result is taken and deliberately not used.                     */
+	r = bdev->bdif->lock(bdev);
 }
 
 static void ext4_bdif_unlock(struct ext4_blockdev *bdev)
 {
+	int r;
+
 	if (!bdev->bdif->unlock)
 		return;
 
-	int r = bdev->bdif->unlock(bdev);
-	ext4_assert(r == EOK);
+	/* ext4edit patch 36: as ext4_bdif_lock -- not asserted, and never  */
+	/* reached in the engine (no unlock callback is installed); the     */
+	/* result is taken and deliberately not used.                       */
+	r = bdev->bdif->unlock(bdev);
 }
 
@@ -87,10 +98,15 @@ int ext4_block_init(struct ext4_blockdev *bdev)
 {
 	int rc;
-	ext4_assert(bdev);
-	ext4_assert(bdev->bdif);
-	ext4_assert(bdev->bdif->open &&
-		   bdev->bdif->close &&
-		   bdev->bdif->bread &&
-		   bdev->bdif->bwrite);
+	/* ext4edit patch 36: a device without its interface or one of the   */
+	/* four calls lwext4 needs is refused, where upstream asserts.        */
+	if (!bdev || !bdev->bdif) {
+		/* No device or no interface: refused, nothing opened. */
+		return EINVAL;
+	}
+	if (!bdev->bdif->open || !bdev->bdif->close ||
+	    !bdev->bdif->bread || !bdev->bdif->bwrite) {
+		/* A required call missing: refused, nothing opened. */
+		return EINVAL;
+	}
 
 	if (bdev->bdif->ph_refctr) {
@@ -110,5 +126,9 @@ int ext4_block_bind_bcache(struct ext4_blockdev *bdev, struct ext4_bcache *bc)
 int ext4_block_bind_bcache(struct ext4_blockdev *bdev, struct ext4_bcache *bc)
 {
-	ext4_assert(bdev && bc);
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!bdev || !bc) {
+		/* Nothing to bind: refused, nothing changed. */
+		return EINVAL;
+	}
 	bdev->bc = bc;
 	bc->bdev = bdev;
@@ -116,16 +136,33 @@ int ext4_block_bind_bcache(struct ext4_blockdev *bdev, struct ext4_bcache *bc)
 }
 
-void ext4_block_set_lb_size(struct ext4_blockdev *bdev, uint32_t lb_bsize)
+/* ext4edit patch 36: returns int -- EINVAL, nothing set, for a logical   */
+/* size that is 0 or not a multiple of the physical (upstream asserts).   */
+/* The engine fixes the physical size at 512 (bfe_blockdev.c's            */
+/* BFD_SECTOR_BYTES) and the superblock bounds hold the block size to     */
+/* 1 KiB - 64 KiB, so data cannot reach it; the three callers (the mount, */
+/* lwext4's mkfs, the engine's bfe_mkfs.c) check it all the same.         */
+int ext4_block_set_lb_size(struct ext4_blockdev *bdev, uint32_t lb_bsize)
 {
 	/*Logical block size has to be multiply of physical */
-	ext4_assert(!(lb_bsize % bdev->bdif->ph_bsize));
+	if (!lb_bsize || !bdev->bdif->ph_bsize ||
+	    (lb_bsize % bdev->bdif->ph_bsize)) {
+		/* Zero, or not whole physical blocks: refused, nothing set. */
+		return EINVAL;
+	}
 
 	bdev->lg_bsize = lb_bsize;
 	bdev->lg_bcnt = bdev->part_size / lb_bsize;
+
+	/* The logical size is set. */
+	return EOK;
 }
 
 int ext4_block_fini(struct ext4_blockdev *bdev)
 {
-	ext4_assert(bdev);
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!bdev) {
+		/* No device: refused, nothing closed. */
+		return EINVAL;
+	}
 
 	if (!bdev->bdif->ph_refctr)
@@ -193,5 +230,10 @@ int ext4_block_cache_shake(struct ext4_blockdev *bdev)
 
 		buf = ext4_buf_lowest_lru(bdev->bc);
-		ext4_assert(buf);
+		/* ext4edit patch 36: the lowest of a non-empty tree is never  */
+		/* NULL; kept as a guard that returns where upstream asserts.  */
+		if (!buf) {
+			/* Cannot happen: the shake stops with EIO. */
+			return EIO;
+		}
 		if (ext4_bcache_test_flag(buf, BC_DIRTY)) {
 			r = ext4_block_flush_buf(bdev, buf);
@@ -212,5 +254,9 @@ int ext4_block_get_noread(struct ext4_blockdev *bdev, struct ext4_block *b,
 	int r;
 
-	ext4_assert(bdev && b);
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!bdev || !b) {
+		/* No device or no block: refused, nothing got. */
+		return EINVAL;
+	}
 
 	if (!bdev->bdif->ph_refctr)
@@ -265,6 +311,10 @@ int ext4_block_set(struct ext4_blockdev *bdev, struct ext4_block *b)
 int ext4_block_set(struct ext4_blockdev *bdev, struct ext4_block *b)
 {
-	ext4_assert(bdev && b);
-	ext4_assert(b->buf);
+	/* ext4edit patch 36: a block never got (no buffer) is refused, where  */
+	/* upstream asserts -- as ext4_bcache_free refuses a double release.   */
+	if (!bdev || !b || !b->buf) {
+		/* Nothing to give back: refused, nothing released. */
+		return EINVAL;
+	}
 
 	if (!bdev->bdif->ph_refctr)
@@ -280,5 +330,9 @@ int ext4_blocks_get_direct(struct ext4_blockdev *bdev, void *buf, uint64_t lba,
 	uint32_t pb_cnt;
 
-	ext4_assert(bdev && buf);
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!bdev || !buf) {
+		/* No device or no buffer: refused, no I/O done. */
+		return EINVAL;
+	}
 
 	pba = (lba * bdev->lg_bsize + bdev->part_offset) / bdev->bdif->ph_bsize;
@@ -294,5 +348,9 @@ int ext4_blocks_set_direct(struct ext4_blockdev *bdev, const void *buf,
 	uint32_t pb_cnt;
 
-	ext4_assert(bdev && buf);
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!bdev || !buf) {
+		/* No device or no buffer: refused, no I/O done. */
+		return EINVAL;
+	}
 
 	pba = (lba * bdev->lg_bsize + bdev->part_offset) / bdev->bdif->ph_bsize;
@@ -312,5 +370,9 @@ int ext4_block_writebytes(struct ext4_blockdev *bdev, uint64_t off,
 	const uint8_t *p = (void *)buf;
 
-	ext4_assert(bdev && buf);
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!bdev || !buf) {
+		/* No device or no buffer: refused, no I/O done. */
+		return EINVAL;
+	}
 
 	if (!bdev->bdif->ph_refctr)
@@ -380,5 +442,9 @@ int ext4_block_readbytes(struct ext4_blockdev *bdev, uint64_t off, void *buf,
 	uint8_t *p = (void *)buf;
 
-	ext4_assert(bdev && buf);
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!bdev || !buf) {
+		/* No device or no buffer: refused, no I/O done. */
+		return EINVAL;
+	}
 
 	if (!bdev->bdif->ph_refctr)
@@ -438,5 +504,10 @@ int ext4_block_cache_flush(struct ext4_blockdev *bdev)
 		int r;
 		struct ext4_buf *buf = SLIST_FIRST(&bdev->bc->dirty_list);
-		ext4_assert(buf);
+		/* ext4edit patch 36: the first of a non-empty list is never */
+		/* NULL; kept as a guard that returns where upstream asserts.  */
+		if (!buf) {
+			/* Cannot happen: the flush stops with EIO. */
+			return EIO;
+		}
 		r = ext4_block_flush_buf(bdev, buf);
 		if (r != EOK)
--- a/src/ext4_debug.c
+++ b/src/ext4_debug.c
@@ -60,4 +60,82 @@ uint32_t ext4_dmask_get(void)
 }
 
+/* ext4edit patch 36: the first failed ext4_assert since the last clear --    */
+/* documentation table.  One engine call runs at a time (the engine's own    */
+/* rule), so these need no lock.                                              */
+static const char *assert_file;	// __FILE__ of the site, a string literal (never freed); NULL: none
+static int assert_line;		// its __LINE__; meaningful only while assert_file is set
+
+/*----------------------------------------------------------------------------*/
+/* ext4_assert_record:                                                        */
+/*                                                                            */
+/* ext4edit patch 36.  Note a failed ext4_assert -- the only thing a failed   */
+/* assertion does.  The FIRST site since the last clear is kept: a later one  */
+/* is most likely a consequence of it, and the first is the one worth         */
+/* reporting.  The engine reads it when it records a failure (BFX_Fail in     */
+/* bifrost_engine.c) and clears it as each call begins (BFX_Enter).          */
+/*                                                                            */
+/* Arguments:                                                                 */
+/*     file : the site's __FILE__ (a string literal; only the pointer is      */
+/*            kept).                                                          */
+/*     line : the site's __LINE__.                                            */
+/*                                                                            */
+/* Returns:                                                                   */
+/*     void : the site is recorded unless one already is.                     */
+/*----------------------------------------------------------------------------*/
+void ext4_assert_record(const char *file, int line)
+{
+	if (!assert_file) {
+		/* Nothing recorded yet: this is the first, so it is kept; a */
+		/* site already held is left as it is.                       */
+		assert_file = file;
+		assert_line = line;
+	}
+}
+
+/*----------------------------------------------------------------------------*/
+/* ext4_assert_last:                                                          */
+/*                                                                            */
+/* ext4edit patch 36.  The site ext4_assert_record kept, for the engine's     */
+/* detail line.  Reading it does not clear it.                                */
+/*                                                                            */
+/* Arguments:                                                                 */
+/*     line : receives the site's line (left alone when none); may be NULL.   */
+/*                                                                            */
+/* Returns:                                                                   */
+/*     const char * : the site's file, or NULL when no assertion failed      */
+/*                    since the last clear.                                   */
+/*----------------------------------------------------------------------------*/
+const char *ext4_assert_last(int *line)
+{
+	if (assert_file && line) {
+		/* A site is held and the caller wants its line: give it. */
+		*line = assert_line;
+	}
+
+	/* The file, or NULL for none -- which tells the caller whether the */
+	/* line was given.                                                  */
+	return assert_file;
+}
+
+/*----------------------------------------------------------------------------*/
+/* ext4_assert_clear:                                                         */
+/*                                                                            */
+/* ext4edit patch 36.  Forget the recorded site, once it has been reported    */
+/* or as an engine call begins, so a later failure never carries a site that  */
+/* belongs to an earlier one.                                                 */
+/*                                                                            */
+/* Arguments:                                                                 */
+/*     None.                                                                  */
+/*                                                                            */
+/* Returns:                                                                   */
+/*     void : nothing is recorded afterwards.                                 */
+/*----------------------------------------------------------------------------*/
+void ext4_assert_clear(void)
+{
+	/* Nothing held: the next failed assertion is the first again. */
+	assert_file = NULL;
+	assert_line = 0;
+}
+
 
 
--- a/src/ext4_dir.c
+++ b/src/ext4_dir.c
@@ -176,8 +176,14 @@ static int ext4_dir_iterator_set(struct ext4_dir_iter *it,
 	en = (void *)(it->curr_blk.data + off_in_block);
 
-	/* Ensure that the whole entry does not overflow the block */
+	/* Ensure that the whole entry does not overflow the block.  ext4edit  */
+	/* patch 36: and that it is long enough to step by -- upstream checks  */
+	/* only the overflow, so a rec_len of 0 makes iterator_next seek to    */
+	/* the same entry for ever: an empty one spins, a used one is returned */
+	/* again and again until a listing runs out of memory.                 */
 	uint16_t length = ext4_dir_en_get_entry_len(en);
-	if (off_in_block + length > block_size)
+	if (ext4_dir_en_len_bad(length, off_in_block, block_size)) {
+		/* Not a length to step by: the block is damaged. */
 		return EIO;
+	}
 
 	/* Ensure the name length is not too large */
@@ -297,26 +303,77 @@ int ext4_dir_iterator_fini(struct ext4_dir_iter *it)
 }
 
-void ext4_dir_write_entry(struct ext4_sblock *sb, struct ext4_dir_en *en,
-			  uint16_t entry_len, struct ext4_inode_ref *child,
-			  const char *name, size_t name_len)
+/*----------------------------------------------------------------------------*/
+/* ext4_dir_inode_entry_type:                                                 */
+/*                                                                            */
+/* ext4edit patch 36.  The directory-entry type byte that names an inode of   */
+/* this mode -- all seven kinds ext4 has, where upstream knows only           */
+/* directory, file and symlink and stops on an assertion (a spin for ever,    */
+/* with lwext4's own assert) for anything else.  Device nodes, FIFOs and      */
+/* sockets are ordinary on a Linux root filesystem -- an OpenWrt overlay's    */
+/* whiteouts are 0,0 character devices -- so with upstream's mapping,         */
+/* RENAMING one, which writes its entry anew, hangs the engine.  A mode that  */
+/* is none of the seven (damage) gives EXT4_DE_UNKNOWN, which the callers     */
+/* refuse before anything is written.                                         */
+/*                                                                            */
+/* Arguments:                                                                 */
+/*     sb    : the superblock (the mode's layout depends on it).              */
+/*     inode : the inode the entry will name.                                 */
+/*                                                                            */
+/* Returns:                                                                   */
+/*     uint8_t : an EXT4_DE_* type, or EXT4_DE_UNKNOWN.                        */
+/*----------------------------------------------------------------------------*/
+uint8_t ext4_dir_inode_entry_type(struct ext4_sblock *sb,
+				  struct ext4_inode *inode)
 {
-	/* Check maximum entry length */
-	ext4_assert(entry_len <= ext4_sb_get_block_size(sb));
+	switch (ext4_inode_type(sb, inode)) {
+	/* One inode type per case: the converse of ext4_fs_correspond_inode_mode. */
+	case EXT4_INODE_MODE_DIRECTORY:
+		return EXT4_DE_DIR;
+	case EXT4_INODE_MODE_FILE:
+		return EXT4_DE_REG_FILE;
+	case EXT4_INODE_MODE_SOFTLINK:
+		return EXT4_DE_SYMLINK;
+	case EXT4_INODE_MODE_CHARDEV:
+		return EXT4_DE_CHRDEV;
+	case EXT4_INODE_MODE_BLOCKDEV:
+		return EXT4_DE_BLKDEV;
+	case EXT4_INODE_MODE_FIFO:
+		return EXT4_DE_FIFO;
+	case EXT4_INODE_MODE_SOCKET:
+		return EXT4_DE_SOCK;
+	default:
+		/* Not a type ext4 has: damage. */
+		return EXT4_DE_UNKNOWN;
+	}
+}
+
+/* ext4edit patch 36: returns int -- EINVAL, and NOTHING written, for an   */
+/* entry longer than a block (a caller's arithmetic, never data) or a      */
+/* child whose mode is no known type (damage) -- where upstream asserts (a */
+/* spin for ever) on both, and on every device node, FIFO and socket       */
+/* besides.  Every caller checks it; ext4_dir_add_entry also refuses an    */
+/* unknown type before it changes anything, so data cannot reach the late  */
+/* refusal.                                                                */
+int ext4_dir_write_entry(struct ext4_sblock *sb, struct ext4_dir_en *en,
+			 uint16_t entry_len, struct ext4_inode_ref *child,
+			 const char *name, size_t name_len)
+{
+	uint8_t de_type;
+
+	/* Both refusals come before the first byte is written. */
+	if (entry_len > ext4_sb_get_block_size(sb)) {
+		/* Longer than a block: refused. */
+		return EINVAL;
+	}
+
+	/* The type the entry will carry, from the child's mode. */
+	de_type = ext4_dir_inode_entry_type(sb, child->inode);
+	if (de_type == EXT4_DE_UNKNOWN) {
+		/* No type an entry can name: refused. */
+		return EINVAL;
+	}
 
 	/* Set type of entry */
-	switch (ext4_inode_type(sb, child->inode)) {
-	case EXT4_INODE_MODE_DIRECTORY:
-		ext4_dir_en_set_inode_type(sb, en, EXT4_DE_DIR);
-		break;
-	case EXT4_INODE_MODE_FILE:
-		ext4_dir_en_set_inode_type(sb, en, EXT4_DE_REG_FILE);
-		break;
-	case EXT4_INODE_MODE_SOFTLINK:
-		ext4_dir_en_set_inode_type(sb, en, EXT4_DE_SYMLINK);
-		break;
-	default:
-		/* FIXME: right now we only support 3 inode type. */
-		ext4_assert(0);
-	}
+	ext4_dir_en_set_inode_type(sb, en, de_type);
 
 	/* Set basic attributes */
@@ -327,4 +384,7 @@ void ext4_dir_write_entry(struct ext4_sblock *sb, struct ext4_dir_en *en,
 	/* Write name */
 	memcpy(en->name, name, name_len);
+
+	/* The entry is whole. */
+	return EOK;
 }
 
@@ -336,9 +396,26 @@ int ext4_dir_add_entry(struct ext4_inode_ref *parent, const char *name,
 	struct ext4_sblock *sb = &parent->fs->sb;
 
+	/* ext4edit patch 36: a child whose mode is no type an entry can name  */
+	/* (damage) is refused HERE, before the inline conversion, the index   */
+	/* or a new block changes anything -- every path below ends in         */
+	/* ext4_dir_write_entry, which would refuse it only after a block had  */
+	/* been claimed for it.                                                */
+	if (ext4_dir_inode_entry_type(sb, child->inode) == EXT4_DE_UNKNOWN) {
+		/* Damage: refused, nothing changed. */
+		return EINVAL;
+	}
+
+	/* ext4edit patch 7: an inline directory becomes a block-backed one
+	 * before an entry is added to it; the linear code below then applies. */
+	r = ext4_inline_convert(parent);
+	if (r != EOK)
+		return r;
+
 #if CONFIG_DIR_INDEX_ENABLE
 	/* Index adding (if allowed) */
 	if ((ext4_sb_feature_com(sb, EXT4_FCOM_DIR_INDEX)) &&
 	    (ext4_inode_has_flag(parent->inode, EXT4_INODE_FLAG_INDEX))) {
-		r = ext4_dir_dx_add_entry(parent, child, name);
+		/* ext4edit patch 50: the name's own length passed on. */
+		r = ext4_dir_dx_add_entry(parent, child, name, name_len);
 
 		/* Check if index is not corrupted */
@@ -390,4 +467,12 @@ int ext4_dir_add_entry(struct ext4_inode_ref *parent, const char *name,
 			success = true;
 
+		/* ext4edit patch 36: only ENOSPC means "try the next block"; */
+		/* any other failure (EIO on a damaged block) is the answer --  */
+		/* moving on would add the name to a later or a NEW block of a  */
+		/* directory known to be damaged.  The insert's result is kept  */
+		/* here, since r is reused for the release.                     */
+		int r_insert;
+		r_insert = r;
+
 		r = ext4_block_set(fs->bdev, &block);
 		if (r != EOK)
@@ -396,4 +481,8 @@ int ext4_dir_add_entry(struct ext4_inode_ref *parent, const char *name,
 		if (success)
 			return EOK;
+		if (r_insert != ENOSPC) {
+			/* A failure other than a full block: the answer. */
+			return r_insert;
+		}
 	}
 
@@ -418,11 +507,28 @@ int ext4_dir_add_entry(struct ext4_inode_ref *parent, const char *name,
 
 	/* Save new block */
+	uint16_t el;	// ext4edit patch 36: the entry's room, kept for the fallback below
+	int r_write;	// ext4edit patch 36: the entry write's result
+	el = block_size;
 	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
-		uint16_t el = block_size - sizeof(struct ext4_dir_entry_tail);
-		ext4_dir_write_entry(sb, blk_en, el, child, name, name_len);
+		el = block_size - sizeof(struct ext4_dir_entry_tail);
+		r_write = ext4_dir_write_entry(sb, blk_en, el, child, name,
+					       name_len);
 		ext4_dir_init_entry_tail(EXT4_DIRENT_TAIL(b.data, block_size));
 	} else {
-		ext4_dir_write_entry(sb, blk_en, block_size, child, name,
-				name_len);
+		r_write = ext4_dir_write_entry(sb, blk_en, block_size, child,
+					       name, name_len);
+	}
+
+	/* ext4edit patch 36: the type was checked on the way in and the     */
+	/* length is the block's, so this cannot fail from data.  If it does, */
+	/* the block -- already the directory's, its size grown -- must not   */
+	/* be left zeroed: a rec_len of 0 is exactly the damage every walk    */
+	/* refuses.  So it becomes one EMPTY record (inode 0) over the same   */
+	/* room, a valid block holding nothing, and the failure is returned.  */
+	if (r_write != EOK) {
+		/* The write failed: one empty record over the whole room. */
+		ext4_dir_en_set_inode(blk_en, 0);
+		ext4_dir_en_set_entry_len(blk_en, el);
+		ext4_dir_en_set_name_len(sb, blk_en, 0);
 	}
 
@@ -430,4 +536,11 @@ int ext4_dir_add_entry(struct ext4_inode_ref *parent, const char *name,
 	ext4_trans_set_block_dirty(b.buf);
 	r = ext4_block_set(fs->bdev, &b);
+
+	/* ext4edit patch 36: the entry's own failure outranks the release's  */
+	/* success.                                                           */
+	if (r_write != EOK) {
+		/* The entry was not written: that is the result. */
+		return r_write;
+	}
 
 	return r;
@@ -505,7 +618,19 @@ int ext4_dir_find_entry(struct ext4_dir_search_result *result,
 		/* Entry not found - put block and continue to the next block */
 
+		/* ext4edit patch 36: ...unless the block was DAMAGED (EIO from  */
+		/* its walk): that is the lookup's answer, not "look further" -- */
+		/* upstream goes on, so a name in the damaged block reads as     */
+		/* absent and could be created a second time.  The walk's result */
+		/* is kept here, since r is reused for the release.              */
+		int r_find;
+		r_find = r;
+
 		r = ext4_block_set(parent->fs->bdev, &b);
 		if (r != EOK)
 			return r;
+		if (r_find != ENOENT) {
+			/* The block was damaged: that is the answer. */
+			return r_find;
+		}
 	}
 
@@ -538,19 +663,4 @@ int ext4_dir_remove_entry(struct ext4_inode_ref *parent, const char *name,
 	 */
 	if (pos != 0) {
-		uint32_t offset = 0;
-
-		/* Start from the first entry in block */
-		struct ext4_dir_en *tmp_de =(void *)result.block.data;
-		uint16_t de_len = ext4_dir_en_get_entry_len(tmp_de);
-
-		/* Find direct predecessor of removed entry */
-		while ((offset + de_len) < pos) {
-			offset += ext4_dir_en_get_entry_len(tmp_de);
-			tmp_de = (void *)(result.block.data + offset);
-			de_len = ext4_dir_en_get_entry_len(tmp_de);
-		}
-
-		ext4_assert(de_len + offset == pos);
-
 		/* Add to removed entry length to predecessor's length */
 		uint16_t del_len;
@@ -592,9 +702,27 @@ int ext4_dir_try_insert_entry(struct ext4_sblock *sb,
 		uint8_t itype = ext4_dir_en_get_inode_type(sb, start);
 
+		/* ext4edit patch 36: the walk steps by rec_len, so a length   */
+		/* no entry can have is damage, refused before anything in the */
+		/* block changes -- upstream steps by it unchecked, and a 0    */
+		/* spins for ever adding a name to a damaged directory.        */
+		if (ext4_dir_en_len_bad(rec_len,
+				(uint32_t)((uint8_t *)start - dst_blk->data),
+				block_size)) {
+			/* Damaged: refused, the block unchanged. */
+			return EIO;
+		}
+
 		/* If invalid and large enough entry, use it */
 		if ((inode == 0) && (itype != EXT4_DIRENTRY_DIR_CSUM) &&
 		    (rec_len >= required_len)) {
-			ext4_dir_write_entry(sb, start, rec_len, child, name,
-					     name_len);
+			/* ext4edit patch 36: the writer's refusal leaves the  */
+			/* block as it was.                                    */
+			int r;
+			r = ext4_dir_write_entry(sb, start, rec_len, child,
+						 name, name_len);
+			if (r != EOK) {
+				/* Refused: nothing written, the result returned. */
+				return r;
+			}
 			ext4_dir_set_csum(inode_ref, (void *)dst_blk->data);
 			ext4_trans_set_block_dirty(dst_blk->buf);
@@ -614,14 +742,33 @@ int ext4_dir_try_insert_entry(struct ext4_sblock *sb,
 				sz += 4 - (used_len % 4);
 
+			/* ext4edit patch 36: a name longer than its own   */
+			/* record is damage -- with upstream's unsigned     */
+			/* free_space below it wraps to a huge "room" and   */
+			/* the new entry is written past the record, over   */
+			/* its neighbours.                                  */
+			if (sz > rec_len) {
+				/* Damaged: refused, the block unchanged. */
+				return EIO;
+			}
+
 			uint16_t free_space = rec_len - sz;
 
 			/* There is free space for new entry */
 			if (free_space >= required_len) {
+				/* ext4edit patch 36: the new entry is written  */
+				/* FIRST and the current one cut only once it   */
+				/* is (upstream cuts first, so a refused write  */
+				/* would orphan the tail).                      */
+				struct ext4_dir_en * new_entry;
+				int r;
+				new_entry = (void *)((uint8_t *)start + sz);
+				r = ext4_dir_write_entry(sb, new_entry,
+						free_space, child, name, name_len);
+				if (r != EOK) {
+					/* Refused: the current entry is not cut. */
+					return r;
+				}
 				/* Cut tail of current entry */
-				struct ext4_dir_en * new_entry;
-				new_entry = (void *)((uint8_t *)start + sz);
 				ext4_dir_en_set_entry_len(start, sz);
-				ext4_dir_write_entry(sb, new_entry, free_space,
-						     child, name, name_len);
 
 				ext4_dir_set_csum(inode_ref,
@@ -671,7 +818,14 @@ int ext4_dir_find_in_block(struct ext4_block *block, struct ext4_sblock *sb,
 		uint16_t de_len = ext4_dir_en_get_entry_len(de);
 
-		/* Corrupted entry */
-		if (de_len == 0)
-			return EINVAL;
+		/* Corrupted entry.  ext4edit patch 36: not only a 0 -- a       */
+		/* length of 1-11 steps into the middle of the next entry, and  */
+		/* one running past the block reads beyond the buffer; all are  */
+		/* EIO, a damaged block, where upstream gives EINVAL for 0 only. */
+		if (ext4_dir_en_len_bad(de_len,
+				(uint32_t)((uint8_t *)de - block->data),
+				ext4_sb_get_block_size(sb))) {
+			/* Not a length to step by: the block is damaged. */
+			return EIO;
+		}
 
 		/* Jump to next entry */
--- a/src/ext4_dir_idx.c
+++ b/src/ext4_dir_idx.c
@@ -382,10 +382,22 @@ int ext4_dir_dx_init(struct ext4_inode_ref *dir, struct ext4_inode_ref *parent)
 
 	/* Initialize dot entries */
+	/* ext4edit patch 36: the writer returns a result -- both are      */
+	/* directories, so it cannot refuse here; a refusal would release  */
+	/* the block and be returned before the root is marked dirty.      */
 	de = (struct ext4_dir_en *)root->dots;
-	ext4_dir_write_entry(sb, de, 12, dir, ".", strlen("."));
+	rc = ext4_dir_write_entry(sb, de, 12, dir, ".", strlen("."));
 
 	de = (struct ext4_dir_en *)(root->dots + 1);
 	uint16_t elen = block_size - 12;
-	ext4_dir_write_entry(sb, de, elen, parent, "..", strlen(".."));
+	if (rc == EOK) {
+		/* "." written: now "..", over the rest of the block. */
+		rc = ext4_dir_write_entry(sb, de, elen, parent, "..",
+					  strlen(".."));
+	}
+	if (rc != EOK) {
+		/* Either refused: the block released unchanged on disk. */
+		ext4_block_set(dir->fs->bdev, &block);
+		return rc;
+	}
 
 	/* Initialize root info structure */
@@ -967,4 +979,19 @@ static int ext4_dir_dx_split_data(struct ext4_inode_ref *inode_ref,
 	uint8_t *entry_buffer_ptr = entry_buffer;
 	while ((void *)de < (void *)(old_data_block->data + block_size)) {
+		/* ext4edit patch 36: the walk steps by each entry's rec_len,   */
+		/* so a length no entry can have is damage, refused before the  */
+		/* new block is claimed -- upstream steps by it unchecked, and  */
+		/* a 0 spins for ever while ADDING to a large directory.        */
+		size_t elen;
+		uint32_t de_off;
+		elen = ext4_dir_en_get_entry_len(de);
+		de_off = (uint32_t)((uint8_t *)de - old_data_block->data);
+		if (ext4_dir_en_len_bad((uint32_t)elen, de_off, block_size)) {
+			/* Damaged: both buffers freed, nothing claimed. */
+			free(sort);
+			free(entry_buffer);
+			return EIO;
+		}
+
 		/* Read only valid entries */
 		if (ext4_dir_en_get_inode(de) && de->name_len) {
@@ -982,4 +1009,13 @@ static int ext4_dir_dx_split_data(struct ext4_inode_ref *inode_ref,
 				rec_len += 4 - (rec_len % 4);
 
+			/* ext4edit patch 36: a name longer than its record  */
+			/* (damage) would copy the neighbours with it.       */
+			if (rec_len > elen) {
+				/* Damaged: both buffers freed, nothing claimed. */
+				free(sort);
+				free(entry_buffer);
+				return EIO;
+			}
+
 			memcpy(entry_buffer_ptr, de, rec_len);
 
@@ -993,5 +1029,5 @@ static int ext4_dir_dx_split_data(struct ext4_inode_ref *inode_ref,
 		}
 
-		size_t elen = ext4_dir_en_get_entry_len(de);
+		/* The next entry (its length checked above). */
 		de = (void *)((uint8_t *)de + elen);
 	}
@@ -1354,6 +1390,10 @@ int ext4_dir_dx_add_entry(struct ext4_inode_ref *parent,
 	r = ext4_dir_try_insert_entry(&fs->sb, parent, &target_block, child,
 					name, name_len);
-	if (r == EOK)
+	/* ext4edit patch 36: only ENOSPC means "split the leaf"; any other  */
+	/* failure (EIO on a damaged leaf) is the answer, as success is.     */
+	if (r != ENOSPC) {
+		/* Inserted, or refused for damage: no split; release and go. */
 		goto release_target_index;
+	}
 
 	/* Split entries to two blocks (includes sorting by hash value) */
@@ -1375,8 +1415,18 @@ int ext4_dir_dx_add_entry(struct ext4_inode_ref *parent,
 						child, name, name_len);
 
+	/* ext4edit patch 36: the insert's result is KEPT for the caller --   */
+	/* upstream overwrites it with the release's below, so a failed       */
+	/* insert after the split reports success and the name is silently    */
+	/* lost.                                                              */
+	int r_insert;
+	r_insert = r;
+
 	/* Cleanup */
 	r = ext4_block_set(fs->bdev, &new_block);
 	if (r != EOK)
 		return r;
+
+	/* The release went through: the result is the insert's. */
+	r = r_insert;
 
 /* Cleanup operations */
--- a/src/ext4_extent.c
+++ b/src/ext4_extent.c
@@ -623,5 +623,10 @@ static int ext4_ext_split_node(struct ext4_inode_ref *inode_ref,
 	int32_t depth = ext_depth(inode_ref->inode);
 
-	ext4_assert(sibling);
+	/* ext4edit patch 36: the caller's out-parameter, refused when        */
+	/* missing (never data), where upstream asserts.                      */
+	if (!sibling) {
+		/* Nowhere to put the new node: refused, nothing split. */
+		return EINVAL;
+	}
 
 	/* FIXME: currently we split at the point after the current extent. */
@@ -770,5 +775,13 @@ static int ext4_ext_insert_index(struct ext4_inode_ref *inode_ref,
 
 	len = EXT_LAST_INDEX(eh) - ix + 1;
-	ext4_assert(len >= 0);
+	/* ext4edit patch 36: negative only when the insertion point lies     */
+	/* past the node's last entry -- a damaged entries_count the bounds   */
+	/* of patch 32 did not catch; the memmove below would take it as      */
+	/* enormous.  EIO, nothing moved, where upstream asserts.             */
+	if (len < 0) {
+		/* Damaged count: refused through the function's exit. */
+		err = EIO;
+		goto out;
+	}
 	if (len > 0)
 		memmove(ix + 1, ix, len * sizeof(struct ext4_extent_index));
@@ -1034,5 +1047,11 @@ static int ext4_ext_insert_leaf(struct ext4_inode_ref *inode_ref,
 
 	len = EXT_LAST_EXTENT(eh) - ex + 1;
-	ext4_assert(len >= 0);
+	/* ext4edit patch 36: as in ext4_ext_insert_index -- a damaged     */
+	/* count; EIO, nothing moved, where upstream asserts.               */
+	if (len < 0) {
+		/* Damaged count: refused through the function's exit. */
+		err = EIO;
+		goto out;
+	}
 	if (len > 0)
 		memmove(ex + 1, ex, len * sizeof(struct ext4_extent));
@@ -1515,5 +1534,15 @@ int ext4_extent_remove_space(struct ext4_inode_ref *inode_ref, ext4_lblk_t from,
 			ext4_lblk_t leaf_from, leaf_to;
 			eh = path[i].header;
-			ext4_assert(to_le16(eh->entries_count) > 0);
+			/* ext4edit patch 36: a leaf with no entries is      */
+			/* damage, reached when such a file is truncated or  */
+			/* removed; EIO, nothing removed, where upstream     */
+			/* asserts (a spin for ever).  Skipping the empty    */
+			/* leaf instead would also do; refusing is the       */
+			/* conservative answer.                              */
+			if (to_le16(eh->entries_count) == 0) {
+				/* Empty leaf: refused through the exit. */
+				ret = EIO;
+				goto out;
+			}
 			first_ex = EXT_FIRST_EXTENT(eh);
 			last_ex = EXT_LAST_EXTENT(eh);
@@ -1663,5 +1692,11 @@ static int ext4_ext_convert_to_initialized(struct ext4_inode_ref *inode_ref,
 	struct ext4_extent *ex = (*ppath)[depth].extent;
 
-	ext4_assert(to_le32(ex->first_block) <= split);
+	/* ext4edit patch 36: a split point before the extent is refused    */
+	/* with EIO, where upstream asserts (reached only by writes into    */
+	/* unwritten extents, which the engine never makes).                */
+	if (to_le32(ex->first_block) > split) {
+		/* Before the extent: refused, nothing converted. */
+		return EIO;
+	}
 
 	if (split + blocks ==
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -66,5 +66,9 @@ int ext4_fs_init(struct ext4_fs *fs, struct ext4_blockdev *bdev,
 	uint32_t bsize;
 
-	ext4_assert(fs && bdev);
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!fs || !bdev) {
+		/* No filesystem or no device: refused, nothing read. */
+		return EINVAL;
+	}
 
 	fs->bdev = bdev;
@@ -126,5 +130,9 @@ int ext4_fs_fini(struct ext4_fs *fs)
 int ext4_fs_fini(struct ext4_fs *fs)
 {
-	ext4_assert(fs);
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!fs) {
+		/* No filesystem: refused, nothing written. */
+		return EINVAL;
+	}
 
 	/*Set superblock state*/
@@ -210,6 +218,11 @@ int ext4_fs_check_features(struct ext4_fs *fs, bool *read_only)
 int ext4_fs_check_features(struct ext4_fs *fs, bool *read_only)
 {
-	ext4_assert(fs && read_only);
 	uint32_t v;
+
+	/* ext4edit patch 36: refused, where upstream asserts. */
+	if (!fs || !read_only) {
+		/* Nothing to check, or nowhere to answer: refused. */
+		return EINVAL;
+	}
 	if (ext4_get32(&fs->sb, rev_level) == 0) {
 		*read_only = false;
@@ -824,7 +837,17 @@ void ext4_fs_inode_blocks_init(struct ext4_fs *fs,
 }
 
+/* ext4edit patch 36: all seven directory-entry types map to their inode   */
+/* types, and an entry that does not say (EXT4_DE_UNKNOWN, or a byte above */
+/* 7 -- damage) gives 0, "unknown", which every caller handles.  Upstream  */
+/* knows only directory, file and symlink and asserts on anything else;    */
+/* with lwext4's own assert that is a spin for ever, so with upstream's    */
+/* mapping merely LISTING a folder holding a device node, a FIFO or a      */
+/* socket -- an OpenWrt overlay's 0,0 whiteouts, a running system's Unix   */
+/* sockets -- hangs the engine for good (the path walk converts every      */
+/* component's entry type).                                                */
 uint32_t ext4_fs_correspond_inode_mode(int filetype)
 {
 	switch (filetype) {
+	/* One entry type per case: the converse of ext4_dir_inode_entry_type. */
 	case EXT4_DE_DIR:
 		return EXT4_INODE_MODE_DIRECTORY;
@@ -833,9 +856,16 @@ uint32_t ext4_fs_correspond_inode_mode(int filetype)
 	case EXT4_DE_SYMLINK:
 		return EXT4_INODE_MODE_SOFTLINK;
+	case EXT4_DE_CHRDEV:
+		return EXT4_INODE_MODE_CHARDEV;
+	case EXT4_DE_BLKDEV:
+		return EXT4_INODE_MODE_BLOCKDEV;
+	case EXT4_DE_FIFO:
+		return EXT4_INODE_MODE_FIFO;
+	case EXT4_DE_SOCK:
+		return EXT4_INODE_MODE_SOCKET;
 	default:
-		/* FIXME: right now we only support 3 file type. */
-		ext4_assert(0);
+		/* The entry does not say: the caller reads the inode instead. */
+		return 0;
 	}
-	return 0;
 }
 
@@ -846,4 +876,12 @@ int ext4_fs_alloc_inode(struct ext4_fs *fs, struct ext4_inode_ref *inode_ref,
 	bool is_dir;
 	uint16_t inode_size = ext4_get16(&fs->sb, inode_size);
+
+	/* ext4edit patch 36: a type with no inode mode is refused before an  */
+	/* inode is taken -- upstream reaches the converter's assertion with  */
+	/* the inode already allocated.                                       */
+	if (ext4_fs_correspond_inode_mode(filetype) == 0) {
+		/* No such type: refused, no inode taken. */
+		return EINVAL;
+	}
 
 	is_dir = (filetype == EXT4_DE_DIR);
@@ -1072,7 +1110,12 @@ static int ext4_fs_release_inode_block(struct ext4_inode_ref *inode_ref,
 	/* Extents are handled otherwise = there is not support in this function
 	 */
-	ext4_assert(!(
-	    ext4_sb_feature_incom(&fs->sb, EXT4_FINCOM_EXTENTS) &&
-	    (ext4_inode_has_flag(inode_ref->inode, EXT4_INODE_FLAG_EXTENTS))));
+	/* ext4edit patch 36: refused with EINVAL, where upstream asserts;   */
+	/* the one caller, the truncate, branches on the same test before    */
+	/* calling, so this is a guard.                                      */
+	if (ext4_sb_feature_incom(&fs->sb, EXT4_FINCOM_EXTENTS) &&
+	    ext4_inode_has_flag(inode_ref->inode, EXT4_INODE_FLAG_EXTENTS)) {
+		/* An extent inode: not this function's to release. */
+		return EINVAL;
+	}
 
 	struct ext4_inode *inode = inode_ref->inode;
@@ -1265,6 +1308,9 @@ int ext4_fs_indirect_find_goal(struct ext4_inode_ref *inode_ref,
 	/* If inode has some blocks, get last block address + 1 */
 	if (iblock_cnt > 0) {
+		/* ext4edit patch 36: true -- a hole here is a sparse file,     */
+		/* not damage (the 0 is tested just below), and the false form  */
+		/* refuses a hole with EIO.                                     */
 		r = ext4_fs_get_inode_dblk_idx(inode_ref, iblock_cnt - 1,
-					       goal, false);
+					       goal, true);
 		if (r != EOK)
 			return r;
@@ -1349,5 +1395,8 @@ static int ext4_fs_get_inode_dblk_idx_internal(struct ext4_inode_ref *inode_ref,
 		*fblock = current_block;
 
-		ext4_assert(*fblock || support_unwritten);
+		/* ext4edit patch 36: a hole (0) is returned as the block-map  */
+		/* branch below returns one; whether the caller may take it is */
+		/* ext4_fs_get_inode_dblk_idx's test.  Upstream asserts here,  */
+		/* so reading a directory whose extents leave a hole spins.    */
 		return EOK;
 	}
@@ -1434,10 +1483,28 @@ static int ext4_fs_get_inode_dblk_idx_internal(struct ext4_inode_ref *inode_ref,
 
 
+/* ext4edit patch 36: with support_unwritten false -- every directory      */
+/* reader, the htree walks and the journal's block map -- a HOLE is EIO,   */
+/* "a hole where a block must be".  Upstream returns 0 from the block-map  */
+/* branch and asserts in the extent branch, and NONE of those callers      */
+/* tests the 0: each goes on to read block 0, the start of the filesystem, */
+/* as a directory block -- and the add-entry scan would WRITE a new entry  */
+/* into it.  The callers that take a hole (file reads, the allocation      */
+/* goal) pass true.                                                        */
 int ext4_fs_get_inode_dblk_idx(struct ext4_inode_ref *inode_ref,
 			       ext4_lblk_t iblock, ext4_fsblk_t *fblock,
 			       bool support_unwritten)
 {
-	return ext4_fs_get_inode_dblk_idx_internal(inode_ref, iblock, fblock,
-						   false, support_unwritten);
+	int r;
+
+	/* The mapping, without allocating. */
+	r = ext4_fs_get_inode_dblk_idx_internal(inode_ref, iblock, fblock,
+						false, support_unwritten);
+	if ((r == EOK) && !support_unwritten && (*fblock == 0)) {
+		/* A hole where a block must be: damage. */
+		return EIO;
+	}
+
+	/* The block, or (support_unwritten) possibly a hole. */
+	return r;
 }
 
--- a/src/ext4_journal.c
+++ b/src/ext4_journal.c
@@ -907,6 +907,10 @@ static void jbd_replay_block_tags(struct jbd_fs *jbd_fs,
  * @param  info  journal replay info
  * @param  block  block address to be replayed.*/
-static void jbd_add_revoke_block_tags(struct recover_info *info,
-				      ext4_fsblk_t block)
+/* ext4edit patch 36: returns int -- EOK, or ENOMEM when the entry cannot   */
+/* be allocated, where upstream asserts (a spin for ever) on a journal with */
+/* very many revokes.  The failure travels up through the revoke pass to    */
+/* jbd_recover, so the replay fails as any failed replay does.              */
+static int jbd_add_revoke_block_tags(struct recover_info *info,
+				     ext4_fsblk_t block)
 {
 	struct revoke_entry *revoke_entry;
@@ -918,14 +922,18 @@ static void jbd_add_revoke_block_tags(struct recover_info *info,
 	if (revoke_entry) {
 		revoke_entry->trans_id = info->this_trans_id;
-		return;
+		return EOK;
 	}
 
 	revoke_entry = jbd_alloc_revoke_entry();
-	ext4_assert(revoke_entry);
+	if (!revoke_entry) {
+		/* Out of memory: nothing is added, and the replay fails. */
+		return ENOMEM;
+	}
 	revoke_entry->block = block;
 	revoke_entry->trans_id = info->this_trans_id;
 	RB_INSERT(jbd_revoke, &info->revoke_root, revoke_entry);
 
-	return;
+	/* The block is in the tree. */
+	return EOK;
 }
 
@@ -933,7 +941,9 @@ static void jbd_destroy_revoke_tree(struct recover_info *info)
 {
 	while (!RB_EMPTY(&info->revoke_root)) {
+		/* One entry per iteration, smallest first.  ext4edit patch */
+		/* 36: the smallest of a non-empty tree is never NULL -- the  */
+		/* loop's own test guards it -- so it is not checked again.   */
 		struct revoke_entry *revoke_entry =
 			RB_MIN(jbd_revoke, &info->revoke_root);
-		ext4_assert(revoke_entry);
 		RB_REMOVE(jbd_revoke, &info->revoke_root, revoke_entry);
 		jbd_free_revoke_entry(revoke_entry);
@@ -956,7 +966,12 @@ do {									\
  * @param  header revoke block header
  * @param  recover_info  journal replay info*/
-static void jbd_build_revoke_tree(struct jbd_fs *jbd_fs,
-				  struct jbd_bhdr *header,
-				  struct recover_info *info)
+/* ext4edit patch 36: returns int -- EOK, the revoke adder's ENOMEM, or EIO  */
+/* for a revoke block whose byte count is smaller than its header or larger  */
+/* than the journal block.  Upstream takes the count unchecked: too small    */
+/* wraps the entry count to some four billion, too large reads past the      */
+/* block.                                                                    */
+static int jbd_build_revoke_tree(struct jbd_fs *jbd_fs,
+				 struct jbd_bhdr *header,
+				 struct recover_info *info)
 {
 	char *blocks_entry;
@@ -964,4 +979,6 @@ static void jbd_build_revoke_tree(struct jbd_fs *jbd_fs,
 		(struct jbd_revoke_header *)header;
 	uint32_t i, nr_entries, record_len = 4;
+	uint32_t count;
+	int r;
 
 	/* If we are working on a 64bit jbd filesystem, */
@@ -970,22 +987,39 @@ static void jbd_build_revoke_tree(struct jbd_fs *jbd_fs,
 		record_len = 8;
 
-	nr_entries = (jbd_get32(revoke_hdr, count) -
-			sizeof(struct jbd_revoke_header)) /
-			record_len;
+	/* The byte count, held to the block it sits in. */
+	count = jbd_get32(revoke_hdr, count);
+	if ((count < sizeof(struct jbd_revoke_header)) ||
+	    (count > jbd_get32(&jbd_fs->sb, blocksize))) {
+		/* Smaller than its own header, or past the block: damage. */
+		return EIO;
+	}
+
+	/* The entries are what follows the header; a partial last record */
+	/* is dropped by the division.                                    */
+	nr_entries = (count - sizeof(struct jbd_revoke_header)) / record_len;
 
 	blocks_entry = (char *)(revoke_hdr + 1);
 
 	for (i = 0;i < nr_entries;i++) {
+		/* One revoked block number per iteration. */
 		if (record_len == 8) {
 			uint64_t *blocks =
 				(uint64_t *)blocks_entry;
-			jbd_add_revoke_block_tags(info, to_be64(*blocks));
+			r = jbd_add_revoke_block_tags(info, to_be64(*blocks));
 		} else {
 			uint32_t *blocks =
 				(uint32_t *)blocks_entry;
-			jbd_add_revoke_block_tags(info, to_be32(*blocks));
+			r = jbd_add_revoke_block_tags(info, to_be32(*blocks));
+		}
+		if (r != EOK) {
+			/* Out of memory: the rest are not added; the caller */
+			/* frees what is in the tree.                         */
+			return r;
 		}
 		blocks_entry += record_len;
 	}
+
+	/* Every entry is in the tree. */
+	return EOK;
 }
 
@@ -1139,6 +1173,13 @@ static int jbd_iterate_log(struct jbd_fs *jbd_fs,
 			if (action == ACTION_REVOKE) {
 				info->this_trans_id = this_trans_id;
-				jbd_build_revoke_tree(jbd_fs,
+				/* ext4edit patch 36: a failure ends the pass */
+				/* and is its result (r is not reassigned     */
+				/* before the loop ends).                     */
+				r = jbd_build_revoke_tree(jbd_fs,
 						header, info);
+				if (r != EOK) {
+					/* Failed: no further block is read. */
+					log_end = true;
+				}
 			}
 			break;
@@ -1230,4 +1271,6 @@ int jbd_journal_start(struct jbd_fs *jbd_fs,
 				   features_incompatible);
 	struct ext4_block block = EXT4_BLOCK_ZERO();
+	/* ext4edit patch 36: a new journal session starts with no fault. */
+	journal->fault = EOK;
 	features_incompatible |= EXT4_FINCOM_RECOVER;
 	ext4_set32(&jbd_fs->inode_ref.fs->sb,
@@ -1281,5 +1324,13 @@ static void jbd_journal_flush_trans(struct jbd_trans *trans)
 	struct ext4_fs *fs = journal->jbd_fs->inode_ref.fs;
 	void *tmp_data = malloc(journal->block_size);
-	ext4_assert(tmp_data);
+	/* ext4edit patch 36: no scratch block means no checkpoint -- the */
+	/* journal's sticky fault, where upstream asserts (a spin for       */
+	/* ever).                                                           */
+	if (!tmp_data) {
+		/* Nothing allocated, so nothing to free: the fault is set and */
+		/* the transaction stays in the journal.                       */
+		journal->fault = ENOMEM;
+		return;
+	}
 
 	TAILQ_FOREACH_SAFE(jbd_buf, &trans->buf_queue, buf_node,
@@ -1291,7 +1342,20 @@ static void jbd_journal_flush_trans(struct jbd_trans *trans)
 				int r;
 				struct ext4_block jbd_block = EXT4_BLOCK_ZERO();
-				ext4_assert(ext4_block_get(fs->bdev,
-							&jbd_block,
-							jbd_buf->jbd_lba) == EOK);
+				/* ext4edit patch 36: the read happens OUTSIDE  */
+				/* any assertion (with assertions compiled out, */
+				/* upstream's form would skip it) and is        */
+				/* checked: a journal block that cannot be read */
+				/* (an I/O error on the card) is the journal's  */
+				/* sticky fault, and the checkpoint stops --    */
+				/* the transaction stays in the journal.        */
+				r = ext4_block_get(fs->bdev, &jbd_block,
+						   jbd_buf->jbd_lba);
+				if (r != EOK) {
+					/* Unreadable: the fault is set, and the  */
+					/* loop ends; the scratch block is freed  */
+					/* after it.                              */
+					journal->fault = r;
+					break;
+				}
 				memcpy(tmp_data, jbd_block.data,
 						journal->block_size);
@@ -1359,6 +1423,16 @@ jbd_journal_purge_cp_trans(struct jbd_journal *journal,
 				jbd_journal_write_sb(journal);
 				break;
-			} else
+			} else {
 				jbd_journal_flush_trans(trans);
+				/* ext4edit patch 36: a faulted checkpoint  */
+				/* makes no progress, and this loop would   */
+				/* wait for ever on a transaction it cannot */
+				/* write.                                   */
+				if (journal->fault) {
+					/* Faulted: stop; the transaction */
+					/* stays queued and in the journal. */
+					break;
+				}
+			}
 		}
 	}
@@ -1377,4 +1451,15 @@ int jbd_journal_stop(struct jbd_journal *journal)
 	 * the disk.*/
 	jbd_journal_purge_cp_trans(journal, true);
+
+	/* ext4edit patch 36: a faulted checkpoint left transactions only in  */
+	/* the journal, so needs_recovery and the journal's start are LEFT as */
+	/* they are -- the next replay (e2fsck, or lwext4's own read-write    */
+	/* mount) applies them -- and the fault is the stop's result.  At the */
+	/* engine this is a failed flush: every later change is refused and  */
+	/* the session stays open for reading.                                */
+	if (journal->fault) {
+		/* Faulted: nothing below runs; the fault is the result. */
+		return journal->fault;
+	}
 
 	/* There should be no block record in this journal
@@ -1460,5 +1545,11 @@ int jbd_trans_get_access(struct jbd_journal *journal,
 	if (ext4_bcache_test_flag(block->buf, BC_DIRTY) &&
 	    block->buf->end_write == jbd_trans_end_write) {
-		ext4_assert(jbd_buf);
+		/* ext4edit patch 36: a dirty buffer marked as the journal's  */
+		/* without its record is inconsistent state -- EIO, the       */
+		/* change being started fails, where upstream asserts.        */
+		if (!jbd_buf) {
+			/* No record to compare: refused, nothing flushed. */
+			return EIO;
+		}
 		if (jbd_buf->trans != trans)
 			r = ext4_block_flush_buf(fs->bdev, block->buf);
@@ -1548,10 +1639,37 @@ jbd_trans_finish_callback(struct jbd_journal *journal,
 				jbd_buf_dirty);
 		if (jbd_buf) {
-			ext4_assert(ext4_block_get(fs->bdev,
-						&jbd_block,
-						jbd_buf->jbd_lba) == EOK);
-			ext4_assert(ext4_block_get_noread(fs->bdev,
-						&block,
-						block_rec->lba) == EOK);
+			/* ext4edit patch 36: both gets happen OUTSIDE any     */
+			/* assertion and are checked.  This branch runs while  */
+			/* a transaction is torn down (its abort, or a block   */
+			/* the prepare step drops unchanged), from callers     */
+			/* that take no result, so a failure (an I/O error, or */
+			/* the cache full) is the journal's sticky fault: the  */
+			/* copy is skipped, the one block already got is       */
+			/* released, and every later checkpoint and commit     */
+			/* refuses -- at the engine, a failed flush, changes   */
+			/* barred, the session readable.                       */
+			int r_get;
+
+			/* The journal's copy of the block, read. */
+			r_get = ext4_block_get(fs->bdev, &jbd_block,
+					       jbd_buf->jbd_lba);
+			if (r_get != EOK) {
+				/* Unreadable: nothing held, so nothing to */
+				/* release; the fault is set.              */
+				journal->fault = r_get;
+				return;
+			}
+
+			/* The block's home in the filesystem, taken without */
+			/* a read, since it is about to be overwritten.      */
+			r_get = ext4_block_get_noread(fs->bdev, &block,
+						      block_rec->lba);
+			if (r_get != EOK) {
+				/* Not to be had: the journal copy already  */
+				/* got is released, and the fault is set.    */
+				ext4_block_set(fs->bdev, &jbd_block);
+				journal->fault = r_get;
+				return;
+			}
 			memcpy(block.data, jbd_block.data,
 					journal->block_size);
@@ -1742,6 +1860,17 @@ static int jbd_trans_write_commit_block(struct jbd_trans *trans)
 
 	commit_iblock = jbd_journal_alloc_block(journal, trans);
-	rc = jbd_block_get_noread(journal->jbd_fs,
+	/* ext4edit patch 36: a fault raised by the checkpoint the allocation  */
+	/* may have forced means the journal could not be emptied, so nothing  */
+	/* more is written into it (the next block may still hold a            */
+	/* transaction waiting for its checkpoint).                            */
+	if (journal->fault) {
+		/* Faulted: the commit block is not taken; the fault is the */
+		/* result.                                                  */
+		rc = journal->fault;
+	} else {
+		/* Not faulted: the commit block, taken without a read. */
+		rc = jbd_block_get_noread(journal->jbd_fs,
 			&commit_block, commit_iblock);
+	}
 	if (rc != EOK)
 		return rc;
@@ -1901,6 +2030,14 @@ static int jbd_journal_prepare(struct jbd_journal *journal,
 
 		data_iblock = jbd_journal_alloc_block(journal, trans);
-		rc = jbd_block_get_noread(journal->jbd_fs,
+		/* ext4edit patch 36: as at the commit block -- nothing more  */
+		/* is written into a faulted journal.                         */
+		if (journal->fault) {
+			/* Faulted: no data block is taken. */
+			rc = journal->fault;
+		} else {
+			/* Not faulted: the data block, taken without a read. */
+			rc = jbd_block_get_noread(journal->jbd_fs,
 				&data_block, data_iblock);
+		}
 		if (rc != EOK)
 			break;
@@ -2102,4 +2239,11 @@ int jbd_journal_commit_trans(struct jbd_journal *journal,
 
 	trans->trans_id = journal->alloc_trans_id;
+	/* ext4edit patch 36: a faulted journal takes no new transaction --  */
+	/* it is aborted below, as any failed commit is.                     */
+	if (journal->fault) {
+		/* Faulted: straight to the abort, with the fault as result. */
+		rc = journal->fault;
+		goto Finish;
+	}
 	rc = jbd_journal_prepare(journal, trans);
 	if (rc != EOK)
--- a/src/ext4_mkfs.c
+++ b/src/ext4_mkfs.c
@@ -723,5 +723,11 @@ int ext4_mkfs(struct ext4_fs *fs, struct ext4_blockdev *bd,
 	struct ext4_bcache bc;
 	memset(&bc, 0, sizeof(struct ext4_bcache));
-	ext4_block_set_lb_size(bd, info->block_size);
+	/* ext4edit patch 36: the size's result is checked (upstream's call   */
+	/* is void and asserts).                                              */
+	r = ext4_block_set_lb_size(bd, info->block_size);
+	if (r != EOK) {
+		/* Refused: the device is closed through the common exit. */
+		goto block_fini;
+	}
 	r = ext4_bcache_init_dynamic(&bc, CONFIG_BLOCK_DEV_CACHE_SIZE,
 				      info->block_size);
--- a/src/ext4_xattr.c
+++ b/src/ext4_xattr.c
@@ -219,5 +219,11 @@ static int ext4_xattr_item_alloc_data(struct ext4_xattr_item *item,
 {
 	void *data = NULL;
-	ext4_assert(!item->data);
+	/* ext4edit patch 36: data allocated twice is lwext4's bookkeeping   */
+	/* gone wrong -- refused with EINVAL (the old data kept), where       */
+	/* upstream asserts.                                                  */
+	if (item->data) {
+		/* Already holds data: refused, nothing allocated. */
+		return EINVAL;
+	}
 	data = malloc(data_size);
 	if (!data)
@@ -234,5 +240,7 @@ static void ext4_xattr_item_free_data(struct ext4_xattr_item *item)
 static void ext4_xattr_item_free_data(struct ext4_xattr_item *item)
 {
-	ext4_assert(item->data);
+	/* ext4edit patch 36: freeing absent data does nothing (free(NULL)   */
+	/* is harmless, and the fields below are reset either way), where    */
+	/* upstream asserts that there is data.                              */
 	free(item->data);
 	item->data = NULL;
@@ -299,5 +307,10 @@ static int ext4_xattr_block_fetch(struct ext4_xattr_ref *xattr_ref)
 	struct ext4_xattr_entry *entry = NULL;
 
-	ext4_assert(xattr_ref->block.data);
+	/* ext4edit patch 36: called only once the block is loaded; a        */
+	/* missing block is EIO, where upstream asserts.                     */
+	if (!xattr_ref->block.data) {
+		/* No block loaded: refused, nothing read. */
+		return EIO;
+	}
 	entry = EXT4_XATTR_BFIRST(&xattr_ref->block);
 
@@ -897,5 +910,16 @@ const char *ext4_extract_xattr_name(const char *full_name, size_t full_name_len,
 {
 	int i;
-	ext4_assert(name_index);
+	/* ext4edit patch 36: a missing out-parameter gives "no such name"  */
+	/* -- NULL and a length of 0, which every caller tests -- where     */
+	/* upstream asserts.                                                */
+	if (!name_index) {
+		/* Nowhere to put the index: answered as no such name. */
+		if (name_len) {
+			/* The caller wants a length: 0. */
+			*name_len = 0;
+		}
+
+		return NULL;
+	}
 	if (!full_name_len) {
 		if (name_len)
```

- Also this patch's: the hunk of `src/ext4.c` at upstream line 378, shown under patch 4.
- Also this patch's: the hunk of `src/ext4_dir.c` at upstream line 521, shown under patch 7.
- Also this patch's: the hunk of `src/ext4_fs.c` at upstream line 1632, shown under patch 1.
- Also this patch's: the hunk of `src/ext4_journal.c` at upstream line 1185, shown under patch 43.
- Also this patch's: the hunk of `src/ext4_journal.c` at upstream line 1842, shown under patch 42.
- Also this patch's: the hunk of `src/ext4_journal.c` at upstream line 1952, shown under patch 51.
<!-- /hunks -->

### Patch 37 -- a multi-block free neither revokes nor invalidates its blocks

**Where:** `src/ext4_balloc.c`, `ext4_balloc_free_blocks`.

**The defect:** the group loop consumes `count` and advances `first`, and
the revoke loop and `ext4_bcache_invalidate_lba` after it use those same
variables -- so the revoke loop runs zero times and the invalidation covers
nothing. A block freed this way stays in the journal unrevoked and in the
cache: when it held journaled metadata (a directory's blocks) and a file is
then written into it, the stale cached blocks are written back over the
file at the next checkpoint, and after a crash the replay writes them over
it again. `e2fsck` sees nothing wrong. The closing `ext4_assert(count ==
0)` passed BECAUSE of the defect.

**Shown:** a journaled image: 120 long names in a new folder, the folder
removed, 96 KiB imported into the space, one session: unpatched, the file
held 8 192 wrong bytes after a clean close and 16 384 after a crash and
replay, `e2fsck` clean; patched, none.

**The fix:** the start of the range is kept; the revoke and the
invalidation cover exactly the blocks freed; the loop's own failure is
returned. (A failure inside the loop still returns at once without revoking
the groups already freed -- Part 2.)

**Upstream:** fixed in master by `9dc5fd9` (the revoke), `faa76ad` and
`47bbb86` (the invalidation). 1 of its 2 hunks fails on master, and it is
not needed there.

<!-- hunks 37 -->
```diff
--- a/src/ext4_balloc.c
+++ b/src/ext4_balloc.c
@@ -236,4 +236,10 @@ int ext4_balloc_free_blocks(struct ext4_inode_ref *inode_ref,
 	struct ext4_sblock *sb = &fs->sb;
 
+	/* ext4edit patch 37: the start of the range, kept -- the group loop  */
+	/* below CONSUMES `count` and advances `first`, so the revoke and the */
+	/* cache invalidation after it must work from this copy.              */
+	ext4_fsblk_t freed_first;
+	freed_first = first;
+
 	/* Compute indexes */
 	uint32_t bg_first = ext4_balloc_get_bgid_of_block(sb, first);
@@ -331,16 +337,44 @@ int ext4_balloc_free_blocks(struct ext4_inode_ref *inode_ref,
 	}
 
+	/* ext4edit patch 37: revoke and invalidate EVERY block this call     */
+	/* freed -- from freed_first up to where the loop stopped: all of the */
+	/* range, or, when giving a group's descriptor back failed, the part  */
+	/* before it.  (The loop's other failures return at once, unrevoked,  */
+	/* as upstream's do.)  Upstream works from `first` and `count` AFTER  */
+	/* the loop has consumed them: its revoke loop runs zero times and    */
+	/* its invalidation covers nothing, so a block freed by a multi-block */
+	/* free stays in the journal unrevoked -- a crash before the          */
+	/* checkpoint could replay its old journaled content over the block's */
+	/* NEXT owner -- and stays in the cache, where a later reader of the  */
+	/* reused block could be handed the old bytes.  Upstream's assertion  */
+	/* `count == 0` passes BECAUSE of that, and on an early break spins   */
+	/* for ever; here the loop's own error is returned, after the part    */
+	/* that was freed is revoked.                                         */
+	int rc_loop;
+	uint32_t freed_cnt;
 	uint32_t i;
-	for (i = 0;i < count;i++) {
-		rc = ext4_trans_try_revoke_block(fs->bdev, first + i);
-		if (rc != EOK)
+
+	/* The loop's result, kept for the end; the revokes reuse rc. */
+	rc_loop = rc;
+
+	/* How far the loop got: `first` has advanced past every block it */
+	/* freed.                                                         */
+	freed_cnt = (uint32_t)(first - freed_first);
+	for (i = 0;i < freed_cnt;i++) {
+		/* One freed block per iteration. */
+		rc = ext4_trans_try_revoke_block(fs->bdev, freed_first + i);
+		if (rc != EOK) {
+			/* The revoke failed: returned as the result. */
 			return rc;
+		}
 
 	}
 
-	ext4_bcache_invalidate_lba(fs->bdev->bc, first, count);
-	/*All blocks should be released*/
-	ext4_assert(count == 0);
-	return rc;
+	/* The freed blocks' cached copies dropped, so no later reader of a */
+	/* reused block is handed the old bytes.                            */
+	ext4_bcache_invalidate_lba(fs->bdev->bc, freed_first, freed_cnt);
+
+	/* Every block freed and revoked, or the group loop's failure. */
+	return rc_loop;
 }
 
```
<!-- /hunks -->

### Patch 38 -- an aborted transaction leaves the superblock's free counts lowered

**Where:** `src/ext4.c`, `__ext4_trans_abort` and a new
`ext4_trans_abort_recount`.

**The defect:** an abort drops the transaction's journaled bitmaps and
group descriptors, but the allocator had already lowered the SUPERBLOCK's
free counts in memory, and the superblock is written at unmount outside the
journal -- so after a write that failed for lack of space the superblock's
free blocks stay short of the groups' sum.

**Shown:** a write larger than the free space, then the superblock's free
count against the groups' sum: 10800 / 10805 unpatched, 10805 / 10805
patched.

**The fix:** after the abort, the superblock's free blocks and free inodes
are set to the groups' sums (a group that cannot be read leaves them).

**Upstream:** no issue found. Not fixed in master; applies to master with
fuzz.

<!-- hunks 38 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -52,4 +52,5 @@
 #include "ext4_xattr.h"
 #include "ext4_journal.h"
+#include "ext4_block_group.h"	// ext4edit patch 38: the group descriptor counts ext4_trans_abort_recount sums
 
 
@@ -604,4 +605,54 @@ static int __ext4_trans_stop(struct ext4_mountpoint *mp)
 }
 
+/*----------------------------------------------------------------------------*/
+/* ext4_trans_abort_recount:                                                  */
+/*                                                                            */
+/* ext4edit patch 38.  After an aborted transaction, set the superblock's     */
+/* free block and free inode counts to the sums of the block groups'.  The    */
+/* abort drops the transaction's journaled metadata -- bitmaps and group      */
+/* descriptors go back to what they were -- but the allocator lowered the     */
+/* SUPERBLOCK's counts in memory as it went (ext4_balloc.c), and the          */
+/* superblock is written at unmount, outside the journal, so without this     */
+/* the decrements would outlive the abort: a full-disk import that fails      */
+/* would leave the superblock's free blocks short of the groups'.  The group  */
+/* descriptors are the truth after the abort, so the counts are made from     */
+/* them.  A group that cannot be read leaves the counts as they were.         */
+/*                                                                            */
+/* Arguments:                                                                 */
+/*     fs : the mounted filesystem whose transaction was just aborted.        */
+/*                                                                            */
+/* Returns:                                                                   */
+/*     void : the superblock's two free counts match the groups.              */
+/*----------------------------------------------------------------------------*/
+static void ext4_trans_abort_recount(struct ext4_fs *fs)
+{
+	struct ext4_block_group_ref ref;
+	uint32_t bg_count;
+	uint64_t free_blocks;
+	uint32_t free_inodes;
+	uint32_t i;
+
+	/* Every group, summed: the totals start at nothing. */
+	bg_count = ext4_block_group_cnt(&fs->sb);
+	free_blocks = 0;
+	free_inodes = 0;
+	for (i = 0; i < bg_count; i++) {
+		/* One group's descriptor per iteration: its two counts added, */
+		/* then the descriptor given back.                              */
+		if (ext4_fs_get_block_group_ref(fs, i, &ref) != EOK) {
+			/* Unreadable: a partial sum would be wrong, so the */
+			/* counts are left as they were.                    */
+			return;
+		}
+		free_blocks += ext4_bg_get_free_blocks_count(ref.block_group, &fs->sb);
+		free_inodes += ext4_bg_get_free_inodes_count(ref.block_group, &fs->sb);
+		ext4_fs_put_block_group_ref(&ref);
+	}
+
+	/* The superblock takes the groups' totals. */
+	ext4_sb_set_free_blocks_cnt(&fs->sb, free_blocks);
+	ext4_set32(&fs->sb, free_inodes_count, free_inodes);
+}
+
 __unused
 static void __ext4_trans_abort(struct ext4_mountpoint *mp)
@@ -612,4 +663,8 @@ static void __ext4_trans_abort(struct ext4_mountpoint *mp)
 		jbd_journal_free_trans(journal, trans, true);
 		mp->fs.curr_trans = NULL;
+
+		/* ext4edit patch 38: the superblock's free counts, which the  */
+		/* abort does not take back, made from the groups again.       */
+		ext4_trans_abort_recount(&mp->fs);
 	}
 }
```
<!-- /hunks -->

### Patch 39 -- listing a directory over-reads its block's buffer

**Where:** `src/ext4.c`, `ext4_dir_entry_next`.

**The defect:** each entry was copied with `memcpy(&d->de, it.curr,
sizeof(ext4_direntry))` -- 263 bytes, a 255-byte name -- whatever its real
length, so an entry starting in a block's last 262 bytes was read from past
the buffer's end: a heap over-read on most listings, which faults only when
the buffer ends at an unmapped page.

**Shown:** a 1 KiB-block directory with eight entries in the last 262 bytes
of its first block (the last copied read 199 bytes past the block): 4
crashes in 300 runs unpatched, none in 1 000 patched.

**The fix:** only the 8-byte header and the name
(`ext4_dir_en_get_name_len`, held to 255) are copied, over a zeroed entry.
Copying the record length instead was rejected: it can exceed the struct.

**Upstream:** fixed in master by `a84a1b6`. 1 of its 2 hunks fails on
master, and it is not needed there.

<!-- hunks 39 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -2951,4 +2951,5 @@ const ext4_direntry *ext4_dir_entry_next(ext4_dir *d)
 	struct ext4_inode_ref dir;
 	struct ext4_dir_iter it;
+	uint16_t name_len;	// ext4edit patch 39: the entry's name length, as the iterator checked it
 
 	EXT4_MP_LOCK(d->f.mp);
```

- Also this patch's: the hunk of `src/ext4.c` at upstream line 2966, shown under patch 28.
<!-- /hunks -->

### Patch 40 -- a damaged journal superblock crashes a writable mount

**Where:** `src/ext4_journal.c`, `jbd_get_fs`, its two failure exits.

**The defect:** each exit cleared the whole `jbd_fs` -- its `inode_ref`
with it -- and THEN called `ext4_fs_put_inode_ref(&jbd_fs->inode_ref)`,
which follows the now-null filesystem pointer.

**Shown:** an ext4 whose journal superblock's magic was zeroed: unpatched,
an access violation on a writable mount; patched, the mount is refused
(EIO).

**The fix:** the reference is given back first, then the structure cleared.

**Upstream:** fixed in master by `dd3f008`. Its one hunk fails on master,
and it is not needed there.

<!-- hunks 40 -->
```diff
--- a/src/ext4_journal.c
+++ b/src/ext4_journal.c
@@ -437,13 +437,22 @@ int jbd_get_fs(struct ext4_fs *fs,
 		return rc;
 	}
+	/* ext4edit patch 40 (upstream master dd3f008): on both failures    */
+	/* below, the journal inode's reference is given back BEFORE the     */
+	/* structure is cleared.  As published the structure was cleared     */
+	/* first, so ext4_fs_put_inode_ref received a zeroed reference and   */
+	/* reached through its null filesystem pointer -- a crash on a       */
+	/* writable open of a volume whose journal superblock cannot be read */
+	/* or is not one -- and the inode table block stayed held.           */
 	rc = jbd_sb_read(jbd_fs, &jbd_fs->sb);
 	if (rc != EOK) {
+		/* Unreadable: the reference returned, then the state cleared. */
+		ext4_fs_put_inode_ref(&jbd_fs->inode_ref);
 		memset(jbd_fs, 0, sizeof(struct jbd_fs));
-		ext4_fs_put_inode_ref(&jbd_fs->inode_ref);
 		return rc;
 	}
 	if (!jbd_verify_sb(&jbd_fs->sb)) {
+		/* Not a journal superblock: likewise, and EIO for the caller. */
+		ext4_fs_put_inode_ref(&jbd_fs->inode_ref);
 		memset(jbd_fs, 0, sizeof(struct jbd_fs));
-		ext4_fs_put_inode_ref(&jbd_fs->inode_ref);
 		rc = EIO;
 	}
```
<!-- /hunks -->

### Patch 41 -- a journal replay leaves the superblock's free counts stale

**Where:** `src/ext4.c`, `__ext4_recover`.

**The defect:** a replay restores the groups' descriptors and bitmaps but
not the superblock's free block and inode counts, which the Linux driver
writes lazily -- stale after a crash. lwext4 adjusts them from there and
writes them at unmount: "Free blocks count wrong".

**Shown:** the superblock's free count lowered by 100 with a harmless
transaction pending: unpatched, after lwext4's replay `e2fsck` "Free blocks
count wrong (6542, counted=6642)"; patched, clean.

**The fix:** after a recovery on a writable mount, both counts are summed
from the groups, with patch 38's recount.

**Upstream:** fixed in master by `2e93de2`. Applies to master with fuzz,
and is not needed there.

<!-- hunks 41 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -539,4 +539,8 @@ static int __ext4_journal_stop(const char *mount_point)
 }
 
+/* ext4edit patch 41: declared ahead of its definition below, for the     */
+/* recovery's recount.                                                     */
+static void ext4_trans_abort_recount(struct ext4_fs *fs);
+
 __unused
 static int __ext4_recover(const char *mount_point)
@@ -567,4 +571,18 @@ static int __ext4_recover(const char *mount_point)
 	}
 
+	/* ext4edit patch 41 (upstream master 2e93de2): after a recovery    */
+	/* on a writable mount, the superblock's free block and inode counts */
+	/* are made from the block groups'.  A replay restores the groups'   */
+	/* descriptors and bitmaps, but the superblock's counts are whatever */
+	/* it last held -- the Linux driver keeps exact counts in memory and */
+	/* writes them lazily, so after a crash they are stale -- and        */
+	/* lwext4 adjusts them from there and writes them at unmount, where  */
+	/* e2fsck finds "free blocks count wrong".  The groups are the truth */
+	/* after the replay; patch 38's sum, which leaves the counts as they */
+	/* were when a group cannot be read, does the work.                  */
+	if (r == EOK && !mp->fs.read_only) {
+		/* Recovered (or nothing to replay), and the mount may write. */
+		ext4_trans_abort_recount(&mp->fs);
+	}
 
 Finish:
```
<!-- /hunks -->

### Patch 42 -- the journal's escape flag is mishandled, and a replay destroys the superblock

**Where:** `src/ext4_journal.c`: `struct tag_info`, `jbd_extract_block_tag`,
`jbd_write_block_tag`, `jbd_iterate_block_table` and its callbacks,
`jbd_replay_block_tags`, `jbd_journal_prepare`.

**The defect:** jbd2 logs a block whose first four bytes are the journal
magic with those bytes zeroed and `JBD_FLAG_ESCAPE` on its tag. Reading
such a tag, lwext4 set the block NUMBER to 0, which the replay takes for
the superblock's block: the logged block's bytes from offset 1024 became
the superblock, and the block itself was never restored. Writing, lwext4
never escaped, so a logged block beginning with the magic (a bitmap can)
reads to any scan as a journal block.

**Shown:** a pending transaction logging a block beginning with the magic,
escaped: unpatched, after lwext4's replay `e2fsck` reported "Bad magic
number in super-block" and the block was not restored; patched, the block
is restored and the image clean -- as e2fsck's own replay of a copy gives.
The write side is by reading.

**The fix:** the tag keeps its number and carries an `is_escape` flag,
passed to the replay, which writes the magic back; `jbd_journal_prepare`
escapes a block that begins with the magic, its checksum taken over the
copy as logged, as jbd2's is.

**Upstream:** fixed in master by `384424b` and `fb6fd61`. 4 of its 15 hunks
fail on master, and it is not needed there.

<!-- hunks 42 -->
```diff
--- a/src/ext4_journal.c
+++ b/src/ext4_journal.c
@@ -602,4 +602,9 @@ struct tag_info {
 	/**@brief  crc32c checksum. */
 	uint32_t checksum;
+
+	/* ext4edit patch 42 (upstream master 384424b): the logged block's  */
+	/* first four bytes are the journal magic, zeroed in the log --     */
+	/* JBD_FLAG_ESCAPE on the tag.                                      */
+	bool is_escape;
 };
 
@@ -621,4 +626,5 @@ jbd_extract_block_tag(struct jbd_fs *jbd_fs,
 	tag_info->uuid_exist = false;
 	tag_info->last_tag = false;
+	tag_info->is_escape = false;
 
 	/* See whether it is possible to hold a valid block tag.*/
@@ -635,6 +641,14 @@ jbd_extract_block_tag(struct jbd_fs *jbd_fs,
 				 (uint64_t)jbd_get32(tag, blocknr_high) << 32;
 
-		if (jbd_get32(tag, flags) & JBD_FLAG_ESCAPE)
-			tag_info->block = 0;
+		/* ext4edit patch 42: an escaped block keeps its number; the */
+		/* flag tells the replay to put the magic back.  As published  */
+		/* the number was set to 0, which the replay takes for the      */
+		/* superblock's block: the logged block's bytes from offset    */
+		/* 1024 became the superblock, and the block itself was never  */
+		/* restored.                                                    */
+		if (jbd_get32(tag, flags) & JBD_FLAG_ESCAPE) {
+			/* Escaped: the magic was zeroed in the log copy. */
+			tag_info->is_escape = true;
+		}
 
 		if (!(jbd_get32(tag, flags) & JBD_FLAG_SAME_UUID)) {
@@ -660,6 +674,9 @@ jbd_extract_block_tag(struct jbd_fs *jbd_fs,
 				 (uint64_t)jbd_get32(tag, blocknr_high) << 32;
 
-		if (jbd_get16(tag, flags) & JBD_FLAG_ESCAPE)
-			tag_info->block = 0;
+		/* ext4edit patch 42: as above, for the older tag format. */
+		if (jbd_get16(tag, flags) & JBD_FLAG_ESCAPE) {
+			/* Escaped: the magic was zeroed in the log copy. */
+			tag_info->is_escape = true;
+		}
 
 		if (!(jbd_get16(tag, flags) & JBD_FLAG_SAME_UUID)) {
@@ -728,4 +745,12 @@ jbd_write_block_tag(struct jbd_fs *jbd_fs,
 				  jbd_get32(tag, flags) | JBD_FLAG_LAST_TAG);
 
+		/* ext4edit patch 42: the escape flag, when the log copy's  */
+		/* magic was zeroed (jbd_journal_prepare).                  */
+		if (tag_info->is_escape) {
+			/* Escaped block. */
+			jbd_set32(tag, flags,
+				  jbd_get32(tag, flags) | JBD_FLAG_ESCAPE);
+		}
+
 	} else {
 		struct jbd_block_tag *tag = __tag;
@@ -754,4 +779,11 @@ jbd_write_block_tag(struct jbd_fs *jbd_fs,
 				  jbd_get16(tag, flags) | JBD_FLAG_LAST_TAG);
 
+		/* ext4edit patch 42: as above, for the older tag format. */
+		if (tag_info->is_escape) {
+			/* Escaped block. */
+			jbd_set16(tag, flags,
+				  jbd_get16(tag, flags) | JBD_FLAG_ESCAPE);
+		}
+
 	}
 	return EOK;
@@ -772,4 +804,5 @@ jbd_iterate_block_table(struct jbd_fs *jbd_fs,
 					ext4_fsblk_t block,
 					uint8_t *uuid,
+					bool is_escape,
 					void *arg),
 			void *arg)
@@ -797,6 +830,8 @@ jbd_iterate_block_table(struct jbd_fs *jbd_fs,
 			break;
 
+		/* ext4edit patch 42: the tag's escape flag goes with it. */
 		if (func)
-			func(jbd_fs, tag_info.block, tag_info.uuid, arg);
+			func(jbd_fs, tag_info.block, tag_info.uuid,
+			     tag_info.is_escape, arg);
 
 		/* Stop the iteration when we reach the last tag. */
@@ -812,4 +847,5 @@ static void jbd_display_block_tags(struct jbd_fs *jbd_fs,
 				   ext4_fsblk_t block,
 				   uint8_t *uuid,
+				   bool is_escape __unused,
 				   void *arg)
 {
@@ -838,4 +874,5 @@ static void jbd_replay_block_tags(struct jbd_fs *jbd_fs,
 				  ext4_fsblk_t block,
 				  uint8_t *uuid __unused,
+				  bool is_escape,
 				  void *__arg)
 {
@@ -876,4 +913,13 @@ static void jbd_replay_block_tags(struct jbd_fs *jbd_fs,
 			journal_block.data,
 			jbd_get32(&jbd_fs->sb, blocksize));
+
+		/* ext4edit patch 42: an escaped block had its first four */
+		/* bytes -- the journal magic -- zeroed in the log; they    */
+		/* are put back, as jbd2's replay does.                     */
+		if (is_escape) {
+			/* Escaped: the magic restored. */
+			jbd_set32((struct jbd_bhdr *)ext4_block.data, magic,
+				  JBD_MAGIC_NUMBER);
+		}
 
 		ext4_bcache_set_dirty(ext4_block.buf);
@@ -1818,4 +1864,6 @@ static int jbd_journal_prepare(struct jbd_journal *journal,
 		struct tag_info tag_info;
 		bool uuid_exist = false;
+		bool is_escape;
+		void *escaped;
 		if (!ext4_bcache_test_flag(jbd_buf->block.buf,
 					   BC_DIRTY)) {
@@ -1842,14 +1890,53 @@ static int jbd_journal_prepare(struct jbd_journal *journal,
 			continue;
 		}
-		checksum = jbd_block_csum(journal->jbd_fs,
-					  jbd_buf->block.data,
-					  checksum,
-					  trans->trans_id);
+
+		/* ext4edit patch 42 (upstream master 384424b, fb6fd61): a    */
+		/* block whose first four bytes are the journal magic is      */
+		/* ESCAPED, as jbd2 logs it -- those bytes zeroed in the log  */
+		/* copy (below) and JBD_FLAG_ESCAPE on its tag -- or any scan  */
+		/* (ours, e2fsck's, the Linux driver's) reads the copy as a    */
+		/* journal block of its own.  The checksum covers the copy as */
+		/* logged, as jbd2's does, so for an escaped block it is taken */
+		/* over a scratch copy with the magic zeroed.                 */
+		is_escape = jbd_get32((struct jbd_bhdr *)jbd_buf->block.data,
+				      magic) == JBD_MAGIC_NUMBER;
+		if (is_escape) {
+			/* Escaped: the checksum over the copy the log will hold. */
+			escaped = malloc(journal->block_size);
+			if (!escaped) {
+				/* No scratch block: the commit fails, as any. */
+				rc = ENOMEM;
+				break;
+			}
+			memcpy(escaped, jbd_buf->block.data,
+			       journal->block_size);
+			jbd_set32((struct jbd_bhdr *)escaped, magic, 0);
+			checksum = jbd_block_csum(journal->jbd_fs,
+						  escaped,
+						  checksum,
+						  trans->trans_id);
+			free(escaped);
+		} else {
+			/* Not escaped: the checksum over the block itself. */
+			checksum = jbd_block_csum(journal->jbd_fs,
+						  jbd_buf->block.data,
+						  checksum,
+						  trans->trans_id);
+		}
 again:
 		if (!desc_iblock) {
 			struct jbd_bhdr *bhdr;
 			desc_iblock = jbd_journal_alloc_block(journal, trans);
-			rc = jbd_block_get_noread(journal->jbd_fs,
+			/* ext4edit patch 36: as at the commit block -- */
+			/* nothing more is written into a faulted journal. */
+			if (journal->fault) {
+				/* Faulted: no descriptor block is taken. */
+				rc = journal->fault;
+			} else {
+				/* Not faulted: the descriptor block, taken */
+				/* without a read.                          */
+				rc = jbd_block_get_noread(journal->jbd_fs,
 					   &desc_block, desc_iblock);
+			}
 			if (rc != EOK)
 				break;
@@ -1883,4 +1970,5 @@ static int jbd_journal_prepare(struct jbd_journal *journal,
 
 		tag_info.checksum = checksum;
+		tag_info.is_escape = is_escape;
 
 		if (uuid_exist)
@@ -1910,4 +1998,12 @@ static int jbd_journal_prepare(struct jbd_journal *journal,
 		memcpy(data_block.data, jbd_buf->block.data,
 			journal->block_size);
+
+		/* ext4edit patch 42: the escaped copy's magic zeroed (the  */
+		/* replay puts it back, from the tag's flag).               */
+		if (is_escape) {
+			/* Escaped. */
+			jbd_set32((struct jbd_bhdr *)data_block.data, magic, 0);
+		}
+
 		jbd_buf->jbd_lba = data_block.lb_id;
 
```
<!-- /hunks -->

### Patch 43 -- on 1 KiB blocks a replay's superblock is overwritten by the pre-replay one

**Where:** `src/ext4_journal.c`, `jbd_recover`.

**The defect:** `jbd_recover` ends by writing the in-memory superblock with
the recovery flag cleared -- the copy read at mount, BEFORE the replay. On
2 and 4 KiB blocks the superblock is in block 0, whose replay branch copies
the logged superblock into memory, so the write carries it; on 1 KiB blocks
it is block 1, replayed to the device through the ordinary path, and the
write then puts the pre-replay superblock back over it.

**Shown:** a pending transaction logging the superblock's block with a new
label: at 1 KiB, unpatched, the old label after lwext4's replay (e2fsck's
replay gives the new one); at 4 KiB the new one either way; patched, the
new one at both.

**The fix:** after a successful replay the cache is flushed and the
superblock read back, keeping the mount's own mount count and state.

**Upstream:** not fixed in master (the same write-back); applies to master.

<!-- hunks 43 -->
```diff
--- a/src/ext4_journal.c
+++ b/src/ext4_journal.c
@@ -1185,8 +1185,41 @@ int jbd_recover(struct jbd_fs *jbd_fs)
 
 	r = jbd_iterate_log(jbd_fs, &info, ACTION_REVOKE);
-	if (r != EOK)
+	if (r != EOK) {
+		/* ext4edit patch 36: the revoke pass can fail part-way, so  */
+		/* the tree built so far is freed before the failure is      */
+		/* returned.                                                 */
+		jbd_destroy_revoke_tree(&info);
 		return r;
+	}
 
 	r = jbd_iterate_log(jbd_fs, &info, ACTION_RECOVER);
+
+	/* ext4edit patch 43: after a replay the in-memory superblock is      */
+	/* read back from the device before the write below.  It was read at */
+	/* mount, BEFORE the replay, so when the log held the superblock's    */
+	/* own block through the ordinary path -- a 1 KiB-block volume, its   */
+	/* superblock in block 1 -- the write below put the pre-replay        */
+	/* superblock back over the replayed one.  (Block 0's special branch  */
+	/* already took the logged copy into memory.)  The cache is flushed   */
+	/* first so the replayed blocks are on the device, and the mount's    */
+	/* mount count and state are kept, as that branch keeps them.         */
+	if (r == EOK) {
+		/* Replayed: the superblock as the replay left it. */
+		struct ext4_fs *rfs = jbd_fs->inode_ref.fs;
+		uint16_t mount_count = ext4_get16(&rfs->sb, mount_count);
+		uint16_t state = ext4_get16(&rfs->sb, state);
+
+		r = ext4_block_cache_flush(rfs->bdev);
+		if (r == EOK) {
+			/* Flushed: read it back. */
+			r = ext4_sb_read(rfs->bdev, &rfs->sb);
+		}
+		if (r == EOK) {
+			/* Read: the mount's own two fields restored. */
+			ext4_set16(&rfs->sb, mount_count, mount_count);
+			ext4_set16(&rfs->sb, state, state);
+		}
+	}
+
 	if (r == EOK) {
 		/* If we successfully replay the journal,
```
<!-- /hunks -->

### Patch 44 -- writing attributes back corrupts a SHARED xattr block

**Where:** `src/ext4_xattr.c`, `ext4_xattr_write_to_disk`.

**The defect:** four, all in the write-back that `ext4_fs_put_xattr_ref`
runs. (1) A shared block (`h_refcount` above 1) was rewritten in place as
this inode's alone, `h_refcount` set to 1: the other inodes saw this one's
attributes, and the next free of the block freed it under them. (2) The
block was not cleared, so a set that shrank kept its old trailing entries.
(3) No entry hash was computed. (4) When the set moved wholly into the
inode, a shared block's lowered count was not re-signed and the inode's
`i_blocks` still counted it.

**Shown:** two inodes whose 200-byte attributes are in blocks, one block
shared with a third inode, each attribute set rewritten: unpatched,
`e2fsck` "Extended attribute block has reference count 1, should be 2";
patched, the inode moved to a block of its own, every attribute in place,
clean. (2) and (3) were not shown by `e2fsck` on that image.

**The fix:** a shared block is copied -- a new block for this inode first,
then the old one's count lowered, re-signed and let go, `i_blocks` adjusted
(a failed allocation leaves everything as it was); the block is cleared
before it is laid out; each entry's hash is computed; (4) re-signs and
adjusts `i_blocks`.

**Upstream:** fixed in master: the hash by `5372f03`, the clear by
`9fdd0b7`, and the rest by master's rewrite of the module (`278e8d4`, GPL
since `b1a34c5`). Applies to master with fuzz, and is not needed there.

<!-- hunks 44 -->
```diff
--- a/src/ext4_xattr.c
+++ b/src/ext4_xattr.c
@@ -649,4 +649,55 @@ static int ext4_xattr_write_to_disk(struct ext4_xattr_ref *xattr_ref)
 	/* If we need an extra block to hold the EA entries*/
 	if (xattr_ref->ea_size > inode_size_rem) {
+		/* ext4edit patch 44: a SHARED block (h_refcount above 1) is   */
+		/* never rewritten in place -- the other inodes still use it.   */
+		/* This inode gets a block of its own first; only then is the   */
+		/* shared one's count lowered, re-signed and let go, so a       */
+		/* failed allocation leaves everything as it was.  As published */
+		/* the shared block was rewritten as this inode's alone, with   */
+		/* h_refcount set to 1 (ext4_xattr_set_block_header): the other */
+		/* inodes saw this one's attributes, and the next free of the   */
+		/* block freed it under them.                                   */
+		if (xattr_ref->block_loaded &&
+		    to_le32(EXT4_XATTR_BHDR(&xattr_ref->block)->h_refcount) > 1) {
+			/* Shared: a new block, then the old one released. */
+			struct ext4_block shared = xattr_ref->block;
+			ext4_fsblk_t shared_blk = ext4_inode_get_file_acl(
+			    xattr_ref->inode_ref->inode, &xattr_ref->fs->sb);
+			struct ext4_xattr_header *shared_hdr;
+
+			xattr_ref->block_loaded = false;
+			ext4_inode_set_file_acl(xattr_ref->inode_ref->inode,
+						&xattr_ref->fs->sb, 0);
+			ret = ext4_xattr_try_alloc_block(xattr_ref);
+			if (ret != EOK) {
+				/* No new block: the shared one kept, as it was. */
+				ext4_inode_set_file_acl(xattr_ref->inode_ref->inode,
+							&xattr_ref->fs->sb,
+							shared_blk);
+				xattr_ref->block = shared;
+				xattr_ref->block_loaded = true;
+				goto Finish;
+			}
+
+			/* This inode's reference to the shared block dropped. */
+			shared_hdr = EXT4_XATTR_BHDR(&shared);
+			shared_hdr->h_refcount =
+			    to_le32(to_le32(shared_hdr->h_refcount) - 1);
+			ext4_xattr_set_block_checksum(xattr_ref->inode_ref,
+						      shared_blk, shared_hdr);
+			ext4_trans_set_block_dirty(shared.buf);
+			ext4_block_set(xattr_ref->fs->bdev, &shared);
+
+			/* The shared block no longer counted in this inode's */
+			/* i_blocks (the new one was counted when allocated).  */
+			ext4_inode_set_blocks_count(
+			    &xattr_ref->fs->sb, xattr_ref->inode_ref->inode,
+			    ext4_inode_get_blocks_count(
+				&xattr_ref->fs->sb,
+				xattr_ref->inode_ref->inode) -
+			    ext4_sb_get_block_size(&xattr_ref->fs->sb) /
+				EXT4_INODE_BLOCK_SIZE);
+			xattr_ref->inode_ref->dirty = true;
+		}
 		if (!xattr_ref->block_loaded) {
 			ret = ext4_xattr_try_alloc_block(xattr_ref);
@@ -654,4 +705,12 @@ static int ext4_xattr_write_to_disk(struct ext4_xattr_ref *xattr_ref)
 				goto Finish;
 		}
+
+		/* ext4edit patch 44: the block cleared before it is laid out */
+		/* again, so no entry or value of the old layout survives past  */
+		/* the new last entry -- and the list ends in a zero word, as   */
+		/* the readers require.  As published the old bytes stayed, so  */
+		/* a set that shrank kept its old trailing entries.             */
+		memset(xattr_ref->block.data, 0,
+		       ext4_sb_get_block_size(&xattr_ref->fs->sb));
 		block_header = EXT4_XATTR_BHDR(&xattr_ref->block);
 		block_entry = EXT4_XATTR_BFIRST(&xattr_ref->block);
@@ -680,4 +739,22 @@ static int ext4_xattr_write_to_disk(struct ext4_xattr_ref *xattr_ref)
 				    xattr_ref->inode_ref->inode,
 				    &xattr_ref->fs->sb, 0);
+
+				/* ext4edit patch 44: the block stays with its  */
+				/* other users, so its lowered count is signed  */
+				/* again, and this inode no longer counts the   */
+				/* block in i_blocks.  As published neither was */
+				/* done: a checksum error on metadata_csum, and */
+				/* e2fsck's "i_blocks is ..., should be ...".   */
+				ext4_xattr_set_block_checksum(
+				    xattr_ref->inode_ref,
+				    xattr_ref->block.lb_id, block_header);
+				ext4_inode_set_blocks_count(
+				    &xattr_ref->fs->sb,
+				    xattr_ref->inode_ref->inode,
+				    ext4_inode_get_blocks_count(
+					&xattr_ref->fs->sb,
+					xattr_ref->inode_ref->inode) -
+				    ext4_sb_get_block_size(&xattr_ref->fs->sb) /
+					EXT4_INODE_BLOCK_SIZE);
 
 				xattr_ref->inode_ref->dirty = true;
@@ -718,4 +795,8 @@ static int ext4_xattr_write_to_disk(struct ext4_xattr_ref *xattr_ref)
 		       item->name_len);
 		memcpy(block_data, item->data, item->data_size);
+		/* ext4edit patch 44 (upstream master 5372f03): the entry's    */
+		/* hash, which e2fsck checks and the block's hash is made from; */
+		/* as published each entry kept whatever hash lay at its place. */
+		ext4_xattr_compute_hash(block_header, block_entry);
 		block_entry = EXT4_XATTR_NEXT(block_entry);
 		block_size_rem -= EXT4_XATTR_SIZE(item->data_size) +
```
<!-- /hunks -->

### Patch 45 -- a freed block goes unrevoked, and a revoked block reused as metadata is lost

**Where:** `src/ext4_journal.c`, `jbd_trans_try_revoke_block` and the end
of `jbd_trans_set_block_dirty`.

**The defect:** two. (1) A missed revoke: when a transaction dirties a
block an older one logged, the older buffer is flushed and the block's
record moves to the new transaction; if that transaction then FREES the
block, `jbd_trans_try_revoke_block` found the record its own and wrote no
revoke -- while the older transaction, still in the log until the log's
start passes it, held the block's old content. A crash then, and the next
replay lays the old copy over the block's next owner (file data, silently).
(2) A revoke not cancelled: a block freed and reallocated as metadata in
ONE transaction carried both its revoke and its new content, and jbd2's
replay (e2fsck's, the Linux driver's) replays a block only from a
transaction after its revoke's, so it drops the new content.

**By reading:** neither was made to fail by a test.

**The fix:** (1) a freed block is revoked whenever a journal record for it
exists, whichever transaction owns it now. (2) dirtying a block in a
transaction removes and frees that transaction's revoke of it.

**Upstream:** fixed in master by `8281a97` (both points) and `321eba3`
(freeing the cancelled record) -- bar one case: master's
`jbd_trans_try_revoke_block` revokes an own-record block only while the
older buffer is still unwritten, so an older copy already written in place
but still in the log goes unrevoked; this patch revokes there too. Applies
to master with fuzz. (Master's `13875d2`, which aligns lwext4's replay with
jbd2's revoke rule, must not be taken without (2).)

<!-- hunks 45 -->
```diff
--- a/src/ext4_journal.c
+++ b/src/ext4_journal.c
@@ -1633,4 +1633,21 @@ int jbd_trans_set_block_dirty(struct jbd_trans *trans,
 
 	ext4_bcache_set_dirty(block->buf);
+
+	/* ext4edit patch 45 (upstream master 8281a97, 321eba3): a revoke   */
+	/* this transaction holds for the block is CANCELLED now that the  */
+	/* block is logged again in it.  jbd2's replay (e2fsck's, the Linux */
+	/* driver's) does not replay a block revoked in its own            */
+	/* transaction, so a block freed and then reused as metadata in    */
+	/* one transaction would lose its new content to that replay.      */
+	{	/* The block's revoke, found in the revoke tree (patch 51). */
+		struct jbd_revoke_rec *rec, key;
+		key.lba = block->lb_id;
+		rec = RB_FIND(jbd_revoke_tree, &trans->revoke_root, &key);
+		if (rec) {
+			/* This block's: cancelled, its record freed. */
+			RB_REMOVE(jbd_revoke_tree, &trans->revoke_root, rec);
+			free(rec);
+		}
+	}
 	return EOK;
 }
@@ -1677,5 +1694,19 @@ int jbd_trans_try_revoke_block(struct jbd_trans *trans,
 
 		}
+	}
 
+	/* ext4edit patch 45 (upstream master 8281a97's revoke part): the  */
+	/* freed block is revoked whenever a journal record for it exists, */
+	/* whichever transaction owns the record now.  The record moves to */
+	/* each transaction that dirties the block, while an older one     */
+	/* that logged it stays in the log until the log's start passes    */
+	/* it; as published only a record owned by ANOTHER transaction was */
+	/* revoked, so a block edited and freed by this one was not, and a */
+	/* crash then replayed the older copy over the block's next owner. */
+	/* A revoke for a block only this transaction logged is harmless;  */
+	/* jbd_trans_set_block_dirty cancels it if the block is dirtied    */
+	/* again in this transaction.                                      */
+	if (block_rec) {
+		/* A record: the block may be in the log -- revoked. */
 		jbd_trans_revoke_block(trans, lba);
 	}
```
<!-- /hunks -->

### Patch 46 -- transaction IDs are compared without their wrap past 2^32

**Where:** `src/ext4_journal.c`: a new `trans_id_diff`; the revoke test in
`jbd_replay_block_tags`; the stop test and the last ID in
`jbd_iterate_log`.

**The defect:** jbd2 transaction IDs are 32-bit sequence numbers that run
past 2^32 to 0, and e2fsck gives a journal it resets a random starting
sequence, so a log can begin near the top. Three places ordered IDs with
plain `<` / `>`: a log whose transactions cross the wrap -- one pending
transaction numbered 0xFFFFFFFF is enough -- was scanned to a last ID of
0, the replay stopped at its first transaction, NOTHING was replayed and
the committed changes were lost; a revoke across the wrap was misjudged.

**Shown:** one pending transaction, ID 0xFFFFFFFF, logging the superblock
with a new label: unpatched, the old label after lwext4's replay; patched,
the new one, `e2fsck` clean -- as e2fsck's own replay gives.

**The fix:** the difference taken as a signed 32-bit number, at all three.

**Upstream:** fixed in master by `bc6e18d` and `ea294e2`. All 4 of its
hunks fail on master, and it is not needed there.

<!-- hunks 46 -->
```diff
--- a/src/ext4_journal.c
+++ b/src/ext4_journal.c
@@ -117,11 +117,80 @@ jbd_block_rec_cmp(struct jbd_block_rec *a, struct jbd_block_rec *b)
 }
 
+/*----------------------------------------------------------------------------*/
+/* jbd_revoke_rec_cmp:                                                        */
+/*                                                                            */
+/* ext4edit patch 51.  The order of a transaction's revoke tree: by block     */
+/* number, as the replay's revoke tree (jbd_revoke_entry_cmp) and the block   */
+/* records are ordered, so a block's revoke is found by its number alone.     */
+/*                                                                            */
+/* Arguments:                                                                 */
+/*     a : one revoke record.                                                 */
+/*     b : the other.                                                         */
+/*                                                                            */
+/* Returns:                                                                   */
+/*     int : 1 when a's block is after b's, -1 before, 0 the same block.      */
+/*----------------------------------------------------------------------------*/
+static int
+jbd_revoke_rec_cmp(struct jbd_revoke_rec *a, struct jbd_revoke_rec *b)
+{
+	if (a->lba > b->lba) {
+		/* a's block is the later one. */
+		return 1;
+	}
+
+	if (a->lba < b->lba) {
+		/* a's block is the earlier one. */
+		return -1;
+	}
+
+	/* The same block. */
+	return 0;
+}
+
 RB_GENERATE_INTERNAL(jbd_revoke, revoke_entry, revoke_node,
 		     jbd_revoke_entry_cmp, static inline)
 RB_GENERATE_INTERNAL(jbd_block, jbd_block_rec, block_rec_node,
 		     jbd_block_rec_cmp, static inline)
+/* ext4edit patch 51: the transaction revoke tree's functions. */
+RB_GENERATE_INTERNAL(jbd_revoke_tree, jbd_revoke_rec, revoke_node,
+		     jbd_revoke_rec_cmp, static inline)
 
 #define jbd_alloc_revoke_entry() calloc(1, sizeof(struct revoke_entry))
 #define jbd_free_revoke_entry(addr) free(addr)
+
+/*----------------------------------------------------------------------------*/
+/* trans_id_diff:                                                             */
+/*                                                                            */
+/* ext4edit patch 46 (upstream master bc6e18d, ea294e2).  How far one         */
+/* transaction ID is ahead of another, as jbd2 compares them: a journal's IDs */
+/* are 32-bit sequence numbers that run on past 2^32 back to 0 (e2fsck gives  */
+/* a journal it resets a RANDOM starting sequence, so a log can begin close   */
+/* to the top), and a plain < or > between two IDs either side of the wrap   */
+/* gets the order backwards -- the replay would stop at its first             */
+/* transaction, the scan would name the wrong last one, and a revoke would be */
+/* judged against the wrong side.  The difference taken as a signed 32-bit    */
+/* number is right for any two IDs less than 2^31 apart, which every live     */
+/* log's are.                                                                 */
+/*                                                                            */
+/* Arguments:                                                                 */
+/*     x : the transaction ID asked about.                                    */
+/*     y : the transaction ID it is compared with.                            */
+/*                                                                            */
+/* Returns:                                                                   */
+/*     int32_t : above 0 when x comes after y, 0 when they are the same, and  */
+/*               below 0 when x comes before y.                               */
+/*----------------------------------------------------------------------------*/
+static inline int32_t
+trans_id_diff(uint32_t x, uint32_t y)
+{
+	int32_t diff;
+
+	/* The unsigned difference wraps modulo 2^32; read as signed, its */
+	/* sign is the order.                                             */
+	diff = (int32_t)(x - y);
+
+	/* The signed distance from y to x. */
+	return diff;
+}
 
 static int jbd_has_csum(struct jbd_sb *jbd_sb)
@@ -853,6 +922,7 @@ static void jbd_replay_block_tags(struct jbd_fs *jbd_fs,
 	 * is equal or greater than that in revoke entry.*/
 	revoke_entry = jbd_revoke_entry_lookup(info, block);
+	/* ext4edit patch 46: "older" judged across the IDs' wrap past 2^32. */
 	if (revoke_entry &&
-	    arg->this_trans_id < revoke_entry->trans_id)
+	    trans_id_diff(arg->this_trans_id, revoke_entry->trans_id) < 0)
 		return;
 
@@ -1047,6 +1117,9 @@ static int jbd_iterate_log(struct jbd_fs *jbd_fs,
 		 * we will stop when we reach the end of
 		 * the journal.*/
+		/* ext4edit patch 46: "past the last" judged across the IDs'  */
+		/* wrap past 2^32 -- a plain > stops a log that starts below  */
+		/* the wrap and ends above it at its very first transaction.  */
 		if (action != ACTION_SCAN)
-			if (this_trans_id > info->last_trans_id) {
+			if (trans_id_diff(this_trans_id, info->last_trans_id) > 0) {
 				log_end = true;
 				continue;
@@ -1158,5 +1231,9 @@ static int jbd_iterate_log(struct jbd_fs *jbd_fs,
 		/* We have finished scanning the journal. */
 		info->start_trans_id = start_trans_id;
-		if (this_trans_id > start_trans_id)
+		/* ext4edit patch 46: whether the scan got past the first      */
+		/* transaction, judged across the IDs' wrap past 2^32 (the     */
+		/* last ID is then the one before where it stopped, 0 - 1      */
+		/* wrapping to 0xFFFFFFFF as the IDs do).                      */
+		if (trans_id_diff(this_trans_id, start_trans_id) > 0)
 			info->last_trans_id = this_trans_id - 1;
 		else
```
<!-- /hunks -->

### Patch 47 -- a new inode is not zeroed

**Where:** `src/ext4_fs.c`, `ext4_fs_alloc_inode`.

**The defect:** the named fields were set and the rest of the slot kept
what the freed inode had, and the inode free clears the xattr BLOCK pointer
but not the in-inode attribute area -- so a file made in a freed slot
inherited the old file's creation time, project id and in-inode attributes
(an ACL, a security label). With lwext4's own `extra_isize` (28: its
`struct ext4_inode` ends before `i_projid`) against the Linux driver's 32,
the inherited attribute is hidden rather than gone, and reappears when
`extra_isize` is grown.

**Shown:** a file with a user attribute in its inode body, removed, and a
new file made in its slot: a raw dump of the slot showed the old
attribute's header and value still there, behind the new `extra_isize`,
with the old creation time; patched, none of it.

**The fix:** the whole slot (`inode_size` bytes) is zeroed before the
fields are set.

**Upstream:** fixed in master by `8163730`. Applies to master with fuzz,
and is not needed there.

<!-- hunks 47 -->
```diff
--- a/src/ext4_fs.c
+++ b/src/ext4_fs.c
@@ -864,4 +864,16 @@ int ext4_fs_alloc_inode(struct ext4_fs *fs, struct ext4_inode_ref *inode_ref,
 	/* Initialize i-node */
 	struct ext4_inode *inode = inode_ref->inode;
+
+	/* ext4edit patch 47 (upstream master 8163730): the WHOLE slot is     */
+	/* zeroed first -- inode_size bytes, the in-inode attribute area      */
+	/* included -- so nothing of the inode it held before survives.  The  */
+	/* fields set below are only some of the slot: a freed inode keeps     */
+	/* its creation time, its project id and its in-inode attributes, and  */
+	/* those were inherited by the new file -- an attribute hidden only    */
+	/* while the new extra_isize (lwext4's, 28) is below the old one (32,  */
+	/* the Linux driver's), and back the moment it is grown.  The slot     */
+	/* lies wholly in the inode table block __ext4_fs_get_inode_ref        */
+	/* placed it in (the superblock check bounds inode_size).              */
+	memset(inode, 0, inode_size);
 
 	uint32_t mode;
```
<!-- /hunks -->

### Patch 48 -- a creating open's transaction nests inside its caller's

**Where:** `src/ext4.c`: `ext4_generic_open2`, `ext4_generic_open`,
`ext4_fopen2`, `ext4_dir_mk`, `ext4_fsymlink`.

**The defect:** `ext4_generic_open2` started a transaction for an
`O_CREAT` open and stopped it at its end. A start does nothing while a
transaction is open, but a stop always commits, so inside `ext4_fsymlink`
(and `ext4_dir_mk`), which open their own first, the open's stop COMMITTED
the caller's transaction: the link's target was then written with no
transaction, and the caller's abort on a failure aborted nothing. And its
failure exit after the path walk returned without ending the transaction it
had started.

**By reading:** the window is inside one call.

**The fix (master's scopes):** `ext4_generic_open2` opens no transaction;
`ext4_generic_open` and `ext4_fopen2` open one around a creating open,
committed on success and aborted on failure; `ext4_dir_mk`'s own is
removed; `ext4_fsymlink`'s one transaction covers the inode, the entry and
the target, inside the write-back window.

**Upstream:** fixed in master by `9b38aef`. 6 of its 10 hunks fail on
master, and it is not needed there.

<!-- hunks 48 -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -924,7 +924,13 @@ static int ext4_generic_open2(ext4_file *f, const char *path, int flags,
 		*parent_inode = ref.index;
 
-	if (flags & O_CREAT)
-		ext4_trans_start(mp);
-
+	/* ext4edit patch 48 (upstream master 9b38aef): no transaction is     */
+	/* started here.  A creating open is journalled by its CALLER --      */
+	/* ext4_generic_open and ext4_fopen2 open one around it, and          */
+	/* ext4_fsymlink's own covers the link's target too.  Started here,   */
+	/* inside a caller's transaction, it was a no-op while this           */
+	/* function's stop COMMITTED the caller's transaction: what the       */
+	/* caller wrote next (a symlink's target) went unjournaled, and its   */
+	/* abort aborted nothing; and the failure exit after the walk below   */
+	/* returned without ending it.                                        */
 	len = ext4_path_check(path, &is_goal);
 	while (1) {
@@ -1053,14 +1059,7 @@ static int ext4_generic_open2(ext4_file *f, const char *path, int flags,
 	}
 
-	r = ext4_fs_put_inode_ref(&ref);
-	if (flags & O_CREAT) {
-		if (r == EOK)
-			ext4_trans_stop(mp);
-		else
-			ext4_trans_abort(mp);
-
-	}
-
-	return r;
+	/* ext4edit patch 48: the reference given back is the result; a      */
+	/* creating caller's transaction is the caller's to end.             */
+	return ext4_fs_put_inode_ref(&ref);
 }
 
@@ -1073,4 +1072,7 @@ static int ext4_generic_open(ext4_file *f, const char *path, const char *flags,
 	uint32_t iflags;
 	int filetype;
+	int r;
+	struct ext4_mountpoint *mp = ext4_get_mount(path);
+
 	if (ext4_parse_flags(flags, &iflags) == false)
 		return EINVAL;
@@ -1081,6 +1083,28 @@ static int ext4_generic_open(ext4_file *f, const char *path, const char *flags,
 		filetype = EXT4_DE_DIR;
 
-	return ext4_generic_open2(f, path, iflags, filetype, parent_inode,
-				  name_off);
+	/* ext4edit patch 48 (upstream master 9b38aef): a creating open is    */
+	/* one transaction, opened here around the walk that makes every      */
+	/* missing level, committed when it succeeds and aborted when it      */
+	/* fails -- ext4_generic_open2 no longer opens one of its own.        */
+	if (iflags & O_CREAT) {
+		/* Creating: the transaction around the open. */
+		ext4_trans_start(mp);
+	}
+
+	r = ext4_generic_open2(f, path, iflags, filetype, parent_inode,
+			       name_off);
+
+	if (iflags & O_CREAT) {
+		/* Creating: committed on success, aborted on failure. */
+		if (r == EOK) {
+			/* Made: committed. */
+			ext4_trans_stop(mp);
+		} else {
+			/* Refused or failed: whatever it began is dropped. */
+			ext4_trans_abort(mp);
+		}
+	}
+
+	return r;
 }
 
@@ -1546,5 +1570,26 @@ int ext4_fopen2(ext4_file *f, const char *path, int flags)
 
 	ext4_block_cache_write_back(mp->fs.bdev, 1);
+
+	/* ext4edit patch 48 (upstream master 9b38aef): a creating open is    */
+	/* one transaction, opened here, as ext4_generic_open opens it --     */
+	/* ext4_generic_open2 no longer opens one of its own.                 */
+	if (flags & O_CREAT) {
+		/* Creating: the transaction around the open. */
+		ext4_trans_start(mp);
+	}
+
 	r = ext4_generic_open2(f, path, flags, filetype, NULL, NULL);
+
+	if (flags & O_CREAT) {
+		/* Creating: committed on success, aborted on failure. */
+		if (r == EOK) {
+			/* Made: committed. */
+			ext4_trans_stop(mp);
+		} else {
+			/* Refused or failed: whatever it began is dropped. */
+			ext4_trans_abort(mp);
+		}
+	}
+
 	ext4_block_cache_write_back(mp->fs.bdev, 0);
 
@@ -2342,10 +2387,11 @@ int ext4_fsymlink(const char *target, const char *path)
 
 Finish:
-	ext4_block_cache_write_back(mp->fs.bdev, 0);
-
 	if (r != EOK)
 		ext4_trans_abort(mp);
 	else
 		ext4_trans_stop(mp);
+
+	/* ext4edit patch 48: write-back ends after the transaction does. */
+	ext4_block_cache_write_back(mp->fs.bdev, 0);
 
 	EXT4_MP_UNLOCK(mp);
@@ -2899,5 +2945,9 @@ int ext4_dir_mk(const char *path)
 
 	EXT4_MP_LOCK(mp);
-	ext4_trans_start(mp);
+
+	/* ext4edit patch 48 (upstream master 9b38aef): no transaction of    */
+	/* this function's own -- the creating open below is one transaction */
+	/* by itself (ext4_generic_open), and the existence check writes      */
+	/* nothing.                                                           */
 
 	/*Check if exist.*/
@@ -2905,5 +2955,4 @@ int ext4_dir_mk(const char *path)
 	if (r == EOK) {
 		/*Directory already created*/
-		ext4_trans_stop(mp);
 		EXT4_MP_UNLOCK(mp);
 		return r;
@@ -2912,11 +2961,6 @@ int ext4_dir_mk(const char *path)
 	/*Create new dir*/
 	r = ext4_generic_open(&f, path, "w", false, 0, 0);
-	if (r != EOK) {
-		ext4_trans_abort(mp);
-		EXT4_MP_UNLOCK(mp);
-		return r;
-	}
 
-	ext4_trans_stop(mp);
+	/* Made, or the creating open's failure (its transaction aborted). */
 	EXT4_MP_UNLOCK(mp);
 	return r;
```

- Also this patch's: the hunk of `src/ext4.c` at upstream line 2327, shown under patch 12.
<!-- /hunks -->

### Patch 49 -- a failed bitmap write leaks a group descriptor reference

**Where:** `src/ext4_balloc.c`, `ext4_balloc_alloc_block`, the two bitmap
writes in the goal's group.

**The defect:** when writing the bitmap block failed, those two exits
returned without giving the group descriptor's reference back. By reading:
an I/O error only.

**The fix:** each gives the reference back first, as the function's other
exits do.

**Upstream:** issue #57; fixed in master by `2248e43` (pull request #58).
1 of its 2 hunks fails on master, and it is not needed there.

<!-- hunks 49 -->
```diff
--- a/src/ext4_balloc.c
+++ b/src/ext4_balloc.c
@@ -433,6 +433,12 @@ int ext4_balloc_alloc_block(struct ext4_inode_ref *inode_ref,
 			ext4_trans_set_block_dirty(b.buf);
 			r = ext4_block_set(inode_ref->fs->bdev, &b);
-			if (r != EOK)
+			if (r != EOK) {
+				/* ext4edit patch 49 (upstream issue #57, master    */
+				/* 2248e43, PR #58): the group's descriptor         */
+				/* reference given back before the failure          */
+				/* returns, as every other exit here gives it back. */
+				ext4_fs_put_block_group_ref(&bg_ref);
 				return r;
+			}
 
 			alloc = ext4_fs_bg_idx_to_addr(sb, tmp_idx, bg_id);
@@ -448,6 +454,11 @@ int ext4_balloc_alloc_block(struct ext4_inode_ref *inode_ref,
 		ext4_trans_set_block_dirty(b.buf);
 		r = ext4_block_set(inode_ref->fs->bdev, &b);
-		if (r != EOK)
+		if (r != EOK) {
+			/* ext4edit patch 49 (upstream issue #57, master       */
+			/* 2248e43, PR #58): the descriptor reference given    */
+			/* back first.                                         */
+			ext4_fs_put_block_group_ref(&bg_ref);
 			return r;
+		}
 
 		alloc = ext4_fs_bg_idx_to_addr(sb, rel_blk_idx, bg_id);
```
<!-- /hunks -->

### Patch 50 -- a missing path level is hashed and named with the rest of the path

**Where:** `src/ext4_dir_idx.c`, `ext4_dir_dx_add_entry` (and its
declaration); its call in `src/ext4_dir.c`.

**The defect:** the name's length was taken as `strlen(name)`. A create
whose path has a missing level (`ext4_generic_open2` makes it as a
directory) hands in a pointer into the whole path, so for the level "nodir"
of "/nodir/sub" the length ran on to "nodir/sub": in an indexed directory
the entry was hashed on, and written with, the rest of the path.

**Shown:** a root of 400 long names indexed by `e2fsck -D`, then a create of
"/nodir/sub": unpatched ENOSPC, nothing made; patched, both levels made and
`e2fsck -fn` (which checks the index's hash order) clean.

**The fix:** the caller's length is passed through.

**Upstream:** fixed in master by `1aa9740`. All 4 of its hunks fail on
master, and it is not needed there.

<!-- hunks 50 -->
```diff
--- a/include/ext4_dir_idx.h
+++ b/include/ext4_dir_idx.h
@@ -89,8 +89,13 @@ int ext4_dir_dx_find_entry(struct ext4_dir_search_result *result,
  * @param child  I-node to be referenced from directory entry
  * @param name   Name of new directory entry
+ * @param name_len Length of the name in bytes
  * @return Error code
  */
+/* ext4edit patch 50 (upstream master 1aa9740): the name's length is      */
+/* passed in, as ext4_dir_add_entry has it, because the name is not       */
+/* always terminated where it ends.                                       */
 int ext4_dir_dx_add_entry(struct ext4_inode_ref *parent,
-			  struct ext4_inode_ref *child, const char *name);
+			  struct ext4_inode_ref *child, const char *name,
+			  uint32_t name_len);
 
 /**@brief Add new entry to indexed directory
--- a/src/ext4_dir_idx.c
+++ b/src/ext4_dir_idx.c
@@ -1299,7 +1299,12 @@ int ext4_dir_dx_add_entry(struct ext4_inode_ref *parent,
 
 	/* Initialize hinfo structure (mainly compute hash) */
-	uint32_t name_len = strlen(name);
+	/* ext4edit patch 50 (upstream master 1aa9740): name_len is the      */
+	/* caller's, not strlen(name).  A create that makes a MISSING level  */
+	/* of its path hands in a pointer into the whole path, so strlen ran  */
+	/* on to the end ("nodir/sub" for the level "nodir"): the entry was   */
+	/* hashed on, and written with, the rest of the path.                */
 	struct ext4_hash_info hinfo;
-	r = ext4_dir_hinfo_init(&hinfo, &root_blk, &fs->sb, name_len, name);
+	r = ext4_dir_hinfo_init(&hinfo, parent, &root_blk, &fs->sb, name_len,
+				name);
 	if (r != EOK) {
 		ext4_block_set(fs->bdev, &root_blk);
```

- Also this patch's: the hunk of `src/ext4_dir.c` at upstream line 336, shown under patch 36.
- Also this patch's: the hunk of `src/ext4_dir_idx.c` at upstream line 1108, shown under patch 5.
<!-- /hunks -->

### Patch 51 -- a transaction's revokes are a list

**Where:** `include/ext4_journal.h` (`struct jbd_revoke_rec`, `struct
jbd_trans`); `src/ext4_journal.c` (the transaction's revoke functions).

**What it is:** a cost, not a wrong result. With patch 45 every block a
transaction dirties looks for its own revoke to cancel; in a list that is a
walk of all of them, and a large delete revokes thousands, so the work grew
as their square. A block revoked twice was also recorded twice.

**The change:** the revokes are an RB tree keyed by block, as master keeps
them: a lookup finds the block's record or none, a second revoke adds
nothing, and the log's revoke block lists them in block order.

**Upstream:** master's own shape (`358f3f8` for the duplicate). 7 of its 9
hunks fail on master, which does not need it.

<!-- hunks 51 -->
```diff
--- a/include/ext4_journal.h
+++ b/include/ext4_journal.h
@@ -65,7 +65,13 @@ struct jbd_buf {
 };
 
+/* ext4edit patch 51 (upstream master's shape): a transaction's revokes   */
+/* are an RB tree keyed by block, not a list -- each revoke is looked up  */
+/* (a second revoke of one block is not added; a block logged again       */
+/* cancels its revoke, patch 45), and a list made every dirtied block a   */
+/* walk of all the transaction's revokes: a large delete's thousands made */
+/* the work grow as their square.                                         */
 struct jbd_revoke_rec {
-	ext4_fsblk_t lba;
-	LIST_ENTRY(jbd_revoke_rec) revoke_node;
+	ext4_fsblk_t lba;                        // the revoked block
+	RB_ENTRY(jbd_revoke_rec) revoke_node;    // its place in the transaction's revoke_root
 };
 
@@ -92,5 +98,5 @@ struct jbd_trans {
 
 	TAILQ_HEAD(jbd_trans_buf, jbd_buf) buf_queue;
-	LIST_HEAD(jbd_revoke_list, jbd_revoke_rec) revoke_list;
+	RB_HEAD(jbd_revoke_tree, jbd_revoke_rec) revoke_root;	// ext4edit patch 51: the revokes, by block
 	LIST_HEAD(jbd_trans_block_rec, jbd_block_rec) tbrec_list;
 	TAILQ_ENTRY(jbd_trans) trans_node;
--- a/src/ext4_journal.c
+++ b/src/ext4_journal.c
@@ -1440,4 +1440,7 @@ jbd_journal_new_trans(struct jbd_journal *journal)
 	trans->error = EOK;
 	TAILQ_INIT(&trans->buf_queue);
+	/* ext4edit patch 51: the revoke tree starts empty (calloc's zeros  */
+	/* are already that; said here, as the queue above is).             */
+	RB_INIT(&trans->revoke_root);
 	return trans;
 }
@@ -1643,11 +1646,22 @@ int jbd_trans_revoke_block(struct jbd_trans *trans,
 			   ext4_fsblk_t lba)
 {
-	struct jbd_revoke_rec *rec =
-		calloc(1, sizeof(struct jbd_revoke_rec));
+	struct jbd_revoke_rec *rec, key;
+
+	/* ext4edit patch 51 (upstream master 358f3f8's case): a block this  */
+	/* transaction has already revoked is not added again -- one record  */
+	/* per block, so the revoke written to the log names it once.        */
+	key.lba = lba;
+	if (RB_FIND(jbd_revoke_tree, &trans->revoke_root, &key)) {
+		/* Already revoked here: nothing to add. */
+		return EOK;
+	}
+
+	rec = calloc(1, sizeof(struct jbd_revoke_rec));
 	if (!rec)
 		return ENOMEM;
 
 	rec->lba = lba;
-	LIST_INSERT_HEAD(&trans->revoke_list, rec, revoke_node);
+	/* ext4edit patch 51: into the tree, by block. */
+	RB_INSERT(jbd_revoke_tree, &trans->revoke_root, rec);
 	return EOK;
 }
@@ -1717,7 +1731,7 @@ void jbd_journal_free_trans(struct jbd_journal *journal,
 		free(jbd_buf);
 	}
-	LIST_FOREACH_SAFE(rec, &trans->revoke_list, revoke_node,
-			  tmp2) {
-		LIST_REMOVE(rec, revoke_node);
+	/* ext4edit patch 51: every revoke record, out of the tree and freed. */
+	RB_FOREACH_SAFE(rec, jbd_revoke_tree, &trans->revoke_root, tmp2) {
+		RB_REMOVE(jbd_revoke_tree, &trans->revoke_root, rec);
 		free(rec);
 	}
@@ -1952,12 +1966,21 @@ jbd_journal_prepare_revoke(struct jbd_journal *journal,
 		record_len = 8;
 
-	LIST_FOREACH_SAFE(rec, &trans->revoke_list, revoke_node,
-			  tmp) {
+	/* ext4edit patch 51: the revokes in block order, from the tree. */
+	RB_FOREACH_SAFE(rec, jbd_revoke_tree, &trans->revoke_root, tmp) {
 again:
 		if (!desc_iblock) {
 			struct jbd_bhdr *bhdr;
 			desc_iblock = jbd_journal_alloc_block(journal, trans);
-			rc = jbd_block_get_noread(journal->jbd_fs,
+			/* ext4edit patch 36: as at the commit block -- */
+			/* nothing more is written into a faulted journal. */
+			if (journal->fault) {
+				/* Faulted: no descriptor block is taken. */
+				rc = journal->fault;
+			} else {
+				/* Not faulted: the descriptor block, taken */
+				/* without a read.                          */
+				rc = jbd_block_get_noread(journal->jbd_fs,
 					   &desc_block, desc_iblock);
+			}
 			if (rc != EOK) {
 				break;
@@ -2110,6 +2133,7 @@ int jbd_journal_commit_trans(struct jbd_journal *journal,
 		goto Finish;
 
+	/* ext4edit patch 51: the revoke tree's emptiness. */
 	if (TAILQ_EMPTY(&trans->buf_queue) &&
-	    LIST_EMPTY(&trans->revoke_list)) {
+	    RB_EMPTY(&trans->revoke_root)) {
 		/* Since there are no entries in both buffer list
 		 * and revoke entry list, we do not consider trans as
```

- Also this patch's: the hunk of `src/ext4_journal.c` at upstream line 117, shown under patch 46.
- Also this patch's: the hunk of `src/ext4_journal.c` at upstream line 1633, shown under patch 45.
<!-- /hunks -->

Two later corrections belong to patch 7's own new code (`ext4_inline.c`)
and apply only with it: an inline size is checked before it sizes a buffer,
an inline directory's entries are held to the block, a conversion can no
longer empty the inode; and dropping an inline inode whose "system.data"
attribute is missing is allowed.

---

### Patch 52 -- a name of exactly 255 bytes cannot be reached

**The defect.** `ext4_path_check` measures the next path component by
scanning for the `/` or NUL that ends it, but its loop runs `i` from 0 to
254 only -- `i < EXT4_DIRECTORY_FILENAME_LEN` -- so a component of exactly
255 bytes, ext4's longest, never meets its terminator inside the loop and
the function returns 0, an empty name. Every operation on such a name then
fails with `ENOENT`: it cannot be created, opened, listed into or removed,
although a filesystem made by `mke2fs` holds 255-byte names without
complaint. A 254-byte name works.

**Shown.** `mkdir` of a 254-byte name succeeds and of a 255-byte name fails
"No such file or directory" on a fresh ext4 image (2026-10-28); after the
patch the 255-byte name is created and listed, and a 256-byte one is still
refused (the walk returns 0 for it, and `ext4_link`'s own `len >
EXT4_DIRECTORY_FILENAME_LEN` check refuses it on creation).

**The fix.** The loop runs to `i <= EXT4_DIRECTORY_FILENAME_LEN`, so index
255 -- where the terminator of a 255-byte name sits -- is examined. The
over-long case is unchanged: a name of 256 bytes or more still returns 0.

**Master:** not fixed -- `58bcf89` has the same loop.

**Applies to master:** as it stands (the function is unchanged there).

<!-- hunks -->
```diff
--- a/src/ext4.c
+++ b/src/ext4.c
@@ -718,5 +718,10 @@ static int ext4_path_check(const char *path, bool *is_goal)
 	int i;
 
-	for (i = 0; i < EXT4_DIRECTORY_FILENAME_LEN; ++i) {
+	/* ext4edit patch 52: the loop must reach index 255 to see the '/' or */
+	/* NUL that ends a name of exactly EXT4_DIRECTORY_FILENAME_LEN bytes; */
+	/* stopping at 254 returned 0 for such a name, and every operation on */
+	/* it then failed with ENOENT although ext4 allows it.  A name of 256 */
+	/* or more still returns 0, and ext4_link refuses it on creation.     */
+	for (i = 0; i <= EXT4_DIRECTORY_FILENAME_LEN; ++i) {
 
 		if (path[i] == '/') {
```
<!-- /hunks -->

---

## Part 2 -- defects found and verified, NOT fixed in the copy

| Defect | Where | Shown | Upstream |
|---|---|---|---|
| An htree checksum check hashes `count_offset + count * 8` bytes with `count` unchecked against `limit`: up to about 512 KiB read past a 1-4 KiB buffer, before the walk's own `cnt > limit` refusal. Reached by any name lookup in an indexed directory on a metadata_csum filesystem whose index `count` is damaged, read-only included. Fix: treat `count > limit` as corrupt before hashing, in both functions. | `ext4_dir_idx.c`, `ext4_dir_dx_csum_verify`, `ext4_dir_set_dx_csum` | reading | issue #169 |
| An htree split can take a block the (damaged) bitmap calls free that is the directory's own root or leaf; the split writes over the root and a later `memmove` runs far past the block (heap overflow). Writable mounts. | `ext4_dir_idx.c`, `ext4_dir_dx_split_data`, `ext4_dir_dx_split_index` | reading (the issue's ASan report) | issue #175, pull request #176 (needs adapting to patch 5's rewrite) |
| Building an uninitialised group's block bitmap sets bits at offsets from the descriptor and superblock without checking them against the bitmap: the block bitmap before its group, the inode bitmap or table past it, an unbounded `s_reserved_gdt_blocks`, the last group's size wrapping when `first_data_block` exceeds the remainder. Bits written up to hundreds of MiB past a 1-4 KiB buffer. Writable mounts of a damaged or crafted filesystem. Fix: Linux's `ext4_check_descriptors` at a writable mount, plus a bit-index guard. | `ext4_fs.c`, `ext4_fs_init_block_bitmap`; `ext4_super.c` | reading | issues #170, #171, #173, #92, #172 (part) |
| A path with ".." inside it, handed to a CREATING call, makes entries whose names contain the slash until the inodes run out. | `ext4.c`, `ext4_generic_open2`'s create path | test | -- |
| A directory path with a trailing "/" opens the directory but leaves its own name empty; a rename from it links the directory under the new name and then fails to unlink the old. | `ext4.c`, `ext4_generic_open2` with `ext4_path_check`; `ext4_frename` | reading | -- |
| A directory can be renamed INTO ITS OWN SUBTREE. | `ext4.c`, `ext4_frename` | test | pull request #146 |
| A filesystem with an EXTERNAL journal is opened for writing by looking for its journal at inode 0, and fails with ERANGE. | `ext4_journal.c`, `jbd_get_fs` | test | -- |
| `fast_commit`: the journal's fast-commit area is neither honoured (lwext4's log can wrap into it) nor replayed (patch 10 only names the flag). | `ext4_journal.c` | reading | -- |
| Writing into an UNWRITTEN extent zeroes it through the block cache, then writes the data directly; a later flush can put the zeros back. | `ext4.c` / `ext4_extent.c`, `ext4_ext_zero_unwritten_range` | reading | pull request #163 (by its title) |
| A failure inside the multi-block free's group loop returns without revoking the groups already freed. | `ext4_balloc.c`, `ext4_balloc_free_blocks` | reading | -- |
| `ext4_bmap_bit_find_clr` returns `sbit`, not `i`, from its first-byte loop. Its present callers never take that path; the fix is one word. | `ext4_bitmap.c` | reading | issue #77 |
| A large write can wrap `size` when the extent index and the write loop disagree (a write starting past the end of file). | `ext4.c`, `ext4_fwrite` | reading | issue #67, pull request #115 |
| Group count on 1 KiB filesystems: `ext4_block_group_cnt` counts groups from the whole block count where Linux subtracts `first_data_block` first; they disagree when the block count is a multiple of `blocks_per_group` plus one (one extra, empty, group). The root of issue #172's last-group wrap. | `ext4_super.c`, `ext4_block_group_cnt` | reading | -- |
| The commit block's v1 checksum branch tests `JBD_FEATURE_COMPAT_CHECKSUM` in the INCOMPAT word (where 0x1 is REVOKE), and the v1 sum it writes is a little-endian crc32 where jbd2's is big-endian. v2 / v3 overwrite those fields, so it matters only to a v1 checksummed journal. | `ext4_journal.c`, `jbd_trans_write_commit_block` | reading | master `d1f4f6d` / `6176f0a` write the header fields as bytes |
| The prepend merge lowers a leaf's first extent's `first_block` without correcting the index above it, so in a tree of depth 1 or more the parent index keeps the old, higher start (e2fsck: "Logical start does not match logical start at next level"). Needs a write before a file's first extent landing just before it physically. | `ext4_extent.c`, `ext4_ext_insert_leaf` | reading | in master too |
| Reading in-inode attributes takes `extra_isize` unbounded and walks the inode body on a remainder only a zero word stops, so a damaged inode can make it read past the inode. | `ext4_xattr.c`, `ext4_xattr_inode_fetch` | reading | in master's rewrite too |
| A removal that ends INSIDE an extent keeps a zero-length tail (`len` cut before `new_len` is taken from it). Reached only by a truncate to a size inside an extent; lwext4's own truncate always removes to the end. | `ext4_extent.c`, `ext4_ext_remove_leaf` | reading | fixed in master (`dbcf993`) |
| Block 0's index in its group is computed wrongly on a 1 KiB-block volume; harmless, as no allocation goal or freed block is ever 0. | `ext4_fs.h`, `ext4_fs_addr_to_idx_bg` | reading | fixed in master (`af50342`) |
| The xattr name parser accepts an ACL name that is a whole prefix (`system.posix_acl_access`) as a prefix. Reached only through lwext4's xattr API. | `ext4_xattr.c`, `ext4_extract_xattr_name` | reading | fixed in master (`b088876`) |

Reported upstream and NOT reproduced here: issue #68 (journal memory not
released while writing one particular file); pull requests #167 and #123's
faults.

---

## Part 3 -- upstream issues that are not lwext4 defects

| Issue | What it is |
|---|---|
| #42 | a MinGW build error in upstream's demo program (`inet_pton`), not in the library |
| #48, #66 | install and packaging |
| #53, #56 | a usage question; a request for examples |
| #55, #73, #82 | logical MBR partitions, MBR CHS values, GPT: lwext4's optional `ext4_mbr.c` |
| #59 | macOS support |
| #60 | partial `ext4_readlink` with a short buffer: the caller sizes its buffer |
| #75 | an offset for upstream's demo file device |
| #79, #83 | concurrency: lwext4 is not thread-safe, by design |
