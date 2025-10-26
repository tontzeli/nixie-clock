// ============================================================
// Nixie clock code © 2025 by Toni Mäkelä is licensed under CC BY-NC-SA 4.0. 
//To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/  
// Nixie Clock main code v4.5
// Last updated 26.10.2025
//============================================================

#include <Wire.h>

// ---------------------- User Settings ----------------------
#define TIMEZONE                      2   // Base timezone (+ or - UTC)
#define USE_DST                       1   // Enable daylight saving time (1=YES, 0=NO), follows European standard, refer to row 76 forwards

#define BLANK_MS                   1000
#define FLICK_MIN_FRAMES              4
#define FLICK_MAX_FRAMES             10
#define FLICK_DELAY_MIN_MS           40
#define FLICK_DELAY_MAX_MS           70
#define LOOP_UPDATE_MS             1000

#define MIDNIGHT_COUNTDOWN_MS     20000
#define MIDNIGHT_TRIGGER_SEC         41

#define CLEAN_STEP_MS               200
#define RESYNC_FRAME_MS             100

#define BLANK_BEFORE_FLICKER          1
#define FLICK_ORDER_RANDOM            0
#define HOUR_FLICKER_BIAS_FRAMES      2
// ------------------------------------------------------------

// ---- Pins ----
const int PIN_SRCLR = 8;
const int PIN_OE    = 9;
const int PIN_RCLK  = 10;
const int PIN_SER   = 11;
const int PIN_HVDIS = 12;
const int PIN_SRCLK = 13;

// ---- DS3231 ----
constexpr uint8_t DS3231_ADDR = 0x68;
inline uint8_t bcd2bin(uint8_t v){ return (v>>4)*10 + (v & 0x0F); }

// ------------------------------------------------------------
// RTC helpers
bool rtcRead(uint8_t startReg, uint8_t* buf, uint8_t len){
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((int)DS3231_ADDR, (int)len);
  for (uint8_t i=0; i<len; i++){ if(!Wire.available()) return false; buf[i]=Wire.read(); }
  return true;
}

bool readRTC_HMS(uint8_t &hour, uint8_t &minute, uint8_t &second){
  uint8_t r[3];
  if(!rtcRead(0x00, r, 3)) return false;
  second = bcd2bin(r[0] & 0x7F);
  minute = bcd2bin(r[1] & 0x7F);
  hour   = bcd2bin(r[2] & 0x3F);
  return true;
}

// ------------------------------------------------------------
// EU DST rule (UTC-based)
bool isDST_EU_UTC(int year, int month, int day, int hourUTC){
  auto weekday = [](int y, int m, int d) {
    if (m < 3) { m += 12; y--; }
    int K = y % 100, J = y / 100;
    int h = (d + 13*(m + 1)/5 + K + K/4 + J/4 + 5*J) % 7;
    return (h + 6) % 7; // 0 = Sunday
  };
  int lastSunMar=0, lastSunOct=0;
  for(int d=31; d>=25; --d){ if(weekday(year,3,d)==0){ lastSunMar=d; break; } }
  for(int d=31; d>=25; --d){ if(weekday(year,10,d)==0){ lastSunOct=d; break; } }

  // EU rule (UTC): starts 01:00 UTC last Sunday in March, ends 01:00 UTC last Sunday in October
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;
  if (month == 3)  return (day > lastSunMar) || (day == lastSunMar && hourUTC >= 1);
  if (month == 10) return (day < lastSunOct) || (day == lastSunOct && hourUTC < 1);
  return false;
}

// ------------------------------------------------------------
// Digit mapping + shifting
static inline uint8_t mapFirstIC_1to8(uint8_t d){ return (d>=1 && d<=8)? (uint8_t)(1u<<(8-d)) : 0; }
static inline uint8_t mapSecondIC_9or0(uint8_t d){ return (d==9)?0x80:(d==0)?0x40:0; }

void writeChain(uint8_t ic1,uint8_t ic2,uint8_t ic3,uint8_t ic4,
                uint8_t ic5,uint8_t ic6,uint8_t ic7,uint8_t ic8){
  digitalWrite(PIN_OE,HIGH);
  digitalWrite(PIN_RCLK,LOW);
  shiftOut(PIN_SER,PIN_SRCLK,LSBFIRST,ic8);
  shiftOut(PIN_SER,PIN_SRCLK,LSBFIRST,ic7);
  shiftOut(PIN_SER,PIN_SRCLK,LSBFIRST,ic6);
  shiftOut(PIN_SER,PIN_SRCLK,LSBFIRST,ic5);
  shiftOut(PIN_SER,PIN_SRCLK,LSBFIRST,ic4);
  shiftOut(PIN_SER,PIN_SRCLK,LSBFIRST,ic3);
  shiftOut(PIN_SER,PIN_SRCLK,LSBFIRST,ic2);
  shiftOut(PIN_SER,PIN_SRCLK,LSBFIRST,ic1);
  digitalWrite(PIN_RCLK,HIGH);
  digitalWrite(PIN_OE,LOW);
}

