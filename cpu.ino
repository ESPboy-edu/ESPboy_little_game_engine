#include <EEPROM.h>
#include "settings.h"

#define FIFO_MAX_SIZE 32

int16_t reg[16] __attribute__ ((aligned));
int16_t shadow_reg[16] __attribute__ ((aligned));
uint16_t pc = 0;
uint16_t interrupt = 0;
uint16_t dataName = 0;
uint32_t accum, saccum;
uint8_t carry = 0;
uint8_t zero = 0;
uint8_t negative = 0;
uint8_t redraw = 0;
int8_t color = 1;
int8_t bgcolor = 0;
int8_t keyPosition;
char s_buffer[7];
char loadedFileName[32] = {0}; 
char strBuf[16];
uint8_t strBufLength = 0;
uint8_t strBufPosition = 0;

struct Fifo_t {
  uint16_t el[FIFO_MAX_SIZE];
  uint8_t position_read;
  uint8_t position_write;
  uint8_t size;
};

struct Fifo_t interruptFifo;

#pragma GCC optimize ("-O2")
#pragma GCC push_options

// Таблица синусов, сдвинутая на 15 бит. Избавляет от программного деления.
static const uint16_t sin_table_15bit[91] PROGMEM = {
      0,   572,  1144,  1715,  2286,  2856,  3425,  3993,  4560,  5126,
   5690,  6252,  6813,  7371,  7927,  8481,  9032,  9580, 10126, 10668,
  11207, 11743, 12275, 12803, 13328, 13851, 14369, 14883, 15393, 15899,
  16400, 16898, 17391, 17880, 18364, 18844, 19319, 19789, 20255, 20716,
  21172, 21623, 22069, 22510, 22946, 23377, 23803, 24223, 24639, 25049,
  25454, 25854, 26248, 26637, 27021, 27400, 27773, 28140, 28502, 28858,
  29208, 29553, 29892, 30225, 30552, 30874, 31189, 31499, 31803, 32101,
  32393, 32679, 32959, 33233, 33501, 33763, 34018, 34268, 34511, 34749,
  34980, 35205, 35424, 35636, 35842, 36042, 36235, 36422, 36603, 36777,
  32768
};

void fifoClear(){
  interruptFifo.position_read = 0; interruptFifo.position_write = 0; interruptFifo.size = 0;
  for(int16_t i = 0; i < FIFO_MAX_SIZE; i++) interruptFifo.el[i] = 0;
}

void pushInFifo(int16_t n){
  if(interruptFifo.size < FIFO_MAX_SIZE){
    interruptFifo.el[interruptFifo.position_write] = n;
    interruptFifo.position_write++;
    if(interruptFifo.position_write >= FIFO_MAX_SIZE) interruptFifo.position_write = 0;
    interruptFifo.size++;
  }
}

uint16_t popOutFifo(){
  uint16_t out = 0;
  if(interruptFifo.size > 0){
    interruptFifo.size--; out = interruptFifo.el[interruptFifo.position_read];
    interruptFifo.position_read++;
    if(interruptFifo.position_read >= FIFO_MAX_SIZE) interruptFifo.position_read = 0;
  }
  return out;
}

inline int16_t flagsTouint8_t(){ return (carry & 0x1) + ((zero & 0x1) << 1)  + ((negative & 0x1) << 2); }
inline void uint8_tToFlags(int16_t b){ carry = b & 0x1; zero = (b & 0x2) >> 1; negative = (b & 0x4) >> 2; }

void setinterrupt(uint16_t adr, int16_t param){
  if(interrupt == 0 && adr != 0){
    saccum = accum; shadow_reg[0] = flagsTouint8_t();
    for(int8_t j = 1; j <= 15; j++) shadow_reg[j] = reg[j];
    reg[0] -= 2; writeInt(reg[0], param);
    reg[0] -= 2; writeInt(reg[0], pc);
    interrupt = pc; pc = adr;
  } else { pushInFifo(adr); pushInFifo(param); }
}

void setLoadedFileName(const char* s){ strncpy(loadedFileName, s, 31); }

