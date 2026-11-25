#include "fs.h"

#include <stdint.h>

#include "kmalloc.h"
#include "kprintf.h"
#include "libc.h"
#include "multiboot.h"
#include "screen.h"
#include "spinlock.h"

static char cwd[FS_PATH_MAX] = "/";

static struct fs_node* root = 0;

/* Guards the tree, cwd and the dirty flag. */
static struct spinlock tree_lock;

/* Set when the tree changed since it was last saved to the disk. */
static int dirty = 0;

/* A name the tree accepts: not empty, not too long, not "." or "..". */
static int name_ok(const char* name) {
    if (name[0] == '\0' || strlen(name) >= FS_NAME_MAX) {
        return 0;
    }

    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static struct fs_node* new_node(const char* name, enum fs_kind kind) {
    struct fs_node* node = (struct fs_node*)kmalloc(sizeof(struct fs_node));

    if (node == 0) {
        print("Error: out of memory.\n");
        return 0;
    }

    memset(node, 0, sizeof(*node));
    memcpy(node->name, name, strlen(name) + 1);
    node->kind = kind;

    return node;
}

static struct fs_node* find_child(struct fs_node* dir, const char* name) {
    for (struct fs_node* node = dir->children; node != 0;
         node = node->sibling) {
        if (strcmp(node->name, name) == 0) {
            return node;
        }
    }

    return 0;
}

/* Hang a node on its parent, keeping the siblings sorted by name. */
static void attach_child(struct fs_node* dir, struct fs_node* node) {
    struct fs_node** slot = &dir->children;

    while (*slot != 0 && strcmp((*slot)->name, node->name) < 0) {
        slot = &(*slot)->sibling;
    }

    node->sibling = *slot;
    node->parent = dir;
    *slot = node;
}

/* Take a node out of its parent's child list. */
static void detach_child(struct fs_node* node) {
    struct fs_node** slot;

    if (node->parent == 0) {
        return;
    }

    slot = &node->parent->children;
    while (*slot != 0 && *slot != node) {
        slot = &(*slot)->sibling;
    }

    if (*slot == node) {
        *slot = node->sibling;
    }

    node->parent = 0;
}

/* Free a node, its content and everything under it. */
static void free_tree(struct fs_node* node) {
    struct fs_node* child = node->children;

    while (child != 0) {
        struct fs_node* next = child->sibling;
        free_tree(child);
        child = next;
    }

    kfree(node->data);
    kfree(node);
}

/* Write the absolute form of "path" into out; 0 if it does not fit. */
static int make_absolute(const char* path, char* out) {
    uint32_t len = (uint32_t)strlen(path);
    uint32_t i = 0;

    if (path[0] != '/') {
        uint32_t here = (uint32_t)strlen(cwd);

        if (here + len + 2 > FS_PATH_MAX) {
            return 0;
        }

        memcpy(out, cwd, here);
        i = here;

        if (out[i - 1] != '/') {
            out[i++] = '/';
        }
    } else if (len + 1 > FS_PATH_MAX) {
        return 0;
    }

    memcpy(out + i, path, len);
    i += len;
    out[i] = '\0';

    while (i > 1 && out[i - 1] == '/') {
        out[--i] = '\0';
    }

    return 1;
}

/* Walk an absolute path from the root; 0 if a component is missing. */
static struct fs_node* walk(const char* abs, char* last, int stop_at_parent) {
    struct fs_node* node = root;
    const char* p = abs;

    if (last != 0) {
        last[0] = '\0';
    }

    if (root == 0) {
        return 0;
    }

    for (;;) {
        const char* start;
        int len;
        struct fs_node* child;

        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        start = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }
        len = (int)(p - start);

        if (len >= FS_NAME_MAX) {
            print("Error: that path has a name that is too long.\n");
            return 0;
        }

        {
            const char* q = p;
            char name[FS_NAME_MAX];

            while (*q == '/') {
                q++;
            }

            memcpy(name, start, (size_t)len);
            name[len] = '\0';

            if (*q == '\0' && stop_at_parent) {
                if (node->kind != FS_DIR) {
                    return 0;
                }
                if (last != 0) {
                    memcpy(last, name, (size_t)len + 1);
                }
                return node;
            }

            if (strcmp(name, ".") == 0) {
                continue;
            }
            if (strcmp(name, "..") == 0) {
                if (node->parent != 0) {
                    node = node->parent;
                }
                continue;
            }

            if (node->kind != FS_DIR) {
                return 0;
            }

            child = find_child(node, name);
            if (child == 0) {
                return 0;
            }
            node = child;
        }
    }

    return stop_at_parent ? 0 : node;
}

