#include "serial.h"

#include <stdint.h>

#include "io.h"

// The serial console: the first UART (COM1) as a second output for
// debug messages, so kernel output can be captured to a file instead
// of only being visible on the screen. That is what makes a black
// screen diagnosable at all - QEMU writes the log with
//   -serial file:serial.log
// and VirtualBox with
//   --uart1 0x3F8 4 --uartmode1 file /path/to/serial.log
//
// Everything here polls: no interrupt is spent on a debug channel, and
// since the UART is a slow device (115200 baud is about 11520 bytes per
// second) every write waits for the transmit register to empty.

/* Port base of COM1 */
#define COM1_PORT 0x3F8

/* Register offsets from the port base */
#define UART_DATA 0
#define UART_INTERRUPT_ENABLE 1
#define UART_FIFO_CONTROL 2
#define UART_LINE_CONTROL 3
#define UART_MODEM_CONTROL 4
#define UART_LINE_STATUS 5

/* Line status bits */
#define UART_STATUS_DATA_READY 0x01
#define UART_STATUS_THR_EMPTY 0x20

/* Line control: bit 7 opens the divisor latch registers */
#define UART_LCR_DLAB 0x80
/* 8 data bits, no parity, one stop bit */
#define UART_LCR_8N1 0x03

/* FIFO: enable it and clear both queues */
#define UART_FIFO_ENABLE 0xC7

/* Modem control: loopback feeds the transmitter back into the receiver */
#define UART_MCR_LOOPBACK 0x1E
/* Modem control: normal operation with OUT2, RTS and DTR on */
#define UART_MCR_NORMAL 0x0B

/* 115200 baud is 115200 / 115200 = 1, the smallest divisor there is */
#define UART_BAUD_DIVISOR 0x0001

/* A byte the probe sends and expects to read back */
#define UART_PROBE_BYTE 0xAE

/* How many times to poll before giving up on the UART. Ports of an
 * absent chip read back as 0xFF, and a wedged UART must never hang the
 * kernel, so every wait is bounded. */
#define UART_TIMEOUT 100000

/* Set once the probe has found a working chip; until then, writes do
 * nothing at all. */
static int serial_present = 0;

/* Wait until the given line-status bits are set; 0 if they never are. */
static int serial_wait_for_status(uint8_t mask) {
    for (int i = 0; i < UART_TIMEOUT; i++) {
        if ((inb(COM1_PORT + UART_LINE_STATUS) & mask) != 0) {
            return 1;
        }
    }

    return 0;
}

/* Send one byte, waiting for room in the transmitter first. */
static void serial_write_byte(uint8_t value) {
    if (!serial_wait_for_status(UART_STATUS_THR_EMPTY)) {
        // The chip stopped accepting bytes: stop talking to it.
        serial_present = 0;
        return;
    }

    outb(COM1_PORT + UART_DATA, value);
}

/* Program COM1 for 115200 8N1 and check that a chip is really there.
 * Returns 1 when it is. */
static int serial_probe(void) {
    // No interrupts from the UART: this is a one-way polling console.
    outb(COM1_PORT + UART_INTERRUPT_ENABLE, 0x00);

    // The divisor registers are hidden behind the DLAB bit.
    outb(COM1_PORT + UART_LINE_CONTROL, UART_LCR_DLAB);
    outb(COM1_PORT + UART_DATA, (uint8_t)(UART_BAUD_DIVISOR & 0xFF));
    outb(COM1_PORT + UART_INTERRUPT_ENABLE,
         (uint8_t)((UART_BAUD_DIVISOR >> 8) & 0xFF));
    outb(COM1_PORT + UART_LINE_CONTROL, UART_LCR_8N1);

    outb(COM1_PORT + UART_FIFO_CONTROL, UART_FIFO_ENABLE);

    // Loopback makes the transmitter feed the receiver, so a real chip
    // hands our byte straight back. A missing one reads 0xFF.
    outb(COM1_PORT + UART_MODEM_CONTROL, UART_MCR_LOOPBACK);
    outb(COM1_PORT + UART_DATA, UART_PROBE_BYTE);

    int present = serial_wait_for_status(UART_STATUS_DATA_READY) &&
                  inb(COM1_PORT + UART_DATA) == UART_PROBE_BYTE;

    // Back to normal operation, loopback off.
    outb(COM1_PORT + UART_MODEM_CONTROL, UART_MCR_NORMAL);

    return present;
}

/* Bring up COM1 and announce the console on it. */
void serial_init(void) {
    serial_present = serial_probe();

    if (serial_present) {
        serial_write("AdaOS: serial console on COM1, 115200 8N1\n");
    }
}

/* Send one character; a '\n' also sends the '\r' a terminal expects. */
void serial_write_char(char c) {
    if (!serial_present) {
        return;
    }

    if (c == '\n') {
        serial_write_byte('\r');
    }

    serial_write_byte((uint8_t)c);
}

/* Send a string. */
void serial_write(const char* s) {
    for (int i = 0; s[i] != '\0'; i++) {
        serial_write_char(s[i]);
    }
}
