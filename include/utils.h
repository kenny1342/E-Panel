#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <cstdint>

enum TIME_STRING_FORMATS { TFMT_LONG, TFMT_DATETIME, TFMT_HOURS };
char * SecondsToDateTimeString(uint32_t seconds, uint8_t format);

#endif