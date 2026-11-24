#ifndef ADAOS_SETUP_H
#define ADAOS_SETUP_H

// First-boot setup: a small full-screen program that runs before the shell when
// the disk holds no file system yet. It offers to format the disk and to
// install the sample files, drawing a progress bar for the slow steps, and
// reboots into the new system when it finishes.

/* Run the setup when the disk holds no file system yet, and return at once
 * otherwise. Call after fs_load_module() and before diskfs_init(). */
void setup_run_if_needed(void);

#endif /* ADAOS_SETUP_H */
