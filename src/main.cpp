#include <Arduino.h>
#include <main.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <logo_kra-tech.h>
#include <Timemark.h>
#include <utils.h>
#include <Button_KRA.h>
#include <secrets.h> // WiFi keys, API keys etc
#include <ca_cert.h> // server cert for validation by https client

ShellyEM_Data_struct EMData;          // Holds latest data from Shelly API
LCD_state_struct LCD_state;           // Display runtime settings

Timemark tm_ClearDisplay(60000);      // for clearing left-over chars ("just-in-case")
Timemark tm_UpdateDisplay(500);       // for redrawing display
Timemark tm_ShiftLastLine(2000);      // for changing content of bottom line
Timemark tm_UpdateShellyData(4000);   // for reading data from Shelly cloud API (API spec say max 1 poll/s!)  

TFT_eSPI tft = TFT_eSPI(135, 240);    // Invoke custom library

volatile int interruptCounter;

Button BtnUP(PIN_SW_UP, 25U, true, true); // No pullup on this pin, enable internal
Button BtnDOWN(PIN_SW_DOWN, 25U, true, true); // No pullup on this pin, enable internal

enum Timers { TM_ClearDisplay, TM_UpdateDisplay, TM_ShiftLastLine, TM_UpdateShellyData };
Timemark *Timers[4] = { &tm_ClearDisplay, &tm_UpdateDisplay, &tm_ShiftLastLine, &tm_UpdateShellyData };

enum Pages { P1 = 0, P2 = 1, P3 = 2 };
#define LAST_PAGE 2U
uint8_t CurrPage = 0;     // index of current page number to show
uint8_t CurrLastLine = 0; // keeps index of what content to show on last line (toggle by tm_ToggleLastLine timer)

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t * timer = NULL;

/**
 * Timer ISR 1ms
 */
void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  interruptCounter++;
  portEXIT_CRITICAL_ISR(&timerMux);

  BtnDOWN.read();
  if(BtnDOWN.isPressed() && BtnDOWN.pressedFor(5000)) {
    ESP.restart();
  }
  if(BtnDOWN.wasReleased()) {
    if(CurrPage > 0) {
      CurrPage--;
    } else {
      CurrPage = LAST_PAGE;
    }
    LCD_state.clear = 1; // page change, tell UpdateDisplay() to clear before drawing
  }

  BtnUP.read();
  if(BtnUP.wasReleased()) {
    if(CurrPage < LAST_PAGE) {
      CurrPage++;
    } else {
      CurrPage = 0;
    }
    LCD_state.clear = 1; // page change, tell UpdateDisplay() to clear before drawing
  }

  if(interruptCounter == 100) {
    // every 100ms
    interruptCounter = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("initializing...");
  Serial.printf("FW version: %s\n", FPSTR(FIRMWARE_VERSION));
  Serial.println("Made by Ken-Roger Andersen, 2024");

  BtnUP.begin();    // This also sets pinMode
  BtnDOWN.begin();  // This also sets pinMode

  tft.init();
  tft.setRotation(3); // 1
  tft.fillScreen(LCD_state.bgcolor);
  tft.setTextColor(LCD_state.fgcolor);
  tft.setCursor(0, 0);
  tft.setTextDatum(MC_DATUM);

  // display "test"
  tft.fillScreen(TFT_RED);
  delay(200);
  tft.fillScreen(TFT_BLUE);
  delay(200);
  tft.fillScreen(TFT_GREEN);
  delay(200);

  // show my logo at startup
  tft.setSwapBytes(true);
  tft.pushImage(0, 0,  240, 135, kra_tech);
  delay(6000);

  tft.setTextWrap(true);

  tft.setTextSize(3);
  tft.fillScreen(LCD_state.bgcolor);
  tft.setCursor(0,30);

  tft.println("   Init...");
  tft.setTextSize(2);
  delay(700);

  connectWifi();
  tft.fillScreen(LCD_state.bgcolor);
  tft.setCursor(0,60);
  tft.printf("Connected to %s", WiFi.SSID().c_str());
  delay(500);
  
  LCD_state.clear = 1;
  updateDisplay();

  Timers[TM_ClearDisplay]->start();
  Timers[TM_UpdateDisplay]->start();
  Timers[TM_UpdateShellyData]->start();
  Timers[TM_ShiftLastLine]->start();
  
  //start Timer ISR
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000, true); // every 1 ms. (1000 microseconds)
  timerAlarmEnable(timer);  //Enable
}

