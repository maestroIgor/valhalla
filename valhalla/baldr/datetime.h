#ifndef VALHALLA_BALDR_DATETIME_H_
#define VALHALLA_BALDR_DATETIME_H_

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#include <valhalla/baldr/graphconstants.h>
#include <valhalla/midgard/constants.h>
#include <valhalla/midgard/logging.h>

namespace valhalla {
namespace baldr {
namespace DateTime {

/**
 * Get the raw timezone spec (CSV).
 * @return  Returns a string containing the timezone information.
 */
std::string get_timezone_csv();

// Structure holding timezone info
struct TimezoneInfo {
  std::string id_;
  std::string abbrev_;
  std::string name_;
  std::string dst_abbrev_;
  std::string dst_name_;
  std::string gmt_offset_;
  std::string dst_adjust_;
  std::string start_date_;
  std::string start_time_;
  std::string end_date_;
  std::string end_time_;
  int gmt_offset_secs_;

  /**
   * Checks if this timezone has a DST entry.
   * @return Returns true if this timezone has DST.
   */
  bool has_dst() const {
    return !dst_adjust_.empty();
  }

  /**
   * Equality operator.
   */
  bool operator==(const TimezoneInfo& other) const {
    return id_ == other.id_;
  }
  /**
   * Simple operator < for finding by id (string compares).
   */
  bool operator<(const TimezoneInfo& other) const {
    return id_ < other.id_;
  }
};

struct TimezoneDB {
  TimezoneDB();

  /**
   * Convert a timezone name into an index. Add 1 to the index so that
   * index 0 represents an invalid or missing timezone.
   * @param id  Timezone Id.
   * @return Returns the index (+1) in the timezone info list.
   */
  size_t to_index(const std::string& id) const {
    TimezoneInfo comp;
    comp.id_ = id;
    auto it = std::find(timezones_.cbegin(), timezones_.cend(), comp);
    if (it == timezones_.end()) {
      LOG_ERROR("Could not find TimezoneInfo for " + id);
      return 0;
    }
    return (it - timezones_.begin()) + 1;
  }

