#ifndef ADAOS_CLOCK_H
#define ADAOS_CLOCK_H

/* The kernel's date and time, as the "time" command prints them. */
struct clock_time {
    /* the full year (1-9999) */
    int year;
    /* 1-12 */
    int month;
    /* 1-31 */
    int day;
    /* 0-23: midnight is hour 0, noon is hour 12 */
    int hour;
    /* 0-59 */
    int minute;
    /* 0-59 */
    int second;
};

/* Start the clock from the CMOS Real-Time Clock; call once before timer_init()
 * lets IRQ0 in. The top row stays empty until clock_resume(). */
void clock_init(void);

/* Called by IRQ0 once a second: move the clock on and repaint its bar. */
void clock_tick(void);

/* Copy the current date and time out. */
void clock_get(struct clock_time* out);

/* Set the clock; values outside each field's range are refused and
 * return 0. Updates both memory and the CMOS Real-Time Clock. */
int clock_set_time(int hour, int minute, int second);
int clock_set_date(int year, int month, int day);

/* How many days a month has; February gets 29 in a leap year. */
int clock_days_in_month(int year, int month);

/* What day of the week a date falls on: 1 = Sunday, 7 = Saturday. */
int clock_day_of_week(int year, int month, int day);

/* Write the date and time into out, as "MM-DD-YYYY HH:MM:SS". With ampm
 * set the hour becomes 12-hour with a trailing " AM" or " PM" instead,
 * so out needs 23 bytes - 24 to be safe. */
void clock_stamp(char* out, int ampm);

/* Paint the top row of the screen with the date and time. */
void clock_draw(void);

/* Hand the top row over to the editor, and take it back afterwards. */
void clock_pause(void);
void clock_resume(void);

#endif /* ADAOS_CLOCK_H */
