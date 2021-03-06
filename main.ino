// WiFi enabled GPS NTP server - Cristiano Monteiro <cristianomonteiro@gmail.com> - 06.May.2021
// Based on the work of:
// Bruce E. Hall, W8BH <bhall66@gmail.com> - http://w8bh.net
// and
// https://forum.arduino.cc/u/ziggy2012/summary
// Satellite Dish by Creative Stall from the Noun Project

//Includes
#include <Arduino.h>
#include <U8g2lib.h>
#include <SoftwareSerial.h>
#include <TimeLib.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include <RtcDS3231.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <RtcDS3231.h>
#include <EepromAT24C32.h> // We will use clock's eeprom to store config

// GLOBAL DEFINES
#define PPS_PIN D6              // Pin on which 1PPS line is attached
#define SYNC_INTERVAL 10        // Time, in seconds, between GPS sync attempts
#define SYNC_TIMEOUT 30         // Time(sec) without GPS input before error
//#define RTC_UPDATE_INTERVAL    SECS_PER_DAY             // time(sec) between RTC SetTime events
#define RTC_UPDATE_INTERVAL 30  // Time(sec) between RTC SetTime events
#define PPS_BLINK_INTERVAL 50   // Set time pps led should be on for blink effect
#define LOCK_LED D3
#define PPS_LED 10
#define WIFI_LED D5
#define WIFI_BUTTON D4

// Set WiFi Connection credentials
#define WIFI_SSID "<Wifi SSID>" //Router SSID
#define WIFI_PASS "<Wifi Password>" // Router Password
#define WIFI_NAME "Amarantha" // Hostname for Router
#define UDP_PORT 123

static const int NTP_PACKET_SIZE = 48;
//buffers for receiving and sending data
byte packetBuffer[NTP_PACKET_SIZE];
// An Ethernet UDP instance
WiFiUDP Udp;

// RTC Setup
RtcDS3231<TwoWire> Rtc(Wire);
EepromAt24c32<TwoWire> RtcEeprom(Wire);

//OLED Settings
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE); // OLED display library parameters

TinyGPSPlus gps;
SoftwareSerial ss(D7, D8); // Serial GPS handler
time_t displayTime = 0;    // time that is currently displayed
time_t syncTime = 0;       // time of last GPS or RTC synchronization
time_t lastSetRTC = 0;     // time that RTC was last set
volatile int pps = 0;      // GPS one-pulse-per-second flag
time_t dstStart = 0;       // start of DST in unix time
time_t dstEnd = 0;         // end of DST in unix time
bool gpsLocked = false;    // indicates recent sync with GPS
int currentYear = 0;       // used for DST
long int pps_blink_time = 0;
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;
uint8_t statusWifi = 1;
ESP8266WebServer server(80);

/*
   ISR Debounce
*/

// use 150ms debounce time
#define DEBOUNCE_TICKS 150

word keytick = 0; // record time of keypress

//#define DEBUG // Comment this in order to remove debug code from release version

#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTDEC(x) Serial.print(x, DEC)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTDEC(x)
#define DEBUG_PRINTLN(x)
#endif

// Button ISR debouncing routine
// returns true if key pressed

boolean KeyCheck()
{
  if (keytick != 0)
  {
    if ((millis() - keytick) > DEBOUNCE_TICKS)
    {
      DEBUG_PRINT(F("KEYTICK: "));
      DEBUG_PRINTLN(keytick);
      keytick = 0;
      DEBUG_PRINTLN(F("KEYCHECK IS TRUE"));
      return true;
    }
  }
  return false;
}

// WiFi Routines

void handleRoot()
{
  server.send(200, "text/html", "<h1>You are connected</h1>");
}

void enableWifi()
{

  // WiFi Initialization
  //You can remove the ap_password parameter if you want the AP to be open.
  //Setup Wifi
  WiFi.mode(WIFI_STA); //Wifi Mode set to access point and station.
  WiFi.hostname(WIFI_NAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS); //Wifi Login Details

  IPAddress myIP = WiFi.softAPIP();
  DEBUG_PRINT(F("AP IP address: "));
  DEBUG_PRINTLN(myIP);
  server.on("/", handleRoot);
  server.begin();
  DEBUG_PRINTLN(F("HTTP server started"));
}

void disableWifi()
{
  server.stop();
  DEBUG_PRINTLN(F("HTTP server stopped"));
  WiFi.softAPdisconnect(true);
  WiFi.enableAP(false);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  DEBUG_PRINTLN(F("WiFi disabled"));
}

