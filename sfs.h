#include <stdint.h>
#include <stdio.h>
#include <time.h>

struct sfs;
typedef struct sfs SFS;

SFS *sfs_init(const char *filename);

int sfs_terminate(SFS *sfs);

uint64_t sfs_get_file_size(SFS *sfs, const char *path);

int sfs_is_dir(SFS *sfs, const char *path);

int sfs_is_file(SFS *sfs, const char *path);

const char *sfs_first(SFS *sfs, const char *path);

const char *sfs_next(SFS *sfs, const char *path);

int sfs_read(SFS *sfs, const char *path, char *buf, size_t size, off_t offset);

int sfs_mkdir(SFS *sfs, const char *path);

int sfs_create(SFS *sfs, const char *path);

int sfs_rmdir(SFS *sfs, const char *path);

int sfs_delete(SFS *sfs, const char *path);

int sfs_get_sfs_time(SFS *sfs, struct timespec *timespec);

int sfs_get_dir_time(SFS *sfs, const char *path, struct timespec *timespec);

int sfs_get_file_time(SFS *sfs, const char *path, struct timespec *timespec);

int sfs_set_time(SFS *sfs, const char *path, struct timespec *timespec);

int sfs_rename(SFS *sfs, const char *path, const char *path_to, int replace);

int sfs_write(SFS *sfs, const char *path, const char *buf, size_t size, off_t offset);

int sfs_resize(SFS *sfs, const char *path, off_t length);
