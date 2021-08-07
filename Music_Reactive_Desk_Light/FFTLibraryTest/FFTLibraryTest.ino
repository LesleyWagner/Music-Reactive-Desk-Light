/*
 Name:		FFTLibraryTest.ino
 Created:	2/14/2021 8:47:36 PM
 Author:	lesley wagner

 Description: Test for the FFT functions of the CMSIS DSP library.
*/

#include "FastLED.h"
#include "arm_math.h"
#include "arm_const_structs.h"

#define numLeds 117
#define dataPin 14
#define N_SAMPLES 1024
#define sampleBias 1522 // the DC bias of the microphone, 1.25 V
#define maxPeak 1240 // Max peak AC signal, 1 V
#define sqrt_2 1.4142 // square root of 2
#define fftLength 1024
#define bassUpper 300 // bass upper frequency in Hz
#define midUpper 1500 // mid upper frequency in Hz
#define trebUpper 5000 // treble upper frequency in Hz
#define fundamentalFreq 273 // fundamental frequency in deciHz



double getAverage(short* samples);
double getRms(short* samples);

CRGB leds[numLeds];
short ledsOn; // number of leds that are turned on

q15_t samples[N_SAMPLES];
q15_t fftOutput[N_SAMPLES*2];
q15_t frequencies[N_SAMPLES];

double average;
double rms;
double peak;

arm_rfft_instance_q15 fftInstance;

void setup() {
    // put your setup code here, to run once:
    // analogReference(EXTERNAL);
    pinMode(A1, INPUT);
    pinMode(dataPin, OUTPUT);

    analogReadRes(12);          // set ADC resolution to this many bits
    analogReadAveraging(1);    // average this many readings

    FastLED.addLeds<1, WS2813, dataPin, GRB>(leds, numLeds);
    Serial.begin(115200);
    Serial.println("Hello");
}

void loop() {
    // reading 100000 samples takes approximately 574 milliseconds
    long micros1, micros2;
    micros1 = micros();
    // Sample window = 36.6 ms, fundamental frequency 27.3 Hz
    for (int i = 0; i < N_SAMPLES; i++) {
        delayMicroseconds(30);
        /*
        Minimum value is -maxPeak = -1240
        Maximum value is maxPeak = 1240
         */
        samples[i] = (analogRead(A1) - sampleBias) * 26; // scale samples to maximise resolution
        // samples[i] = arm_sin_q15((i*128) % 32768); // sample 4 periods of a sine wave
    }
    micros2 = micros();
    Serial.print("Time to compute fft: ");
    Serial.println(micros2 - micros1);
    

    // peak = getPeak(samples);
    // Serial.println(peak);
    arm_rfft_init_q15(&fftInstance, fftLength, 0, 1);
    arm_rfft_q15(&fftInstance, samples, fftOutput); // Q10.6 output format

    /*Serial.println("Fundamental frequency: ");
    Serial.print("Real: ");
    Serial.println(fftOutput[2]); 
    Serial.print("Imaginary: ");
    Serial.println(fftOutput[3]);*/

    q15_t max;
    uint32_t maxIndex;
    arm_max_q15(fftOutput, fftLength, &max, &maxIndex);
    Serial.print("Max value in fft output: ");
    Serial.println(max);
    arm_cmplx_mag_q15(fftOutput, frequencies, N_SAMPLES); // output is in Q2,14 format

    Serial.print("Fundamental frequency (170 Hz): ");
    unsigned char* p1 = (unsigned char*)(frequencies + 1);
    for (int i = (sizeof *frequencies) - 1; i >= 0; i--) {
        Serial.print(p1[i], HEX);
    }
    Serial.println();

    /*Serial.print("4th harmonic : ");
    unsigned char* p2 = (unsigned char*)(frequencies + 4);
    for (int i = (sizeof * frequencies) - 1; i >= 0; i--) {
        Serial.print(p2[i], HEX);
    }
    Serial.println();

    Serial.print("Real: ");
    unsigned char* p3 = (unsigned char*)(fftOutput + 8);
    for (int i = (sizeof * fftOutput) - 1; i >= 0; i--) {
        Serial.print(p3[i], HEX);
    }
    Serial.println();
    Serial.println(fftOutput[8]);
    Serial.print("Imaginary: ");
    unsigned char* p4 = (unsigned char*)(fftOutput + 9);
    for (int i = (sizeof * fftOutput) - 1; i >= 0; i--) {
        Serial.print(p4[i], HEX);
    }
    Serial.println(fftOutput[9]);*/

    for (int i = 2; i < 50; i++) {
        Serial.print("Harmonic ");
        Serial.print(i);
        Serial.print(": ");
        unsigned char* p = (unsigned char*)(frequencies + i);
        for (int j = (sizeof *frequencies) - 1; j >= 0 ; j--) {
            Serial.print(p[j], HEX);
        }
        Serial.println();
        /*Serial.print("Real: ");

        unsigned char* p5 = (unsigned char*)(fftOutput + 2*i);
        for (int j = (sizeof * fftOutput) - 1; j >= 0; j--) {
            Serial.print(p5[j], HEX);
        }
        Serial.println();
        Serial.print("Imaginary: ");
        unsigned char* p6 = (unsigned char*)(fftOutput + 2*i + 1);
        for (int j = (sizeof * fftOutput) - 1; j >= 0; j--) {
            Serial.print(p6[j], HEX);
        }
        Serial.println();*/
    }

    /*    ledsOn = numLeds * peak / maxPeak;

    for (int i = 0; i < ledsOn; i++) {
        leds[i] = CRGB::Blue;
    }
    for (int i = ledsOn; i < numLeds; i++) {
        leds[i] = CRGB::Black;
    }

    FastLED.show();*/
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

