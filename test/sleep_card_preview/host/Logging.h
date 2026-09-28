#pragma once

// Host logging: errors go to stderr (a card preview should never print one),
// info and debug are dropped.
#include <cstdio>

#define LOG_ERR(origin, format, ...) std::fprintf(stderr, "[ERR] [%s] " format "\n", origin, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) ((void)0)
#define LOG_DBG(origin, format, ...) ((void)0)
