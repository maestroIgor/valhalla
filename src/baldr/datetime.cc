#include <algorithm>
#include <bitset>
#include <fstream>
#include <iostream>
#include <sstream>

#include "baldr/datetime.h"
#include "baldr/graphconstants.h"
#include "baldr/timedomain.h"
#include "midgard/logging.h"

#include "date_time_zonespec.h"

using namespace valhalla::baldr;

namespace valhalla {
namespace baldr {
namespace DateTime {

// Get the raw timezone spec
std::string get_timezone_csv() {
  return std::string(date_time_zonespec_csv, date_time_zonespec_csv + date_time_zonespec_csv_len);
}

TimezoneDB::TimezoneDB() {
  // Load timezone info from CSV
  std::string tz_data(date_time_zonespec_csv, date_time_zonespec_csv + date_time_zonespec_csv_len);
  std::stringstream ss(tz_data);

  // Parse each line into a TimezoneInfo struct
  std::string buf;
  while (std::getline(ss, buf)) {
    // Split into tokens
    int i = 0;
    std::stringstream ss2(buf);
    std::string item;
    TimezoneInfo tz;
    while (std::getline(ss2, item, ',')) {
      // Strip quotes
      std::string v;
      if (item[0] == '"') {
        v = item.substr(1, std::string::npos);
      }
      if (v.back() == '"') {
        v.pop_back();
      }
      switch (i) {
        case 0:
          tz.id_ = v;
          break;
        case 1:
          tz.abbrev_ = v;
          break;
        case 2:
          tz.name_ = v;
          break;
        case 3:
          tz.dst_abbrev_ = v;
          break;
        case 4:
          tz.dst_name_ = v;
          break;
        case 5:
          // Store GMT offset in seconds
          tz.gmt_offset_secs_ = timestring_to_seconds(v);

          // Remove seconds - all GMT offsets have them but they
          // are all 0.
          v.pop_back();
          v.pop_back();
          v.pop_back();
          tz.gmt_offset_ = v;
          break;
        case 6:
          tz.dst_adjust_ = v;
          break;
        case 7:
          tz.start_date_ = v;
          break;
        case 8:
          tz.start_time_ = v;
          break;
        case 9:
          tz.end_date_ = v;
          break;
        case 10:
          tz.end_time_ = v;
          break;
        default:
          LOG_ERROR("Too many entries in Timezone info " + buf);
      }
      ++i;
    }
    timezones_.emplace_back(std::move(tz));
  }
}

const TimezoneDB& GetTimezoneDB() {
  // thread safe static initialization of global singleton
  static const TimezoneDB tz_db;
  return tz_db;
}

// Convenience method to convert HH:MM::SS string into seconds. Supports
// + or - as the first character.
int32_t timestring_to_seconds(const std::string& str) {
  // Check if first char is a sign (+ or -)
  bool negate = false;
  size_t idx = 0;
  if (str[0] == '-') {
    idx = 1;
    negate = true;
  } else if (str[0] == '+') {
    idx = 1;
  }

  int secs = 0;
  int multiplier = static_cast<int>(midgard::kSecondsPerHour);
  std::string s = str.substr(idx, std::string::npos);
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ':')) {
    if (negate) {
      secs -= std::stoi(item) * multiplier;
    } else {
      secs += std::stoi(item) * multiplier;
    }
    multiplier = (multiplier == midgard::kSecondsPerHour) ? midgard::kSecondsPerMinute : 1;
  }
  return secs;
}

/**
// NOTE - get_ldt is only used within DateTime methods here...
// get a local_date_time with support for dst.
// 2016-11-06T02:00 ---> 2016-11-06T01:00
boost::local_time::local_date_time get_ldt(const boost::gregorian::date& date,
                                           const boost::posix_time::time_duration& time_duration,
                                           const boost::local_time::time_zone_ptr& time_zone) {

  boost::posix_time::time_duration td = time_duration;
  boost::local_time::local_date_time
      in_local_time(date, td, time_zone, boost::local_time::local_date_time::NOT_DATE_TIME_ON_ERROR);

  // create not-a-date-time if invalid (eg: in dst transition)
  if (in_local_time.is_not_a_date_time()) {

    if (time_zone->dst_local_start_time(date.year()).date() == date) {
      td += time_zone->dst_offset(); // clocks ahead.
      in_local_time = boost::local_time::
          local_date_time(date, td, time_zone,
                          boost::local_time::local_date_time::NOT_DATE_TIME_ON_ERROR);
    } else {
      // Daylight Savings Results are ambiguous: time given: 2016-Nov-06 01:00:00
      boost::posix_time::time_duration time_dur = time_zone->dst_offset();

      in_local_time = boost::local_time::
          local_date_time(date, td + time_dur, time_zone,
                          boost::local_time::local_date_time::NOT_DATE_TIME_ON_ERROR);
      in_local_time -= time_dur;
    }
  }
  return in_local_time;
}
**/

// NOTE - only used in TripPathBuilder. Always used along with seconds_since_epoch -
// seems that a more general method to accomplish the functionality needed by each
// method. Not sure what is_depart_at does and why both origin and dest strings are
// populate here? Could a more general method be created?
// Get the date from seconds and timezone.
void seconds_to_date(const bool is_depart_at,
                     const uint64_t origin_seconds,
                     const uint64_t dest_seconds,
                     const TimezoneInfo& origin_tz,
                     const TimezoneInfo& dest_tz,
                     std::string& iso_origin,
                     std::string& iso_dest) {
  /** TODO
    iso_origin = "";
    iso_dest = "";

    if (!origin_tz || !dest_tz) {
      return;
    }

    try {
      std::string tz_string;
      const boost::posix_time::ptime time_epoch(boost::gregorian::date(1970, 1, 1));
      boost::posix_time::ptime origin_pt = time_epoch + boost::posix_time::seconds(origin_seconds);
      boost::local_time::local_date_time origin_date_time(origin_pt, origin_tz);

      boost::posix_time::ptime dest_pt = time_epoch + boost::posix_time::seconds(dest_seconds);
      boost::local_time::local_date_time dest_date_time(dest_pt, dest_tz);

      boost::gregorian::date o_date = origin_date_time.local_time().date();
      boost::gregorian::date d_date = dest_date_time.local_time().date();

      if (is_depart_at && dest_date_time.is_dst()) {
        boost::gregorian::date dst_date = dest_tz->dst_local_end_time(d_date.year()).date();
        bool in_range = (o_date <= dst_date && dst_date <= d_date);

        if (in_range) { // in range meaning via the dates.
          if (o_date == dst_date) {
            // must start before dst end time - the offset otherwise the time is ambiguous
            in_range =
                origin_date_time.local_time().time_of_day() <
                (dest_tz->dst_local_end_time(d_date.year()).time_of_day() - dest_tz->dst_offset());

            if (in_range) {
              // starts and ends on the same day.
              if (o_date == d_date) {
                in_range = dest_tz->dst_local_end_time(d_date.year()).time_of_day() <=
                           dest_date_time.local_time().time_of_day();
              }
            }
          } else if (dst_date == d_date) {
            in_range = dest_tz->dst_local_end_time(d_date.year()).time_of_day() <=
                       dest_date_time.local_time().time_of_day();
          }
        }
        if (in_range) {
          dest_date_time -= dest_tz->dst_offset();
        }
      }

      if (!is_depart_at) {
        boost::gregorian::date dst_date = origin_tz->dst_local_end_time(o_date.year()).date();
        bool in_range = (o_date <= dst_date && dst_date <= d_date);

        if (in_range) { // in range meaning via the dates.
          if (o_date == dst_date) {
            // must start before dst end time
            in_range = origin_date_time.local_time().time_of_day() <=
                       (origin_tz->dst_local_end_time(o_date.year()).time_of_day());

            if (in_range) {
              // starts and ends on the same day.
              if (o_date == d_date) {
                in_range = origin_tz->dst_local_end_time(o_date.year()).time_of_day() >
                           dest_date_time.local_time().time_of_day();
              }
            }
          } else if (dst_date == d_date) {
            in_range = origin_tz->dst_local_end_time(o_date.year()).time_of_day() >
                       dest_date_time.local_time().time_of_day();
          }
        }

        if (in_range) {
          origin_date_time -= origin_tz->dst_offset();
        }
      }

      origin_pt = origin_date_time.local_time();
      boost::gregorian::date date = origin_pt.date();
      std::stringstream ss_time;
      ss_time << origin_pt.time_of_day();
      std::string time = ss_time.str();

      std::size_t found = time.find_last_of(':'); // remove seconds.
      if (found != std::string::npos) {
        time = time.substr(0, found);
      }

      ss_time.str("");
      if (origin_date_time.is_dst()) {
        ss_time << origin_tz->dst_offset() + origin_tz->base_utc_offset();
      } else {
        ss_time << origin_tz->base_utc_offset();
      }

      // positive tz
      if (ss_time.str().find('+') == std::string::npos &&
          ss_time.str().find('-') == std::string::npos) {
        iso_origin = to_iso_extended_string(date) + "T" + time + "+" + ss_time.str();
      } else {
        iso_origin = to_iso_extended_string(date) + "T" + time + ss_time.str();
      }

      found = iso_origin.find_last_of(':'); // remove seconds.
      if (found != std::string::npos) {
        iso_origin = iso_origin.substr(0, found);
      }

      dest_pt = dest_date_time.local_time();
      date = dest_pt.date();
      ss_time.str("");
      ss_time << dest_pt.time_of_day();
      time = ss_time.str();

      found = time.find_last_of(':'); // remove seconds.
      if (found != std::string::npos) {
        time = time.substr(0, found);
      }

      ss_time.str("");
      if (dest_date_time.is_dst()) {
        ss_time << dest_tz->dst_offset() + dest_tz->base_utc_offset();
      } else {
        ss_time << dest_tz->base_utc_offset();
      }

      // positive tz
      if (ss_time.str().find('+') == std::string::npos &&
          ss_time.str().find('-') == std::string::npos) {
        iso_dest = to_iso_extended_string(date) + "T" + time + "+" + ss_time.str();
      } else {
        iso_dest = to_iso_extended_string(date) + "T" + time + ss_time.str();
      }

      found = iso_dest.find_last_of(':'); // remove seconds.
      if (found != std::string::npos) {
        iso_dest = iso_dest.substr(0, found);
      }

    } catch (std::exception& e) {}
  **/
}

// does this date fall in the begin and end date range?
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
                   const TimezoneInfo& time_zone) {
  return false;
  /**
    bool dow_in_range = true;
    bool dt_in_range = false;

    try {
      boost::gregorian::date begin_date, end_date;
      boost::posix_time::time_duration b_td = boost::posix_time::hours(0),
                                       e_td = boost::posix_time::hours(23) +
                                              boost::posix_time::minutes(59);

      const boost::posix_time::ptime time_epoch(boost::gregorian::date(1970, 1, 1));
      boost::posix_time::ptime origin_pt = time_epoch + boost::posix_time::seconds(current_time);
      boost::local_time::local_date_time in_local_time(origin_pt, time_zone);
      boost::gregorian::date d = in_local_time.date();
      boost::posix_time::time_duration td = in_local_time.local_time().time_of_day();

      // we have dow
      if (dow) {

        uint8_t local_dow = 0;
        switch (d.day_of_week()) {
          case boost::date_time::Sunday:
            local_dow = kSunday;
            break;
          case boost::date_time::Monday:
            local_dow = kMonday;
            break;
          case boost::date_time::Tuesday:
            local_dow = kTuesday;
            break;
          case boost::date_time::Wednesday:
            local_dow = kWednesday;
            break;
          case boost::date_time::Thursday:
            local_dow = kThursday;
            break;
          case boost::date_time::Friday:
            local_dow = kFriday;
            break;
          case boost::date_time::Saturday:
            local_dow = kSaturday;
            break;
          default:
            return false; // should never happen
            break;
        }
        dow_in_range = (dow & local_dow);
      }

      uint8_t b_month = begin_month;
      uint8_t e_month = end_month;
      uint8_t b_day_dow = begin_day_dow;
      uint8_t e_day_dow = end_day_dow;
      uint8_t b_week = begin_week;
      uint8_t e_week = end_week;

      if (type == kNthDow && begin_week && !begin_day_dow && !begin_month) { // Su[-1]
        b_month = d.month().as_enum();
      }
      if (type == kNthDow && end_week && !end_day_dow && !end_month) { // Su[-1]
        e_month = d.month().as_enum();
      }

      if (type == kNthDow && begin_week && !begin_day_dow && !begin_month && !end_week &&
          !end_day_dow && !end_month) { // only Su[-1] set in begin.
        // First Sunday of every month only.
        e_month = b_month;
        b_day_dow = e_day_dow = dow;
        e_week = b_week;
      } else if (type == kYMD && (b_month && e_month) &&
                 (!b_day_dow && !e_day_dow)) { // Sep-Jun We 08:15-08:45

        b_day_dow = 1;
        boost::gregorian::date e_d = boost::gregorian::date(d.year(), e_month, 1);
        e_day_dow = e_d.end_of_month().day();
      }

      // month only
      if (type == kYMD && (b_month && e_month) && (!b_day_dow && !e_day_dow && !b_week && !b_week) &&
          b_month == e_month) {

        dt_in_range = (b_month <= d.month().as_enum() && d.month().as_enum() <= e_month);

        if (begin_hrs || begin_mins || end_hrs || end_mins) {
          b_td = boost::posix_time::hours(begin_hrs) + boost::posix_time::minutes(begin_mins);
          e_td = boost::posix_time::hours(end_hrs) + boost::posix_time::minutes(end_mins);
        }

        dt_in_range = (dt_in_range && (b_td <= td && td <= e_td));
        return (dow_in_range && dt_in_range);

      } else if (type == kYMD && b_month && b_day_dow) {

        uint32_t e_year = d.year(), b_year = d.year();
        if (b_month == e_month) {
          if (b_day_dow > e_day_dow) { // Mar 15 - Mar 1
            e_year = d.year() + 1;
          }
        } else if (b_month > e_month) { // Oct 10 - Mar 3
          if (b_month > d.month().as_enum()) {
            b_year = d.year() - 1;
          } else {
            e_year = d.year() + 1;
          }
        }

        begin_date = boost::gregorian::date(b_year, b_month, b_day_dow);
        end_date = boost::gregorian::date(e_year, e_month, e_day_dow);

      } else if (type == kNthDow && b_month && b_day_dow && e_month &&
                 e_day_dow) { // kNthDow types can have a mix of ymd and nthdow. (e.g. Dec Su[-1]-Mar
                              // 3 Sat 15:00-17:00)

        uint32_t e_year = d.year(), b_year = d.year();
        if (b_month == e_month) {
          if (b_day_dow > e_day_dow) { // Mar 15 - Mar 1
            e_year = d.year() + 1;
          }
        } else if (b_month > e_month) { // Oct 10 - Mar 3
          if (b_month > d.month().as_enum()) {
            b_year = d.year() - 1;
          } else {
            e_year = d.year() + 1;
          }
        }

        if (b_week && b_week <= 5) { // kNthDow
          boost::gregorian::nth_day_of_the_week_in_month
              nthdow(static_cast<boost::gregorian::nth_day_of_the_week_in_month::week_num>(b_week),
                     b_day_dow - 1, b_month);
          begin_date = nthdow.get_date(b_year);
        } else { // YMD
          begin_date = boost::gregorian::date(b_year, b_month, b_day_dow);
        }

        if (e_week && e_week <= 5) { // kNthDow
          boost::gregorian::nth_day_of_the_week_in_month
              nthdow(static_cast<boost::gregorian::nth_day_of_the_week_in_month::week_num>(e_week),
                     e_day_dow - 1, e_month);
          end_date = nthdow.get_date(e_year);
        } else {                                                         // YMD
          end_date = boost::gregorian::date(e_year, e_month, e_day_dow); // Dec 5 to Mar 3
        }
      } else { // do we have just time?

        if (begin_hrs || begin_mins || end_hrs || end_mins) {
          b_td = boost::posix_time::hours(begin_hrs) + boost::posix_time::minutes(begin_mins);
          e_td = boost::posix_time::hours(end_hrs) + boost::posix_time::minutes(end_mins);

          if (begin_hrs > end_hrs) { // 19:00 - 06:00
            dt_in_range = !(e_td <= td && td <= b_td);
          } else {
            dt_in_range = (b_td <= td && td <= e_td);
          }
        }
        return (dow_in_range && dt_in_range);
      }

      if (begin_hrs || begin_mins || end_hrs || end_mins) {
        b_td = boost::posix_time::hours(begin_hrs) + boost::posix_time::minutes(begin_mins);
        e_td = boost::posix_time::hours(end_hrs) + boost::posix_time::minutes(end_mins);
      }

      boost::local_time::local_date_time b_in_local_time = get_ldt(begin_date, b_td, time_zone);
      boost::local_time::local_date_time e_in_local_time = get_ldt(end_date, e_td, time_zone);

      dt_in_range = (b_in_local_time.date() <= in_local_time.date() &&
                     in_local_time.date() <= e_in_local_time.date());

      bool time_in_range = false;

      if (begin_hrs > end_hrs) { // 19:00 - 06:00
        time_in_range = !(e_td <= td && td <= b_td);
      } else {
        time_in_range = (b_td <= td && td <= e_td);
      }

      dt_in_range = (dt_in_range && time_in_range);
    } catch (std::exception& e) {}
    return (dow_in_range && dt_in_range);
  **/
}

} // namespace DateTime
} // namespace baldr
} // namespace valhalla
