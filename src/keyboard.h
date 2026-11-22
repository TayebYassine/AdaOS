#ifndef ADAOS_KEYBOARD_H
#define ADAOS_KEYBOARD_H

/*
 * Keyboard interface for the shell: the controller raises IRQ1 for
 * every scancode, keyboard.c translates it to ASCII (Shift and Caps
 * Lock included) and buffers it, and keyboard_getchar() hands the keys
 * to the caller one at a time (see keyboard.c).
 */

/* Arrow keys (returned for the 0xE0-prefixed scancodes). */
#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103

/* Start receiving keystrokes on IRQ1; call once, after interrupt_init(). */
void keyboard_init(void);

/* Block until a key is pressed, then return its ASCII character ('\n', '\b')
 * or a KEY_* value. Handles Shift and Caps Lock; other keys are ignored. */
int keyboard_getchar(void);

#endif /* ADAOS_KEYBOARD_H */
