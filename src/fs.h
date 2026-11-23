#ifndef ADAOS_FS_H
#define ADAOS_FS_H

#include <stdint.h>

/* Longest name one file or directory may have. */
#define FS_NAME_MAX 28
/* Longest absolute path, current directory included. */
#define FS_PATH_MAX 128

/* What a node in the tree holds. */
enum fs_kind {
    FS_FILE,
    FS_DIR,
};

/* One file or directory; children chain through sibling, sorted by name. */
struct fs_node {
    char name[FS_NAME_MAX];
    enum fs_kind kind;
    uint32_t size;
    char* data;
    struct fs_node* parent;
    struct fs_node* children;
    struct fs_node* sibling;
};

/* Build the empty tree with just "/" in it; call once after the heap. */
void fs_init(void);

/* Throw the tree away, leaving just "/", and go back to the root. */
void fs_reset(void);

/* Did anything change since the tree was last saved to the disk? */
int fs_dirty(void);

/* Record that the tree in memory matches the disk again. */
void fs_mark_clean(void);

/* Unpack the multiboot tar module into the tree; returns files added. */
int fs_load_module(uint32_t magic, uint32_t info_addr);

/* The tree's root. */
struct fs_node* fs_root(void);

/* The absolute path of a node; returns 0 if it does not fit. */
int fs_node_path(struct fs_node* node, char* out);

/* The node a path names, or 0; the error is printed here. */
struct fs_node* fs_resolve(const char* path);

/* The same lookup, but silent. */
struct fs_node* fs_lookup(const char* path);

/* Would fs_write() accept this path? */
int fs_can_write(const char* path);

/* Make a directory; an existing one is returned unchanged. */
struct fs_node* fs_mkdir(const char* path);

/* Create a file, or replace the contents of an existing one. */
struct fs_node* fs_write(const char* path, const char* data, uint32_t len);

/* Remove a file, or a directory and everything under it. */
int fs_remove(const char* path);

/* Move or rename a file or directory. Returns 1 on success. */
int fs_move(const char* from, const char* to);

/* Copy a file or directory, with everything under it. Returns 1 on success. */
int fs_copy(const char* from, const char* to);

/* The current directory. */
const char* fs_cwd(void);

/* Move the current directory. Returns 1 on success. */
int fs_chdir(const char* path);

#endif /* ADAOS_FS_H */
