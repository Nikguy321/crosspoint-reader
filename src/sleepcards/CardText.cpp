#include "CardText.h"

#include <I18n.h>

namespace sleepcards {
namespace {

constexpr StrId MONTHS[12] = {StrId::STR_MONTH_JANUARY, StrId::STR_MONTH_FEBRUARY, StrId::STR_MONTH_MARCH,
                              StrId::STR_MONTH_APRIL,   StrId::STR_MONTH_MAY,      StrId::STR_MONTH_JUNE,
                              StrId::STR_MONTH_JULY,    StrId::STR_MONTH_AUGUST,   StrId::STR_MONTH_SEPTEMBER,
                              StrId::STR_MONTH_OCTOBER, StrId::STR_MONTH_NOVEMBER, StrId::STR_MONTH_DECEMBER};
constexpr StrId MONTHS_SHORT[12] = {StrId::STR_MONTH_SHORT_JAN, StrId::STR_MONTH_SHORT_FEB, StrId::STR_MONTH_SHORT_MAR,
                                    StrId::STR_MONTH_SHORT_APR, StrId::STR_MONTH_SHORT_MAY, StrId::STR_MONTH_SHORT_JUN,
                                    StrId::STR_MONTH_SHORT_JUL, StrId::STR_MONTH_SHORT_AUG, StrId::STR_MONTH_SHORT_SEP,
                                    StrId::STR_MONTH_SHORT_OCT, StrId::STR_MONTH_SHORT_NOV, StrId::STR_MONTH_SHORT_DEC};
constexpr StrId WEEKDAYS[7] = {StrId::STR_WEEKDAY_SUNDAY,    StrId::STR_WEEKDAY_MONDAY,   StrId::STR_WEEKDAY_TUESDAY,
                               StrId::STR_WEEKDAY_WEDNESDAY, StrId::STR_WEEKDAY_THURSDAY, StrId::STR_WEEKDAY_FRIDAY,
                               StrId::STR_WEEKDAY_SATURDAY};
constexpr StrId WEEKDAYS_SHORT[7] = {StrId::STR_WEEKDAY_SHORT_SUN, StrId::STR_WEEKDAY_SHORT_MON,
                                     StrId::STR_WEEKDAY_SHORT_TUE, StrId::STR_WEEKDAY_SHORT_WED,
                                     StrId::STR_WEEKDAY_SHORT_THU, StrId::STR_WEEKDAY_SHORT_FRI,
                                     StrId::STR_WEEKDAY_SHORT_SAT};

}  // namespace

const char* monthName(const int month) { return month >= 1 && month <= 12 ? I18N.get(MONTHS[month - 1]) : ""; }
const char* monthShortName(const int month) {
  return month >= 1 && month <= 12 ? I18N.get(MONTHS_SHORT[month - 1]) : "";
}
const char* weekdayName(const int weekday) { return weekday >= 0 && weekday <= 6 ? I18N.get(WEEKDAYS[weekday]) : ""; }
const char* weekdayShortName(const int weekday) {
  return weekday >= 0 && weekday <= 6 ? I18N.get(WEEKDAYS_SHORT[weekday]) : "";
}

}  // namespace sleepcards
