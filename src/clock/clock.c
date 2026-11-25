#include "clock.h"

#include <stdint.h>

#include "interrupt.h"
#include "io.h"
#include "libc.h"
#include "screen.h"
#include "version.h"

/* CMOS address and data ports: write a register number, read or write it. */
#define CMOS_ADDRESS 0x70
#define CMOS_DATA 0x71

// CMOS register numbers. The RTC keeps each field in its own byte.
#define RTC_SECONDS 0x00
#define RTC_MINUTES 0x02
#define RTC_HOURS 0x04
#define RTC_DAY_OF_WEEK 0x06
#define RTC_DAY 0x07
#define RTC_MONTH 0x08
#define RTC_YEAR 0x09
#define RTC_STATUS_A 0x0A  // bit 7 is set while the RTC updates
#define RTC_STATUS_B 0x0B  // bit 1 = 24h, bit 2 = binary, bit 7 = no updates
#define RTC_CENTURY 0x32

/* Black text on a white bar, exactly like the editor's title row. */
#define COLOR_BAR 0x70

/* Room for the longest stamp, "MM-DD-YYYY HH:MM:SS PM", plus the '\0'. */
#define STAMP_MAX 24

static struct clock_time now;

/* Set while the editor is on screen: its title bar owns the top row. */
static int draw_paused = 0;

/* Set when the last read showed a century register the RTC really keeps.
 * Register 0x32 does not exist on every machine, and writing a century into
 * whatever else lives there would corrupt an unrelated CMOS byte, so the date
 * is only written back when the read proved the register is there. */
static int century_present = 0;

/* Read one byte from the given CMOS register. */
static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

/* Write one byte to the given CMOS register. */
static void cmos_write(uint8_t reg, uint8_t value) {
    outb(CMOS_ADDRESS, reg);
    outb(CMOS_DATA, value);
}

/* Returns non-zero while the RTC is in the middle of updating its registers. */
static int rtc_is_updating(void) {
    return (cmos_read(RTC_STATUS_A) & 0x80) != 0;
}

/* Convert a binary-coded decimal byte into standard binary. */
static int bcd_to_binary(int value) {
    return ((value / 16) * 10) + (value % 16);
}

/* Convert a binary value (0-99) into binary-coded decimal. */
static int binary_to_bcd(int value) {
    return ((value / 10) * 16) + (value % 10);
}

/* February has an extra day every fourth year, but not every hundredth. */
static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

/* Days in a month; 0 when the month is not one. */
int clock_days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};

    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }

    return days[month - 1];
}

/* Read every raw register of the RTC into the given pointers. */
static void rtc_read_raw(uint8_t* second, uint8_t* minute, uint8_t* hour,
                         uint8_t* day, uint8_t* month, uint8_t* year,
                         uint8_t* century) {
    *second = cmos_read(RTC_SECONDS);
    *minute = cmos_read(RTC_MINUTES);
    *hour = cmos_read(RTC_HOURS);
    *day = cmos_read(RTC_DAY);
    *month = cmos_read(RTC_MONTH);
    *year = cmos_read(RTC_YEAR);
    *century = cmos_read(RTC_CENTURY);
}

/* Check that the time fields are within normal ranges. */
static int is_valid_time(int hour, int minute, int second) {
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 &&
           second >= 0 && second <= 59;
}

/* Check that the date fields form a real date. */
static int is_valid_date(int year, int month, int day) {
    return year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 &&
           day <= clock_days_in_month(year, month);
}