/* Rebuild the absolute path of a node by walking its parents. */
static int node_path(struct fs_node* node, char* out) {
    char buf[FS_PATH_MAX];
    uint32_t pos = FS_PATH_MAX;

    buf[--pos] = '\0';

    while (node != 0 && node->parent != 0) {
        uint32_t n = (uint32_t)strlen(node->name);

        if (pos < n + 1) {
            return 0;
        }

        while (n > 0) {
            buf[--pos] = node->name[--n];
        }
        buf[--pos] = '/';

        node = node->parent;
    }

    if (pos == FS_PATH_MAX - 1) {
        buf[--pos] = '/';
    }

    memcpy(out, buf + pos, strlen(buf + pos) + 1);
    return 1;
}

/* Is "node" the same as "ancestor" or somewhere under it? */
static int within(struct fs_node* node, struct fs_node* ancestor) {
    while (node != 0) {
        if (node == ancestor) {
            return 1;
        }
        node = node->parent;
    }

    return 0;
}

/* Build the empty tree with just "/" in it. */
void fs_init(void) {
    spinlock_acquire(&tree_lock);

    root = new_node("/", FS_DIR);
    memcpy(cwd, "/", 2);
    dirty = 0;

    spinlock_release(&tree_lock);
}

/* Throw the tree away, leaving just "/", and go back to the root. */
void fs_reset(void) {
    struct fs_node* child;

    spinlock_acquire(&tree_lock);

    if (root == 0) {
        spinlock_release(&tree_lock);
        return;
    }

    child = root->children;
    while (child != 0) {
        struct fs_node* next = child->sibling;

        free_tree(child);
        child = next;
    }

    root->children = 0;
    memcpy(cwd, "/", 2);
    dirty = 0;

    spinlock_release(&tree_lock);
}

/* Did anything change since the tree was last saved to the disk? */
int fs_dirty(void) {
    int value;

    spinlock_acquire(&tree_lock);
    value = dirty;
    spinlock_release(&tree_lock);

    return value;
}

/* Record that the tree in memory matches the disk again. */
void fs_mark_clean(void) {
    spinlock_acquire(&tree_lock);
    dirty = 0;
    spinlock_release(&tree_lock);
}

/* The tree's root; it must be used before the tree changes again. */
struct fs_node* fs_root(void) { return root; }

/* The absolute path of a node; 0 if it does not fit. */
int fs_node_path(struct fs_node* node, char* out) {
    int ok;

    spinlock_acquire(&tree_lock);
    ok = node_path(node, out);
    spinlock_release(&tree_lock);

    return ok;
}

/* The body of fs_resolve(), which runs with the tree lock held. */
static struct fs_node* resolve_tree(const char* path) {
    char abs[FS_PATH_MAX];
    struct fs_node* node;

    if (!make_absolute(path, abs)) {
        print("Error: that path is too long.\n");
        return 0;
    }

    node = walk(abs, 0, 0);
    if (node == 0) {
        print("Error: no such file or directory.\n");
    }

    return node;
}

/* The node a path names, or 0; the error is printed here. */
struct fs_node* fs_resolve(const char* path) {
    struct fs_node* node;

    spinlock_acquire(&tree_lock);
    node = resolve_tree(path);
    spinlock_release(&tree_lock);

    return node;
}

/* The node a path names, or 0; silent about why it failed. */
struct fs_node* fs_lookup(const char* path) {
    char abs[FS_PATH_MAX];
    struct fs_node* node;

    spinlock_acquire(&tree_lock);

    if (!make_absolute(path, abs)) {
        spinlock_release(&tree_lock);
        return 0;
    }

    node = walk(abs, 0, 0);

    spinlock_release(&tree_lock);

    return node;
}