void cpuInit(){
  for(uint8_t i = 1; i < 16; i++) reg[i] = 0;
  strBufPosition = 0; interrupt = 0; fifoClear(); display_init();
  reg[0] = RAM_SIZE - 1;
  clearScr(0); color = 1; bgcolor = 0; setCharX(0); setCharY(0);
  pc = 0; carry = 0; zero = 0; negative = 0; accum = 0; saccum = 0; dataName = 0;
  setRtttlPlay(0);
}

void debug(){
  for(uint8_t i = 0; i < 16; i++){
    Serial.print(" R"); Serial.print(i); Serial.print(':'); Serial.print(reg[i]);
  }
  Serial.print(F(" OP:")); Serial.print(readMem(pc),HEX);
  Serial.print(F(" PC:")); Serial.println(pc);
  Serial.print(F("carry: ")); Serial.print(carry);
  Serial.print(F(" zero: ")); Serial.print(zero);
  Serial.print(F(" negative: ")); Serial.print(negative);
  Serial.print(F(" interrupt: ")); Serial.print(interrupt);
  Serial.print('/'); Serial.println(interruptFifo.size);
  spriteDebug();
  Serial.print(F("send to screen ms:")); Serial.print(timeGpu);
  Serial.print(F(" draw sprite ms:")); Serial.print(timeSpr); Serial.print(' ');
}

inline void setRedraw(){ redraw = 1; }
inline void setFlags(int32_t n){ carry = (n > 0xffff) ? 1 : 0; negative = n >> 31; zero = (n == 0); }

int16_t isqrt(int16_t n) {
  int g = 0x8000, c = 0x8000;
  for (;;) { if (g*g > n) g ^= c; c >>= 1; if (c == 0) return g; g |= c; }
}

int16_t distancepp(int16_t x1, int16_t y1, int16_t x2, int16_t y2){ return isqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1)); }
inline void setDataName(uint16_t address){ dataName = address; }

boolean testDataName(uint16_t pos){
  uint8_t c;
  if(dataName){
    for(uint16_t i = 0; i < 12; i++){
      c = EEPROM.read(pos + i);
      if(c != readMem(dataName + i)) return false;
      if(c == 0) return true;
    }
  } else {
    for(uint16_t i = 0; i < 12; i++){
      c = EEPROM.read(pos + i);
      if(c != loadedFileName[i]) return false;
      if(c == 0) return true;
    }
  } return true;
}

uint16_t findData(){
  uint16_t pos = 0; uint8_t c;
  while(pos < EEPROM_SIZE){
    c = EEPROM.read(pos);
    if(c == 0 || c == 0xff) return EEPROM_SIZE;
    if(testDataName(pos + 1)) return pos;
    pos += c;
  } return EEPROM_SIZE;
}

uint16_t findEndData(){
  uint16_t pos = 0; uint8_t c;
  while(pos < EEPROM_SIZE){
    c = EEPROM.read(pos);
    if(c == 0 || c == 0xff) return pos;
    pos += c;
  } return EEPROM_SIZE;
}

uint16_t saveData(uint16_t arrayAddress, uint16_t count){
  uint16_t i,pos; uint8_t c;
  if(count > 242) count = 242;
  pos = findData();
  if(pos == EEPROM_SIZE) pos = findEndData();
  if((EEPROM_SIZE - pos) - 12 < count) return (EEPROM_SIZE - pos) - 12;
  c = EEPROM.read(pos);
  if(c == 0 || c == 0xff){
    EEPROM.write(pos, 12 + count);
    for(i = 0; i < 12; i++) EEPROM.write(pos + i + 1, readMem(dataName + i));
  } else if(c < count) return c;
  pos += 12;
  for(i = 0; i < count; i++) EEPROM.write(pos + i, readMem(arrayAddress + i));
  EEPROM.commit(); return count;
}

uint16_t loadData(uint16_t arrayAddress){
  uint16_t i,pos; uint8_t c;
  pos = findData();
  if(pos == EEPROM_SIZE) return 0;
  c = EEPROM.read(pos) - 12; pos += 12;
  for(i = 0; i < c; i++) writeMem(arrayAddress + i, EEPROM.read(pos + i));
  return c;
}

