#include <cstdint>
#include <TFT_eSPI.h>

#define FIRMWARE_VERSION "1.08"
#define IGNORE_SSL_CERT 1   // should be 0 in a production environment, see ca_cert.h
#define POWER_LIMIT 1000    // Watt limit for text to turn RED
#define FG_COLOR TFT_GREEN  // Default foreground color
#define BG_COLOR TFT_BLACK  // Default background color

#define PIN_SW_DOWN         35 // (IO35)
#define PIN_SW_UP           0 //(IO0)

struct LCD_state_struct {
    bool clear = true; // flag to clear display
    uint16_t bgcolor = BG_COLOR;
    uint16_t fgcolor = FG_COLOR;    
} ;

struct ShellyEM_Data_struct {
    bool haschanged;
    bool isok;
    bool online;
    uint32_t serial;  // serial of current dataset, increments at every cloud update
    char time[20];    // hh:mm, TODO: read unixtimestamp and convert to human readable
    uint16_t power;
    float voltage;
    float amperage;
    bool cloud_connected;
} ;

void connectWifi();
int connectToSSID(const char *ssid, const char *password);
void updateDisplay();
void showErrorMessage(const char *text, uint16_t duration_ms);