/**
 * @brief Connect to WiFi ssid1, if failing try backup ssid2
 * 
 */
void connectWifi() {
  Serial.printf("[WiFi] Trying SSID1: %s", ssid1);
  if(!connectToSSID(ssid1, password1)) {
    Serial.printf("[WiFi] Trying SSID2: %s", ssid2);
    if(!connectToSSID(ssid2, password2)) {
      Serial.println("[WiFi] Failed to connect to WiFi!");
      tft.setCursor(0, 0); tft.fillScreen(LCD_state.bgcolor);
      tft.println("Failed to connect to WiFi!");  
      tft.println("Rebooting in 30 secs...");  

      // Use disconnect function to force stop trying to connect
      WiFi.disconnect();
      for(int d=0; d<60; d++) { delay(500); }
      ESP.restart();
    }
  }

}

/**
 * @brief Connect to specific WiFi SSID 
 * returns 1 on success, 0 on failure
 * @param ssid 
 * @param password 
 * @return int 
 */
int connectToSSID(const char *ssid, const char *password) {
  Serial.println();
  Serial.print("[WiFi] Connecting to ");
  Serial.println(ssid);
  tft.setCursor(0,60);
  tft.println("Connecting WiFi...");
  tft.printf("SSID: %s           \n", ssid);

  WiFi.begin(ssid, password);
  WiFi.setAutoReconnect(true);

  // Will try for about 20 seconds
  int tryDelay = 2000;
  int numberOfTries = 10;
  
  // Wait for the WiFi event
  while(true) {
    switch (WiFi.status()) {
      case WL_NO_SSID_AVAIL: 
        Serial.println("[WiFi] SSID not found"); 
        tft.setCursor(0, 0); tft.fillScreen(LCD_state.bgcolor);
        tft.printf("WiFi SSID not found: %s\n", ssid);  
        break;
      case WL_CONNECT_FAILED:
        Serial.print("[WiFi] Failed - WiFi not connected! Reason: ");
        break;
      case WL_CONNECTION_LOST: Serial.println("[WiFi] Connection was lost"); break;
      case WL_SCAN_COMPLETED:  Serial.println("[WiFi] Scan is completed"); break;
      case WL_DISCONNECTED:    Serial.println("[WiFi] WiFi is disconnected"); break;
      case WL_CONNECTED:
        tft.setCursor(0, 0); tft.fillScreen(LCD_state.bgcolor);
        tft.printf("WiFi connected!\nIP: %s\n", WiFi.localIP().toString().c_str());  
        Serial.println("[WiFi] WiFi is connected!");
        Serial.print("[WiFi] IP address: ");
        Serial.println(WiFi.localIP());
        return 1;
        break;
      default:
        Serial.print("[WiFi] WiFi Status: ");
        Serial.println(WiFi.status());
        break;
    }
    delay(tryDelay);

    if (numberOfTries <= 0) {
      Serial.print("[WiFi] Failed to connect to WiFi!");
      // Use disconnect function to force stop trying to connect
      WiFi.disconnect();
      return 0;
    } else {
      numberOfTries--;
    }
  }
}

/**
 * @brief Program loop
 * 
 */