int16_t fixed_sin(int x) {
  char pos = 1; 
  if (x < 0) { x = -x; pos = !pos; }
  x %= 360;
  if (x > 180) { x -= 180; pos = !pos; }
  if (x > 90) { x = 180 - x; }
  uint32_t val = pgm_read_word(&sin_table_15bit[x]);
  int32_t s = (val * (1 << fixed_res_bit)) >> 15;
  return pos ? (int16_t)s : (int16_t)-s;
}

inline int16_t fixed_cos(int16_t g){ return fixed_sin(g + 90); }

void copyMem(uint16_t to_adr, uint16_t from_adr, uint16_t num_bytes) {
  for (uint16_t i = 0; i < num_bytes; i++) writeMem(to_adr++, readMem(from_adr++));
}

void unpackingRLE(uint16_t to_adr, uint16_t a, uint16_t num_bytes) {
    uint16_t  i = 0;
    uint16_t  repeat = readMem(a);
    a++;
    uint16_t  color = readMem(a);
    while (i < num_bytes) {
      if (repeat > 0x81) {
        writeMem(to_adr++, color);
        i++; a++; repeat--;
        color = readMem(a);
      } else if (repeat == 0x81) {
        repeat = readMem(a);
        a++;
        color = readMem(a);
      } else if (repeat > 0) {
        writeMem(to_adr++, color);
        i++; repeat--;
      } else if (repeat == 0) {
        a++; repeat = readMem(a); a++;
        color = readMem(a);
      }
    }
}

void unpackingLZ(uint16_t to_adr, uint16_t a, uint16_t num_bytes) {
    uint16_t  i = 0, j, length, position, point;
    while (i < num_bytes) {
      if ((readMem(a) & 128) == 0) {
        length = ((readMem(a++) & 127) << 8) + readMem(a++);
        for (j = 0; j < length; j++) {
          writeMem(to_adr++, readMem(a++));
          i++;
        }
      } else {
        length = (readMem(a) & 127) >> 1;
        position = (((readMem(a++) & 1) << 8) + readMem(a++));
        point = to_adr - position;
        for (j = 0; j < length; j++) {
          writeMem(to_adr++, readMem(point + j));
          i++;
        }
      }
    }
}

#ifdef ESPBOY
void setLedColor(uint16_t r5g6b5){
  uint8_t r,g,b;
  r = ((((r5g6b5 >> 11) & 0x1F) * 527) + 23) >> 6;
  g = ((((r5g6b5 >> 5) & 0x3F) * 259) + 33) >> 6;
  b = (((r5g6b5 & 0x1F) * 527) + 23) >> 6;
  myESPboy.myLED.setRGB(r, g, b);
}
#endif