/* Would fs_write() accept this path? */
int fs_can_write(const char* path) {
    char abs[FS_PATH_MAX];
    char name[FS_NAME_MAX];
    struct fs_node* existing;
    struct fs_node* parent;
    int ok = 0;

    spinlock_acquire(&tree_lock);

    if (!make_absolute(path, abs)) {
        print("Error: that path is too long.\n");
        spinlock_release(&tree_lock);
        return 0;
    }

    existing = walk(abs, 0, 0);
    if (existing != 0) {
        if (existing->kind == FS_DIR) {
            print("Error: that path is a directory.\n");
        } else {
            ok = 1;
        }

        spinlock_release(&tree_lock);
        return ok;
    }

    parent = walk(abs, name, 1);
    if (parent == 0) {
        print("Error: no such directory.\n");
    } else if (!name_ok(name)) {
        print("Error: that is not a usable name.\n");
    } else {
        ok = 1;
    }

    spinlock_release(&tree_lock);

    return ok;
}

/* The body of fs_mkdir(), which runs with the tree lock held. */
static struct fs_node* mkdir_tree(const char* path) {
    char abs[FS_PATH_MAX];
    char name[FS_NAME_MAX];
    struct fs_node* existing;
    struct fs_node* parent;
    struct fs_node* node;

    if (!make_absolute(path, abs)) {
        print("Error: that path is too long.\n");
        return 0;
    }

    existing = walk(abs, 0, 0);
    if (existing != 0) {
        if (existing->kind == FS_DIR) {
            return existing;
        }

        print("Error: a file with that name already exists.\n");
        return 0;
    }

    parent = walk(abs, name, 1);
    if (parent == 0) {
        print("Error: no such directory.\n");
        return 0;
    }
    if (!name_ok(name)) {
        print("Error: that is not a usable name.\n");
        return 0;
    }

    node = new_node(name, FS_DIR);
    if (node != 0) {
        attach_child(parent, node);
        dirty = 1;
    }

    return node;
}

/* Make a directory; an existing one is returned unchanged. */
struct fs_node* fs_mkdir(const char* path) {
    struct fs_node* node;

    spinlock_acquire(&tree_lock);
    node = mkdir_tree(path);
    spinlock_release(&tree_lock);

    return node;
}

/* Point a file node at new content, replacing whatever was there. */
static int fill_file(struct fs_node* node, const char* data, uint32_t len) {
    char* buf = (char*)kmalloc(len + 1);

    if (buf == 0) {
        print("Error: out of memory.\n");
        return 0;
    }

    memcpy(buf, data, len);
    buf[len] = '\0';

    kfree(node->data);
    node->data = buf;
    node->size = len;

    return 1;
}

/* The body of fs_write(), which runs with the tree lock held. */
static struct fs_node* write_tree(const char* path, const char* data,
                                  uint32_t len) {
    char abs[FS_PATH_MAX];
    char name[FS_NAME_MAX];
    struct fs_node* existing;
    struct fs_node* parent;
    struct fs_node* node;

    if (!make_absolute(path, abs)) {
        print("Error: that path is too long.\n");
        return 0;
    }

    existing = walk(abs, 0, 0);
    if (existing != 0) {
        if (existing->kind == FS_DIR) {
            print("Error: that path is a directory.\n");
            return 0;
        }

        if (!fill_file(existing, data, len)) {
            return 0;
        }

        dirty = 1;
        return existing;
    }

    parent = walk(abs, name, 1);
    if (parent == 0) {
        print("Error: no such directory.\n");
        return 0;
    }
    if (!name_ok(name)) {
        print("Error: that is not a usable name.\n");
        return 0;
    }

    node = new_node(name, FS_FILE);
    if (node == 0) {
        return 0;
    }

    if (!fill_file(node, data, len)) {
        kfree(node);
        return 0;
    }

    attach_child(parent, node);
    dirty = 1;

    return node;
}

/* Create a file, or replace the contents of an existing one. */
struct fs_node* fs_write(const char* path, const char* data, uint32_t len) {
    struct fs_node* node;

    spinlock_acquire(&tree_lock);
    node = write_tree(path, data, len);
    spinlock_release(&tree_lock);

    return node;
}

/* Remove a file, or a directory and everything under it. */
int fs_remove(const char* path) {
    char abs[FS_PATH_MAX];
    struct fs_node* node;

    spinlock_acquire(&tree_lock);

    if (!make_absolute(path, abs)) {
        print("Error: that path is too long.\n");
        spinlock_release(&tree_lock);
        return 0;
    }

    node = walk(abs, 0, 0);
    if (node == 0) {
        print("Error: no such file or directory.\n");
        spinlock_release(&tree_lock);
        return 0;
    }
    if (node == root) {
        print("Error: the root directory cannot be removed.\n");
        spinlock_release(&tree_lock);
        return 0;
    }

    detach_child(node);
    free_tree(node);
    dirty = 1;

    // The current directory may be gone with the subtree that held it.
    if (walk(cwd, 0, 0) == 0) {
        memcpy(cwd, "/", 2);
    }

    spinlock_release(&tree_lock);

    return 1;
}

