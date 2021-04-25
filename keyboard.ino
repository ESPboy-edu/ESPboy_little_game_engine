#include <Wire.h>
#include "settings.h"

void geti2cAdress(){
  uint8_t error,address;
  i2c_adress=0;
  for(address = 1; address < 127; address++ ){
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0){
      i2c_adress=address;
      return;
    }
    yield();
  }
}

void scani2c(){
  uint8_t error, address;
  int nDevices;
  nDevices = 0;
  for(address = 1; address < 127; address++ ){
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0){
      Serial.print(F("I2C device found at address 0x"));
      if (address<16)
        Serial.print(F("0"));
      Serial.print(address,HEX);
      Serial.println(F("  !"));
      nDevices++;
    }
    else if (error==4){
      Serial.print(F("Unknown error at address 0x"));
      if (address<16)
        Serial.print(F("0"));
      Serial.println(address,HEX);
    }    
  }
  if (nDevices == 0)
    Serial.println(F("No I2C devices found\n"));
}

#ifdef ESPBOY
void getKey(){
  uint8_t keyread;
  thiskey = 0;
  keyread = myESPboy.mcp.readGPIOAB()&255;
  if(!(keyread&1)) thiskey |= 4; 
  if(!(keyread&2)) thiskey |= 1;
  if(!(keyread&4)) thiskey |= 2;
  if(!(keyread&8)) thiskey |= 8;
  if(!(keyread&16)) thiskey |= 16;
  if(!(keyread&32)) thiskey |= 32;
  if(!(keyread&64)) thiskey |= 64;
  if(!(keyread&128)) thiskey |= 128;
}

#else
void getKey(){
  uint8_t dio_in;
  Wire.beginTransmission(i2c_adress);
  Wire.write(B11111111); //Конфигурация всех портов PCF8574P на клавиатуре как входа
  Wire.endTransmission();
  Wire.requestFrom(i2c_adress,1);
  dio_in = Wire.read();  //читаем состояние портов PCF8574P(кнопок)
  thiskey = 0;
  if((dio_in & 128) == 0)
    thiskey |= 4;
  if((dio_in & 64) == 0)
    thiskey |= 8;
  if((dio_in & 32) == 0)
    thiskey |= 2;
  if((dio_in & 16) == 0)
    thiskey |= 1; //up
  if((dio_in & 8) == 0)
    thiskey |= 128;
  if((dio_in & 4) == 0)
    thiskey |= 32;
  if((dio_in & 2) == 0)
    thiskey |= 16; 
  if((dio_in & 1) == 0)
    thiskey |= 64; 
}
#endif

const uint8_t v_keys[] PROGMEM =      "0123456789[]qwertyuiop\"/asdfghjkl;= zxcvbnm,.-  ";
const uint8_t v_keysShift[] PROGMEM = "!@#$%^&*(){}QWERTYUIOP|?ASDFGHJKL:+ ZXCVBNM<>_  ";

