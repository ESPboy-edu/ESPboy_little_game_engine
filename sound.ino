#include "settings.h"
#define NEXT_CHAR rtttl.startposition++; c = (char)readMem(rtttl.startposition); 
#define NEXT_CHAR_IN_P rtttl.position++; c = (char)readMem(rtttl.startposition + rtttl.position); 
#define OCTAVE_OFFSET 0

#pragma GCC optimize ("-O2")
#pragma GCC push_options

const int notes[] PROGMEM = { 0,
  262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
  523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988,
  1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976,
  2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951
};

struct RTTTL {
  uint16_t address;
  uint16_t position;
  uint16_t startposition;
  uint8_t loop;
  uint8_t play;
  uint8_t default_dur;
  uint8_t default_oct;
  uint16_t bpm;
  uint32_t wholenote;
  uint16_t this_tone;
};

struct RTTTL rtttl;
uint32_t sfx_end_time = 0;
uint32_t rtttl_next_time = 0;

// ОПТИМИЗАЦИЯ: Звук полностью вынесен из прерываний. Нет конфликтов таймеров.
inline void addTone(uint16_t f, uint16_t t){
  sfx_end_time = millis() + t;
#ifdef SOUNDPIN
  if(SOUNDPIN >= 0) tone(SOUNDPIN, f);
#endif
}

uint8_t loadRtttl(){
  uint16_t num; char c;
  rtttl.default_dur = 4; rtttl.default_oct = 6; rtttl.bpm = 63;
  rtttl.startposition = rtttl.address;
  c = readMem(rtttl.startposition);
  while(c != ':'){ NEXT_CHAR }
  NEXT_CHAR
  if(c == 'd'){
    NEXT_CHAR NEXT_CHAR
    num = 0;
    while(isdigit(c)){ num = (num * 10) + (c - '0'); NEXT_CHAR }
    if(num > 0) rtttl.default_dur = num;
    NEXT_CHAR
  }
  if(c == 'o'){
    NEXT_CHAR NEXT_CHAR
    num = c - '0'; NEXT_CHAR
    if(num >= 3 && num <=7) rtttl.default_oct = num;
    NEXT_CHAR
  }
  if(c == 'b'){
    NEXT_CHAR NEXT_CHAR
    num = 0;
    while(isdigit(c)){ num = (num * 10) + (c - '0'); NEXT_CHAR }
    rtttl.bpm = num;
  }
  rtttl.wholenote = (60 * 1000 / rtttl.bpm) * 4;
  NEXT_CHAR
  rtttl.position = 0;
  return 1;
}

void setRtttlAddress(uint16_t adr){
  rtttl.address = adr;
  loadRtttl();
}

void setRtttlLoop(uint8_t loop){ rtttl.loop = loop; }

void setRtttlPlay(uint8_t play){
  rtttl.play = play;
  if(play == 0 || play == 2){
    if (play == 2) rtttl.position = 0;
    if(millis() >= sfx_end_time) {
#ifdef SOUNDPIN
      if(SOUNDPIN >= 0) noTone(SOUNDPIN);
#endif
    }
  }
  rtttl_next_time = millis();
}

void playRtttlNote() {
  if(rtttl.play == 0 || rtttl.startposition == 0) return;
  char c = (char)readMem(rtttl.startposition + rtttl.position);
  if(c == 0){
    if(!rtttl.loop){
      rtttl.play = 0; rtttl.this_tone = 0;
      if (sfx_end_time == 0 || millis() >= sfx_end_time) {
#ifdef SOUNDPIN
        if(SOUNDPIN >= 0) noTone(SOUNDPIN);
#endif
      }
      return;
    }
    rtttl.position = 0;
    c = (char)readMem(rtttl.startposition + rtttl.position);
  }
  uint16_t num = 0;
  while(isdigit(c)){ num = (num * 10) + (c - '0'); NEXT_CHAR_IN_P }
  uint32_t duration = num ? (rtttl.wholenote / num) : (rtttl.wholenote / rtttl.default_dur);
  uint8_t note = 0;
  switch(c){
    case 'c': case 'C': note = 1; break;
    case 'd': case 'D': note = 3; break;
    case 'e': case 'E': note = 5; break;
    case 'f': case 'F': note = 6; break;
    case 'g': case 'G': note = 8; break;
    case 'a': case 'A': note = 10; break;
    case 'b': case 'B': note = 12; break;
    default: note = 0;
  }
  NEXT_CHAR_IN_P
  if(c == '#'){ note++; NEXT_CHAR_IN_P }
  if(c == '.'){ duration += duration/2; NEXT_CHAR_IN_P }
  else if(c == ';'){ duration += duration; NEXT_CHAR_IN_P }
  else if(c == '&'){ duration += duration / 2 * 3; NEXT_CHAR_IN_P }
  
  uint8_t scale = isdigit(c) ? (c - '0') : rtttl.default_oct;
  if(isdigit(c)) NEXT_CHAR_IN_P
  if(c == ',') NEXT_CHAR_IN_P
  
  rtttl_next_time = millis() + duration;
  if(note){
    rtttl.this_tone = pgm_read_word(&notes[note + ((scale - 4) * 12)]);
    if (sfx_end_time == 0 || millis() >= sfx_end_time) {
#ifdef SOUNDPIN
      if(SOUNDPIN >= 0) tone(SOUNDPIN, rtttl.this_tone);
#endif
    }
  } else {
    rtttl.this_tone = 0;
    if (sfx_end_time == 0 || millis() >= sfx_end_time) {
#ifdef SOUNDPIN
      if(SOUNDPIN >= 0) noTone(SOUNDPIN);
#endif
    }
  }
}

// Главный безопасный цикл звука, вызывается из main loop (в coos_cpu)
void handleSound() {
  uint32_t now = millis();
  if (sfx_end_time > 0 && now >= sfx_end_time) {
    sfx_end_time = 0;
#ifdef SOUNDPIN
    if(SOUNDPIN >= 0) noTone(SOUNDPIN);
#endif
    if (rtttl.play == 1 && rtttl.this_tone > 0 && now < rtttl_next_time) {
#ifdef SOUNDPIN
      if(SOUNDPIN >= 0) tone(SOUNDPIN, rtttl.this_tone);
#endif
    }
  }
  if (rtttl.play == 1 && now >= rtttl_next_time) {
    playRtttlNote();
  }
}