/* The body of fs_move(), which runs with the tree lock held. */
static int move_tree(const char* from, const char* to) {
    char src_abs[FS_PATH_MAX];
    char dst_abs[FS_PATH_MAX];
    char path[FS_PATH_MAX];
    char name[FS_NAME_MAX];
    const char* suffix = 0;
    struct fs_node* src;
    struct fs_node* dst;
    struct fs_node* parent;
    struct fs_node* target;

    if (!make_absolute(from, src_abs)) {
        print("Error: that path is too long.\n");
        return 0;
    }
    if (!make_absolute(to, dst_abs)) {
        print("Error: that path is too long.\n");
        return 0;
    }

    src = walk(src_abs, 0, 0);
    if (src == 0) {
        print("Error: no such file or directory.\n");
        return 0;
    }
    if (src == root) {
        print("Error: the root directory cannot be moved.\n");
        return 0;
    }

    dst = walk(dst_abs, 0, 0);

    if (src == dst) {
        return 1;
    }

    if (dst != 0 && dst->kind == FS_DIR) {
        parent = dst;
        memcpy(name, src->name, strlen(src->name) + 1);
    } else {
        parent = walk(dst_abs, name, 1);
        if (parent == 0) {
            print("Error: no such directory.\n");
            return 0;
        }
        if (!name_ok(name)) {
            print("Error: that is not a usable name.\n");
            return 0;
        }
    }

    if (src->kind == FS_DIR && within(parent, src)) {
        print("Error: a directory cannot be moved into itself.\n");
        return 0;
    }

    target = find_child(parent, name);

    if (target == src) {
        return 1;
    }

    if (target != 0) {
        if (target->kind == FS_DIR) {
            print("Error: that path is a directory.\n");
            return 0;
        }
        if (src->kind == FS_DIR) {
            print("Error: a file with that name already exists.\n");
            return 0;
        }

        detach_child(target);
        free_tree(target);
    }

    if (node_path(src, path)) {
        uint32_t len = (uint32_t)strlen(path);

        if (strncmp(cwd, path, len) == 0 &&
            (cwd[len] == '\0' || cwd[len] == '/')) {
            suffix = cwd + len;
        }
    }

    detach_child(src);
    memcpy(src->name, name, strlen(name) + 1);
    attach_child(parent, src);
    dirty = 1;

    if (suffix != 0 && node_path(src, path) &&
        strlen(path) + strlen(suffix) + 1 <= FS_PATH_MAX) {
        strcat(path, suffix);
        strcpy(cwd, path);
    }

    if (walk(cwd, 0, 0) == 0) {
        memcpy(cwd, "/", 2);
    }

    return 1;
}

/* Move or rename a file or directory; 1 on success. */
int fs_move(const char* from, const char* to) {
    int ok;

    spinlock_acquire(&tree_lock);
    ok = move_tree(from, to);
    spinlock_release(&tree_lock);

    return ok;
}

/* Copy a node and everything under it into "parent" as "name". */
static struct fs_node* copy_node(struct fs_node* src, struct fs_node* parent,
                                 const char* name) {
    struct fs_node* node = new_node(name, src->kind);

    if (node == 0) {
        return 0;
    }

    if (src->kind == FS_FILE) {
        if (!fill_file(node, src->data, src->size)) {
            kfree(node);
            return 0;
        }
    }

    for (struct fs_node* child = src->children; child != 0;
         child = child->sibling) {
        if (copy_node(child, node, child->name) == 0) {
            free_tree(node);
            return 0;
        }
    }

    attach_child(parent, node);

    return node;
}

