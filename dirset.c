#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dirset.h"

#define SFS_DIRSET_SZ 0x10

struct sfs_dirset_list **dirset_init()
{
    return calloc(SFS_DIRSET_SZ, sizeof(struct sfs_dirset_list));
}

static int hash(char *name)
{
    int result = 0;
    while (*name) {
        result *= 19;
        result += *name;
        name++;
    }
    return result % SFS_DIRSET_SZ;
}

void dirset_put(struct sfs_dirset_list **sfs_dirset_list, char *name)
{
    int index = hash(name);
    struct sfs_dirset_list *dirset_list = malloc(sizeof(dirset_list));
    int len = strlen(name);
    dirset_list = malloc(len + 1);
    strcpy(dirset_list->name, name);
    dirset_list->next = sfs_dirset_list[index];
    sfs_dirset_list[index] = dirset_list;
}

int dirset_get(struct sfs_dirset_list **sfs_dirset_list, char *name)
{
    struct sfs_dirset_list *dirset_list = sfs_dirset_list[hash(name)];
    while (dirset_list != NULL) {
        if (strcmp(dirset_list->name, name) == 0)
            return 1;
        dirset_list = dirset_list->next;
    }
    return 0;
}

void dirset_del(struct sfs_dirset_list **sfs_dirset_list, char *name)
{
    struct sfs_dirset_list *tmp;
    struct sfs_dirset_list **p = &sfs_dirset_list[hash(name)];
    while (*p != NULL) {
        if (strcmp((*p)->name, name) == 0) {
            free((*p)->name);
            tmp = *p;
            *p = (*p)->next;
            free(tmp);
            break;
        }    
        p = &(*p)->next;
    }
}

void dirset_free(struct sfs_dirset_list **sfs_dirset_list)
{
    free (sfs_dirset_list);
}
