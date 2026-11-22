#ifndef ADAOS_KEYBOARD_H
#define ADAOS_KEYBOARD_H

/*
 * Block until the user presses a key, then return the ASCII character it
 * stands for ('\n' for Enter, '\b' for Backspace). Handles Shift and
 * Caps Lock. Unknown keys are simply ignored.
 */
char keyboard_getchar(void);

#endif /* ADAOS_KEYBOARD_H */