void loop() {
  
  if(!WiFi.status == WL_CONNECTED) {
    LCD_state.fgcolor = TFT_RED;
    LCD_state.bgcolor = TFT_WHITE;
    tft.fillScreen(LCD_state.bgcolor);
    tft.setTextColor(LCD_state.fgcolor, LCD_state.bgcolor);
    tft.setTextWrap(false);
    tft.setCursor(0, 50);
    tft.setTextSize(3);
    tft.printf("NO WIFI");  
    delay(2000);
    connectWifi();
    return;
  }

  // "just in case", periodically clear any left-over chars from display
  if(Timers[TM_ClearDisplay]->expired()) {
    LCD_state.bgcolor = BG_COLOR;
    tft.setTextColor(LCD_state.fgcolor, LCD_state.bgcolor);    
    tft.fillScreen(LCD_state.bgcolor);
    LCD_state.clear = 0;
  }

  // Time to redraw display?
  if(Timers[TM_UpdateDisplay]->expired()) {
    updateDisplay();
  }

  // Time to read data from Shelly API?
  if(Timers[TM_UpdateShellyData]->expired()) {      
    WiFiClientSecure *client = new WiFiClientSecure;
    if (client) {
      
      #if IGNORE_SSL_CERT
      client->setInsecure();
      #else
      client->setCACert(rootCACertificate);
      #endif
      {
          // Add a scoping block for HTTPClient https to make sure it is destroyed before NetworkClientSecure *client is
          HTTPClient https;              
          if (https.begin(*client, shelly_api_url)) {
          
            https.addHeader("Content-Type", "application/x-www-form-urlencoded");
      
            const int URL_size = strlen(shelly_em_id) + strlen(shelly_api_key) +14; // total size of URL (included "id=&auth_key=")
            char httpRequestData[URL_size]; 
            snprintf(httpRequestData, URL_size, "id=%s&auth_key=%s", shelly_em_id, shelly_api_key);
            Serial.printf("URL: %s\n", httpRequestData);

            int httpResponseCode = https.POST(httpRequestData); // -11 on connection error
            
            String json = https.getString();
            
            // Shelly API returns JSON string, we parse it using ArduinoJson library
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, json);
            https.end();

            Serial.printf("HTTP Response code: %d\n", httpResponseCode);
            if(httpResponseCode != 200) {
              Serial.printf("HTTP failed, code %d", httpResponseCode);
              char msg[30];
              sprintf(msg, "HTTP request returned %d", httpResponseCode);
              showErrorMessage(msg, 2000);
            }
            
            if (error) { // usually "EmptyInput" if no data returned
                Serial.print(json);
                Serial.print(F("\n^JSON_ERR: "));
                Serial.println(error.f_str());
                showErrorMessage("JSON error", 2000);
            } else {
              Serial.print("JSON doc = ");
              serializeJson(doc, Serial);
              
              EMData.isok = doc["isok"];
              if(doc["data"]["device_status"]["serial"].as<uint32_t>() > EMData.serial) {
                EMData.haschanged = true;
              }
              
              EMData.serial = doc["data"]["device_status"]["serial"];
              EMData.online = doc["data"]["online"];
              EMData.cloud_connected = doc["data"]["device_status"]["cloud"]["connected"];
              if(!doc["data"]["device_status"]["time"].isNull()) {
                strcpy(EMData.time, doc["data"]["device_status"]["time"]);
              }
              EMData.power = doc["data"]["device_status"]["emeters"][0]["power"];
              EMData.voltage = doc["data"]["device_status"]["emeters"][0]["voltage"];
              EMData.amperage = EMData.power / EMData.voltage;
              Serial.printf("\npower=%d, voltage=%02f\n", EMData.power, EMData.voltage);
            }

          } else {
            Serial.printf("[HTTPS] Unable to connect\n");
            showErrorMessage("HTTPS conn failed", 5000);
          }
          // End extra scoping block
        }
        delete client;
    } else {
      Serial.println("Unable to create client");
      showErrorMessage("No HTTPClient, rebooting", 5000);
      ESP.restart();
    }
  } // TM_UpdateShellyData
}