void processWifi()
{
  // Toggle WiFi on/off and corresponding LED
  DEBUG_PRINT(F("Status Wifi: "));
  DEBUG_PRINTLN(statusWifi);

  if (statusWifi)
  {
    enableWifi();
    digitalWrite(WIFI_LED, HIGH);
  }
  else
  {
    disableWifi();
    digitalWrite(WIFI_LED, LOW);
  }
}

// SERIAL MONITOR ROUTINES
// These routines print the date/time information to the serial monitor
// Serial monitor must be initialized in setup() before calling
void PrintDigit(int d)
{
  if (d < 10)
    DEBUG_PRINT('0');
  DEBUG_PRINT(d);
}

void PrintTime(time_t t)
// display time and date to serial monitor
{
  PrintDigit(gps.date.month());
  DEBUG_PRINT("-");
  PrintDigit(gps.date.day());
  DEBUG_PRINT("-");
  PrintDigit(gps.date.year());
  DEBUG_PRINT(" ");
  PrintDigit(gps.time.hour());
  DEBUG_PRINT(":");
  PrintDigit(gps.time.minute());
  DEBUG_PRINT(":");
  PrintDigit(gps.time.second());
  DEBUG_PRINTLN(" UTC");
}

//  RTC SUPPORT
//  These routines add the ability to get and/or set the time from an attached real-time-clock module
//  such as the DS1307 or the DS3231.  The module should be connected to the I2C pins (SDA/SCL).

void PrintRTCstatus()
// send current RTC information to serial monitor
{
  RtcDateTime Now = Rtc.GetDateTime();
  time_t t = Now.Epoch32Time();
  if (t)
  {
    DEBUG_PRINT("PrintRTCstatus: ");
    DEBUG_PRINTLN("Called PrintTime from PrintRTCstatus");
#ifdef DEBUG
    PrintTime(t);
#endif
  }
  else
    DEBUG_PRINTLN("ERROR: cannot read the RTC.");
}

// Update RTC from current system time
void SetRTC(time_t t)
{
  RtcDateTime timeToSet;

  timeToSet.InitWithEpoch32Time(t);

  Rtc.SetDateTime(timeToSet);
  if (Rtc.LastError() == 0)
  {
    DEBUG_PRINT("SetRTC: ");
    DEBUG_PRINTLN("Called PrintTime from SetRTC");
#ifdef DEBUG
    PrintTime(t);
#endif
  }
  else
    DEBUG_PRINT("ERROR: cannot set RTC time");
}

void ManuallySetRTC()
// Use this routine to manually set the RTC to a specific UTC time.
// Since time is automatically set from GPS, this routine is mainly for
// debugging purposes.  Change numeric constants to the time desired.
{
  //  tmElements_t tm;
  //  tm.Year   = 2017 - 1970;                              // Year in unix years
  //  tm.Month  = 5;
  //  tm.Day    = 31;
  //  tm.Hour   = 5;
  //  tm.Minute = 59;
  //  tm.Second = 30;
  //  SetRTC(makeTime(tm));                                 // set RTC to desired time
}

void UpdateRTC()
// keep the RTC time updated by setting it every (RTC_UPDATE_INTERVAL) seconds
// should only be called when system time is known to be good, such as in a GPS sync event
{
  time_t t = now();                            // get current time
  if ((t - lastSetRTC) >= RTC_UPDATE_INTERVAL) // is it time to update RTC internal clock?
  {
    DEBUG_PRINT("Called SetRTC from UpdateRTC with ");
    DEBUG_PRINTLN(t);
    SetRTC(t);      // set RTC with current time
    lastSetRTC = t; // remember time of this event
  }
}

// --------------------------------------------------------------------------------------------------
// LCD SPECIFIC ROUTINES
// These routines are used to display time and/or date information on the LCD display
// Assumes the presence of a global object "lcd" of the type "LiquidCrystal" like this:
//    LiquidCrystal   lcd(6,9,10,11,12,13);
// where the six numbers represent the digital pin numbers for RS,Enable,D4,D5,D6,and D7 LCD pins

// Modified Version of Titillium Web Bold Font designed by Accademia di Belle Arti di Urbino https://www.accademiadiurbino.it