/* Read the RTC into "out"; 1 on success, 0 when a field is not valid. */
static int rtc_read(struct clock_time* out) {
    interrupt_flags flags = interrupt_save();
    uint8_t sec = 0, min = 0, hr = 0, day = 0, month = 0, yr = 0, cen = 0;
    uint8_t last_sec, last_min, last_hr, last_day, last_month, last_yr,
        last_cen;
    uint8_t status_b;
    int is_bcd;
    int is_12hr;
    int pm = 0;
    int full_year;

    for (int i = 0; i < 10000 && rtc_is_updating(); i++) {
    }

    rtc_read_raw(&sec, &min, &hr, &day, &month, &yr, &cen);

    // A read that straddles an update returns mixed fields, so read twice
    // and keep trying until both reads agree.
    for (int attempt = 0; attempt < 5; attempt++) {
        last_sec = sec;
        last_min = min;
        last_hr = hr;
        last_day = day;
        last_month = month;
        last_yr = yr;
        last_cen = cen;

        for (int i = 0; i < 10000 && rtc_is_updating(); i++) {
        }

        rtc_read_raw(&sec, &min, &hr, &day, &month, &yr, &cen);

        if (sec == last_sec && min == last_min && hr == last_hr &&
            day == last_day && month == last_month && yr == last_yr &&
            cen == last_cen) {
            break;
        }
    }

    status_b = cmos_read(RTC_STATUS_B);
    interrupt_restore(flags);

    // Status B says how the fields are encoded.
    is_bcd = !(status_b & 0x04);
    is_12hr = !(status_b & 0x02);

    if (is_12hr) {
        pm = hr & 0x80;
        hr &= 0x7F;
    }

    if (is_bcd) {
        sec = (uint8_t)bcd_to_binary(sec);
        min = (uint8_t)bcd_to_binary(min);
        hr = (uint8_t)bcd_to_binary(hr);
        day = (uint8_t)bcd_to_binary(day);
        month = (uint8_t)bcd_to_binary(month);
        yr = (uint8_t)bcd_to_binary(yr);
        if (cen != 0) {
            cen = (uint8_t)bcd_to_binary(cen);
        }
    }

    if (is_12hr) {
        hr = (uint8_t)((hr % 12) + (pm ? 12 : 0));
    }

    // Without a century register, a two-digit year is split at 80.
    int has_century = (cen >= 19 && cen <= 99);

    if (has_century) {
        full_year = cen * 100 + yr;
    } else {
        full_year = (yr >= 80 ? 1900 : 2000) + yr;
    }

    if (!is_valid_time(hr, min, sec) || !is_valid_date(full_year, month, day)) {
        century_present = 0;
        return 0;
    }

    century_present = has_century;

    out->year = full_year;
    out->month = month;
    out->day = day;
    out->hour = hr;
    out->minute = min;
    out->second = sec;

    return 1;
}

/* Write hour, minute, and second back to the CMOS Real-Time Clock. */
static void rtc_write_time(int hour, int minute, int second) {
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    int is_bcd = !(status_b & 0x04);
    int is_12hr = !(status_b & 0x02);
    uint8_t rtc_sec = (uint8_t)second;
    uint8_t rtc_min = (uint8_t)minute;
    uint8_t rtc_hour = (uint8_t)hour;
    uint8_t pm = 0;

    if (is_12hr) {
        if (rtc_hour >= 12) {
            pm = 0x80;
            rtc_hour -= 12;
        }
        if (rtc_hour == 0) {
            rtc_hour = 12;
        }
    }

    if (is_bcd) {
        rtc_sec = (uint8_t)binary_to_bcd(rtc_sec);
        rtc_min = (uint8_t)binary_to_bcd(rtc_min);
        rtc_hour = (uint8_t)binary_to_bcd(rtc_hour);
    }

    if (is_12hr) {
        rtc_hour |= pm;
    }

    // Bit 7 of status B stops the RTC counting while the fields are written.
    cmos_write(RTC_STATUS_B, status_b | 0x80);

    cmos_write(RTC_SECONDS, rtc_sec);
    cmos_write(RTC_MINUTES, rtc_min);
    cmos_write(RTC_HOURS, rtc_hour);

    cmos_write(RTC_STATUS_B, status_b & 0x7F);
}

/* Sakamoto's algorithm: the day of the week (1 = Sunday, 7 = Saturday). */
int clock_day_of_week(int year, int month, int day) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    if (month < 3) {
        year -= 1;
    }

    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) %
               7 +
           1;
}

/* Write year, month, and day back to the CMOS Real-Time Clock. */
static void rtc_write_date(int year, int month, int day) {
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    int is_bcd = !(status_b & 0x04);
    uint8_t rtc_day = (uint8_t)day;
    uint8_t rtc_month = (uint8_t)month;
    uint8_t rtc_year = (uint8_t)(year % 100);
    uint8_t rtc_century = (uint8_t)(year / 100);
    uint8_t rtc_dow = (uint8_t)clock_day_of_week(year, month, day);

    if (is_bcd) {
        rtc_day = (uint8_t)binary_to_bcd(rtc_day);
        rtc_month = (uint8_t)binary_to_bcd(rtc_month);
        rtc_year = (uint8_t)binary_to_bcd(rtc_year);
        rtc_century = (uint8_t)binary_to_bcd(rtc_century);
        rtc_dow = (uint8_t)binary_to_bcd(rtc_dow);
    }

    cmos_write(RTC_STATUS_B, status_b | 0x80);

    cmos_write(RTC_DAY, rtc_day);
    cmos_write(RTC_MONTH, rtc_month);
    cmos_write(RTC_YEAR, rtc_year);

    // Only touch the century byte when this RTC was seen to have one.
    if (century_present) {
        cmos_write(RTC_CENTURY, rtc_century);
    }

    cmos_write(RTC_DAY_OF_WEEK, rtc_dow);

    cmos_write(RTC_STATUS_B, status_b & 0x7F);
}