/* The body of fs_copy(), which runs with the tree lock held. */
static int copy_tree(const char* from, const char* to) {
    char src_abs[FS_PATH_MAX];
    char dst_abs[FS_PATH_MAX];
    char name[FS_NAME_MAX];
    struct fs_node* src;
    struct fs_node* dst;
    struct fs_node* parent;
    struct fs_node* target;

    if (!make_absolute(from, src_abs)) {
        print("Error: that path is too long.\n");
        return 0;
    }
    if (!make_absolute(to, dst_abs)) {
        print("Error: that path is too long.\n");
        return 0;
    }

    src = walk(src_abs, 0, 0);
    if (src == 0) {
        print("Error: no such file or directory.\n");
        return 0;
    }

    dst = walk(dst_abs, 0, 0);

    if (dst != 0 && dst->kind == FS_DIR) {
        parent = dst;
        memcpy(name, src->name, strlen(src->name) + 1);
    } else {
        parent = walk(dst_abs, name, 1);
        if (parent == 0) {
            print("Error: no such directory.\n");
            return 0;
        }
        if (!name_ok(name)) {
            print("Error: that is not a usable name.\n");
            return 0;
        }
    }

    if (src->kind == FS_DIR && within(parent, src)) {
        print("Error: a directory cannot be copied into itself.\n");
        return 0;
    }

    target = find_child(parent, name);

    if (target == src) {
        return 1;
    }

    if (target != 0) {
        if (target->kind == FS_DIR) {
            print("Error: that path is a directory.\n");
            return 0;
        }
        if (src->kind == FS_DIR) {
            print("Error: a file with that name already exists.\n");
            return 0;
        }

        if (!fill_file(target, src->data, src->size)) {
            return 0;
        }

        dirty = 1;
        return 1;
    }

    if (copy_node(src, parent, name) == 0) {
        return 0;
    }

    dirty = 1;
    return 1;
}

/* Copy a file or directory, with everything under it; 1 on success. */
int fs_copy(const char* from, const char* to) {
    int ok;

    spinlock_acquire(&tree_lock);
    ok = copy_tree(from, to);
    spinlock_release(&tree_lock);

    return ok;
}

/* The current directory, valid until something changes it. */
const char* fs_cwd(void) { return cwd; }

/* Move the current directory; 1 on success. */
int fs_chdir(const char* path) {
    struct fs_node* node;
    int ok = 0;

    spinlock_acquire(&tree_lock);

    node = resolve_tree(path);

    if (node != 0) {
        if (node->kind != FS_DIR) {
            print("Error: that path is not a directory.\n");
        } else if (!node_path(node, cwd)) {
            print("Error: that path is too long.\n");
        } else {
            ok = 1;
        }
    }

    spinlock_release(&tree_lock);

    return ok;
}

#define TAR_BLOCK 512            // bytes per archive block
#define TAR_CHECKSUM_OFFSET 148  // where the header's checksum sits

// The 512-byte tar header; every field is fixed width and text, sizes and
// times in octal. Only name, prefix, size and typeflag are used here.
struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;  // '0' or '\0' file, '5' directory
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));

/* Read a fixed-width header field as a NUL-terminated string. */
static void tar_field(char* out, uint32_t out_size, const char* field,
                      uint32_t field_size) {
    uint32_t n = 0;

    while (n < field_size && field[n] != '\0') {
        n++;
    }
    if (n >= out_size) {
        n = out_size - 1;
    }

    memcpy(out, field, n);
    out[n] = '\0';
}

/* Read a field of octal digits (file sizes are written that way). */
static int tar_octal(const char* field, uint32_t size, uint32_t* out) {
    uint32_t value = 0;
    uint32_t digits = 0;

    for (uint32_t i = 0; i < size; i++) {
        char c = field[i];

        if (c == '\0') {
            break;
        }
        if (c == ' ') {
            if (digits > 0) {
                break;
            }
            continue;
        }
        if (c < '0' || c > '7') {
            return 0;
        }

        value = (value << 3) + (uint32_t)(c - '0');
        digits++;
    }

    if (digits == 0) {
        return 0;
    }

    *out = value;
    return 1;
}

/* Check the header's own checksum; a corrupt archive stops here. */
static int tar_header_ok(const struct tar_header* header) {
    const unsigned char* raw = (const unsigned char*)header;
    uint32_t stored = 0;
    uint32_t sum = 0;

    if (!tar_octal(header->checksum, sizeof(header->checksum), &stored)) {
        return 0;
    }

    for (uint32_t i = 0; i < TAR_BLOCK; i++) {
        if (i >= TAR_CHECKSUM_OFFSET && i < TAR_CHECKSUM_OFFSET + 8) {
            sum += ' ';
        } else {
            sum += raw[i];
        }
    }

    return sum == stored;
}

