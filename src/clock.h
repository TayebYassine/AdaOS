#ifndef ADAOS_CLOCK_H
#define ADAOS_CLOCK_H

// The wall clock: one date and one time kept in memory, started from the CMOS
// Real-Time Clock at boot and moved on by the timer's ticks, one second per
// hundred. Formatting for display and the conversions between fields and text
// live here too. Setting the time writes the memory copy and the CMOS chip, so
// the value survives a reboot.

/* A date and a time, in the form the "time" command prints them. */
struct clock_time {
    int year;    // full year
    int month;   // 1-12
    int day;     // 1-31
    int hour;    // 0-23
    int minute;  // 0-59
    int second;  // 0-59
};

/* Start the clock from the CMOS Real-Time Clock; call before timer_init(). */
void clock_init(void);

/* Called by IRQ0 once a second: move the clock on and repaint its bar. */
void clock_tick(void);

/* Copy the current date and time out. */
void clock_get(struct clock_time* out);

/* Set the time; out-of-range fields are refused and return 0. */
int clock_set_time(int hour, int minute, int second);

/* Set the date; out-of-range fields are refused and return 0. */
int clock_set_date(int year, int month, int day);

/* How many days a month has; February gets 29 in a leap year. */
int clock_days_in_month(int year, int month);

/* Day of the week a date falls on: 1 = Sunday, 7 = Saturday. */
int clock_day_of_week(int year, int month, int day);

/* Write "MM-DD-YYYY HH:MM:SS" into out. With ampm the hour is 12-hour with a
 * trailing " AM"/" PM" instead, so out needs 24 bytes to be safe. */
void clock_stamp(char* out, int ampm);

/* Paint the top row of the screen with the date and time. */
void clock_draw(void);

/* Hand the top row over to a full-screen program, and take it back after. */
void clock_pause(void);
void clock_resume(void);

#endif /* ADAOS_CLOCK_H */