const uint8_t unifont_custom[280] U8G2_FONT_SECTION("unifont_custom") =
  "\20\0\3\3\4\4\2\4\5\13\14\0\0\14\0\14\0\0\0\0\0\0\370 \5\0&\5-\7%\363"
  "\305a\0\60\22\312\242\36\365\264$\24\11=N\204\242\245C\314\4\61\15\307\243\346\350P\221LB\303"
  "\375\0\62\17\310\243\206\351<]\134\35J\245\207\203\0\63\17\310\243\306\345<\235\212\234\247\323\303\201\2"
  "\64\23\310\243\236t*]\25IF\22\231\344p\220\212%\0\65\20\310\243\316\341\60V\221]\16a\325"
  "\303\201\2\66\25\311\243\226\351\62\226\3D\262C\344\20\21\26m\242[\11\0\67\20\310\243\306\203tU"
  ":\225N\245S\351\20\0\70\27\312\242\226\355\20Z\22\212\326N\207\320L\62\234\314&\207\230\11\0\71"
  "\25\311\242V\355\64\62J\204\222C\344\20\233\310\1\342\311\311\4:\10\203c\305A|\20C\15\310\243"
  "\316\303t*\326<\35\237\14T\30\311b\306\3Q\16\220\3\344\0\71@\16\220\3\344\0\71@\16\220"
  "\1U\14\312#\307\320\377(:\304L\0\0\0\0";

void ShowDate(time_t t)
{
  String data = "";

  int y = gps.date.day();
  if (y < 10)
    data = data + "0";
  data = data + String(y) + "-";

  int m = gps.date.month();
  if (m < 10)
    data = data + "0";
  data = data + String(m) + "-";

  int d = gps.date.year();
  if (d < 10)
    data = data + "0";
  data = data + String(d);

  u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
  u8g2.drawGlyph(0, 43, 107);

  u8g2.setFont(unifont_custom); // choose a suitable font
  u8g2.drawStr(18, 43, data.c_str());

  DEBUG_PRINTLN("UpdateDisplay");
}

void ShowTime(time_t t)
{
  String hora = "";
  int h = gps.time.hour();
  if (h < 10)
    hora = hora + "0";
  hora = hora + String(h) + ":";

  int m = gps.time.minute();
  if (m < 10)
    hora = hora + "0";
  hora = hora + String(m) + ":";

  int s = gps.time.second();
  if (s < 10)
    hora = hora + "0";
  hora = hora + String(s) + " UTC";

  u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
  u8g2.drawGlyph(0, 64, 123);

  u8g2.setFont(unifont_custom); // choose a suitable font
  u8g2.drawStr(18, 64, hora.c_str());
}

void ShowDateTime(time_t t)
{
  ShowDate(t);

  ShowTime(t);
}

void ShowSyncFlag()
{
  String sats = "";
  if (gps.satellites.value() != 255)
  { sats = String(gps.satellites.value());
  }
  else
  { sats = "0";

  }

  if (gpsLocked)
    digitalWrite(LOCK_LED, HIGH);
  else
    digitalWrite(LOCK_LED, LOW);

  String resol = "";
  if (gpsLocked)
    resol = String(gps.hdop.value());
  else
    resol = "0";

  // Satellite Lock Icon
#define Lock_width 24
#define Lock_height 17
  static const unsigned char Lock_24x17_bits[] U8X8_PROGMEM = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x3c, 0x3c, 0x3c,
    0xf8, 0xff, 0x3f,
    0x8c, 0xe7, 0x33,
    0xf8, 0xff, 0x3f,
    0x3c, 0x3c, 0x3c,
    0x00, 0x18, 0x00,
    0x00, 0x10, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x18, 0x00,
    0x00, 0x08, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x7e, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
  };

  u8g2.drawXBM(0, 2, Lock_width, Lock_height, Lock_24x17_bits);

  // Satellite Dish Icon
#define Satellite_width 24
#define Satellite_height 22
  static const unsigned char Satellite_24x22_bits[] U8X8_PROGMEM = {
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x01, 0x18,
    0x80, 0x03, 0x3c,
    0xc0, 0x07, 0x3c,
    0xc0, 0x0f, 0x1e,
    0xe0, 0x1f, 0x07,
    0xe0, 0xbf, 0x03,
    0xe0, 0xff, 0x01,
    0xe0, 0xff, 0x00,
    0xe0, 0xff, 0x01,
    0xe0, 0xff, 0x03,
    0xc0, 0xff, 0x07,
    0xc0, 0xff, 0x0f,
    0xc0, 0xff, 0x1f,
    0x80, 0xff, 0x3f,
    0x00, 0xff, 0x7f,
    0x00, 0xff, 0x3f,
    0x80, 0xff, 0x1f,
    0x80, 0xf7, 0x03,
    0xc0, 0x0f, 0x00,
    0xe0, 0x0f, 0x00,
  };

  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawXBM(60, 0, Satellite_width, Satellite_height, Satellite_24x22_bits);

  u8g2.setFont(unifont_custom); // choose a suitable font
  u8g2.drawStr(24, 18, sats.c_str());
  u8g2.drawStr(88, 18, resol.c_str());
}

