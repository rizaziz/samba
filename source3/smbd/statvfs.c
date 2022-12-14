/* 
   Unix SMB/CIFS implementation.
   VFS API's statvfs abstraction
   Copyright (C) Alexander Bokovoy			2005
   Copyright (C) Steve French				2005
   Copyright (C) James Peach				2006
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "system/filesys.h"
#include "smbd/smbd.h"

#if defined(DARWINOS)
#include <sys/attr.h>

static int darwin_fs_capabilities(const char * path)
{
	int caps = 0;
	vol_capabilities_attr_t *vcaps;
	struct attrlist	attrlist;
	char attrbuf[sizeof(u_int32_t) + sizeof(vol_capabilities_attr_t)];

#define FORMAT_CAP(vinfo, cap) \
	( ((vinfo)->valid[VOL_CAPABILITIES_FORMAT] & (cap)) && \
	   ((vinfo)->capabilities[VOL_CAPABILITIES_FORMAT] & (cap)) )

#define INTERFACE_CAP(vinfo, cap) \
	( ((vinfo)->valid[VOL_CAPABILITIES_INTERFACES] & (cap)) && \
	   ((vinfo)->capabilities[VOL_CAPABILITIES_INTERFACES] & (cap)) )

	ZERO_STRUCT(attrlist);
	attrlist.bitmapcount = ATTR_BIT_MAP_COUNT;
	attrlist.volattr = ATTR_VOL_CAPABILITIES;

	if (getattrlist(path, &attrlist, attrbuf, sizeof(attrbuf), 0) != 0) {
		DEBUG(0, ("getattrlist for %s capabilities failed: %s\n",
			    path, strerror(errno)));
		/* Return no capabilities on failure. */
		return 0;
	}

	vcaps =
	    (vol_capabilities_attr_t *)(attrbuf + sizeof(u_int32_t));

	if (FORMAT_CAP(vcaps, VOL_CAP_FMT_SPARSE_FILES)) {
		caps |= FILE_SUPPORTS_SPARSE_FILES;
	}

	if (FORMAT_CAP(vcaps, VOL_CAP_FMT_CASE_SENSITIVE)) {
		caps |= FILE_CASE_SENSITIVE_SEARCH;
	}

	if (FORMAT_CAP(vcaps, VOL_CAP_FMT_CASE_PRESERVING)) {
		caps |= FILE_CASE_PRESERVED_NAMES;
	}

	if (INTERFACE_CAP(vcaps, VOL_CAP_INT_EXTENDED_SECURITY)) {
		caps |= FILE_PERSISTENT_ACLS;
	}

	return caps;
}
#endif /* DARWINOS */

#if defined(BSD_STYLE_STATVFS)
static int bsd_statvfs(const char *path, struct vfs_statvfs_struct *statbuf)
{
	struct statfs sbuf;
	int ret;

	ret = statfs(path, &sbuf);
	if (ret == 0) {
		statbuf->OptimalTransferSize = sbuf.f_iosize;
		statbuf->BlockSize = sbuf.f_bsize;
		statbuf->TotalBlocks = sbuf.f_blocks;
		statbuf->BlocksAvail = sbuf.f_bfree;
		statbuf->UserBlocksAvail = sbuf.f_bavail;
		statbuf->TotalFileNodes = sbuf.f_files;
		statbuf->FreeFileNodes = sbuf.f_ffree;
		statbuf->FsIdentifier =
			(((uint64_t) sbuf.f_fsid.val[0] << 32) & 0xffffffff00000000LL) |
			    (uint64_t) sbuf.f_fsid.val[1];
#ifdef DARWINOS
		statbuf->FsCapabilities = darwin_fs_capabilities(sbuf.f_mntonname);
#else
		/* Try to extrapolate some of the fs flags into the
		 * capabilities
		 */
		statbuf->FsCapabilities =
		    FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES;
#ifdef MNT_ACLS
		if (sbuf.f_flags & MNT_ACLS)
			statbuf->FsCapabilities |= FILE_PERSISTENT_ACLS;
#endif
#endif
		if (sbuf.f_flags & MNT_QUOTA)
			statbuf->FsCapabilities |= FILE_VOLUME_QUOTAS;
		if (sbuf.f_flags & MNT_RDONLY)
			statbuf->FsCapabilities |= FILE_READ_ONLY_VOLUME;
	}

	return ret;
}
#elif defined(STAT_STATVFS) && defined(HAVE_FSID_INT)
static int posix_statvfs(const char *path, struct vfs_statvfs_struct *statbuf)
{
	struct statvfs statvfs_buf;
	int result;

	result = statvfs(path, &statvfs_buf);

	if (!result) {
		/* statvfs bsize is not the statfs bsize, the naming is terrible,
		 * see bug 11810 */
		statbuf->OptimalTransferSize = statvfs_buf.f_bsize;
		statbuf->BlockSize = statvfs_buf.f_frsize;
		statbuf->TotalBlocks = statvfs_buf.f_blocks;
		statbuf->BlocksAvail = statvfs_buf.f_bfree;
		statbuf->UserBlocksAvail = statvfs_buf.f_bavail;
		statbuf->TotalFileNodes = statvfs_buf.f_files;
		statbuf->FreeFileNodes = statvfs_buf.f_ffree;
		statbuf->FsIdentifier = statvfs_buf.f_fsid;
		/* Try to extrapolate some of the fs flags into the
		 * capabilities
		 */
		statbuf->FsCapabilities =
		    FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES;
#ifdef ST_QUOTA
		if (statvfs_buf.f_flag & ST_QUOTA)
			statbuf->FsCapabilities |= FILE_VOLUME_QUOTAS;
#endif
		if (statvfs_buf.f_flag & ST_RDONLY)
			statbuf->FsCapabilities |= FILE_READ_ONLY_VOLUME;

#if defined(HAVE_FALLOC_FL_PUNCH_HOLE) && defined(HAVE_LSEEK_HOLE_DATA)
		/*
		 * Only flag sparse file support if ZERO_DATA can be used to
		 * deallocate blocks, and SEEK_HOLE / SEEK_DATA can be used
		 * to provide QUERY_ALLOCATED_RANGES information.
		 */
		statbuf->FsCapabilities |= FILE_SUPPORTS_SPARSE_FILES;
#endif
	}
	return result;
}
#endif

/* 
 sys_statvfs() is an abstraction layer over system-dependent statvfs()/statfs()
 for particular POSIX systems. Due to controversy of what is considered more important
 between LSB and FreeBSD/POSIX.1 (IEEE Std 1003.1-2001) we need to abstract the interface
 so that particular OS would use its preferred interface.
*/
int sys_statvfs(const char *path, struct vfs_statvfs_struct *statbuf)
{
#if defined(BSD_STYLE_STATVFS)
	return bsd_statvfs(path, statbuf);
#elif defined(STAT_STATVFS) && defined(HAVE_FSID_INT)
	return posix_statvfs(path, statbuf);
#else
	/* BB change this to return invalid level */
#ifdef EOPNOTSUPP
	return EOPNOTSUPP;
#else
	return -1;
#endif /* EOPNOTSUPP */
#endif /* LINUX */

}