  /**
   * Return timezone info given an index.
   */
  const TimezoneInfo& from_index(const size_t index) const {
    // Validate index
    if (index < 1 || index > timezones_.size()) {
      LOG_ERROR("Timezone index " + std::to_string(index) + " is out of range");
      return timezones_[0];
    }
    return timezones_[index - 1];
  }

protected:
  std::vector<TimezoneInfo> timezones_;
};

/**
 * Get the timezone database singleton
 * @return  timezone database
 */
const TimezoneDB& GetTimezoneDB();

/**
 * Convenience method to convert HH:MM::SS string into seconds.
 * @param str  Time string with form HH:MM::SS
 * @return Returns the number of seconds.
 */
int32_t timestring_to_seconds(const std::string& str);

/**
 * Get the iso date time from seconds since epoch and timezone.
 * @param   is_depart_at        is this a depart at or arrive by
 * @param   origin_seconds      seconds since epoch for origin
 * @param   dest_seconds        seconds since epoch for dest
 * @param   origin_tz           timezone for origin
 * @param   dest_tz             timezone for dest
 * @param   iso_origin          origin string that will be updated
 * @param   iso_dest            dest string that will be updated
 */
void seconds_to_date(const bool is_depart_at,
                     const uint64_t origin_seconds,
                     const uint64_t dest_seconds,
                     const TimezoneInfo& origin_tz,
                     const TimezoneInfo& dest_tz,
                     std::string& iso_origin,
                     std::string& iso_dest);

/**
 * Checks if a date is restricted within a begin and end range.
 * @param   type          type of restriction kYMD or kNthDow
 * @param   begin_hrs     begin hours
 * @param   begin_mins    begin minutes
 * @param   end_hrs       end hours
 * @param   end_mins      end minutes
 * @param   dow           days of the week to apply this restriction
 * @param   begin_week    only set for kNthDow.  which week in the month
 * @param   begin_month   begin month
 * @param   begin_day_dow if kNthDow, then which dow to start the restriction.
 *                        if kYMD then it is the day of the month
 * @param   end_week      only set for kNthDow.  which week in the month
 * @param   end_month     end month
 * @param   end_day_dow   if kNthDow, then which dow to end the restriction.
 *                        if kYMD then it is the day of the month
 * @param   current_time  seconds since epoch
 * @param   time_zone     timezone for the date_time
 * @return true or false
 */
bool is_restricted(const bool type,
                   const uint8_t begin_hrs,
                   const uint8_t begin_mins,
                   const uint8_t end_hrs,
                   const uint8_t end_mins,
                   const uint8_t dow,
                   const uint8_t begin_week,
                   const uint8_t begin_month,
                   const uint8_t begin_day_dow,
                   const uint8_t end_week,
                   const uint8_t end_month,
                   const uint8_t end_day_dow,
                   const uint64_t current_time,
                   const TimezoneInfo& time_zone);

/**
 * Convert std::tm into ISO date time string ((YYYY-MM-DDThh:mm)
 * @param  tm  std::tm time structure
 * @return Returns ISO date time string.
 */
static std::string tm_to_iso(const std::tm& t) {
  char iso[18];
  std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M", &t);
  return std::string(iso);
}

/**
 * Convert ISO 8601 time into std::tm.
 * @param iso  ISO time string (YYYY-MM-DDThh:mm)
 * @return Returns std::tm time structure. If the input string is not valid this method
 *         sets tm_year to 0.
 */
static std::tm iso_to_tm(const std::string& iso) {
  // Create an invalid tm, then populate it from the ISO string using get_time
  std::tm t = {0, -1, -1, -1, -1, 0, 0, 0};

  // Check for invalid string (not the right separators and sizes)
  if (iso.size() != 16 || iso.at(4) != '-' || iso.at(7) != '-' || iso.at(10) != 'T' ||
      iso.at(13) != ':') {
    return t;
  }

  std::istringstream ss(iso);
  ss.imbue(std::locale("C"));
  ss >> std::get_time(&t, "%Y-%m-%dT%H:%M");

  // Validate fields. Set tm_year to 0 if any of the year,month,day,hour,minute are invalid.
  if (t.tm_year > 200 || t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 0 || t.tm_mday > 31 ||
      t.tm_hour < 0 || t.tm_hour > 23 || t.tm_min < 0 || t.tm_min > 59) {
    t.tm_year = 0;
  }
  return t;
}

/**
 * Checks if string is in the format of %Y-%m-%dT%H:%M
 * @param   date_time should be in the format of 2015-05-06T08:00
 * @return true or false
 */
static bool is_iso_valid(const std::string& date_time) {
  return iso_to_tm(date_time).tm_year > 0;
}

/**
 * Get the current time and return date, time in ISO format.
 * @param   time_zone        Timezone.
 * @return  Returns the formated date YYYY-MM-DDThh:mm.
 */
static std::string get_local_datetime(const TimezoneInfo& time_zone) {
  // Get the current time
  std::time_t t = std::time(nullptr);
  auto tm = std::gmtime(&t);

  // TODO timezone logic

  // Convert to ISO date time string
  return tm_to_iso(*tm);
}

/**
 * Get the day of the week given a time string
 * @param dt Date time string.
 */
static uint32_t day_of_week(const std::string& dt) {
  // Get the std::tm struct given the ISO string
  std::tm t = iso_to_tm(dt);

  // Use std::mktime to fill in day of week
  std::mktime(&t);
  return t.tm_wday;
}

/**
 * Get the number of seconds elapsed from midnight. Hours can be greater than 24
 * to allow support for transit schedules. See GTFS spec:
 * https://developers.google.com/transit/gtfs/reference#stop_times_fields
 * @param   date_time in format 2015-05-06T08:00
 * @return  Returns the seconds from midnight.
 */
static uint32_t seconds_from_midnight(const std::string& date_time) {
  std::string str;
  std::size_t found = date_time.find('T'); // YYYY-MM-DDTHH:MM
  if (found != std::string::npos) {
    str = date_time.substr(found + 1);
  } else {
    str = date_time;
  }

  return timestring_to_seconds(str);
}

/**
 * Returns seconds of week within the range [0, kSecondsPerWeek]
 * @param  secs  Seconds within the week.
 * @return Returns the seconds within the week within the valid range.
 */
static int32_t normalize_seconds_of_week(const int32_t secs) {
  if (secs < 0) {
    return secs + midgard::kSecondsPerWeek;
  } else if (secs > midgard::kSecondsPerWeek) {
    return secs - midgard::kSecondsPerWeek;
  } else {
    return secs;
  }
}

/**
 * Get the number of days elapsed from the pivot date until the input date.
 * @param   date_time date time string (2015-05-06T08:00)
 * @return  Returns the number of days. Returns 0 if the date is prior to
 *          the pivot date.
 */
static uint32_t days_from_pivot_date(const std::string& date_time) {
  // Seconds since epoch for the specified date time
  std::tm t = iso_to_tm(date_time);
  auto sec = std::mktime(&t);

  // Pivot date for transit schedules
  std::string kPivotDateStr("2014-01-01T00:00");
  std::tm pivot_tm = iso_to_tm(kPivotDateStr);
  auto pivot = std::mktime(&pivot_tm);
  return (sec > pivot) ? (sec - pivot) / midgard::kSecondsPerDay : 0;
}

/**
 * Get the dow mask.
 * @param   date_time in the format: 2015-05-06T08:00
 * @return  Returns the dow mask.
 */
static uint32_t day_of_week_mask(const std::string& date_time) {
  switch (day_of_week(date_time)) {
    case 0:
      return kSunday;
    case 1:
      return kMonday;
    case 2:
      return kTuesday;
    case 3:
      return kWednesday;
    case 4:
      return kThursday;
    case 5:
      return kFriday;
    case 6:
      return kSaturday;
    default:
      return kDOWNone;
  }
}

// TODO - seems like a better name is needed here? Really adds some time to
// a date_time string and formats the resulting time string (with a timezone offset)

/**
 * Add seconds to a date_time and return a ISO date_time string with timezone
 * offset appended.
 * @param   date_time in format YYYY-MM-DDThh:mm
 * @param   seconds   seconds to add to the date.
 * @param   tz        timezone information
 * @return  Returns ISO formatted string with timezone offset.
 */
static std::string
get_duration(const std::string& date_time, const uint32_t seconds, const TimezoneInfo& tz) {
  // Convert local date_time string to seconds from epoch
  auto t1 = iso_to_tm(date_time);
  auto n = std::mktime(&t1);

  // Add seconds elapsed time and GMT offset for the timezone.
  auto new_time = n + tz.gmt_offset_secs_ + seconds;

  // Form new ISO string
  auto t2 = std::gmtime(&new_time);
  return tm_to_iso(*t2) + tz.gmt_offset_;
}

/**
 * Get the difference between two timezones using the current time (seconds from epoch
 * so that DST can be take into account).
 * @param   seconds  seconds since epoch
 * @param   tz1      timezone at start
 * @param   tz2      timezone at end
 * @return Returns the seconds difference between the 2 timezones.
 */
static int timezone_diff(const uint64_t seconds, const TimezoneInfo& tz1, const TimezoneInfo& tz2) {
  // Get the GMT offset at timezone 1
  int gmt_offset1 = tz1.gmt_offset_secs_;

  // Get the GMT offset at timezone 2
  int gmt_offset2 = tz2.gmt_offset_secs_;

  // TODO - resolve any DST differences (determine if the timezones have
  // DST in effect based on the seconds since epoch).

  // Returns the difference
  return gmt_offset2 - gmt_offset1;
}

/**
 * Get the seconds from epoch for a date_time string
 * @param   date_time   date_time.
 * @param   time_zone   Timezone.
 * @return  Returns the seconds from epoch.
 */
static uint64_t seconds_since_epoch(const std::string& date_time, const TimezoneInfo& time_zone) {
  auto t1 = iso_to_tm(date_time);
  auto n = std::mktime(&t1);

  // Adjust based on timezone?
  // What timezone is the original date_time string in?
  return n;
}

} // namespace DateTime
} // namespace baldr
} // namespace valhalla
#endif // VALHALLA_BALDR_DATETIME_H_
