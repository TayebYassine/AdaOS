#ifndef ADAOS_SERIAL_H
#define ADAOS_SERIAL_H

// The serial console: COM1 driven by polling, used as a second output for debug
// text so kernel messages can be captured to a file instead of only being seen
// on screen. The UART is set to 115200 8N1, and every write waits for the
// transmit register to empty, which keeps the output in order.

/* Bring up COM1 as a debug console (115200 8N1, polling). */
void serial_init(void);

/* Send one character to the serial console; '\n' becomes CR LF. */
void serial_write_char(char c);

/* Send a string to the serial console. */
void serial_write(const char* s);

#endif /* ADAOS_SERIAL_H */
