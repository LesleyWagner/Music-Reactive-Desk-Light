/*
 Name:		WS2812_Serial_Library_Test.ino
 Created:	05/24/2021 5:08:27 PM
 Author:	Lesley Wagner

 Description: Test for WS2813B led strip using WS2812 Serial library.
*/

#include "WS2812Serial.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#define USE_WS2812SERIAL
#include <FastLED.h>

// How many leds in your strip?
#define NUM_LEDS 117

// Usable pins:
//   Teensy LC:   1, 4, 5, 24
//   Teensy 3.2:  1, 5, 8, 10, 31   (overclock to 120 MHz for pin 8)
//   Teensy 3.5:  1, 5, 8, 10, 26, 32, 33, 48
//   Teensy 3.6:  1, 5, 8, 10, 26, 32, 33
//   Teensy 4.0:  1, 8, 14, 17, 20, 24, 29, 39
//   Teensy 4.1:  1, 8, 14, 17, 20, 24, 29, 35, 47, 53

#define DATA_PIN 14

// Define the array of leds
CRGB leds[NUM_LEDS];

void setup() {
	Serial.begin(57600);
	Serial.println("resetting");
	LEDS.addLeds<WS2812SERIAL, DATA_PIN, RGB>(leds, NUM_LEDS);
	LEDS.setBrightness(84);
}

void fadeall() { for (int i = 0; i < NUM_LEDS; i++) { leds[i].nscale8(250); } }

void loop() {
	uint8_t hue = 100;
	Serial.print("x");
	// First slide the led in one direction
	for (int i = 0; i < NUM_LEDS; i++) {
		// Set the i'th led to red
		leds[i] = CHSV(hue++, 255, 255);
		// Show the leds
		FastLED.show();
		// Wait a little bit before we loop around and do it again
		delay(20);
	}
}