void InitLCD()
{
  u8g2.begin(); // Initialize OLED library
}

// --------------------------------------------------------------------------------------------------
// TIME SYNCHONIZATION ROUTINES
// These routines will synchonize time with GPS and/or RTC as necessary
// Sync with GPS occur when the 1pps interrupt signal from the GPS goes high.
// GPS synchonization events are attempted every (SYNC_INTERVAL) seconds.
// If a valid GPS signal is not received within (SYNC_TIMEOUT) seconds, the clock with synchonized
// with RTC instead.  The RTC time is updated with GPS data once every 24 hours.

void SyncWithGPS()
{
  int y;
  byte h, m, s, mon, d, hundredths;
  unsigned long age;
  gps.time.value(); // get time from GPS

  if (age < 1000 or age > 3000)                             // dont use data older than 1 second
  {
    setTime(h, m, s, d, mon, y); // copy GPS time to system time
    DEBUG_PRINT("Time from GPS: ");
    DEBUG_PRINT(h);
    DEBUG_PRINT(":");
    DEBUG_PRINT(m);
    DEBUG_PRINT(":");
    DEBUG_PRINTLN(s);
    adjustTime(1);                     // 1pps signal = start of next second
    syncTime = now();                  // remember time of this sync
    gpsLocked = true;                  // set flag that time is reflects GPS time
    UpdateRTC();                       // update internal RTC clock periodically
    DEBUG_PRINTLN("GPS synchronized"); // send message to serial monitor
  }
  else
  {
    DEBUG_PRINT("Age: ");
    DEBUG_PRINTLN(age);
  }
}

void SyncWithRTC()
{
  RtcDateTime time = Rtc.GetDateTime();
  long int a = time.Epoch32Time();
  setTime(a); // set system time from RTC
  DEBUG_PRINT("SyncFromRTC: ");
  DEBUG_PRINTLN(a);
  syncTime = now();                       // and remember time of this sync event
  DEBUG_PRINTLN("Synchronized from RTC"); // send message to serial monitor
}

void SyncCheck()
// Manage synchonization of clock to GPS module
// First, check to see if it is time to synchonize
// Do time synchonization on the 1pps signal
// This call must be made frequently (keep in main loop)
{
  unsigned long timeSinceSync = now() - syncTime; // how long has it been since last sync?
  if (pps && (timeSinceSync >= SYNC_INTERVAL))
  { // is it time to sync with GPS yet?
    DEBUG_PRINTLN("Called SyncWithGPS from SyncCheck");
    SyncWithGPS(); // yes, so attempt it.
  }
  pps = 0;                           // reset 1-pulse-per-second flag, regardless
  if (timeSinceSync >= SYNC_TIMEOUT) // GPS sync has failed
  {
    gpsLocked = false; // flag that clock is no longer in GPS sync
    DEBUG_PRINTLN("Called SyncWithRTC from SyncCheck");
    SyncWithRTC(); // sync with RTC instead
  }
}

// --------------------------------------------------------------------------------------------------
// MAIN PROGRAM

void ICACHE_RAM_ATTR isr() // INTERRUPT SERVICE REQUEST
{
  pps = 1;                     // Flag the 1pps input signal
  digitalWrite(PPS_LED, HIGH); // Ligth up led pps monitor
  pps_blink_time = millis();   // Capture time in order to turn led off so we can get the blink effect ever x milliseconds - On loop
  DEBUG_PRINTLN("pps");
}

// Handle button pressed interrupt
void ICACHE_RAM_ATTR btw() // INTERRUPT SERVICE REQUEST
{
  keytick = millis();
  DEBUG_PRINTLN(F("BUTTON PRESSED!"));
}

void processKeypress()
{
  if (statusWifi)
    statusWifi = 0;
  else
    statusWifi = 1;

  processWifi();
  DEBUG_PRINTLN(F("BUTTON CLICK PROCESSED!"));
}

