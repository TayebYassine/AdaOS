#include "keyboard.h"

#include <stdint.h>

#include "io.h"

/*
 * The keyboard controller stores every key press/release as a "scancode"
 * (a small number) in I/O port 0x60. We read that port in a loop, translate
 * the scancode into an ASCII character, and hand it to the caller.
 *
 * Two kinds of scancodes exist:
 *   - "make" codes: the key was pressed
 *   - "break" codes: the key was released (same number with bit 7 set)
 */

#define KEYBOARD_DATA_PORT 0x60   /* The keyboard sends its scancodes here */
#define KEYBOARD_STATUS_PORT 0x64 /* Bit 0 = 1 when a new byte is available */

#define SCANCODE_EXTENDED 0xE0 /* Prefix of "extended" keys (arrows, ...) */
#define SCANCODE_RELEASED 0x80 /* Bit 7 set = the key was let go */

/* Keys we track by name instead of through the character tables. */
#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36
#define SCANCODE_CAPSLOCK 0x3A

/* Arrow keys always come with the 0xE0 prefix; these are the second byte. */
#define SCANCODE_UP 0x48
#define SCANCODE_DOWN 0x50
#define SCANCODE_LEFT 0x4B
#define SCANCODE_RIGHT 0x4D

static int shift_held = 0; /* is either Shift key down right now? */
static int caps_lock = 0;  /* is Caps Lock toggled on? */

/* Scancode set 1 -> ASCII, without Shift. Indexed directly by the scancode;
 * keys we do not map stay 0 (Ctrl, Tab, F1, ...). */
static const char normal_map[128] = {
    0,    0,                     // 0x00 unused, 0x01 Escape
    '1',  '2',  '3',  '4', '5',  // 0x02-0x06
    '6',  '7',  '8',  '9', '0',  // 0x07-0x0B
    '-',  '=',  '\b', 0,         // 0x0C '-', 0x0D '=', 0x0E Backspace, 0x0F Tab
    'q',  'w',  'e',  'r', 't',  'y',  // 0x10-0x15
    'u',  'i',  'o',  'p', '[',  ']',  // 0x16-0x1B
    '\n', 0,                           // 0x1C Enter, 0x1D Ctrl
    'a',  's',  'd',  'f', 'g',  'h',  // 0x1E-0x23
    'j',  'k',  'l',  ';', '\'', '`',  // 0x24-0x29
    0,    '\\',                        // 0x2A Left Shift, 0x2B '\'
    'z',  'x',  'c',  'v', 'b',  'n',  // 0x2C-0x31
    'm',  ',',  '.',  '/',             // 0x32-0x35
    0,    '*',  0,    ' ',  // 0x36 Right Shift, 0x37 '*', 0x38 Alt, 0x39 Space
    0,                      // 0x3A Caps Lock (handled separately)
};

/* Same as above but with Shift held down: symbols and uppercase letters. */
static const char shifted_map[128] = {
    0,    0,                         // 0x00 unused, 0x01 Escape
    '!',  '@', '#',  '$', '%',       // 0x02-0x06
    '^',  '&', '*',  '(', ')',       // 0x07-0x0B
    '_',  '+', '\b', 0,              // 0x0C-0x0F
    'Q',  'W', 'E',  'R', 'T', 'Y',  // 0x10-0x15
    'U',  'I', 'O',  'P', '{', '}',  // 0x16-0x1B
    '\n', 0,                         // 0x1C-0x1D
    'A',  'S', 'D',  'F', 'G', 'H',  // 0x1E-0x23
    'J',  'K', 'L',  ':', '"', '~',  // 0x24-0x29
    0,    '|',                       // 0x2A-0x2B
    'Z',  'X', 'C',  'V', 'B', 'N',  // 0x2C-0x31
    'M',  '<', '>',  '?',            // 0x32-0x35
    0,    '*', 0,    ' ',            // 0x36-0x39
    0,                               // 0x3A
};

/*
 * Wait for the keyboard controller to give us a scancode, then read it.
 * We "poll" (check over and over) because AdaOS has no interrupts yet.
 */
static uint8_t read_scan_code(void) {
    while ((inb(KEYBOARD_STATUS_PORT) & 0x01) == 0) {
        // No key has arrived yet, keep waiting
    }

    return inb(KEYBOARD_DATA_PORT);
}

int keyboard_getchar(void) {
    for (;;) {
        uint8_t scancode = read_scan_code();

        // Extended keys (arrows, right Ctrl, ...) come as 0xE0 followed by
        // another byte. Read both bytes together.
        if (scancode == SCANCODE_EXTENDED) {
            uint8_t extended = read_scan_code();
            int released = (extended & SCANCODE_RELEASED) != 0;
            extended &= 0x7F; // keep only the key number

            // The arrow keys have no ASCII character: report them as
            // KEY_* so the shell can browse the command history.
            if (!released) {
                if (extended == SCANCODE_UP) {
                    return KEY_UP;
                }
                if (extended == SCANCODE_DOWN) {
                    return KEY_DOWN;
                }
                if (extended == SCANCODE_LEFT) {
                    return KEY_LEFT;
                }
                if (extended == SCANCODE_RIGHT) {
                    return KEY_RIGHT;
                }
            }

            continue; // any other extended key: ignore it
        }

        int released = (scancode & SCANCODE_RELEASED) != 0;
        scancode &= 0x7F; // keep only the key number

        // Track whether Shift is currently held down.
        if (scancode == SCANCODE_LSHIFT || scancode == SCANCODE_RSHIFT) {
            shift_held = !released;
            continue;
        }

        // Caps Lock toggles only when the key goes down, not when released.
        if (scancode == SCANCODE_CAPSLOCK && !released) {
            caps_lock = !caps_lock;
            continue;
        }

        // We only care about key presses from here on.
        if (released) {
            continue;
        }

        // Look up the character: the shifted table while Shift is down.
        char c = shift_held ? shifted_map[scancode] : normal_map[scancode];

        // Caps Lock inverts the case of letters (but not of symbols).
        if (caps_lock) {
            if (shift_held && c >= 'A' && c <= 'Z') {
                c += 32;
            } else if (!shift_held && c >= 'a' && c <= 'z') {
                c -= 32;
            }
        }

        // 0 means "this key has no character", skip it and keep waiting.
        if (c != '\0') {
            return c;
        }
    }
}