/* The path an archive entry belongs at, always starting from "/". */
static int entry_path(const struct tar_header* header, char* out) {
    char name[101];
    char prefix[156];
    uint32_t len = 0;
    uint32_t n;

    tar_field(name, sizeof(name), header->name, sizeof(header->name));
    tar_field(prefix, sizeof(prefix), header->prefix, sizeof(header->prefix));

    out[len++] = '/';

    if (prefix[0] != '\0') {
        n = (uint32_t)strlen(prefix);
        if (len + n + 2 > FS_PATH_MAX) {
            return 0;
        }
        memcpy(out + len, prefix, n);
        len += n;
        out[len++] = '/';
    }

    n = (uint32_t)strlen(name);
    if (len + n + 1 > FS_PATH_MAX) {
        return 0;
    }
    memcpy(out + len, name, n);
    len += n;
    out[len] = '\0';

    return 1;
}

/* An archive may not step out of the tree it fills: an entry named
 * "../../x" would otherwise land at "/x", outside where it belongs. */
static int entry_name_safe(const char* path) {
    const char* p = path;

    while (*p != '\0') {
        const char* start;
        int len;

        while (*p == '/') {
            p++;
        }

        start = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }

        len = (int)(p - start);
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            return 0;
        }
    }

    return 1;
}

/* Is this block all zeroes, i.e. the end of the archive? */
static int tar_is_empty(const unsigned char* block) {
    for (uint32_t i = 0; i < TAR_BLOCK; i++) {
        if (block[i] != 0) {
            return 0;
        }
    }

    return 1;
}

/* Unpack one archive. Returns how many files it added. */
static int load_tar(uint32_t start, uint32_t end) {
    const unsigned char* data = (const unsigned char*)start;
    uint32_t total;
    uint32_t offset = 0;
    int files = 0;

    // An end that does not follow its start would wrap "total" and turn the
    // archive into a walk over memory the kernel was never given.
    if (end <= start) {
        return 0;
    }

    total = end - start;

    if (total < TAR_BLOCK) {
        return 0;
    }

    while (offset + TAR_BLOCK <= total) {
        const struct tar_header* header =
            (const struct tar_header*)(data + offset);
        uint32_t size = 0;
        char path[FS_PATH_MAX];

        if (tar_is_empty(data + offset)) {
            break;
        }
        if (!tar_header_ok(header)) {
            break;
        }
        if (!tar_octal(header->size, sizeof(header->size), &size)) {
            break;
        }

        offset += TAR_BLOCK;

        if (size > total - offset) {
            break;
        }

        if (!entry_path(header, path)) {
            kprintf("File System: initrd entry has too long a name\n");
        } else if (!entry_name_safe(path)) {
            kprintf(
                "File System: an initrd entry names a \"..\" path and was "
                "skipped\n");
        } else if (header->typeflag == '5') {
            mkdir_tree(path);
        } else if (header->typeflag == '0' || header->typeflag == '\0') {
            if (write_tree(path, (const char*)(data + offset), size) != 0) {
                files++;
            }
        }

        offset += ((size + TAR_BLOCK - 1) / TAR_BLOCK) * TAR_BLOCK;
    }

    return files;
}

/* Unpack the boot modules into the tree; returns files added. */
int fs_load_module(uint32_t magic, uint32_t info_addr) {
    struct multiboot_info* info;
    struct multiboot_module* mods;
    int files = 0;

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        return 0;
    }

    info = (struct multiboot_info*)info_addr;
    if ((info->flags & MULTIBOOT_INFO_MODS) == 0 || info->mods_count == 0) {
        return 0;
    }

    // The module list is bootloader data: a count or a range that cannot be
    // right must not be walked, because both would read memory the kernel was
    // never told about.
    if (info->mods_count > 64) {
        print("File System: the boot module list is not usable.\n");
        return 0;
    }

    mods = (struct multiboot_module*)info->mods_addr;

    spinlock_acquire(&tree_lock);

    for (uint32_t i = 0; i < info->mods_count; i++) {
        if (mods[i].mod_end <= mods[i].mod_start) {
            continue;
        }
        files += load_tar(mods[i].mod_start, mods[i].mod_end);
    }

    spinlock_release(&tree_lock);

    return files;
}
