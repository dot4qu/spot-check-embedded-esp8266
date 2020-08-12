/*-----------------------------------------------------------------------------
  Spot Check Display
  Receives text serially over UART and displays it by scrolling the characters sideways
  on a set of LED strips arranged in a matrix.

  Scrolling logic heavily borrowed and apapted from Allen C. Huffman's Library here:
  https://github.com/allenhuffman/LEDSign
  Simple LED Scrolling Message
  By Allen C. Huffman (alsplace@pobox.com)
  www.subethasoftware.com

  -----------------------------------------------------------------------------*/
#define VERSION "0.1"

#include <Adafruit_NeoPixel.h>
#include "tides_display.h"

// TVOut library installed from forked repo that bundles the TVOut and TVoutfonts
// libraries together instead of the messiness double-library that's in the main repo.
// https://github.com/pkendall64/arduino-tvout
// Simply download the zip of the repo and add to you sketch through
// Sketch -> Include Library -> ZIP library
#include <TVout.h>
#include <TVoutfonts/fontALL.h>

#include <SoftwareSerial.h>

// Prints ascii text through the serial port that mirrors LEDs when defined
//#define DEBUG

#define PIN            3            // Data pin for the LED strips

#define LEDSTRIPS      6            // Total count of separate strip rows
#define LEDSPERSTRIP   50           // Number of individual LED groups per strip
#define LEDSPERROW     50           // Unless rows are spiraled, this matches LEDSPERSTRIP
#define LEDS           (LEDSPERSTRIP*LEDSTRIPS)
#define ROWS           (LEDS/LEDSPERROW)

#define LAYOUTSTART    BOTTOMRIGHT  // First LED where data comes in [TOPLEFT, TOPRIGHT, BOTTOMLEFT, or BOTTOMRIGHT]
#define LAYOUTMODE     ZIGZAG       // Is the end of a strip connected on the same side of the matrix
// to the next [ZIGZAG], or back at the next strips beginning? [STRAIGHT]

#define SCROLLSPEED    100          // Speed in ms to delay before shifting text
#define LEDBRIGHTNESS  64           // Neopixel param between 0-255 for brightness. Be mindful of power consumption (start low and work up)
#define MAXMSGLEN 80                // Longest message we can display.

#define FONTWIDTH      (pgm_read_byte_near(&font[0])) // Font arrays hold metadata in their first 3 bytes
#define FONTHEIGHT     (pgm_read_byte_near(&font[1]))
#define FONTSTARTCHAR  (pgm_read_byte_near(&font[2]))
#define FONTDATAOFFSET 3                              // First byte in font data that's actual text bytes

#define ESP_BAUD_RATE 9600                  // Rate at which the ESP8266 is serially sending data
#define SERIAL_RX_PIN 10
#define SERIAL_TX_PIN 11
#define SERIAL_JSON_TIMEOUT_MILLIS 1000

const unsigned char *font = font4x6;    // Font data bytes. Most fonts barely or don't at all use their bottom row or two, so you
// might be able to make a 6x8 work with only 6 or 7 rows depnding on your text

// Parameter 3 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
Adafruit_NeoPixel strip = Adafruit_NeoPixel(LEDS, PIN, NEO_GRB + NEO_KHZ800);

SoftwareSerial esp_serial(SERIAL_RX_PIN, SERIAL_TX_PIN);

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

// For the Adafruit LPN8806/NeoPixel libraries, three bytes of RAM are
// used for each pixel. Make sure we're not going over our limits
#if (LEDS*3>2000)
#error USING UP OVER 2000 BYTES OF RAM!
#endif

// A debug Serial.print()/Serial.println() macro.
#if defined(DEBUG)
#define DEBUG_PRINT(...)   Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else // If not debugging, it will not be included.
#define DEBUG_PRINT(...)
#define DEBUG_PRINTLN(...)
#endif

