#ifndef ADAOS_KEYBOARD_H
#define ADAOS_KEYBOARD_H

// The PS/2 keyboard driver: the controller leaves a scancode in port 0x60 and
// raises IRQ1, the handler translates make and break codes into characters and
// key values, and a ring buffer holds them until someone reads. Shift, Caps
// Lock and Ctrl are tracked; the arrow keys arrive as 0xE0-prefixed extended
// codes and are reported as the KEY_* values below.

// Keys with no ASCII character, reported above the byte range.
#define KEY_UP 0x100     // arrow up, from the 0xE0 prefix
#define KEY_DOWN 0x101   // arrow down
#define KEY_LEFT 0x102   // arrow left
#define KEY_RIGHT 0x103  // arrow right

#define KEY_ESC 0x104  // escape

#define KEY_CTRL_E 0x05  // Ctrl+E, echoed as the matching control code
#define KEY_CTRL_R 0x12  // Ctrl+R
#define KEY_CTRL_S 0x13  // Ctrl+S

/* Start receiving keystrokes on IRQ1; call once, after interrupt_init(). */
void keyboard_init(void);

/* Return the oldest key without waiting, or 0 if no key is ready. */
int keyboard_poll(void);

/* Block until a key is pressed and return it: an ASCII character, a KEY_CTRL_*
 * code or a KEY_* value. Other keys are dropped. */
int keyboard_getchar(void);

/* Was Ctrl+E pressed since the last keyboard_clear_ctrl_e()? */
int keyboard_ctrl_e_seen(void);

void keyboard_clear_ctrl_e(void);

#endif /* ADAOS_KEYBOARD_H */
