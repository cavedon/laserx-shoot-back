/*
Copyright (c) 2023 Ludovico Cavedon <ludovico.cavedon@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <IRremote.hpp>
#include <RGBLed.h>

const int IR_RECEIVE_PIN = 2;
const int IR_SEND_PIN = 3;

// RGB LED
const int RED_PIN = 11;
const int GREEN_PIN = 10;
const int BLUE_PIN = 9;

// Infrared commands
const uint8_t HIT_RED = 0x51;  // Hit RED players
const uint8_t HIT_BLUE = 0x52;  // Hit BLUE players
const uint8_t HIT_ALL = 0x53;  // Hit ALL players

// Timing of infrared commands (in microseconds)
// Infradred commands are in the format:
//   HEADER  PAUSE  BIT_7  PAUSE  BIT_6  PAUSE ... BIT_0
const int HEADER_USEC = 6200;  // Duration of the header
const int PAUSE_USEC = 600;  // Duration of the pause
const int ZERO_USEC = 450;  // Duration of a zero bit
const int ONE_USEC = 1500;  // Duration of a one bi

// Once shot, keep shooting at a random interval between the two values below.
const long SHOOT_AGAIN_MIN_MILLIS = 1000;
const long SHOOT_AGAIN_MAX_MILLIS = 10000;

// Internal state
unsigned long ledOffAt = 0;  // Turn off the RGB LED at this time (in millis)
const unsigned long BLINK_INTERVAL = 50;  // Duration of RGB LED flashes (in millis)
bool ledOn = false;  // Whether the RGB LED is is on or off
unsigned long flipLedAt = 0;  // Flip the RGB LED on/off at this time (in millis)
int *ledColor;  // The color of the RGB LED
unsigned long shootAt = 0;  // Shoot all player at this time (in millis)

RGBLed led(RED_PIN, GREEN_PIN, BLUE_PIN, RGBLed::COMMON_CATHODE);

// Buffer for infrared sequence to be transmitted
uint16_t sequence[] = {
  HEADER_USEC,
  PAUSE_USEC,
  0,  // bit 7
  PAUSE_USEC,
  0,  // bit 6
  PAUSE_USEC,
  0,  // bit 5
  PAUSE_USEC,
  0,  // bit 4
  PAUSE_USEC,
  0,  // bit 3
  PAUSE_USEC,
  0,  // bit 2
  PAUSE_USEC,
  0,  // bit 1
  PAUSE_USEC,
  0,  // bit 0
};

// Send an 8-bit infrafred sequence 
void send_sequence(uint8_t command) {
  for (int i = 0; i < 8; ++i) {
    sequence[16 - i * 2] = (command >> i) & 1 ? ONE_USEC : ZERO_USEC;
  }
  Serial.print("Sending 0x");
  Serial.println(command, HEX);
  IrSender.sendRaw(sequence, sizeof(sequence), 38 /* kHz */);
}

// Shoot all players 
void shoot() {
  Serial.println("Shooting all players");
  led.setColor(RGBLed::WHITE);
  send_sequence(HIT_ALL);
  // Shoot again 
  shootAt = millis() + random(SHOOT_AGAIN_MIN_MILLIS, SHOOT_AGAIN_MAX_MILLIS);
  led.off();
}

void setup() {
  Serial.begin(9600);
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  IrSender.begin(IR_SEND_PIN);
  // Uncomment the line below during debuggin to wait for the serial port to be connected
  //while (!Serial) {};
  Serial.println("Started");
}

void loop() {
  unsigned long now = millis();
  if (ledOffAt > 0) {
    if (now >= ledOffAt) {
      // The RGB LED needs to be turned off
      led.off();
      ledOffAt = 0;
      ledOn = false;
    } else if (now >= flipLedAt) {
      // The RGB LED needs to flip between on/off (blinking)
      ledOn = !ledOn;
      if (ledOn) {
        led.setColor(ledColor);
      } else {
        led.off();
      }
      flipLedAt += BLINK_INTERVAL;
    }
  }

  if (shootAt > 0 && now >= shootAt) {
    // It is time to shoot again
    shoot();
  }

  if (IrReceiver.decode()) {
    // Dump the IR data received. The first number is length of the infrared packets.
    // The other numbers are the length (in ticks) of markes and pauses (in square breackets).
    irparams_struct *d = IrReceiver.decodedIRData.rawDataPtr;
    Serial.print(d->rawlen);
    Serial.print(" ");
    for (int i = 1; i < d->rawlen; ++i) {
      Serial.print(" ");
      if (i % 2 == 0) {
        Serial.print("[");
      }
      if (d->rawbuf[i] < 10) {
        Serial.print(" ");
      }
      Serial.print(d->rawbuf[i]);
      if (i % 2 == 0) {
        Serial.print("]");
      }
    }
    uint8_t msg = 0;
    bool valid = false;
    if (d && d->rawlen >= 18) {
      // If we have at least 18 numbers (initial pause, header, 8 bits with respective pause),
      // then decode the packets. All durations are in ticks (1 tick is 50 microseconds).
      // Header should be about 125 ticks
      if (d->rawbuf[1] >= 110 && d->rawbuf[1] <= 140) {
        // Header is about 125 ticks
        valid = true;
        for (int i = 2; i < 18; ++i) {
          if (i % 2 == 0) {
            // Pauses should be around 12 ticks
            if (d->rawbuf[i] < 4 || d->rawbuf[i] > 15) {
              valid = false;
              break;
            }
          } else {
            if (d->rawbuf[i] < 4) {
              valid = false;
              break;
            } else if (d->rawbuf[i] <= 16) {
              // Bit 0 should be around 9 ticks 
              msg <<= 1;
            } else if (d->rawbuf[i] < 26) {
              valid = false;
              break;
            } else if (d->rawbuf[i] <= 34) {
              // Bit 0 should be around 30 ticks
              msg = (msg << 1) | 1;
            } else {
              break;
            }
          }
        }
      }
    }
    // Enable receiving of the next value
    IrReceiver.resume();
    if (valid) {
      Serial.print(" 0x");
      Serial.print(msg, HEX);
    }
    Serial.println();
    if (valid) {
      // We have received a valid command.
      // Blink the RGB the led matching the color of the player who shot.
      switch (msg) {
        case HIT_BLUE:
          Serial.println("Shot by RED");
          ledColor = RGBLed::RED;
          break;
        case HIT_RED:
          Serial.println("Shot by BLUE");
          ledColor = RGBLed::BLUE;
          break;
        case HIT_ALL:
          Serial.println("Shot by ALL");
          ledColor = RGBLed::MAGENTA;
          break;
        default:
          Serial.println("Unknown message");
          ledColor = RGBLed::YELLOW;
          break;
      }
      unsigned long now = millis();
      led.setColor(ledColor);
      ledOn = true;
      flipLedAt = now + BLINK_INTERVAL;
      // Blink then RGB LED for 1 second, then shoot back all players.
      ledOffAt = now + 1000;
      shootAt = ledOffAt;
    }
  }
}