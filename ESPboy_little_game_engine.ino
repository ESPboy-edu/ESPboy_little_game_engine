#include <Arduino.h>
#include <Ticker.h>
#include <LittleFS.h>
#include <EEPROM.h>
#include "user_interface.h"
#include "settings.h"
#include "acoos.h"
#include "ESP8266WiFi.h"

#ifdef ESPBOY
  #include "lib/ESPboyInit.h"
  #include "lib/ESPboyInit.cpp"
  #include "lib/ESPboy_keyboard.h"
  #include "lib/ESPboy_keyboard.cpp"
//  #include "lib/ESPboyTerminalGUI.h"
//  #include "lib/ESPboyTerminalGUI.cpp"
//  #include "lib/ESPboyOTA2.h"
//  #include "lib/ESPboyOTA2.cpp"
    ESPboyInit myESPboy;
    keyboardModule *keybModule=NULL;
  
#else
  #include <TFT_eSPI.h>
  #include <SPI.h>
  #include <Wire.h>
    TFT_eSPI tft = TFT_eSPI();  
#endif

Coos <4, 0> coos;

uint8_t i2c_adress;
uint8_t thiskey;
uint8_t serial_used = 0;
char c;
Ticker timer;
uint16_t cadr_count = 0;
unsigned long timeF,timeR;
uint16_t timeCpu = 0,timeGpu = 0,timeSpr = 0,cpuOPS = 0,cpuOPSD = 0;
uint8_t fps, fileIsLoad;
uint8_t timeForRedraw = 48;
uint8_t fixed_res_bit = 8;
volatile uint16_t timers[8];

static const PROGMEM uint16_t bpalette[] = {
    0x0020, 0xE718, 0xB9A8, 0x7DB6, 0x61EB, 0x6D2D, 0x21EC, 0xD5CA,
    0xAC4D, 0x42CB, 0xBB09, 0x3186, 0x73AE, 0x8D4B, 0x3DF9, 0xBDD7
};

uint16_t palette[16] __attribute__ ((aligned));
uint16_t sprtpalette[16] __attribute__ ((aligned));

uint16_t bgr_to_rgb(uint16_t c){
  return ((c & 0x001f) << 11) + ((c & 0xf800) >> 11) + (c & 0x07e0);
}

unsigned char hexToByte(char h){
  if(h < 48) return 48;
  if (h >= 48 && h <= 57) h = map(h, 48, 57, 0, 9);
  else if (h >= 65 && h <= 70) h = map(h, 65, 70, 10, 15);
  else if (h >= 97 && h <= 102) h = map(h, 97, 102, 10, 15);
  return h;
}

void loadFromSerial(){
  char c; unsigned char n; int16_t j = 0;
  for(int16_t i = 0; i < RAM_SIZE; i++) writeMem(i, 0);
  while(c != '.'){
    if(Serial.available()){
      c = Serial.read(); Serial.print(c);
      if(c == '$'){
        n = 48; while(n > 15){ c = Serial.read(); n = hexToByte(c); }
        Serial.print(c); writeMem(j, n << 4);
        n = 48; while(n > 15){ c = Serial.read(); n = hexToByte(c); }
        Serial.print(c); writeMem(j, readMem(j) + n);
        j++;
      }
    }
  }
  Serial.println(F("load ")); Serial.print(j); Serial.println(F(" byte"));
  Serial.print(F("free heap ")); Serial.println(system_get_free_heap_size());
  cpuInit();
}

void viewEEPROM(){
  for(int16_t i = 0; i < EEPROM_SIZE; i++){
    if(i % 32 == 0) Serial.println();
    if(EEPROM.read(i) < 0x10) Serial.print('0');
    Serial.print(EEPROM.read(i), HEX); Serial.print(' ');
  }
}

void changeSettings(){
  fileIsLoad = false;
  if(Serial.available()){
    c = Serial.read(); Serial.print(c);
    if(c == 'm'){ loadFromSerial(); fileIsLoad = true; cpuInit(); return; }
    else if(c == 'r'){ ESP.reset(); return; }
    else if(c == 'd'){ debug(); Serial.print(F("kIPS")); Serial.println(cpuOPSD, DEC); return; }
    else if(c == 'e'){ viewEEPROM(); }
    else if(c == 'v'){
      Serial.println(); Serial.println(F("input new resolution"));
      int w = 0; int h = 0;
      while(Serial.available() == 0){} c = Serial.read();
      if(c <= 47 || c > 57){ while(Serial.available() == 0){} c = Serial.read(); }
      while(c > 47 && c <= 57){ w = w * 10 + (c - 48); while(Serial.available() == 0){} c = Serial.read(); }
      Serial.print(w); Serial.print(' ');
      while(Serial.available() == 0){} c = Serial.read();
      while(c > 47 && c <= 57){ h = h * 10 + (c - 48); while(Serial.available() == 0){} c = Serial.read(); }
      Serial.println(h); setScreenResolution(w, h); return;
    }
  }
}

void coos_cpu(void){   
  while(1){
    COOS_DELAY(0);
    timeR = millis();
    cpuOPS += 1;
    cpuRun(1000);
    timeCpu += millis() - timeR;
    handleSound(); // БЕЗОПАСНЫЙ ПОЛЛИНГ ЗВУКА
  }
}