void showDigits(uint8_t H1,uint8_t H2,uint8_t M1,uint8_t M2){
  auto f=[](uint8_t d){return (d==255)?0:mapFirstIC_1to8(d);};
  auto s=[](uint8_t d){return (d==255)?0:mapSecondIC_9or0(d);};
  writeChain(f(H1),s(H1),f(H2),s(H2),f(M1),s(M1),f(M2),s(M2));
}

// ------------------------------------------------------------
// Fancy animations
uint8_t randDigit(){return (uint8_t)random(0,10);}

void cathodeClean(){
  uint8_t d[4]={0,0,0,0};
  for(uint8_t x=1;x<=9;++x){d[0]=d[1];d[1]=d[2];d[2]=d[3];d[3]=x%10;showDigits(d[0],d[1],d[2],d[3]);delay(CLEAN_STEP_MS);}
  for(uint8_t k=0;k<3;k++){d[0]=d[1];d[1]=d[2];d[2]=d[3];d[3]=0;showDigits(d[0],d[1],d[2],d[3]);delay(CLEAN_STEP_MS);}
  showDigits(0,0,0,0);
}

void resyncToZeros(){
  uint8_t d[4]={randDigit(),randDigit(),randDigit(),randDigit()};
  bool lock[4]={0,0,0,0};uint8_t i=0;
  while(i<4){for(int j=i;j<4;j++)if(!lock[j])d[j]=randDigit();
    showDigits(d[0],d[1],d[2],d[3]);delay(RESYNC_FRAME_MS);
    if(d[i]==0){lock[i]=1;i++;}}
  showDigits(0,0,0,0);
}

// ------------------------------------------------------------
// Sequential minute transition
void minuteTransitionSequential(const uint8_t tgt[4],const bool chg[4]){
  uint8_t tmp[4]; bool set[4]={0,0,0,0};
  if(BLANK_BEFORE_FLICKER){for(uint8_t i=0;i<4;i++)tmp[i]=chg[i]?255:tgt[i];showDigits(tmp[0],tmp[1],tmp[2],tmp[3]);delay(BLANK_MS);}
  uint8_t order[4],n=0;for(uint8_t i=0;i<4;i++)if(chg[i])order[n++]=i;
  for(uint8_t idx=0;idx<n;idx++){uint8_t pos=order[idx];uint8_t frames=random(FLICK_MIN_FRAMES,FLICK_MAX_FRAMES+1);
    if(pos<2)frames+=HOUR_FLICKER_BIAS_FRAMES;
    for(uint8_t f=0;f<frames;f++){for(uint8_t i=0;i<4;i++){if(!chg[i])tmp[i]=tgt[i];else if(set[i])tmp[i]=tgt[i];else tmp[i]=randDigit();}
      showDigits(tmp[0],tmp[1],tmp[2],tmp[3]);delay(random(FLICK_DELAY_MIN_MS,FLICK_DELAY_MAX_MS+1));}
    set[pos]=true;for(uint8_t i=0;i<4;i++)tmp[i]=set[i]?tgt[i]:(chg[i]?randDigit():tgt[i]);
    showDigits(tmp[0],tmp[1],tmp[2],tmp[3]);}
  showDigits(tgt[0],tgt[1],tgt[2],tgt[3]);
}

// ------------------------------------------------------------
// Midnight countdown + New Year
bool midnightAnimRan = false;

void midnightCountdown(uint16_t total_ms){
  uint32_t frame_us=((uint32_t)total_ms*1000UL)/1440UL;
  for(int t=1439;t>=0;--t){
    uint8_t h=t/60,m=t%60;
    showDigits(h/10,h%10,m/10,m%10);
    delayMicroseconds(frame_us);
  }
  showDigits(255,255,255,255); // blank before animation
  delay(200);
}

void newYearDisplay(uint16_t duration_ms = 10000) {
  uint8_t dateBuf[3];
  if (!rtcRead(0x04, dateBuf, 3)) return;

  uint16_t currentYear = bcd2bin(dateBuf[2]) + 2000;
  uint16_t nextYear = currentYear + 1;

  uint8_t y1 = (nextYear / 1000) % 10;
  uint8_t y2 = (nextYear / 100) % 10;
  uint8_t y3 = (nextYear / 10) % 10;
  uint8_t y4 = nextYear % 10;

  showDigits(y1,y2,y3,y4);
  delay(duration_ms);
  showDigits(255,255,255,255);
  delay(200);
}

