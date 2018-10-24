#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <stddef.h>
#include <limits.h>

#include "sfs.h"

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1 << 1)
#endif

/****h* sfs/sfs_fuse
 *  NAME
 *    sfs_fuse -- FUSE interface for the sfs implementation
 *  DESCRIPTION
 *    FUSE interface implementing severatl FUSE functions.
 ******
 */

static SFS *sfs;

static struct options {
    const char *filename;
    char *absolute_filename;
    int show_help;
} options;

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }

static const struct fuse_opt option_spec[] = {
    OPTION("--name=%s", filename),
    OPTION("-h", show_help),
    OPTION("--help", show_help),
    FUSE_OPT_END
};

static void *sfs_fuse_init(struct fuse_conn_info *conn,
                        struct fuse_config *cfg)
{
    printf("### sfs_fuse_init: fn=\"%s\"\n", options.absolute_filename);
    sfs = sfs_init(options.absolute_filename);
    cfg->kernel_cache = 1;
    return NULL;
}

static void sfs_fuse_destroy(void *private_data)
{
    printf("### sfs_fuse_destroy\n");
    sfs_terminate(sfs);
    sfs = NULL;
}

static const char *fix_path(const char *path)
{
    char *result = (char *)path;
    while (*result == '/')
        result++;
    return result;
}

static int sfs_fuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info* fi)
{
    printf("### sfs_fuse_getattr: '%s'\n", path);

    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();

    struct timespec timespec;
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        if (sfs_get_sfs_time(sfs, &timespec) == 0) {
            stbuf->st_mtim = timespec;
            return 0;
        } else {
            fprintf(stderr, "sfs_get_sfs_mtime error\n");
            return -1;
        }
    }

    const char *fxpath = fix_path(path);
    if (sfs_is_dir(sfs, fxpath)) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        if (sfs_get_dir_time(sfs, fxpath, &timespec) == 0) {
            stbuf->st_mtim = timespec;
            return 0;
        } else {
            fprintf(stderr, "sfs_get_sfs_mtime error\n");
            return -1;
        }
    }

    if (sfs_is_file(sfs, fxpath)) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = sfs_get_file_size(sfs, fxpath);
        if (sfs_get_file_time(sfs, fxpath, &timespec) == 0) {
            stbuf->st_mtim = timespec;
            return 0;
        } else {
            fprintf(stderr, "sfs_get_sfs_mtime error\n");
            return -1;
        }
    }

    return -ENOENT;
}

static int sfs_fuse_read(path, buf, size, offset, fi)
    const char *path;
    char *buf;
    size_t size;
    off_t offset;
    struct fuse_file_info *fi;
{
    printf("### sfs_fuse_read: '%s', size: 0x%lx, offset: 0x%lx\n", path, size, offset);

    int sz = sfs_read(sfs, fix_path(path), buf, size, offset);
    if (sz >= 0) {
        return sz;
    } else {
        return -ENOENT;
    }
}

static int sfs_fuse_readdir(path, buf, filler, offset, fi)
    const char *path;
    void *buf;
    fuse_fill_dir_t filler;
    off_t offset;
    struct fuse_file_info *fi;
{
    printf("### sfs_fuse_readdir: '%s', offset:%lx\n", path, offset);
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    const char *fxpath = fix_path(path);
    char *name = (char*)sfs_first(sfs, fxpath);
    while (name != NULL) {
        printf("\tadding: '%s'\n", name);
        if (filler(buf, name, NULL, 0, 0) == 1) {
            printf("buffer full\n");
        }
        name = (char*)sfs_next(sfs, fxpath);
    }
    return 0;
}

static int sfs_fuse_mkdir(const char *path, mode_t mode)
{
    printf("### sfs_fuse_mkdir \"%s\"\n", path);
    int result = sfs_mkdir(sfs, fix_path(path));
    if (result == 0) {
        return 0;
    } else {
        return -EACCES;
    }
}

static int sfs_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    printf("### sfs_fuse_create \"%s\"\n", path);
    int result = sfs_create(sfs, fix_path(path));
    if (result == 0) {
        return 0;
    } else {
        return -EACCES;
    }
}

static int sfs_fuse_rmdir(const char *path)
{
    printf("### sfs_fuse_rmdir \"%s\"\n", path);
    int result = sfs_rmdir(sfs, fix_path(path));
    if (result == 0) {
        return 0;
    } else {
        return -EACCES;
    }
}

