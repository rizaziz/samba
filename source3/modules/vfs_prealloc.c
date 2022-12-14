/*
 * XFS preallocation support module.
 *
 * Copyright (c) James Peach 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"
#include "system/filesys.h"
#include "smbd/smbd.h"

/* Extent preallocation module.
 *
 * The purpose of this module is to preallocate space on the filesystem when
 * we have a good idea of how large files are supposed to be. This lets writes
 * proceed without having to allocate new extents and results in better file
 * layouts on disk.
 *
 * Currently only implemented for XFS. This module is based on an original idea
 * and implementation by Sebastian Brings.
 *
 * Tunables.
 *
 *      prealloc: <ext>	    Number of bytes to preallocate for a file with
 *			    the matching extension.
 *      prealloc:debug	    Debug level at which to emit messages.
 *
 * Example.
 *
 *	prealloc:mpeg = 500M  # Preallocate *.mpeg to 500 MiB.
 */

#ifdef HAVE_XFS_LIBXFS_H
#include <xfs/libxfs.h>
#define lock_type xfs_flock64_t
#else
#define lock_type struct flock64
#endif

#define MODULE "prealloc"
static int module_debug;

static int preallocate_space(int fd, off_t size)
{
	int err;
	lock_type fl = {0};

	if (size <= 0) {
		return 0;
	}

	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = size;

	/* IMPORTANT: We use RESVSP because we want the extents to be
	 * allocated, but we don't want the allocation to show up in
	 * st_size or persist after the close(2).
	 */

#if defined(XFS_IOC_RESVSP64)
	/* On Linux this comes in via libxfs.h. */
	err = xfsctl(NULL, fd, XFS_IOC_RESVSP64, &fl);
#elif defined(F_RESVSP64)
	/* On IRIX, this comes from fcntl.h. */
	err = fcntl(fd, F_RESVSP64, &fl);
#else
	err = -1;
	errno = ENOSYS;
#endif
	if (err) {
		DEBUG(module_debug,
			("%s: preallocate failed on fd=%d size=%lld: %s\n",
			MODULE, fd, (long long)size, strerror(errno)));
	}

	return err;
}

static int prealloc_connect(
                struct vfs_handle_struct *  handle,
                const char *                service,
                const char *                user)
{
	int ret = SMB_VFS_NEXT_CONNECT(handle, service, user);

	if (ret < 0) {
		return ret;
	}

	module_debug = lp_parm_int(SNUM(handle->conn),
					MODULE, "debug", 100);

	return 0;
}

static int prealloc_openat(struct vfs_handle_struct* handle,
			   const struct files_struct *dirfsp,
			   const struct smb_filename *smb_fname,
			   files_struct *fsp,
			   const struct vfs_open_how *how)
{
	int fd;
	off_t size = 0;
	const char * dot;
	char fext[10];

	if (!(how->flags & (O_CREAT|O_TRUNC))) {
		/* Caller is not intending to rewrite the file. Let's not mess
		 * with the allocation in this case.
		 */
		goto normal_open;
	}

	*fext = '\0';
	dot = strrchr(smb_fname->base_name, '.');
	if (dot && *++dot) {
		if (strlen(dot) < sizeof(fext)) {
			bool ok;
			strncpy(fext, dot, sizeof(fext));
			ok = strlower_m(fext);
			if (!ok);
				goto normal_open;
			}
		}
	}

	if (*fext == '\0') {
		goto normal_open;
	}

	/* Syntax for specifying preallocation size is:
	 *	MODULE: <extension> = <size>
	 * where
	 *	<extension> is the file extension in lower case
	 *	<size> is a size like 10, 10K, 10M
	 */
	size = conv_str_size(lp_parm_const_string(SNUM(handle->conn), MODULE,
						    fext, NULL));
	if (size <= 0) {
		/* No need to preallocate this file. */
		goto normal_open;
	}

	fd = SMB_VFS_NEXT_OPENAT(handle, dirfsp, smb_fname, fsp, how);
	if (fd < 0) {
		return fd;
	}

	/* Preallocate only if the file is being created or replaced. Note that
	 * Samba won't ever pass down O_TRUNC, which is why we have to handle
	 * truncate calls specially.
	 */
	if ((how->flags & O_CREAT) || (how->flags & O_TRUNC)) {
		off_t * psize;

		psize = VFS_ADD_FSP_EXTENSION(handle, fsp, off_t, NULL);
		if (psize == NULL || *psize == -1) {
			return fd;
		}

		DEBUG(module_debug,
			("%s: preallocating %s (fd=%d) to %lld bytes\n",
			    MODULE, smb_fname_str_dbg(smb_fname), fd,
			    (long long)size));

		*psize = size;
		if (preallocate_space(fd, *psize) < 0) {
			VFS_REMOVE_FSP_EXTENSION(handle, fsp);
		}
	}

	return fd;

normal_open:
	/* We are not creating or replacing a file. Skip the
	 * preallocation.
	 */
	DEBUG(module_debug, ("%s: skipping preallocation for %s\n",
		MODULE, smb_fname_str_dbg(smb_fname)));
	return SMB_VFS_NEXT_OPENAT(handle, dirfsp, smb_fname, fsp, how);
}

static int prealloc_ftruncate(vfs_handle_struct * handle,
			files_struct *	fsp,
			off_t	offset)
{
	off_t *psize;
	int ret = SMB_VFS_NEXT_FTRUNCATE(handle, fsp, offset);

	/* Maintain the allocated space even in the face of truncates. */
	if ((psize = VFS_FETCH_FSP_EXTENSION(handle, fsp))) {
		preallocate_space(fsp_get_io_fd(fsp), *psize);
	}

	return ret;
}

static struct vfs_fn_pointers prealloc_fns = {
	.openat_fn = prealloc_openat,
	.ftruncate_fn = prealloc_ftruncate,
	.connect_fn = prealloc_connect,
};

static_decl_vfs;
NTSTATUS vfs_prealloc_init(TALLOC_CTX *ctx)
{
	return smb_register_vfs(SMB_VFS_INTERFACE_VERSION,
				MODULE, &prealloc_fns);
}
