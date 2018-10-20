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
    printf("###sfs_fuse_init: fn=\"%s\"\n", options.absolute_filename);
    sfs = sfs_init(options.absolute_filename);
    cfg->kernel_cache = 1;
    return NULL;
}

static void sfs_fuse_destroy(void *private_data)
{
    printf("###sfs_fuse_destroy\n");
    sfs_terminate(sfs);
    sfs = NULL;
}

static int sfs_fuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info* fi)
{
    printf("###sfs_fuse_getattr: '%s'\n", path);

    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    if (sfs_is_dir(sfs, path)) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    if (sfs_is_file(sfs, path)) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = sfs_get_file_size(sfs, path);
        printf("\t=> size = 0x%lx\n", stbuf->st_size);
        return 0;
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
    printf("###sfs_fuse_read: '%s', size: 0x%lx, offset: 0x%lx\n", path, size, offset);

    int sz = sfs_read(sfs, path, buf, size, offset);
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
    printf("###sfs_fuse_readdir: '%s', offset:%lx\n", path, offset);
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    char *name = sfs_first(sfs, path);
    while (name != NULL) {
        printf("\tadding: '%s'\n", name);
        if (filler(buf, name, NULL, 0, 0) == 1) {
            printf("buffer full\n");
        }
        name = sfs_next(sfs, path);
    }
    return 0;
}

static int sfs_fuse_mkdir(const char *path, mode_t mode)
{
    printf("###sfs_fuse_mkdir \"%s\"\n", path);
    int result = sfs_mkdir(sfs, path);
    if (result == 0) {
        return 0;
    } else {
        return -EACCES;
    }
}

static int sfs_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    printf("###sfs_fuse_create \"%s\"\n", path);
    int result = sfs_create(sfs, path);
    if (result == 0) {
        return 0;
    } else {
        return -EACCES;
    }
}

static struct fuse_operations fuse_operations = {
    .init = sfs_fuse_init,
    .destroy = sfs_fuse_destroy,
    .getattr = sfs_fuse_getattr,
    .read = sfs_fuse_read,
    .readdir = sfs_fuse_readdir,
    .mkdir = sfs_fuse_mkdir,
    .create = sfs_fuse_create
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
