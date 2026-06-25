#include "datetime.h"
#include "io.h"
#include <stdint.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#ifndef DATETIME_UTC_OFFSET_MINUTES
#define DATETIME_UTC_OFFSET_MINUTES (9 * 60)
#endif

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t weekday;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
} cmos_datetime_t;

static const char *s_weekday_short[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *s_weekday_long[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

static const char *s_month_short[13] = {
    "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char *s_month_long[13] = {
    "", "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int cmos_update_in_progress(void) {
    return (cmos_read(0x0A) & 0x80U) != 0;
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0FU) + ((value >> 4) * 10U));
}

static void cmos_wait_stable(void) {
    int guard;
    for (guard = 0; guard < 100000; guard++) {
        if (!cmos_update_in_progress())
            return;
    }
}

static void cmos_read_raw(cmos_datetime_t *raw) {
    cmos_wait_stable();
    raw->second  = cmos_read(0x00);
    raw->minute  = cmos_read(0x02);
    raw->hour    = cmos_read(0x04);
    raw->weekday = cmos_read(0x06);
    raw->day     = cmos_read(0x07);
    raw->month   = cmos_read(0x08);
    raw->year    = cmos_read(0x09);
    raw->century = cmos_read(0x32);
}

static int cmos_same_second(const cmos_datetime_t *a, const cmos_datetime_t *b) {
    return a->second == b->second &&
           a->minute == b->minute &&
           a->hour   == b->hour &&
           a->day    == b->day &&
           a->month  == b->month &&
           a->year   == b->year;
}

int datetime_is_leap_year(int year) {
    if ((year % 4) != 0) return 0;
    if ((year % 100) != 0) return 1;
    return (year % 400) == 0;
}

int datetime_days_in_month(int year, int month) {
    static const int days[13] = {
        0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month < 1 || month > 12) return 30;
    if (month == 2 && datetime_is_leap_year(year)) return 29;
    return days[month];
}

int datetime_day_of_week(int year, int month, int day) {
    static const int offsets[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 1 || month > 12 || day < 1) return 0;
    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 +
            offsets[month - 1] + day) % 7;
}

int datetime_day_of_year(int year, int month, int day) {
    int m;
    int total = day;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) total = 1;
    for (m = 1; m < month; m++)
        total += datetime_days_in_month(year, m);
    return total;
}

int datetime_week_of_year(int year, int month, int day) {
    int yday = datetime_day_of_year(year, month, day);
    return ((yday - 1) / 7) + 1;
}

const char *datetime_weekday_short(int weekday) {
    if (weekday < 0 || weekday > 6) return s_weekday_short[0];
    return s_weekday_short[weekday];
}

const char *datetime_weekday_long(int weekday) {
    if (weekday < 0 || weekday > 6) return s_weekday_long[0];
    return s_weekday_long[weekday];
}

const char *datetime_month_short(int month) {
    if (month < 1 || month > 12) return s_month_short[1];
    return s_month_short[month];
}

const char *datetime_month_long(int month) {
    if (month < 1 || month > 12) return s_month_long[1];
    return s_month_long[month];
}

static int rtc_read_utc(datetime_t *dt) {
    cmos_datetime_t a, b;
    uint8_t status_b;
    uint8_t hour_pm;
    int attempt;
    int binary_mode;
    int hour_24;
    int year;

    if (!dt) return 0;

    cmos_read_raw(&a);
    b = a;
    for (attempt = 0; attempt < 5; attempt++) {
        cmos_read_raw(&b);
        if (cmos_same_second(&a, &b))
            break;
        a = b;
    }

    status_b = cmos_read(0x0B);
    binary_mode = (status_b & 0x04U) != 0;
    hour_24 = (status_b & 0x02U) != 0;
    hour_pm = (uint8_t)(b.hour & 0x80U);
    b.hour = (uint8_t)(b.hour & 0x7FU);

    if (!binary_mode) {
        b.second  = bcd_to_binary(b.second);
        b.minute  = bcd_to_binary(b.minute);
        b.hour    = bcd_to_binary(b.hour);
        b.day     = bcd_to_binary(b.day);
        b.month   = bcd_to_binary(b.month);
        b.year    = bcd_to_binary(b.year);
        b.century = bcd_to_binary(b.century);
    }

    if (!hour_24) {
        if (hour_pm && b.hour < 12)
            b.hour = (uint8_t)(b.hour + 12);
        if (!hour_pm && b.hour == 12)
            b.hour = 0;
    }

    if (b.century >= 19 && b.century <= 99)
        year = (int)b.century * 100 + (int)b.year;
    else
        year = ((b.year >= 70) ? 1900 : 2000) + (int)b.year;

    if (year < 1970 || b.month < 1 || b.month > 12 ||
        b.day < 1 || b.day > datetime_days_in_month(year, b.month) ||
        b.hour > 23 || b.minute > 59 || b.second > 59) {
        dt->year = 1970;
        dt->month = 1;
        dt->day = 1;
        dt->hour = 0;
        dt->minute = 0;
        dt->second = 0;
        dt->weekday = datetime_day_of_week(dt->year, dt->month, dt->day);
        return 0;
    }

    dt->year = year;
    dt->month = b.month;
    dt->day = b.day;
    dt->hour = b.hour;
    dt->minute = b.minute;
    dt->second = b.second;
    dt->weekday = datetime_day_of_week(dt->year, dt->month, dt->day);
    return 1;
}

static void datetime_shift_days(datetime_t *dt, int delta) {
    while (delta > 0) {
        dt->day++;
        if (dt->day > datetime_days_in_month(dt->year, dt->month)) {
            dt->day = 1;
            dt->month++;
            if (dt->month > 12) {
                dt->month = 1;
                dt->year++;
            }
        }
        delta--;
    }
    while (delta < 0) {
        dt->day--;
        if (dt->day < 1) {
            dt->month--;
            if (dt->month < 1) {
                dt->month = 12;
                dt->year--;
            }
            dt->day = datetime_days_in_month(dt->year, dt->month);
        }
        delta++;
    }
    dt->weekday = datetime_day_of_week(dt->year, dt->month, dt->day);
}

static void datetime_apply_offset(datetime_t *dt, int offset_minutes) {
    int total_minutes;
    if (!dt) return;
    total_minutes = dt->hour * 60 + dt->minute + offset_minutes;
    while (total_minutes < 0) {
        datetime_shift_days(dt, -1);
        total_minutes += 24 * 60;
    }
    while (total_minutes >= 24 * 60) {
        datetime_shift_days(dt, 1);
        total_minutes -= 24 * 60;
    }
    dt->hour = total_minutes / 60;
    dt->minute = total_minutes % 60;
}

int datetime_now(datetime_t *dt) {
    int ok;
    if (!dt) return 0;
    ok = rtc_read_utc(dt);
    datetime_apply_offset(dt, DATETIME_UTC_OFFSET_MINUTES);
    return ok;
}

int datetime_now_utc(datetime_t *dt) {
    if (!dt) return 0;
    return rtc_read_utc(dt);
}