/* Start the clock from the RTC; the top row stays empty until clock_resume().
 */
void clock_init(void) {
    struct clock_time rtc_time;

    if (rtc_read(&rtc_time)) {
        now = rtc_time;
    } else {
        // No usable RTC: start from a known date rather than nothing.
        now.year = 2024;
        now.month = 1;
        now.day = 1;
        now.hour = 0;
        now.minute = 0;
        now.second = 0;
    }

    draw_paused = 1;
}

/* Copy the current date and time out, with IRQ0 held off. */
void clock_get(struct clock_time* out) {
    interrupt_flags flags = interrupt_save();

    *out = now;

    interrupt_restore(flags);
}

/* Move the clock on by one second, rolling the date over at midnight. */
static void advance(void) {
    now.second++;

    if (now.second < 60) {
        return;
    }

    now.second = 0;
    now.minute++;

    if (now.minute < 60) {
        return;
    }

    now.minute = 0;
    now.hour++;

    if (now.hour < 24) {
        return;
    }

    now.hour = 0;
    now.day++;

    if (now.day <= clock_days_in_month(now.year, now.month)) {
        return;
    }

    now.day = 1;
    now.month++;

    if (now.month <= 12) {
        return;
    }

    now.month = 1;
    now.year++;

    if (now.year > 9999) {
        now.year = 1;
    }
}

/* Write "value" as "width" digits at out[at]; returns where the next goes. */
static int write_number(char* out, int at, int value, int width) {
    char digits[10];
    int count = 0;

    do {
        digits[count] = (char)('0' + (value % 10));
        count++;
        value /= 10;
    } while (value != 0);

    for (int i = count; i < width; i++) {
        out[at] = '0';
        at++;
    }

    while (count > 0) {
        count--;
        out[at] = digits[count];
        at++;
    }

    return at;
}

/* The date and time as text, the way the screen shows them. */
void clock_stamp(char* out, int ampm) {
    int at = 0;
    int hour = now.hour;

    if (ampm) {
        hour = now.hour % 12;
        if (hour == 0) {
            hour = 12;
        }
    }

    at = write_number(out, at, now.month, 2);
    out[at] = '-';
    at++;
    at = write_number(out, at, now.day, 2);
    out[at] = '-';
    at++;
    at = write_number(out, at, now.year, 4);
    out[at] = ' ';
    at++;
    at = write_number(out, at, hour, 2);
    out[at] = ':';
    at++;
    at = write_number(out, at, now.minute, 2);
    out[at] = ':';
    at++;
    at = write_number(out, at, now.second, 2);

    if (ampm) {
        out[at] = ' ';
        at++;
        out[at] = (now.hour < 12) ? 'A' : 'P';
        at++;
        out[at] = 'M';
        at++;
    }

    out[at] = '\0';
}

/* Paint the top row: the version at the left, the date and time at the right.
 */
void clock_draw(void) {
    char stamp[STAMP_MAX];

    if (draw_paused) {
        return;
    }

    clock_stamp(stamp, 0);

    screen_fill_row(0, ' ', COLOR_BAR);
    screen_puts_at(0, 0, ADAOS_VERSION, COLOR_BAR);
    screen_puts_at(0, SCREEN_COLS - (int)strlen(stamp), stamp, COLOR_BAR);
}

/* IRQ0, once a second: one more second has gone by. */
void clock_tick(void) {
    advance();
    clock_draw();
}

/* Set the time; values outside each field's range are refused. */
int clock_set_time(int hour, int minute, int second) {
    interrupt_flags flags;

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 59) {
        return 0;
    }

    // IRQ0 is held off so a tick cannot mix the old and the new time.
    flags = interrupt_save();

    now.hour = hour;
    now.minute = minute;
    now.second = second;

    rtc_write_time(hour, minute, second);

    interrupt_restore(flags);

    clock_draw();

    return 1;
}

/* Set the date; values outside each field's range are refused. */
int clock_set_date(int year, int month, int day) {
    interrupt_flags flags;

    if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > clock_days_in_month(year, month)) {
        return 0;
    }

    flags = interrupt_save();

    now.year = year;
    now.month = month;
    now.day = day;

    rtc_write_date(year, month, day);

    interrupt_restore(flags);

    clock_draw();

    return 1;
}

/* Hand the top row over to a full-screen program. */
void clock_pause(void) { draw_paused = 1; }

/* Take the top row back, drawn with whatever the time is now. */
void clock_resume(void) {
    draw_paused = 0;
    clock_draw();
}