void coos_screen(void){
   while(1){
    yield();
    COOS_DELAY(timeForRedraw);
    timeR = millis();
    clearSpriteScr();
    redrawSprites();
    moveSprites();
    testSpriteCollision();
    redrawParticles();
    timeSpr += millis() - timeR;
    timeR = millis();
    redrawScreen();
    setRedraw(); 
    timeGpu += millis() - timeR;
    cadr_count++;
    if(millis() - timeF > 1000){
      timeF = millis();
      fps = cadr_count;
      cadr_count = cadr_count % 2;
    }  
  } 
}

// ОПТИМИЗАЦИЯ: Прерывания теперь легкие, ничего не ломают
void IRAM_ATTR timer_tick(void){
  for(int16_t i = 0; i < 8; i++){
    if(timers[i] >= 1) timers[i]--;
  }
}

void coos_key(void){   
  while(1){
    COOS_DELAY(100);
    getKey();
    if(thiskey == 192) pause();
    if(!serial_used) changeSettings();
  }
}

void coos_info(void){   
  while(1){
    COOS_DELAY(1000);
   #ifdef DEBUG_ON_SCREEN
    if(getDisplayXOffset() > 30){
      tft.fillRect(0, 0, 30, 92, 0x0000);
      tft.setCursor(1, 0); tft.println("fps"); tft.println(fps);
      tft.println("cpu"); tft.println(timeCpu, DEC);
      tft.println("gpu"); tft.println(timeGpu, DEC);
      tft.println("spr"); tft.println(timeSpr, DEC);
      tft.println("kIPS"); tft.println(cpuOPS, DEC);
    }
   #endif
    timeCpu = 0; timeGpu = 0; timeSpr = 0;
    cpuOPSD = cpuOPS; cpuOPS = 0;
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  Serial.println();
  Serial.print(F("version ")); Serial.print(F(BUILD_VERSION_MAJOR)); Serial.print('.');
  Serial.print(F(BUILD_VERSION_MINOR)); Serial.print(F(" build ")); Serial.print(F(__DATE__));
  randomSeed(RANDOM_REG32);
  
 #ifdef ESPBOY
  Serial.println(); Serial.println(F("ESPboy"));
  myESPboy.begin("Little game engine");

  myESPboy.tft.fillScreen(0x0000);
  keybModule = new keyboardModule(1,1,7000);  
  if (keybModule->begin()) Serial.println(F("\nESPboy keyboard module found"));
 #else
  Wire.begin(D2, D1);
  geti2cAdress();
  tft.init(); tft.setRotation(1); tft.fillScreen(0x0000);
 #endif

  LittleFSConfig cfg;
  cfg.setAutoFormat(false);
  LittleFS.setConfig(cfg);
 #ifdef ESPBOY
  myESPboy.tft.setTextColor(TFT_GREEN);
 #else
  tft.setTextColor(TFT_GREEN);
 #endif
  if(LittleFS.begin()){
    Serial.println(F("LittleFS Initialize....ok"));
  } else {
   #ifdef ESPBOY
    myESPboy.tft.setCursor(2, 0); myESPboy.tft.print(F("LittleFS init FAILED"));
    myESPboy.tft.setCursor(2, 10); myESPboy.tft.print(F("FORMATING..."));
   #else
    tft.setCursor(2, 0); tft.print(F("LittleFS init FAILED"));
    tft.setCursor(2, 10); tft.print(F("FORMATING..."));
   #endif
    Serial.println(F("LittleFS init FAILED. Formating..."));
    if(LittleFS.format()){
      Serial.println(F("Formating DONE"));
     #ifdef ESPBOY
      myESPboy.tft.setCursor(2, 18); myESPboy.tft.print(F("DONE!"));
     #else
      tft.setCursor(2, 18); tft.print(F("DONE!"));
     #endif
      LittleFS.begin(); delay(2000);
    } else {
      Serial.println(F("Formatting FAILED")); 
     #ifdef ESPBOY
      myESPboy.tft.setCursor(2, 18); myESPboy.tft.print(F("Formatting FAILED"));
     #else
      tft.setCursor(2, 18); tft.print(F("Formatting FAILED"));
     #endif 
      delay(2000);
    }
  }
  
  memoryAlloc();
  loadSplashscreen();
  cpuInit();
  Serial.print(F("FreeHeap:")); Serial.println(ESP.getFreeHeap());
 #ifdef ESPBOY
  setScreenResolution(128, 128);
 #else
  setScreenResolution(min(SCREEN_REAL_WIDTH, SCREEN_REAL_HEIGHT) - 1, min(SCREEN_REAL_WIDTH, SCREEN_REAL_HEIGHT) - 1);
 #endif
  clearScr(0);
  setColor(1);
  timer.attach(0.001, timer_tick);
  coos.register_task(coos_cpu); 
  coos.register_task(coos_screen);   
  coos.register_task(coos_key);
  coos.register_task(coos_info);
  coos.start();
}

void loop() {
  coos.run();
}