static int sfs_fuse_unlink(const char *path)
{
    printf("### sfs_fuse_unlink \"%s\"\n", path);
    int result = sfs_delete(sfs, fix_path(path));
    if (result == 0) {
        return 0;
    } else {
        return -EACCES;
    }
}

static int sfs_fuse_utimens(path, tv, fi)
    const char *path;
    const struct timespec tv[2];
    struct fuse_file_info *fi;
{
    printf("### sfs_fuse_utimens \"%s\"\n", path);
    struct timespec timespec = tv[1];
    if (timespec.tv_nsec == UTIME_NOW) {    // current time
        printf("\tset now\n");
        clock_gettime(CLOCK_REALTIME, &timespec);
    } else if (timespec.tv_nsec == UTIME_OMIT) {  // no change
        printf("\tomit\n");
        return 0;
    }
    printf("\ttv_sec=0x%08lx\n", timespec.tv_sec);
    printf("\ttv_nsec=0x%08lx\n", timespec.tv_nsec);
    int result = sfs_set_time(sfs, fix_path(path), &timespec);
    if (result == 0) {
        return 0;
    } else {
        return -EACCES;
    }
}

static int sfs_fuse_rename(oldpath, newpath, flags)
    const char *oldpath;
    const char *newpath;
    unsigned int flags;
{
    printf("### sfs_fuse_rename \"%s\"->\"%s\"\n", oldpath, newpath);
    if (flags & RENAME_EXCHANGE) {
        fprintf(stderr, "rename exchange not implemented\n");
        return -EACCES;
    } else { 
        int replace = ((flags & RENAME_NOREPLACE) == 0);
        printf("\treplace=%d\n", replace);
        int result = sfs_rename(sfs, fix_path(oldpath), fix_path(newpath), replace);
        if (result == 0) {
            return 0;
        } else {
            return -EACCES;
        }
    }
}

//       ssize_t write(int fd, const void *buf, size_t count);

static int sfs_fuse_write(path, buf, size, offset, fi)
    const char *path;
    const char *buf;
    size_t size;
    off_t offset;
    struct fuse_file_info *fi;
{
    printf("### sfs_fuse_write: '%s', size: 0x%lx, offset: 0x%lx\n", path, size, offset);

    const char *fxpath = fix_path(path);
    uint64_t file_size = sfs_get_file_size(sfs, fxpath);
    uint64_t min_size = offset + size;
    if (min_size > file_size) {    // if writing beyond the end of file, resize first
        if (sfs_resize(sfs, fxpath, min_size) != 0) {
            return -1;
        }
    }

    int res = sfs_write(sfs, fxpath, buf, size, offset);
    if (res >= 0) {
        return res;
    } else {
        return -ENOENT;
    }
}

static int sfs_fuse_truncate(path, length, fi)
    const char *path;
    off_t length;
    struct fuse_file_info *fi;
{
    printf("### sfs_fuse_truncate: '%s', offset: 0x%lx\n", path, length);

    int res = sfs_resize(sfs, fix_path(path), length);
    if (res == 0) {
        return length;
    } else {
        return -ENOENT;
    }
}

static struct fuse_operations fuse_operations = {
    .init = sfs_fuse_init,
    .destroy = sfs_fuse_destroy,
    .getattr = sfs_fuse_getattr,
    .read = sfs_fuse_read,
    .readdir = sfs_fuse_readdir,
    .mkdir = sfs_fuse_mkdir,
    .create = sfs_fuse_create,
    .rmdir = sfs_fuse_rmdir,
    .unlink = sfs_fuse_unlink,
    .utimens = sfs_fuse_utimens,
    .rename = sfs_fuse_rename,
    .write = sfs_fuse_write,
    .truncate = sfs_fuse_truncate
};

static void show_help(const char *progname)
{
    printf("usage: %s [options] <mountpoint>\n\n", progname);
    printf("File-system specific options:\n"
        "    --name=<s>          Name of the \"hello\" file\n"
        "                        (default: \"hello\")\n"
        "\n");
}

int main(int argc, char **argv)
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    options.filename = NULL;

    if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
        return 1;
    if (access(options.filename, R_OK) != 0) {
        fprintf(stderr, "%s is not readable\n", options.filename);
        fuse_opt_free_args(&args);
        return 2;
    }
    options.absolute_filename = realpath(options.filename, NULL);
    int ret;
    if (options.show_help || options.filename == NULL) {
        show_help(argv[0]);
        ret = 0;
    } else {
        ret = fuse_main(args.argc, args.argv, &fuse_operations, NULL);
    }
    fuse_opt_free_args(&args);
    return ret;
}