void updateDisplay() {
    tft.setTextWrap(false);

    if(LCD_state.clear) {
      LCD_state.bgcolor = BG_COLOR;
      tft.fillScreen(LCD_state.bgcolor);
      LCD_state.clear = 0;
    }
    
    switch(CurrPage) {
      case P1:
        LCD_state.fgcolor = TFT_GOLD;
        tft.setTextColor(LCD_state.fgcolor, LCD_state.bgcolor);
        tft.setCursor(0, 0);
        tft.setTextSize(3);
        tft.printf("Dina's House:\n");  
        LCD_state.fgcolor = FG_COLOR;

        tft.setCursor(0, 35);
        if(EMData.power >= POWER_LIMIT) {
          LCD_state.fgcolor = TFT_RED;
        }   
        tft.setTextColor(LCD_state.fgcolor, LCD_state.bgcolor);    
        tft.printf("%uW", EMData.power);
        LCD_state.fgcolor = FG_COLOR;
        tft.setTextColor(LCD_state.fgcolor, LCD_state.bgcolor);
        tft.printf(" %d.%01d V\n", (int)EMData.voltage, (int)(EMData.voltage*10)%10 );
        tft.setCursor(0, 68);
        tft.printf("%d.%01dA  ", (int)EMData.amperage, (int)(EMData.amperage*10)%10 );
      break;
      case P2:
        tft.setCursor(0, 0);
        tft.setTextSize(3);
        tft.printf("ShellyEM:    \n");  
        tft.setTextSize(2);
        tft.printf("ID: %s\n", shelly_em_id);
        tft.printf("Data SN: %u\n", EMData.serial);
        tft.printf("IsOK: %u\n", EMData.isok);
        tft.printf("Online: %u\n", EMData.online);
        tft.printf("CloudConnected: %u\n", EMData.cloud_connected);
        tft.printf("Time: %s\n", EMData.time);
      break;
      case P3: // Not implemented yet
        tft.setCursor(0, 70);
        tft.setTextSize(3);
        tft.printf("PAGE 3");
      break;
    }

    // Handle the bottom line with shifting content
    tft.setTextSize(2);
    LCD_state.fgcolor = TFT_WHITE;
    LCD_state.bgcolor = TFT_BLACK;
    tft.setTextColor(LCD_state.fgcolor, LCD_state.bgcolor);

    if(Timers[TM_ShiftLastLine]->expired()) {
      if(++CurrLastLine > 4) {
        CurrLastLine = 0;
      }      
      tft.setCursor(0, 122); // bottom line with size 2
      for(uint8_t y=0; y<20; y++) tft.printf(" "); // line shift, clear line
    }

    tft.setCursor(0, 122);

    switch(CurrLastLine) {
      case 0: tft.printf("ID: %u (v%s)  \n",  (uint16_t)(ESP.getEfuseMac()>>32), FIRMWARE_VERSION); break;
      case 1: tft.printf("Uptime: %s\n", SecondsToDateTimeString(millis()/1000, TFMT_HOURS)); break;
      case 2: tft.printf("RSSI:%d SSID:%s \n", WiFi.RSSI(), WiFi.SSID().c_str()); break;
      case 3: tft.printf("Free mem: %u B\n", ESP.getFreeHeap()); break;
      case 4: tft.printf("IP: %s\n", WiFi.localIP().toString().c_str()); break;
    }
    EMData.haschanged = false;
}

/**
 * @brief Display red/white message on display
 * 
 * @param text 
 * @param duration_ms 
 */
void showErrorMessage(const char *text, uint16_t duration_ms) {
  LCD_state.fgcolor = TFT_RED;
  LCD_state.bgcolor = TFT_WHITE;

  tft.fillScreen(LCD_state.bgcolor);
  tft.setTextColor(LCD_state.fgcolor, LCD_state.bgcolor);
  tft.setTextWrap(true);
  tft.setCursor(0, 0);
  tft.setTextSize(3);
  tft.printf("%s", text);  
  tft.setTextWrap(false);
  delay(duration_ms);
  // Restore colors
  LCD_state.fgcolor = FG_COLOR;
  LCD_state.bgcolor = BG_COLOR;
  tft.setTextColor(LCD_state.fgcolor, LCD_state.bgcolor);
  tft.fillScreen(LCD_state.bgcolor);
}