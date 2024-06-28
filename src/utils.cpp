#include <Arduino.h>
#include <utils.h>
/**
 * @brief Build human-readable time string from Seconds
 * 
 * @param seconds 
 * @param format 
 * @return char* 
 */
char * SecondsToDateTimeString(uint32_t seconds, uint8_t format)
{
  time_t curSec;
  struct tm *curDate;
  static char dateString[32];
  
  curSec = time(NULL) + seconds;
  curDate = localtime(&curSec);

  switch(format) {    
    case TFMT_LONG: strftime(dateString, sizeof(dateString), "%A, %B %d %Y %H:%M:%S", curDate); break;
    case TFMT_DATETIME: strftime(dateString, sizeof(dateString), "%Y-%m-%d %H:%M:%S", curDate); break;
    case TFMT_UPTIME: 
    {
      int days = seconds / 86400;
      
      uint32_t t = seconds % 86400;
      int h = t / 3600;
      uint32_t t2 = t % 3600;
      int m = t2 / 60;
      int s = t2 % 60;

      sprintf(dateString, "%d days %02d:%02d:%02d", days, h, m, s);
    }
      break;

    default: strftime(dateString, sizeof(dateString), "%Y-%m-%d %H:%M:%S", curDate);
  }
  return dateString;
}

