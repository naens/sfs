#include <stdint.h>
#include <stdio.h>

struct sfs;
typedef struct sfs SFS;

SFS *sfs_init(const char *filename);

int sfs_terminate(SFS *sfs);

uint64_t sfs_get_file_size(SFS *sfs, const char *path);

int sfs_is_dir(SFS *sfs, const char *path);

int sfs_is_file(SFS *sfs, const char *path);

char *sfs_first(SFS *sfs, const char *path);

char *sfs_next(SFS *sfs, const char *path);

int sfs_read(SFS *sfs, const char *path, char *buf, size_t size, off_t offset);

int sfs_mkdir(SFS *sfs, const char *path);

int sfs_create(SFS *sfs, const char *path);

int sfs_rmdir(SFS *sfs, const char *path);
