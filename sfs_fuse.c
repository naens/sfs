#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

static int sfs_getattr(const char *path, struct stat *stbuf)
{
    printf("sfs_getattr: '%s'\n", path);

    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0744;
        stbuf->st_nlink = 2;
        return 0;
    }

    char *filepath = "/file";
    if (strcmp(path, filepath) == 0) {
        stbuf->st_mode = S_IFREG | 0755;
        stbuf->st_nlink = 1;
        char *filecontent = "asdflkj\n";
        stbuf->st_size = strlen(filecontent);
        return 0;
    }

    return -ENOENT;
}

static int sfs_read(path, buf, size, offset, fi)
    const char *path;
    char *buf;
    size_t size;
    off_t offset;
    struct fuse_file_info *fi;
{
    printf("sfs_read: '%s', size:%ld, offset:%lx\n", path, size, offset);
    char *filepath = "/file";
    char *filecontent = "asdflkj\n";
    if (strcmp(path, filepath) == 0) {
        size_t len = strlen(filecontent);
        if (offset >= strlen(filecontent))
            return 0;

        if (offset + size > len) {
            memcpy(buf, filecontent + offset, len - offset);
            return len - offset;
        }

        memcpy(buf, filecontent + offset, size);
        return size;
    }
    return -ENOENT;
}

static int sfs_readdir(path, buf, filler, offset, fi)
    const char *path;
    void *buf;
    fuse_fill_dir_t filler;
    off_t offset;
    struct fuse_file_info *fi;
{
    printf("sfs_readdir: '%s', offset:%lx\n", path, offset);
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    char *filename = "file";
    filler(buf, filename, NULL, 0);
    return 0;
}

static struct fuse_operations fuse_operations = {
    .getattr = sfs_getattr,
    .read = sfs_read,
    .readdir = sfs_readdir
};

int main(int argc, char **argv)
{
    return fuse_main(argc, argv, &fuse_operations, NULL);
}
