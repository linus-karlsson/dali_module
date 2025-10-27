#include <sys/time.h>

#include "platform.h"

static struct tm time_info_to_tm(time_info_t* time_info)
{
  struct tm result = {};
  result.tm_sec = time_info->second;
  result.tm_min = time_info->minute;
  result.tm_hour = time_info->hour;
  result.tm_mday = time_info->month_day;
  result.tm_mon = time_info->month;
  result.tm_year = time_info->year;
  result.tm_wday = time_info->week_day;
  result.tm_yday = time_info->year_day;
  result.tm_isdst = time_info->day_light_saving;
  return result;
}

static time_info_t tm_to_time_info(struct tm* tm)
{
  time_info_t result = {};
  result.second = tm->tm_sec;
  result.minute = tm->tm_min;
  result.hour = tm->tm_hour;
  result.month_day = tm->tm_mday;
  result.month = tm->tm_mon;
  result.year = tm->tm_year;
  result.week_day = tm->tm_wday;
  result.year_day = tm->tm_yday;
  result.day_light_saving = tm->tm_isdst;
  return result;
}

uint32_t lsx_get_time(void)
{
  return (uint32_t)time(NULL);
}

void lsx_get_time_info(uint32_t time_sec, time_info_t* time_info)
{
  time_t time_s = (time_t)time_sec;
  struct tm tm_temp = {};
  localtime_r(&time_s, &tm_temp);
  (*time_info) = tm_to_time_info(&tm_temp);
}

void lsx_get_time_info_default(time_info_t* time_info)
{
  lsx_get_time_info(lsx_get_time(), time_info);
}

uint32_t lsx_make_time(time_info_t* time_info)
{
  struct tm converted = time_info_to_tm(time_info);
  return (uint32_t)mktime(&converted);
}

void lsx_set_time(uint32_t time_sec)
{
  struct timeval now = {};
  now.tv_sec = time_sec;
  settimeofday(&now, NULL);
}

void lsx_set_time_info(time_info_t* time_info)
{
  lsx_set_time(lsx_make_time(time_info));
}