void setup()
{
  pinMode(LOCK_LED, OUTPUT);
  pinMode(PPS_LED, OUTPUT);
  pinMode(WIFI_LED, OUTPUT);
  pinMode(WIFI_BUTTON, INPUT_PULLUP);

  digitalWrite(LOCK_LED, LOW);
  digitalWrite(PPS_LED, LOW);
  digitalWrite(WIFI_LED, LOW);
  // if you are using ESP-01 then uncomment the line below to reset the pins to
  // the available pins for SDA, SCL
  Wire.begin(D2, D1); // due to limited pins, use pin 0 and 2 for SDA, SCL
  Rtc.Begin();
  RtcEeprom.Begin();

  InitLCD(); // initialize LCD display

  ss.begin(57600); // set GPS baud rate
#ifdef DEBUG
  Serial.begin(57600); // set serial monitor
#endif

  Serial.begin(57600);
  delay(2000);
  DEBUG_PRINTLN("Iniciado");

  // Initialize RTC
  while (!Rtc.GetIsRunning())
  {
    Rtc.SetIsRunning(true);
    DEBUG_PRINTLN(F("RTC had to be force started"));
  }

  DEBUG_PRINTLN(F("RTC started"));

  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);

#ifdef DEBUG
  PrintRTCstatus(); // show RTC diagnostics
#endif
  SyncWithRTC();                         // start clock with RTC data
  attachInterrupt(PPS_PIN, isr, RISING); // enable GPS 1pps interrupt input
  attachInterrupt(WIFI_BUTTON, btw, FALLING);

  processWifi();

  // Startup UDP
  Udp.begin(UDP_PORT);
}

void FeedGpsParser()
// feed currently available data from GPS module into tinyGPS++ parser
{
  while (ss.available()) // look for data from GPS module
  {
    char c = ss.read(); // read in all available chars
    gps.encode(c);      // and feed chars to GPS parser
    //Serial.write(c); // Uncomment for some extra debug info if in doubt about GPS feed
  }
}

void UpdateDisplay()
//  Call this from the main loop
//  Updates display if time has changed
{
  time_t t = now();     // get current time
  if (t != displayTime) // has time changed?
  {
    u8g2.clearBuffer(); // Clear buffer contents
    ShowDateTime(t);    // Display the new UTC time
    ShowSyncFlag();     // show if display is in GPS sync
    u8g2.sendBuffer();  // Send new information to display
    displayTime = t;    // save current display value
    DEBUG_PRINTLN("Called PrintTime from UpdateDisplay");
#ifdef DEBUG
    PrintTime(t); // copy time to serial monitor
#endif
  }
}

////////////////////////////////////////

const uint8_t daysInMonth[] PROGMEM = {
  31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
}; //const or compiler complains

const unsigned long seventyYears = 2208988800UL; // to convert unix time to epoch

// NTP since 1900/01/01
static unsigned long int numberOfSecondsSince1900Epoch(uint16_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t mm, uint8_t s)
{
  if (y >= 1970)
    y -= 1970;
  uint16_t days = d;
  for (uint8_t i = 1; i < m; ++i)
    days += pgm_read_byte(daysInMonth + i - 1);
  if (m > 2 && y % 4 == 0)
    ++days;
  days += 365 * y + (y + 3) / 4 - 1;
  return days * 24L * 3600L + h * 3600L + mm * 60L + s + seventyYears;
}

////////////////////////////////////////

