#ifndef ADAOS_SERIAL_H
#define ADAOS_SERIAL_H

/* Bring up COM1 as a debug console (115200 8N1, polling). */
void serial_init(void);

/* Send one character to the serial console; '\n' becomes CR LF. */
void serial_write_char(char c);

/* Send a string to the serial console. */
void serial_write(const char* s);

#endif /* ADAOS_SERIAL_H */