// ------------------------------------------------------------
// Debug print
inline void print2(uint8_t v){if(v<10)Serial.print('0');Serial.print(v);}
void printStatus(uint16_t y,uint8_t mo,uint8_t d,
                 uint8_t rtcH,uint8_t rtcM,uint8_t rtcS,
                 uint8_t locH,uint8_t locM,bool dst){
  Serial.print(F("RTC(UTC) ")); print2(rtcH); Serial.print(':'); print2(rtcM);
  Serial.print(':'); print2(rtcS);
  Serial.print(F(" | Local ")); print2(locH); Serial.print(':'); print2(locM);
  Serial.print(F(" | Date ")); Serial.print(y); Serial.print('-'); print2(mo);
  Serial.print('-'); print2(d);
  Serial.print(F(" | DST: ")); Serial.println(dst ? F("ON") : F("OFF"));
}

// ------------------------------------------------------------
void setup(){
  Wire.begin();
  Serial.begin(9600);
  delay(500);
  Serial.println(F("Makela Nixie Clock booting..."));

  pinMode(PIN_SRCLR,OUTPUT); pinMode(PIN_OE,OUTPUT);
  pinMode(PIN_RCLK,OUTPUT);  pinMode(PIN_SER,OUTPUT);
  pinMode(PIN_SRCLK,OUTPUT); pinMode(PIN_HVDIS,OUTPUT);
  digitalWrite(PIN_HVDIS,LOW); digitalWrite(PIN_SRCLR,HIGH);
  digitalWrite(PIN_OE,HIGH); writeChain(0,0,0,0,0,0,0,0); digitalWrite(PIN_OE,LOW);
  randomSeed(analogRead(A0));

  resyncToZeros();
  cathodeClean();
}

// ------------------------------------------------------------
void loop(){
  static uint8_t lastDigits[4] = {255,255,255,255};
  static uint8_t lastCleanHour = 255;

  uint8_t rtcH, rtcM, rtcS;
  if(!readRTC_HMS(rtcH, rtcM, rtcS)){ delay(200); return; }

  uint8_t dateBuf[3]; uint16_t year=0; uint8_t month=0, day=0;
  if(rtcRead(0x04,dateBuf,3)){
    day   = bcd2bin(dateBuf[0] & 0x3F);
    month = bcd2bin(dateBuf[1] & 0x1F);
    year  = 2000 + bcd2bin(dateBuf[2]);
  }

  bool dst = USE_DST && isDST_EU_UTC(year, month, day, rtcH);
  int offset = TIMEZONE + (dst ? 1 : 0);
  int localH = (rtcH + offset) % 24; if (localH < 0) localH += 24;

  printStatus(year, month, day, rtcH, rtcM, rtcS, localH, rtcM, dst);

  // --- Midnight countdown + New Year (LOCAL time based) ---
  if (!midnightAnimRan && localH == 23 && rtcM == 59 && rtcS >= MIDNIGHT_TRIGGER_SEC) {
    midnightAnimRan = true;

    // Read date *before* countdown starts
    uint8_t dateBufStart[3];
    bool isNewYearEve = false;
    if (rtcRead(0x04, dateBufStart, 3)) {
      uint8_t dayS   = bcd2bin(dateBufStart[0] & 0x3F);
      uint8_t monthS = bcd2bin(dateBufStart[1] & 0x1F);
      if (monthS == 12 && dayS == 31) isNewYearEve = true;
    }

    // Countdown animation
    midnightCountdown(MIDNIGHT_COUNTDOWN_MS);

    // Immediately after countdown: blank and hold
    showDigits(255,255,255,255);
    delay(200);

    // If New Year’s Eve, show next year
    if (isNewYearEve) {
      newYearDisplay(10000);
    } else {
      showDigits(0,0,0,0);
      delay(1000);
    }

    return; // Skip displaying 23:59 again
  }

  // Reset after 01:00 local
  if (midnightAnimRan && localH >= 1) midnightAnimRan = false;

  // --- Hourly cathode clean ---
  if(rtcM==1 && localH!=lastCleanHour){
    cathodeClean(); lastCleanHour=localH;
  }

  // --- Minute transition logic ---
  uint8_t newDigits[4] = { (uint8_t)(localH/10), (uint8_t)(localH%10),
                           (uint8_t)(rtcM/10),   (uint8_t)(rtcM%10) };

  bool changedMask[4]; bool anyChanged=false;
  for (uint8_t i=0;i<4;i++){
    changedMask[i]=(newDigits[i]!=lastDigits[i]);
    anyChanged|=changedMask[i];
  }

  if (!midnightAnimRan && anyChanged){
    minuteTransitionSequential(newDigits, changedMask);
  } else {
    showDigits(newDigits[0], newDigits[1], newDigits[2], newDigits[3]);
  }

  for (uint8_t i=0;i<4;i++) lastDigits[i]=newDigits[i];

  delay(LOOP_UPDATE_MS);
}
