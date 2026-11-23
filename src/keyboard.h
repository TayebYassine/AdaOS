#ifndef ADAOS_KEYBOARD_H
#define ADAOS_KEYBOARD_H

// Arrow keys (returned for the 0xE0-prefixed scancodes).
#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103

#define KEY_CTRL_E 0x05
#define KEY_CTRL_S 0x13

/* Start receiving keystrokes on IRQ1; call once, after interrupt_init(). */
void keyboard_init(void);

/* Block until a key is pressed, then return its ASCII character ('\n', '\b'),
 * a control code (KEY_CTRL_*), or a KEY_* value. Handles Shift, Caps Lock
 * and Ctrl; other keys are ignored. */
int keyboard_getchar(void);

#endif /* ADAOS_KEYBOARD_H */
