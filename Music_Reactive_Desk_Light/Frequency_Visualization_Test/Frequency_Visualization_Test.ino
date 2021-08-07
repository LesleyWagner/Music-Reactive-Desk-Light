/*
 Name:		FFTLibraryTest.ino
 Created:	2/14/2021 8:47:36 PM
 Author:	lesley wagner

 Description: Test for the visualizing frequencies with FFT functions from the CMSIS DSP library.
*/

#include "FastLED.h"
#include "arm_math.h"
#include "arm_const_structs.h"

#define numLeds 117
#define numLedsBy3 39 // number of leds divided by 3
#define dataPin 14
#define N_SAMPLES 1024
#define sampleBias 1522 // the DC bias of the microphone, 1.25 V
#define maxPeak 1240 // Max peak AC signal, 1 V
#define sqrt_2 1.4142 // square root of 2
#define fftLength 1024
#define bassUpper 3000 // bass upper frequency in deciHz
#define midUpper 15000 // mid upper frequency in deciHz
#define trebleUpper 50000 // treble upper frequency in deciHz
#define maxBass 1000000 // max bass amplitude
#define maxMid 1000000 // max mid amplitude
#define maxTreble 1000000 // max treble amplitude
#define fundamentalFreq 273 // fundamental frequency in deciHz

double getAverage(short* samples);
double getRms(short* samples);

CRGB leds[numLeds];
q31_t bassAmplitude; 
q31_t midAmplitude;
q31_t trebleAmplitude;
short bassLedsOn; // number of leds that are turned on in the bass range
short midLedsOn; // number of leds that are turned on in the bass range
short trebleLedsOn; // number of leds that are turned on in the bass range

q15_t samples[N_SAMPLES];
q15_t fftOutput[N_SAMPLES * 2];
q15_t frequencies[N_SAMPLES];

q31_t bassReal; // sum of real components in the bass frequency range
q31_t bassImaginary; // sum of imaginary components in the bass frequency range
q31_t midReal; // sum of real components in the mid frequency range
q31_t midImaginary; // sum of imaginary components in the mid frequency range
q31_t trebleReal; // sum of real components in the treble frequency range
q31_t trebleImaginary; // sum of imaginary components in the treble frequency range

q31_t bass[2]; // sum of real and sum of imaginary components in the bass frequency range
q31_t mid[2]; // sum of real and sum of imaginary components in the mid frequency range
q31_t treble[2]; // sum of real and sum of imaginary components in the treble frequency range

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
    // long micros1, micros2;
    // micros1 = micros();
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
    // micros2 = micros();
    // Serial.print("Time to compute fft: ");
    // Serial.println(micros2 - micros1);


    // peak = getPeak(samples);
    // Serial.println(peak);
    arm_rfft_init_q15(&fftInstance, fftLength, 0, 1);
    arm_rfft_q15(&fftInstance, samples, fftOutput); // Q10.6 output format

    // Add up fft real and complex magnitudes for base frequencies, < 300 Hz
    int iFFT; // counter
    bass[0] = 0;
    bass[1] = 0;
    mid[0] = 0;
    mid[1] = 0;
    treble[0] = 0;
    treble[1] = 0;
    for (iFFT = 2; (iFFT>>1)*fundamentalFreq < bassUpper; iFFT+=2) {
        bass[0] += abs(fftOutput[iFFT]);
        bass[1] += abs(fftOutput[iFFT + 1]);
    }

    // Add up fft real and complex magnitudes for midrange frequencies, [300, 1500] Hz
    for (iFFT; (iFFT>>1) * fundamentalFreq < midUpper; iFFT+=2) {
        mid[0] += abs(fftOutput[iFFT]);
        mid[1] += abs(fftOutput[iFFT + 1]);
    }

    // Add up fft real and complex magnitudes for treble frequencies, [1500, 5000] Hz
    for (iFFT; (iFFT>>1) * fundamentalFreq < trebleUpper; iFFT+=2) {
        treble[0] += abs(fftOutput[iFFT]);
        treble[1] += abs(fftOutput[iFFT + 1]);
    }
    bass[0] <<= 8;
    bass[1] <<= 8;
    mid[0] <<= 8;
    mid[1] <<= 8;
    treble[0] <<= 8;
    treble[1] <<= 8;

    //for (int i = 2; i < 50; i++) {
    //    Serial.print("Harmonic ");
    //    Serial.print(i);
    //    Serial.print(": ");
    //    unsigned char* p = (unsigned char*)(frequencies + i);
    //    for (int j = (sizeof * frequencies) - 1; j >= 0; j--) {
    //        Serial.print(p[j], HEX);
    //    }
    //    Serial.println();
    //    /*Serial.print("Real: ");

    //    unsigned char* p5 = (unsigned char*)(fftOutput + 2*i);
    //    for (int j = (sizeof * fftOutput) - 1; j >= 0; j--) {
    //        Serial.print(p5[j], HEX);
    //    }
    //    Serial.println();
    //    Serial.print("Imaginary: ");
    //    unsigned char* p6 = (unsigned char*)(fftOutput + 2*i + 1);
    //    for (int j = (sizeof * fftOutput) - 1; j >= 0; j--) {
    //        Serial.print(p6[j], HEX);
    //    }
    //    Serial.println();*/
    //}
    arm_cmplx_mag_q31(bass, &bassAmplitude, 1); // output is in Q2,30 format
    arm_cmplx_mag_q31(mid, &midAmplitude, 1); // output is in Q2,30 format
    arm_cmplx_mag_q31(treble, &trebleAmplitude, 1); // output is in Q2,30 format
    Serial.print("Bass: ");
    Serial.println(bassAmplitude);
    Serial.print("Mid: ");
    Serial.println(midAmplitude);
    Serial.print("Treble: ");
    Serial.println(trebleAmplitude);
    bassLedsOn = numLedsBy3 * bassAmplitude / maxBass;
    midLedsOn = numLedsBy3 * midAmplitude / maxMid;
    trebleLedsOn = numLedsBy3 * trebleAmplitude / maxTreble;
    if (bassLedsOn > numLedsBy3) bassLedsOn = numLedsBy3;
    if (midLedsOn > numLedsBy3) midLedsOn = numLedsBy3;
    if (trebleLedsOn > numLedsBy3) trebleLedsOn = numLedsBy3;

    Serial.print("Bass leds: ");
    Serial.println(bassLedsOn);
    Serial.print("Mid leds: ");
    Serial.println(midLedsOn);
    Serial.print("Treble leds: ");
    Serial.println(trebleLedsOn);

    int ledCounter;
    for (ledCounter = 0; ledCounter < bassLedsOn; ledCounter++) {
        leds[ledCounter] = CRGB::Blue;
    }
    for (; ledCounter < numLedsBy3; ledCounter++) {
        leds[ledCounter] = CRGB::Black;
    }
    for (ledCounter = 0; ledCounter < midLedsOn; ledCounter++) {
        leds[ledCounter + numLedsBy3] = CRGB::Yellow;
    }
    for (; ledCounter < numLedsBy3; ledCounter++) {
        leds[ledCounter + numLedsBy3] = CRGB::Black;
    }
    for (ledCounter = 0; ledCounter < trebleLedsOn; ledCounter++) {
        leds[ledCounter + numLedsBy3*2] = CRGB::Red;
    }
    for (; ledCounter < numLedsBy3; ledCounter++) {
        leds[ledCounter + numLedsBy3*2] = CRGB::Black;
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

