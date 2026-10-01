#include "FooterText.h"

#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "CardText.h"

namespace sleepcards::footer {

size_t expand(const char* tmpl, const Field* fields, const size_t count, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  size_t len = 0;
  const auto put = [&](const char* s, const size_t n) {
    for (size_t i = 0; i < n && len + 1 < cap; i++) out[len++] = s[i];
  };
  for (const char* p = tmpl ? tmpl : ""; *p != '\0';) {
    if (*p == '{') {
      const char* close = std::strchr(p + 1, '}');
      const Field* match = nullptr;
      if (close != nullptr) {
        const size_t nameLen = static_cast<size_t>(close - p - 1);
        for (size_t f = 0; f < count && match == nullptr; f++) {
          if (std::strlen(fields[f].name) == nameLen && std::strncmp(fields[f].name, p + 1, nameLen) == 0) {
            match = &fields[f];
          }
        }
      }
      if (match != nullptr) {
        const char* value = match->value ? match->value : "";
        put(value, std::strlen(value));
        p = close + 1;
        continue;
      }
    }
    put(p, 1);
    p++;
  }
  out[len] = '\0';
  return len;
}

size_t formatUpdated(const LocalDate& when, const bool clock12h, const Form form, char* out, const size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  char time[16];
  formatHourMinute(when.hour, when.minute, clock12h, time, sizeof(time));
  char day[4];
  std::snprintf(day, sizeof(day), "%d", when.day);
  const Field dateFields[] = {
      {"weekday", weekdayShortName(when.weekday)}, {"month", monthShortName(when.month)}, {"day", day}};
  char date[48];
  expand(form == Form::Full ? tr(STR_FOOTER_DATE_WEEKDAY) : tr(STR_FOOTER_DATE), dateFields, 3, date, sizeof(date));
  const Field lineFields[] = {{"time", time}, {"date", date}};
  const char* sentence = tr(STR_SCREEN_UPDATED);
  if (form == Form::Short) sentence = tr(STR_UPDATED_AT_DATE);
  if (form == Form::TimeOnly) sentence = tr(STR_UPDATED_AT);
  return expand(sentence, lineFields, 2, out, cap);
}

Form fitUpdated(const LocalDate& when, const bool clock12h, const int maxWidth, const WidthFn width, void* user,
                char* out, const size_t cap) {
  for (uint8_t f = 0; f < static_cast<uint8_t>(Form::Count); f++) {
    const auto form = static_cast<Form>(f);
    formatUpdated(when, clock12h, form, out, cap);
    if (form == Form::TimeOnly || width == nullptr || width(user, out) <= maxWidth) return form;
  }
  return Form::TimeOnly;
}

}  // namespace sleepcards::footer