void cpuRun(uint16_t n){
  uint8_t op1, op2;
  uint16_t reg1, reg2, reg3, adr, j, z;

  static const void* dispatch_table[] = {
    &&OP_0, &&OP_1, &&OP_2, &&OP_3, &&OP_4, &&OP_5, &&OP_6, &&OP_7,
    &&OP_8, &&OP_9, &&OP_A, &&OP_B, &&OP_C, &&OP_D, &&OP_E, &&OP_F
  };

  for(z = 0; z < n; z++){
    op1 = readMem(pc++); op2 = readMem(pc++);
    goto *dispatch_table[op1 >> 4];

    OP_0:
        switch(op1){ 
          case 0x01: reg1 = op2 & 0xf; accum = readInt(pc); reg[reg1] = (int16_t)accum; pc += 2; break;
          case 0x02: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = readInt(reg[reg2]); reg[reg1] = (int16_t)accum; break;
          case 0x03: reg1 = (op2 & 0xf); accum = readInt(readInt(pc)); reg[reg1] = (int16_t)accum; pc += 2; break;
          case 0x04: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = readInt(reg[reg2] + readInt(pc)); reg[reg1] = (int16_t)accum; pc += 2; break;
          case 0x05: reg1 = op2 >> 4; reg2 = op2 & 0xf; writeInt(reg[reg1],reg[reg2]); break;
          case 0x06:
            if((op2 & 0x0f) == 0){ reg1 = op2 >> 4; writeInt(readInt(pc),reg[reg1]); pc += 2; }
            else{ reg1 = op2 >> 4; reg2 = op2 & 0xf; writeInt(readInt(pc) + reg[reg1],reg[reg2]); pc += 2; }
            break;
          case 0x07: reg1 = op2 >> 4; reg2 = op2 & 0xf; reg[reg1] = reg[reg2]; break;
          case 0x08: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = readInt(reg[reg2] * 2 + readInt(pc)); reg[reg1] = (int16_t)accum; pc += 2; break;
          case 0x09: reg1 = op2 >> 4; reg2 = op2 & 0xf; writeInt(readInt(pc) + reg[reg1] * 2,reg[reg2]); pc += 2; break;
          default: pc++;
        } goto OP_DONE;

    OP_1:
        reg1 = (op1 & 0xf); accum = op2; reg[reg1] = (int16_t)accum; goto OP_DONE;

    OP_2:
        if(op1 == 0x20){ reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = readMem(reg[reg2]); reg[reg1] = (int16_t)accum; }
        else { reg1 = op1 & 0xf; reg2 = op2 >> 4; reg3 = op2 & 0xf; accum = readMem(reg[reg2] + reg[reg3]); reg[reg1] = (int16_t)accum; }
        goto OP_DONE;

    OP_3:
        switch(op1){
          case 0x30: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = readMem(reg[reg2] + readInt(pc)); reg[reg1] = (int16_t)accum; pc += 2; break;
          case 0x31: reg1 = op2 & 0xf; accum = readMem(readInt(pc)); reg[reg1] = (int16_t)accum; pc += 2; break;
          case 0x32: reg1 = op2 >> 4; writeMem(readInt(pc),reg[reg1]); pc += 2; break;
          case 0x33: reg1 = op2 >> 4; reg2 = op2 & 0xf; writeMem(readInt(pc) + reg[reg1],reg[reg2]); pc += 2; break;
        } goto OP_DONE;

    OP_4:
        if(op1 == 0x40){ reg1 = op2 >> 4; reg2 = op2 & 0xf; writeMem(reg[reg1], reg[reg2]); }
        else { reg1 = op1 & 0xf; reg2 = op2 >> 4; reg3 = op2 & 0xf; writeMem(reg[reg1] + reg[reg2], reg[reg3]); }
        goto OP_DONE;

    OP_5:
        switch(op1){ 
          case 0x50: clearSpriteScr(); noTone(SOUNDPIN); fileList("/"); break;
          case 0x51: reg1 = op2 >> 4; reg2 = op2 & 0xf; timers[reg[reg1] & 0x7] = reg[reg2]; break;
          case 0x52: reg1 = op2 & 0xf; accum = timers[reg[reg1] & 0x7]; reg[reg1] = (int16_t)accum; break;
          case 0x53: 
#ifdef ESPBOY 
            reg1 = op2 & 0xf; setLedColor(reg[reg1]); 
#endif 
            break;
          case 0x54: reg1 = op2 >> 4; reg2 = op2 & 0xf; setRtttlAddress((uint16_t)reg[reg1]); setRtttlLoop(reg[reg2]); break;
          case 0x55:
            switch(op2){
              case 0x00: setRtttlPlay(1); break;
              case 0x01: setRtttlPlay(0); break;
              case 0x02: setRtttlPlay(2); break;
            } break;
          case 0x56: reg1 = op2 >> 4; reg2 = op2 & 0xf; addTone(reg[reg1], reg[reg2]); break;
          case 0x57:
            if (op2 < 0x10){ reg2 = op2 & 0xf; reg[reg2] = loadData(reg[reg2]); }
            else if (op2 < 0x20){ reg2 = op2 & 0xf; setDataName(reg[reg2]); } break;
          case 0x58: reg1 = op2 >> 4; reg2 = op2 & 0xf; reg[reg1] = saveData(reg[reg1], reg[reg2]); break;
          case 0x59:
            if (op2 < 0x10) { reg2 = op2 & 0xf; reg[reg2] = serialBegin(); } 
            else if (op2 < 0x20) { reg2 = op2 & 0xf; reg[reg2] = serialAvailable(); } 
            else if (op2 < 0x30) { reg2 = op2 & 0xf; reg[reg2] = serialRead(); } 
            else if (op2 < 0x40) { reg2 = op2 & 0xf; serialWrite(reg[reg2]); } break;
        } goto OP_DONE;

    OP_6:
        reg1 = op1 & 0xf; reg2 = op2 >> 4; reg3 = op2 & 0xf; accum = readInt(reg[reg2] + reg[reg3]); reg[reg1] = (int16_t)accum; goto OP_DONE;

    OP_7:
        reg1 = op1 & 0xf; reg2 = op2 >> 4; reg3 = op2 & 0xf; writeInt(reg[reg1] + reg[reg2], reg[reg3]); goto OP_DONE;

    OP_8:
        switch(op1){
          case 0x80: reg1 = op2 & 0xf; reg[reg1] = readInt(reg[0]); reg[0] += 2; break;
          case 0x81: reg1 = op2 & 0xf; for(j = reg1; j >= 1; j--){ reg[j] = readInt(reg[0]); reg[0] += 2; } break;
          case 0x82: reg1 = op2 & 0xf; reg[0] -= 2; writeInt(reg[0], reg[reg1]); break;
          case 0x83: reg1 = op2 & 0xf; for(j = 1; j <= reg1; j++){ reg[0] -= 2; writeInt(reg[0], reg[j]); } break;
        } goto OP_DONE;

    OP_9:
        switch(op1){
          case 0x90: pc = readInt(pc); break;
          case 0x91: setFlags(accum); if(zero == 0) pc = readInt(pc); else pc += 2; break;
          case 0x92: setFlags(accum); if(zero != 0) pc = readInt(pc); else pc += 2; break;
          case 0x93: setFlags(accum); if(negative == 1) pc = readInt(pc); else pc += 2; break;
          case 0x94: setFlags(accum); if(negative != 1) pc = readInt(pc); else pc += 2; break;
          case 0x95: setFlags(accum); if(carry != 1) pc = readInt(pc); else pc += 2; break;
          case 0x96: setFlags(accum); if(carry == 1) pc = readInt(pc); else pc += 2; break;
          case 0x97: setFlags(accum); reg1 = op2 & 0xf; if(reg[reg1] == 0) pc = readInt(pc); else pc += 2; break;
          case 0x98: setFlags(accum); reg1 = op2 & 0xf; if(reg[reg1] != 0) pc = readInt(pc); else pc += 2; break;
          case 0x99: reg[0] -= 2; writeInt(reg[0], pc + 2); pc = readInt(pc); break;
          case 0x9A:
            if(!interrupt){ pc = readInt(reg[0]); reg[0] += 2; }
            else{
              pc = readInt(reg[0]);
              if(pc == interrupt){
                reg[0] += 4; accum = saccum;
                for(int8_t j = 15; j >= 1; j--){ reg[j] = shadow_reg[j]; }
                uint8_tToFlags(shadow_reg[0]); interrupt = 0;
                if(interruptFifo.size > 0) setinterrupt(popOutFifo(), popOutFifo());
              } else reg[0] += 2;
            } break;
        } goto OP_DONE;

    OP_A:
        switch(op1){
          case 0xA0: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] + reg[reg2]; reg[reg1] = (int16_t)accum; break;
          case 0xA1: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] + reg[reg2] + carry; reg[reg1] = (int16_t)accum; break;
          case 0xA2: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] - reg[reg2]; reg[reg1] = (int16_t)accum; break;
          case 0xA3: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] - reg[reg2] - carry; reg[reg1] = (int16_t)accum; break;
          case 0xA4: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] * reg[reg2]; reg[reg1] = (int16_t)accum; break;
          case 0xA5: reg1 = op2 >> 4; reg2 = op2 & 0xf;
            if(reg[reg2] != 0){ accum = reg[reg1] / reg[reg2]; reg[reg2] = reg[reg1] % reg[reg2]; }
            else{ accum = 0; reg[reg2] = 0; } reg[reg1] = (int16_t)accum; break;
          case 0xA6: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] & reg[reg2]; reg[reg1] = (int16_t)accum; break;
          case 0xA7: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] | reg[reg2]; reg[reg1] = (int16_t)accum; break;
          case 0xA8:
            if(op2 == 0x10){ reg1 = op2 & 0xf; accum = readInt(readInt(pc)) + 1; writeInt(readInt(pc), (int16_t)accum); pc += 2; }
            else if(op2 > 0x10){ reg1 = op2 & 0xf; accum = reg[reg1] + (op2 >> 4); reg[reg1] = (int16_t)accum; }
            else { reg1 = op2 & 0xf; accum = reg[reg1] + 1; reg[reg1] = (int16_t)accum; } break;
          case 0xA9:
            if(op2 == 0x10){ reg1 = op2 & 0xf; accum = readInt(readInt(pc)) - 1; writeInt(readInt(pc), (int16_t)accum); pc += 2; }
            else if(op2 > 0x10){ reg1 = op2 & 0xf; accum = reg[reg1] - (op2 >> 4); reg[reg1] = (int16_t)accum; }
            else { reg1 = op2 & 0xf; accum = reg[reg1] - 1; reg[reg1] = (int16_t)accum; } break;
          case 0xAA: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] ^ reg[reg2]; reg[reg1] = (int16_t)accum; break;
          case 0xAB: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] << reg[reg2]; reg[reg1] = (int16_t)accum; break;
          case 0xAC: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] >> reg[reg2]; reg[reg1] = (int16_t)accum; break;
          case 0xAD:
            reg1 = op2 & 0xf; reg2 = op2 & 0xf0;
            switch(reg2){
              case 0x00: accum = random(0, reg[reg1] + 1); reg[reg1] = (int16_t)accum; break;
              case 0x10: accum = isqrt(reg[reg1]); reg[reg1] = (int16_t)accum; break;
              case 0x20: accum = (~reg[reg1]); reg[reg1] = (int16_t)accum; break;
            } break;
          case 0xAE: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = (reg[reg1] != 0 && reg[reg2] != 0) ? 1 : 0; reg[reg1] = (int16_t)accum; break;
          case 0xAF: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = (reg[reg1] != 0 || reg[reg2] != 0) ? 1 : 0; reg[reg1] = (int16_t)accum; break;
        } goto OP_DONE;

    OP_B:
        reg1 = (op1 & 0x0f); accum = reg[reg1] - op2; goto OP_DONE;

    OP_C:
        switch(op1){
          case 0xC0: reg1 = op2 >> 4; accum = reg[reg1] - readInt(pc); pc += 2; break;
          case 0xC1: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = reg[reg1] - reg[reg2]; break;
          case 0xC2:
            reg1 = op2 >> 4; reg2 = op2 & 0xf; setFlags(accum);
            switch(reg2){
              case 0: reg[reg1] = carry; break;
              case 1: reg[reg1] = zero; break;
              case 2: reg[reg1] = negative; break;
              case 3: if(negative == 0 && zero == 0) reg[reg1] = 1; else reg[reg1] = 0; break;
              case 4: if(negative == 0 && zero == 0) reg[reg1] = 0; else reg[reg1] = 1; break;
              case 5: reg[reg1] = 1 - zero; break;
              case 6: reg[reg1] = redraw; redraw = 0; break;
              default: reg[reg1] = 0;
            } break;
          case 0xc3:
            reg1 = op2 & 0x0f; reg2 = op2 & 0xf0;
            switch(reg2){
              case 0x00: reg[reg1] = reg[reg1] * (1 << fixed_res_bit); break;
              case 0x10: reg[reg1] = reg[reg1] / (1 << fixed_res_bit); break;
              case 0x20: reg[reg1] = fixed_sin(reg[reg1]); break;
              case 0x30: reg[reg1] = fixed_cos(reg[reg1]); break;
              case 0x40: adr = reg[reg1]; copyMem(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0x50: adr = reg[reg1]; unpackingRLE(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0x60: adr = reg[reg1]; unpackingLZ(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0x70: fixed_res_bit = reg1; break;
            } break;
          case 0xC4: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = (reg[reg1] * reg[reg2]) / (1 << fixed_res_bit); reg[reg1] = (uint16_t) accum; break;
          case 0xC5: reg1 = op2 >> 4; reg2 = op2 & 0xf; accum = (reg[reg1] * (1 << fixed_res_bit)) / reg[reg2]; reg[reg1] = (uint16_t) accum; break;
        } goto OP_DONE;

    OP_D:
        switch(op1){ 
          case 0xD0:
            if((op2 & 0xff) == 0) clearScr(bgcolor);
            else{ reg1 = op2 >> 4; reg2 = op2 & 0xf; reg[reg1] = getSpriteInXY(reg[reg1], reg[reg2]); } break;
          case 0xD1:
            switch(op2 & 0xf0){
              case 0x00: reg1 = (op2 & 0xf); printc((char)reg[reg1], color, bgcolor); break;
              case 0x10: reg1 = (op2 & 0xf); j = 0; while(!(readMem(reg[reg1] + j) == 0 || j > 1000)){ printc((char)(readMem(reg[reg1] + j)), color, bgcolor); j++; } break;
              case 0x20: reg1 = (op2 & 0xf); if(reg[reg1] < 32768) itoa(reg[reg1], s_buffer, 10); else itoa(reg[reg1] - 0x10000, s_buffer, 10);
                j = 0; while(s_buffer[j]){ printc(s_buffer[j], color, bgcolor); j++; } break;
              case 0x30: reg1 = (op2 & 0xf); setCharX(reg[reg1] & 0xff); break;
              case 0x40: reg1 = (op2 & 0xf); setCharY(reg[reg1] & 0xff); break;
              case 0x50: reg1 = (op2 & 0xf); adr = reg[reg1]; drwRect(readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0x60: reg1 = (op2 & 0xf); adr = reg[reg1]; fllRect(readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0x70: reg1 = (op2 & 0xf); adr = reg[reg1]; drwCirc(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0x80: reg1 = (op2 & 0xf); adr = reg[reg1]; fllCirc(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0x90: reg1 = (op2 & 0xf); adr = reg[reg1]; drwTriangle(readInt(adr + 10), readInt(adr + 8), readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0xA0: reg1 = (op2 & 0xf); adr = reg[reg1]; fllTriangle(readInt(adr + 10), readInt(adr + 8), readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0xB0: reg1 = (op2 & 0xf); printfix(reg[reg1], color, bgcolor); break;
              case 0xC0: reg1 = (op2 & 0xf); adr = reg[reg1]; drawChar(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0xD0: reg1 = (op2 & 0xf); adr = reg[reg1]; drawString(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0xE0: reg1 = (op2 & 0xf); adr = reg[reg1]; fontload(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              case 0xF0: reg1 = (op2 & 0xf); adr = reg[reg1]; fontsize(readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
            } break;
          case 0xD2: 
            switch(op2 & 0xf0){
              case 0x00:
                reg1 = (op2 & 0xf);
                if(strBufLength == 0){
                  if(getCharY() > 8) strBufLength = virtualKeyboard(2, 2, strBuf, 16);
                  else strBufLength = virtualKeyboard(2, 78, strBuf, 16);
                  if(strBufLength == 0){ strBuf[0] = '\n'; strBufLength = 1; }
                  setRedrawRect(0, 128); strBufPosition = 0;
                }
                if(strBufLength > 0){
                  if(strBufPosition < strBufLength){ reg[reg1] = strBuf[strBufPosition]; }
                  else{ strBufPosition = 0; strBufLength = 0; pc -= 2; }
                  strBufPosition++;
                } else pc -= 2;
                thiskey = 0; break;
              case 0x10: reg1 = (op2 & 0xf); reg[reg1] = thiskey; break;
            } break;
          case 0xD3: reg1 = op2 >> 4; reg2 = op2 & 0xf; setPix(reg[reg1], reg[reg2], color); break;
          case 0xD4:
            switch(op2 & 0xf0){
                case 0x00: reg1 = op2 & 0xf; adr = reg[reg1]; drawImg(readInt(adr + 8), readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0x10: reg1 = op2 & 0xf; color = reg[reg1] & 0xf; break;
                case 0x20: reg1 = op2 & 0xf; bgcolor = reg[reg1] & 0xf; break;
                case 0x30: reg1 = op2 & 0xf; reg[reg1] = color; break;
                case 0x40: reg1 = op2 & 0xf; reg[reg1] = bgcolor; break;
                case 0x50: reg1 = op2 & 0xf; setImageSize(reg[reg1]); break;
                case 0x60: reg1 = op2 & 0xf; adr = reg[reg1]; drwLine(readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0x070: reg1 = op2 & 0xf; adr = reg[reg1]; drawImgRLE(readInt(adr + 8), readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0x80: reg1 = op2 & 0xf; adr = reg[reg1]; loadTile(readInt(adr + 8), readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0x90: reg1 = op2 & 0xf; adr = reg[reg1]; spriteSetDirectionAndSpeed(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0xA0: reg1 = op2 & 0xf; adr = reg[reg1]; drawImageBit(readInt(adr + 8), readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0xB0: reg1 = op2 & 0xf; adr = reg[reg1]; setClip(readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0xC0: reg1 = op2 & 0xf; if(reg[reg1] >= 1 && reg[reg1] <= 40) timeForRedraw = 1000 / reg[reg1]; break;
                case 0xD0: reg1 = op2 & 0xf; setTileCollisionMap(reg[reg1]); break;
              } break;
          case 0xD5: reg1 = op2 >> 4; reg2 = op2 & 0x0f; setSpr((reg[reg1] < SPRITE_COUNT) ? reg[reg1] : 0, reg[reg2]); break;
          case 0xD6: reg1 = op2 >> 4; reg2 = op2 & 0xf; changePalette(reg[reg1], reg[reg2]); break;
          case 0xD7:
              reg1 = op2 & 0xf; adr = reg[reg1];
              switch(op2 & 0xf0){
                case 0x0: setParticle(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0x10: setEmitter(readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0x20: drawParticle(readInt(adr + 4), readInt(adr + 2), readInt(adr) & 0xf); break;
                case 0x50: reg[1] = distancepp(readInt(adr + 6), readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
                case 0x60: setEmitterSize(readInt(adr + 4), readInt(adr + 2), readInt(adr)); break;
              } break;
          case 0xD8: reg1 = op2 >> 4; reg2 = op2 & 0xf; scrollScreen(1, reg[reg2]); break;
          case 0xD9: reg1 = op2 >> 4; reg2 = op2 & 0xf; reg[reg1] = getPix(reg[reg1], reg[reg2]); break;
          case 0xDA: reg1 = op2 >> 4; reg2 = op2 & 0xf; drawTile(reg[reg1], reg[reg2]); break;
          case 0xDB: reg1 = (op2 & 0xf0) >> 4; reg2 = op2 & 0xf; setSprSize((reg[reg1] < SPRITE_COUNT) ? reg[reg1] : 0, reg[reg2]); break;
          case 0xDC: reg1 = op2 >> 4; reg2 = op2 & 0xf; reg[reg1] = getSpriteValue((reg[reg1] < SPRITE_COUNT) ? reg[reg1] : 0, reg[reg2]); break;
          case 0xDD: break;
          case 0xDE: reg1 = op2 >> 4; reg2 = op2 & 0xf; reg[reg1] = angleBetweenSprites(reg[reg1], reg[reg2]); break;
          case 0xDF: reg1 = op2 >> 4; reg2 = op2 & 0xf; reg[reg1] = getTileInXY(reg[reg1], reg[reg2], 0); break;
        } goto OP_DONE;

    OP_E:
        reg1 = (op1 & 0xf); reg2 = op2 >> 4; reg3 = op2 & 0xf;
        setSprPosition((reg[reg1] < SPRITE_COUNT) ? reg[reg1] : 0, reg[reg2], reg[reg3]);
        if(getSpriteValue((reg[reg1] < SPRITE_COUNT) ? reg[reg1] : 0, 7) < 1)
          setSpriteValue((reg[reg1] < SPRITE_COUNT) ? reg[reg1] : 0, 7, 1);
        goto OP_DONE;

    OP_F:
        reg1 = (op1 & 0xf); reg2 = op2 >> 4; reg3 = op2 & 0xf;
        setSpriteValue((reg[reg1] < SPRITE_COUNT) ? reg[reg1] : 0, reg[reg2], reg[reg3]); 
        goto OP_DONE;

    OP_DONE:;
  }
}
