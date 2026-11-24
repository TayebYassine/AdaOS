#include "keyboard.h"

#include <stdint.h>

#include "interrupt.h"
#include "io.h"

// The keyboard controller stores every key press/release as a "scancode"
// (a small number) in I/O port 0x60 and raises IRQ1 whenever a new byte
// is waiting. That IRQ becomes interrupt 0x21 (see pic.c), which lands
// in keyboard_irq() below: it reads the port, translates the scancode
// into an ASCII character and drops it into a buffer.
// keyboard_getchar() then only has to take the next character out of
// that buffer - it idles the CPU instead of spinning while no key has
// been typed.
//
// Two kinds of scancodes exist:
//   - "make" codes: the key was pressed
//   - "break" codes: the key was released (same number with bit 7 set)

/* The keyboard sends its scancodes here */
#define KEYBOARD_DATA_PORT 0x60

/* Prefix of "extended" keys (arrows, ...) */
#define SCANCODE_EXTENDED 0xE0
/* Bit 7 set = the key was let go */
#define SCANCODE_RELEASED 0x80

// Keys we track by name instead of through the character tables.
#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36
#define SCANCODE_CAPSLOCK 0x3A
#define SCANCODE_CTRL 0x1D
#define SCANCODE_ESC 0x01

// Arrow keys always come with the 0xE0 prefix; these are the second byte.
#define SCANCODE_UP 0x48
#define SCANCODE_DOWN 0x50
#define SCANCODE_LEFT 0x4B
#define SCANCODE_RIGHT 0x4D

/* Keys we can hold ahead of the shell */
#define KEY_BUFFER_SIZE 128

static int shift_held = 0;
static int ctrl_held = 0;
static int caps_lock = 0;
static int extended_prefix = 0;

/* Scancode set 1 -> ASCII without Shift; unmapped keys stay 0. */
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

// Ring buffer of keys waiting for the shell: head = IRQ1's write side, tail =
// the read side, count tells full from empty.
static int key_buffer[KEY_BUFFER_SIZE];
static int key_buffer_head = 0;
static int key_buffer_tail = 0;
static int key_buffer_count = 0;

/* Scancode -> ASCII character, KEY_* value (arrows), or 0 for bytes the
 * shell does not want. Remembers the 0xE0 prefix across interrupts. */
static int translate(uint8_t scancode) {
    if (scancode == SCANCODE_EXTENDED) {
        extended_prefix = 1;
        return 0;
    }

    if (extended_prefix) {
        extended_prefix = 0;
        int released = (scancode & SCANCODE_RELEASED) != 0;
        scancode &= 0x7F;  // keep only the key number

        if (scancode == SCANCODE_CTRL) {
            ctrl_held = !released;
            return 0;
        }

        if (!released) {
            if (scancode == SCANCODE_UP) {
                return KEY_UP;
            }
            if (scancode == SCANCODE_DOWN) {
                return KEY_DOWN;
            }
            if (scancode == SCANCODE_LEFT) {
                return KEY_LEFT;
            }
            if (scancode == SCANCODE_RIGHT) {
                return KEY_RIGHT;
            }
        }

        return 0;
    }

    int released = (scancode & SCANCODE_RELEASED) != 0;
    scancode &= 0x7F;

    // Track whether Shift is currently held down.
    if (scancode == SCANCODE_LSHIFT || scancode == SCANCODE_RSHIFT) {
        shift_held = !released;
        return 0;
    }

    // Track whether Ctrl is currently held down.
    if (scancode == SCANCODE_CTRL) {
        ctrl_held = !released;
        return 0;
    }

    // Caps Lock toggles only when the key goes down, not when released.
    if (scancode == SCANCODE_CAPSLOCK && !released) {
        caps_lock = !caps_lock;
        return 0;
    }

    if (released) {
        return 0;
    }

    if (scancode == SCANCODE_ESC) {
        return KEY_ESC;
    }

    char c = shift_held ? shifted_map[scancode] : normal_map[scancode];

    // Caps Lock inverts the case of letters (but not of symbols).
    if (caps_lock) {
        if (shift_held && c >= 'A' && c <= 'Z') {
            c += 32;
        } else if (!shift_held && c >= 'a' && c <= 'z') {
            c -= 32;
        }
    }

    // Ctrl + letter becomes the control code: Ctrl+S = 0x13, Ctrl+E = 0x05.
    if (ctrl_held) {
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 1;
        }
        if (c >= 'A' && c <= 'Z') {
            return c - 'A' + 1;
        }
        return 0;
    }

    return c != '\0' ? c : 0;
}

/* Store a key for keyboard_getchar(); IRQ1 context already has interrupts off.
 */
static void buffer_push(int key) {
    if (key_buffer_count == KEY_BUFFER_SIZE) {
        return;  // the shell is not keeping up: drop the key
    }

    key_buffer[key_buffer_head] = key;
    key_buffer_head = (key_buffer_head + 1) % KEY_BUFFER_SIZE;
    key_buffer_count++;
}

/* IRQ1: the keyboard has a new scancode waiting in port 0x60. */
static void keyboard_irq(struct interrupt_frame* frame) {
    (void)frame;  // we do not need to know what was interrupted

    // Reading port 0x60 takes the byte and acks the controller.
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    int key = translate(scancode);

    if (key != 0) {
        buffer_push(key);
    }
}

void keyboard_init(void) { interrupt_register_irq(1, keyboard_irq); }

/* Take the oldest key out of the buffer, or 0 if there is none. Interrupts
 * are held off while the counters change, so IRQ1 cannot interrupt us. */
static int buffer_pop(void) {
    interrupt_disable();

    int key = 0;

    if (key_buffer_count > 0) {
        key = key_buffer[key_buffer_tail];
        key_buffer_tail = (key_buffer_tail + 1) % KEY_BUFFER_SIZE;
        key_buffer_count--;
    }

    interrupt_enable();
    return key;
}

/* Return the oldest key without waiting, or 0 if no key is ready. */
int keyboard_poll(void) { return buffer_pop(); }

/* Block until a key is available, then return it. */
int keyboard_getchar(void) {
    for (;;) {
        int key = buffer_pop();

        if (key != 0) {
            return key;
        }

        interrupt_wait();
    }
}
