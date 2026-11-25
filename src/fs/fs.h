#ifndef ADAOS_FS_H
#define ADAOS_FS_H

#include <stdint.h>

// The RAM file system: a tree of nodes in the kernel heap, rooted at "/", with
// files held as byte buffers and directories as lists of children sorted by
// name. Paths are resolved relative to a current directory using "." and "..".
// Every call that walks or changes the tree holds a lock, because an interrupt
// handler or another task can reach it too.

/* Longest name one file or directory may have. */
#define FS_NAME_MAX 28  // including the ending '\0'
/* Longest absolute path, current directory included. This matches the 112
 * bytes a disk entry reserves for a path, so anything the tree accepts can also
 * be written out; a longer one would be created happily in RAM and then be
 * impossible to save. */
#define FS_PATH_MAX 112  // including the ending '\0'

/* What a node in the tree holds. */
enum fs_kind {
    FS_FILE,  // a file: "data" holds its bytes
    FS_DIR,   // a directory: "children" holds its entries
};

/* One file or directory; children chain through sibling, sorted by name. */
struct fs_node {
    char name[FS_NAME_MAX];
    enum fs_kind kind;
    uint32_t size;  // bytes of data
    char* data;     // file contents, heap owned
    struct fs_node* parent;
    struct fs_node* children;  // first child of a directory
    struct fs_node* sibling;   // next entry in the parent's list
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
