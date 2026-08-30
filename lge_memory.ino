#include <stdint.h>
#include "settings.h"

uint8_t *lge_mem __attribute__ ((aligned));

void memoryAlloc(){
  lge_mem = (uint8_t*)malloc(RAM_SIZE * sizeof(uint8_t));
  if(lge_mem == NULL)
    Serial.println(F("Out of memory: lge_mem"));
    
  screenMemoryAlloc();
}

void memoryFree(){
  screenMemoryFree();
  free(lge_mem);
}

// Возвращаем проверки границ, так как 20480 не является степенью двойки
inline void writeMem(uint16_t adr, int16_t n){
  if(adr < RAM_SIZE) {
    lge_mem[adr] = n;
  }
}

inline uint8_t readMem(uint16_t adr){
  if(adr < RAM_SIZE) {
    return lge_mem[adr];
  }
  return 0;
}

inline void writeInt(uint16_t adr, int16_t n){
  if(adr < RAM_SIZE - 1) {
    lge_mem[adr] = n & 0xFF;
    lge_mem[adr + 1] = (n >> 8) & 0xFF;
  }
}

inline int16_t readInt(uint16_t adr){
  if(adr < RAM_SIZE - 1) {
    return lge_mem[adr] | (lge_mem[adr + 1] << 8);
  }
  return 0;
}