void processNTP()
{

  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    Udp.read(packetBuffer, NTP_PACKET_SIZE);
    IPAddress Remote = Udp.remoteIP();
    int PortNum = Udp.remotePort();

#ifdef DEBUG
    Serial.println();
    Serial.print("Received UDP packet size ");
    Serial.println(packetSize);
    Serial.print("From ");

    for (int i = 0; i < 4; i++)
    {
      Serial.print(Remote[i], DEC);
      if (i < 3)
      {
        Serial.print(".");
      }
    }
    Serial.print(", port ");
    Serial.print(PortNum);

    byte LIVNMODE = packetBuffer[0];
    Serial.print("  LI, Vers, Mode :");
    Serial.print(packetBuffer[0], HEX);

    byte STRATUM = packetBuffer[1];
    Serial.print("  Stratum :");
    Serial.print(packetBuffer[1], HEX);

    byte POLLING = packetBuffer[2];
    Serial.print("  Polling :");
    Serial.print(packetBuffer[2], HEX);

    byte PRECISION = packetBuffer[3];
    Serial.print("  Precision :");
    Serial.println(packetBuffer[3], HEX);

    for (int z = 0; z < NTP_PACKET_SIZE; z++)
    {
      Serial.print(packetBuffer[z], HEX);
      if (((z + 1) % 4) == 0)
      {
        Serial.println();
      }
    }
    Serial.println();

#endif

    packetBuffer[0] = 0b00100100; // LI, Version, Mode
    //packetBuffer[1] = 1 ;   // stratum
    //think that should be at least 4 or so as you do not use fractional seconds

    packetBuffer[1] = 4;    // stratum
    packetBuffer[2] = 6;    // polling minimum
    packetBuffer[3] = 0xFA; // precision

    packetBuffer[7] = 0; // root delay
    packetBuffer[8] = 0;
    packetBuffer[9] = 8;
    packetBuffer[10] = 0;

    packetBuffer[11] = 0; // root dispersion
    packetBuffer[12] = 0;
    packetBuffer[13] = 0xC;
    packetBuffer[14] = 0;

    //int year;
    //byte month, day, hour, minute, second, hundredths;
    unsigned long date, time, age;
    uint32_t timestamp, tempval;
    time_t t = now();

    //gps.crack_datetime(&year, &month, &day, &hour, &minute, &second, &hundredths, &age);
    //timestamp = numberOfSecondsSince1900Epoch(year,month,day,hour,minute,second);

    timestamp = numberOfSecondsSince1900Epoch(gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second());

#ifdef DEBUG
    Serial.println(timestamp);
    //print_date(gps);
#endif

    tempval = timestamp;

    packetBuffer[12] = 71; //"G";
    packetBuffer[13] = 80; //"P";
    packetBuffer[14] = 83; //"S";
    packetBuffer[15] = 0;  //"0";

    // reference timestamp
    packetBuffer[16] = (tempval >> 24) & 0XFF;
    tempval = timestamp;
    packetBuffer[17] = (tempval >> 16) & 0xFF;
    tempval = timestamp;
    packetBuffer[18] = (tempval >> 8) & 0xFF;
    tempval = timestamp;
    packetBuffer[19] = (tempval) & 0xFF;

    packetBuffer[20] = 0;
    packetBuffer[21] = 0;
    packetBuffer[22] = 0;
    packetBuffer[23] = 0;

    //copy originate timestamp from incoming UDP transmit timestamp
    packetBuffer[24] = packetBuffer[40];
    packetBuffer[25] = packetBuffer[41];
    packetBuffer[26] = packetBuffer[42];
    packetBuffer[27] = packetBuffer[43];
    packetBuffer[28] = packetBuffer[44];
    packetBuffer[29] = packetBuffer[45];
    packetBuffer[30] = packetBuffer[46];
    packetBuffer[31] = packetBuffer[47];

    //receive timestamp
    packetBuffer[32] = (tempval >> 24) & 0XFF;
    tempval = timestamp;
    packetBuffer[33] = (tempval >> 16) & 0xFF;
    tempval = timestamp;
    packetBuffer[34] = (tempval >> 8) & 0xFF;
    tempval = timestamp;
    packetBuffer[35] = (tempval) & 0xFF;

    packetBuffer[36] = 0;
    packetBuffer[37] = 0;
    packetBuffer[38] = 0;
    packetBuffer[39] = 0;

    //transmitt timestamp
    packetBuffer[40] = (tempval >> 24) & 0XFF;
    tempval = timestamp;
    packetBuffer[41] = (tempval >> 16) & 0xFF;
    tempval = timestamp;
    packetBuffer[42] = (tempval >> 8) & 0xFF;
    tempval = timestamp;
    packetBuffer[43] = (tempval) & 0xFF;

    packetBuffer[44] = 0;
    packetBuffer[45] = 0;
    packetBuffer[46] = 0;
    packetBuffer[47] = 0;

    // Reply to the IP address and port that sent the NTP request

    Udp.beginPacket(Remote, PortNum);
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
  }
}

void loop()
{
  FeedGpsParser();                                    // decode incoming GPS data
  SyncCheck();                                        // synchronize to GPS or RTC
  UpdateDisplay();                                    // if time has changed, display it
  if (millis() - pps_blink_time > PPS_BLINK_INTERVAL) // If x milliseconds passed, then it's time to switch led off for blink effect
    digitalWrite(PPS_LED, LOW);
  if (KeyCheck()) // Malabarism to cover mechanical switch debouncing
    processKeypress();
  server.handleClient();
  processNTP();
}
