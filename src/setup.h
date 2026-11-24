#ifndef ADAOS_SETUP_H
#define ADAOS_SETUP_H

/* Offer the first-boot setup when the disk holds no file system yet: show
 * the welcome screen, ask what to prepare, write the disk, and reboot into
 * the new system. Does nothing and returns at once when the disk is already
 * set up. Puts the machine to a halt when the user declines a step that
 * cannot be skipped. Call after fs_load_module() and before diskfs_init(). */
void setup_run_if_needed(void);

#endif /* ADAOS_SETUP_H */
