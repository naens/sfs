#ifndef SFS_DIRSET_H
#define SFS_DIRSET_H

struct sfs_dirset_list {
    char *name;
    struct sfs_dirset_list *next;
};

struct sfs_dirset_list **dirset_init();
void dirset_put(struct sfs_dirset_list **sfs_dirset_list, char *name);
int dirset_get(struct sfs_dirset_list **sfs_dirset_list, char *name);
void dirset_del(struct sfs_dirset_list **sfs_dirset_list, char *name);
void dirset_free(struct sfs_dirset_list **sfs_dirset_list);

#endif /* SFS_DIRSET_H */