uint8_t virtualKeyboard(uint8_t kx, uint8_t ky, char buf[], uint8_t len){
  int16_t x = 4, y = 4, px = 0, py = 0, pos = 0;
  uint8_t isShift = 0;
#ifdef ESPBOY
  TFT_eSprite img = TFT_eSprite(&myESPboy.tft);
#else
  TFT_eSprite img = TFT_eSprite(&tft);
#endif  
  img.setColorDepth(1);
  img.createSprite(120, 32);
  img.setTextSize(2);
#ifdef ESPBOY
  myESPboy.tft.fillRect(kx, ky, 124, 48, 0x0000);
  myESPboy.tft.drawRect(kx, ky, 124, 48, 0xFC00);  
#else
  tft.fillRect(kx, ky, 124, 48, 0x0000);
  tft.drawRect(kx, ky, 124, 48, 0xFC00);  
#endif
  getKey();
  while(1){
     while(x != px * 16 || y !=  py * 16){
        if(x < px * 16)
          x += 4;
        else if(x > px * 16)
          x -= 4;
        if(y < py * 16)
          y += 4;
        else if(y > py * 16)
          y -= 4;
        img.fillRect(0, 0, 120, 32, 0x0000);
        for(uint8_t i = 0; i < 4; i++){
          for(uint8_t j = 0; j < 12; j++){
            if(i == 3 && j == 11){
              img.setTextSize(1);
              img.drawChar('D', 56 + j * 16 - x - 4, i * 16 - y + 4);
              img.drawChar('e', 56 + j * 16 - x + 2, i * 16 - y + 4);
              img.drawChar('l', 56 + j * 16 - x + 8, i * 16 - y + 4);
              img.setTextSize(2);
            }
            else if(i == 2 && j == 11){
              img.setTextSize(1);
              img.drawChar('E', 56 + j * 16 - x - 4, i * 16 - y);
              img.drawChar('n', 56 + j * 16 - x + 2, i * 16 - y);
              img.drawChar('t', 56 + j * 16 - x - 4, i * 16 - y + 8);
              img.drawChar('e', 56 + j * 16 - x + 2, i * 16 - y + 8);
              img.drawChar('r', 56 + j * 16 - x + 8, i * 16 - y + 8);
              img.setTextSize(2);
            }
            else{
              if(isShift)
                img.drawChar(pgm_read_byte(&v_keysShift[i * 12 + j]), 56 + j * 16 - x, i * 16 - y);
              else
                img.drawChar(pgm_read_byte(&v_keys[i * 12 + j]), 56 + j * 16 - x, i * 16 - y);
            }
          }
        }
        img.pushSprite(kx + 2, ky + 14);
        #ifdef ESPBOY
        myESPboy.tft.drawRect(kx + 53, ky + 13, 18, 17, 0xFC00);
        #else
        tft.drawRect(kx + 53, ky + 13, 18, 17, 0xFC00);
        #endif
        delay(10);
    }
    delay(200);
  #ifdef ESPBOY
    if (keybModule.getPressedKey()){
      if((char)keybModule.getLastPressedKey() == '>'){
        img.deleteSprite();
        if(pos + 1 < len){
          buf[pos] = '\n';
          pos++;
        }
        return pos;
      }
      if((char)keybModule.getLastPressedKey() == '<'){
         if(pos > 0){
           buf[pos] = 0;
           pos--;
         }
       }
      else{
         if(pos < len){
           buf[pos] = (char)keybModule.getLastPressedKey();
           pos++;
         }
      }
      myESPboy.tft.fillRect(kx + 1, ky + 1, 122, 10, 0x0000);
      myESPboy.tft.setCursor(kx + 4,ky + 3);
      for(int i = max(0, pos - 10); i < pos; i++)
      myESPboy.tft.print(buf[i]);
      myESPboy.tft.setTextColor(0x6d2d);
      myESPboy.tft.setCursor(kx + 100 - ((pos > 9) ? ((pos > 99)? 12: 6) : 0) - ((len > 9) ? ((len > 99)? 12: 6) : 0), ky + 3);
      myESPboy.tft.print(pos);
      myESPboy.tft.print('/');
      myESPboy.tft.print(len);
      myESPboy.tft.setTextColor(0xffff);
    }
  #endif
    getKey();
    if(thiskey == 192){ //key select + start
      if(pause())
        return 0;
    }
    if(thiskey & 2){//down
      if(py < 3)
        py++;
      else
        py = 0;
    }
    else if(thiskey & 1){//up
      if(py > 0)
        py--;
      else
        py = 3;
    }
    else if(thiskey & 8){//left
      if(px < 11)
        px++;
      else
        px = 0;
    }
    else if(thiskey & 4){//right
      if(px > 0)
        px--;
      else
        px = 11;
    }
    else if(thiskey & 16){//ok
      if(py == 3 && px == 11){//delete
        if(pos > 0){
          buf[pos] = 0;
          pos--;
        }
      #ifdef ESPBOY
        myESPboy.tft.fillRect(kx + 1, ky + 1, 122, 10, 0x0000);
        myESPboy.tft.setCursor(kx + 4,ky + 3);
        for(int i = max(0, pos - 10); i < pos; i++)
          myESPboy.tft.print(buf[i]);
        myESPboy.tft.setTextColor(0x6d2d);
        myESPboy.tft.setCursor(kx + 100 - ((pos > 9) ? ((pos > 99)? 12: 6) : 0) - ((len > 9) ? ((len > 99)? 12: 6) : 0), ky + 3);
        myESPboy.tft.print(pos);
        myESPboy.tft.print('/');
        myESPboy.tft.print(len);
        myESPboy.tft.setTextColor(0xffff);
      #else
        tft.fillRect(kx + 1, ky + 1, 122, 10, 0x0000);
        tft.setCursor(kx + 4,ky + 3);
        for(int i = max(0, pos - 10); i < pos; i++)
          tft.print(buf[i]);
        tft.setTextColor(0x6d2d);
        tft.setCursor(kx + 100 - ((pos > 9) ? ((pos > 99)? 12: 6) : 0) - ((len > 9) ? ((len > 99)? 12: 6) : 0), ky + 3);
        tft.print(pos);
        tft.print('/');
        tft.print(len);
        tft.setTextColor(0xffff);
      #endif
        delay(200);
      }
      else if(py == 2 && px == 11){
        img.deleteSprite();
        if(pos + 1 < len){
          buf[pos] = '\n';
          pos++;
        }
        return pos;
      }
      else{
        if(pos < len){
          if(isShift)
            buf[pos] = pgm_read_byte(&v_keysShift[px + py * 12]);
          else
            buf[pos] = pgm_read_byte(&v_keys[px + py * 12]);
         #ifdef ESPBOY
          myESPboy.tft.fillRect(kx + 1, ky + 1, 122, 10, 0x0000);
          myESPboy.tft.setCursor(kx + 4,ky + 3);
          pos++;
          for(int i = max(0, pos - 10); i < pos; i++)
            myESPboy.tft.print(buf[i]);
          myESPboy.tft.setTextColor(0x6d2d);
          myESPboy.tft.setCursor(kx + 100 - ((pos > 9) ? ((pos > 99)? 12: 6) : 0) - ((len > 9) ? ((len > 99)? 12: 6) : 0), ky + 3);
          myESPboy.tft.print(pos);
          myESPboy.tft.print('/');
          myESPboy.tft.print(len);
          myESPboy.tft.setTextColor(0xffff);
         #else
          tft.fillRect(kx + 1, ky + 1, 122, 10, 0x0000);
          tft.setCursor(kx + 4,ky + 3);
          pos++;
          for(int i = max(0, pos - 10); i < pos; i++)
            tft.print(buf[i]);
          tft.setTextColor(0x6d2d);
          tft.setCursor(kx + 100 - ((pos > 9) ? ((pos > 99)? 12: 6) : 0) - ((len > 9) ? ((len > 99)? 12: 6) : 0), ky + 3);
          tft.print(pos);
          tft.print('/');
          tft.print(len);
          tft.setTextColor(0xffff);
        #endif
        }
      }
    }
    else if(thiskey & 32){//shift
      isShift = 1 - isShift;
      x += 4;
    }
  }
  return 0;
}