void display_text(char* message, int message_length) {
  uint8_t fontWidth, fontHeight, fontStartChar;
  uint8_t letter, fontByte, fontBit;
  uint8_t letterOffset;
  uint8_t row, col;
  uint8_t offset;
  char    ch;
  uint8_t layoutStart, layoutMode;
  uint8_t colDir, rowDir;
  uint8_t colOffset, rowOffset;

  layoutStart = LAYOUTSTART;
  layoutMode = LAYOUTMODE;

  // If LED 0 starts at the bottom, we need to invert the rows when we
  // display the message.
  if (layoutStart == TOPLEFT || layoutStart == TOPRIGHT)
  {
    rowDir = DOWN;
  }
  else
  {
    rowDir = UP;
  }

  // If we start from the right side, we will be going backwards.
  if (layoutStart == TOPLEFT || layoutStart == BOTTOMLEFT)
  {
    colDir = RIGHT;
  }
  else
  {
    colDir = LEFT;
  }

  fontWidth = FONTWIDTH;
  fontHeight = FONTHEIGHT;
  fontStartChar = FONTSTARTCHAR;

  // Loop through each letter in the message.
  for (letter = 0; letter < message_length; letter++)
  {
    // Scroll fontWidth pixels for each letter.
    for (offset = 0; offset < fontWidth; offset++)
      // If you comment out the above for loop, and then just set offset
      // to 0, the sign will scroll a character at a time.
      //offset = 0;
    {
      // Loop through each row...
      for (row = 0; row < ROWS && row < fontHeight ; row++)
      {
        letterOffset = 0;
        fontBit = offset;

        // If going down (starting at top), we will use the loop row,
        // else we will calculate a row that goes backwards.
        if (rowDir == DOWN)
        {
          rowOffset = row;
        }
        else
        {
          rowOffset = (fontHeight < ROWS ? fontHeight : ROWS) - 1 - row;
        }

        // Now loop through each pixel in the row (column).
        for (col = 0; col < LEDSPERROW; col++)
        {
          // If going right (starting at left), we will use the loop col,
          // else we will calculate a col that goes backwards.
          if (colDir == RIGHT)
          {
            colOffset = col;
          }
          else
          {
            colOffset = LEDSPERROW - 1 - col; // -1
          }

          // If we run out of letters, replace with a space.
          if (letter + letterOffset >= message_length)
          {
            ch = ' ';
          }
          else // Otherwise, get the actual letter.
          {
            ch = message[letter + letterOffset];
          }

          // Get the appropriate byte from the font data.
          if (bitRead(pgm_read_byte_near(&font[FONTDATAOFFSET +
                                               (ch - fontStartChar)*fontHeight + rowOffset]),
                      //(colDir==RIGHT ? 7-fontBit : 7-(fontWidth-1)+fontBit))==1)
                      //(colDir==LEFT ? 7-fontBit : 7-(fontWidth-1)+fontBit))==1)
                      7 - fontBit) == 1)
          {
            // 1 = set a pixel.
            strip.setPixelColor((row * LEDSPERROW) + colOffset,
                                strip.Color(127, 0, 0));

            DEBUG_PRINT(F("#"));
          }
          else
          {
            // 0 = unset a pixel.
            strip.setPixelColor((row * LEDSPERROW) + colOffset, 0);

            DEBUG_PRINT(F(" "));
          }

          // Move to next bit in the character.
          fontBit++;
          // If we get to the width of the font, move to the next letter.
          if (fontBit >= fontWidth)
          {
            fontBit = 0;
            letterOffset++;
          }
        } // end of for (col=0; col<LEDSPERROW; col++)

        DEBUG_PRINTLN();

        // If the LED strips are zig zagged, at the end of each row we
        // will reverse the direction.
        if (layoutMode == ZIGZAG)
        {
          // Invert direction.
          if (colDir == RIGHT)
          {
            colDir = LEFT;
          }
          else
          {
            colDir = RIGHT;
          }
        } // end of if (layoutMode==ZIGZAG)
      } // end of for (row=0; row<ROWS && row<fontHeight ; row++)
      strip.show();

#if defined(DEBUG)
      for (uint8_t i = 0; i < LEDSPERROW; i++) Serial.print(F("-"));
      Serial.println();
#endif
      delay(SCROLLSPEED);
    }
  }
}

void setup()
{
  Serial.begin(57600);

  Serial.print(F("Spot Check Display "));
  Serial.println(VERSION);
  DEBUG_PRINTLN(F("DEBUG MODE"));
  Serial.print(F("Font size    : "));
  Serial.print(FONTWIDTH);
  Serial.print(F("x"));
  Serial.println(FONTHEIGHT);

  strip.begin();
  strip.show();

#if defined(LEDBRIGHTNESS)
  strip.setBrightness(LEDBRIGHTNESS);
#endif

  esp_serial.begin(ESP_BAUD_RATE);
}

void loop() {
  String received_str = "";
  //  unsigned long timeout_start = 0;
  bool display_received_text = false;

  // Assume we'll have 10 strings max
  String display_strs[10];
  int display_str_index = 0;
  bool building_list = false;

  while (1) {
    if (esp_serial.available()) {
      char c = esp_serial.read();
      Serial.print(c);
      
      if (c == '$') {
        // Our string terminator, go ahead and display
        if (building_list) {
          display_strs[display_str_index] = received_str;
          display_str_index++;
        } else {
          Serial.println("Got a '$' terminated string when not in list-building mode");
        }

        received_str = "";
      } else if (c == '%') {
        // Command string
        if (received_str == "START_LIST") {
          display_str_index = 0;
          building_list = true;
        } else if (received_str == "END_LIST") {
          building_list = false;
          display_received_text = true;
        } else {
          Serial.print("Received unsupported command: ");
          Serial.println(received_str);
        }

        received_str = "";
      } else {
        received_str += c;
      }
    }
    
    if (display_received_text) {
      display_received_text = false;
      for (int i = 0; i < display_str_index; i++) {
        Serial.println(display_strs[i]);
        display_text(display_strs[i].c_str(), display_strs[i].length());
      }
      
      received_str = "";
    }
  }
}
