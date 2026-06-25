#ifndef DATETIME_H
#define DATETIME_H

#include <stdint.h>

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday; /* 0=Sunday, 6=Saturday */
} datetime_t;

int datetime_now(datetime_t *dt);
int datetime_now_utc(datetime_t *dt);
int datetime_is_leap_year(int year);
int datetime_days_in_month(int year, int month);
int datetime_day_of_week(int year, int month, int day);
int datetime_day_of_year(int year, int month, int day);
int datetime_week_of_year(int year, int month, int day);

const char *datetime_weekday_short(int weekday);
const char *datetime_weekday_long(int weekday);
const char *datetime_month_short(int month);
const char *datetime_month_long(int month);

#endif /* DATETIME_H */
