/*
 Name:		MRDL_Reactive_Leds_Test.ino
 Created:	2/13/2021 5:08:27 PM
 Author:	Lesley Wagner

 Description: Test for WS2813B led strip using fastled library.
*/

#include "FastLED.h"
#include "arm_math.h"
#include "arm_const_structs.h"

#define numLeds 117
#define dataPin 14
#define N_SAMPLES 100000
#define sampleBias 1522 // the DC bias of the microphone, 1.25 V
#define maxPeak 1240 // Max peak AC signal, 1 V
#define sqrt_2 1.4142 // square root of 2
#define fftLength 1024

double getAverage(short* samples);
double getRms(short* samples);

CRGB leds[numLeds];
short ledsOn; // number of leds that are turned on

short samples[N_SAMPLES];
double average;
double rms;
double peak;

arm_rfft_instance_q15 fftInstance;

void setup() {
    // put your setup code here, to run once:
    // analogReference(EXTERNAL);
    pinMode(A1, INPUT);
    pinMode(dataPin, OUTPUT);

    analogReadRes(10);          // set ADC resolution to this many bits
    analogReadAveraging(1);    // average this many readings

    FastLED.addLeds<1, WS2813, dataPin, GRB>(leds, numLeds);

    arm_rfft_init_q15(&fftInstance, fftLength, 0, 1);
}

void loop() {
    // reading 100000 samples takes approximately 574 milliseconds
    for (int i = 0; i < N_SAMPLES; i++) {
        samples[i] = analogRead(A1); // subtract the DC bias value in order to analyse the AC signal
    }

    average = getAverage(samples);
    Serial.println(average);
    delay(1000);

    ledsOn = numLeds * peak / maxPeak;

    for (int i = 0; i < ledsOn; i++) {
        leds[i] = CRGB::Blue;
    }
    for (int i = ledsOn; i < numLeds; i++) {
        leds[i] = CRGB::Black;
    }

    FastLED.show();
}

/*
 * Returns average from an array of samples.
 */
double getAverage(short* samples) {
    long sum = 0;

    for (int i = 0; i < N_SAMPLES; i++) {
        sum += samples[i];
    }

    return (double)sum / N_SAMPLES;
}

/*
 * Returns rms value from an array of samples..
 */
double getRms(short* samples) {
    long long sum = 0;

    for (int i = 0; i < N_SAMPLES; i++) {
        sum += (long long)samples[i] * samples[i];
    }
    return sqrt((double)sum / N_SAMPLES);
}

/*
 * Returns peak value from an array of samples from an AC signal.
 */
double getPeak(short* samples) {
    long long sum = 0; // sum of the squares

    for (int i = 0; i < N_SAMPLES; i++) {
        sum += (long long)samples[i] * samples[i];
    }
    return sqrt(2 * sum / (long double)N_SAMPLES); // sqrt(2)*rms
}
