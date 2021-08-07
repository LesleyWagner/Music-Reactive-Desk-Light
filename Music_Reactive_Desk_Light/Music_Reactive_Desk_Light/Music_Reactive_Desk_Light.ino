/*
 Name:		Music_Reactive_Desk_Light.ino
 Created:	5/24/2021
 Author:	lesley wagner

 Description: Visualizing music using FFT functions from the CMSIS DSP library, a custom ADC library for sampling and 
 the FastLed library for controlling a WS2813B led strip.
*/

#include "WS2812Serial.h"
#define USE_WS2812SERIAL
#include "FastLED.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#include <ADC.h>
#include "math.h"
#include "My_ADC.h"
#include <list>

/*
* ADC variables and definitions
*/
#define ADC_IR_Priority 64 // interrupt priority
#define N_SAMPLES 8192 // number of samples

void readAdc(void);

My_ADC ADC0(0);
short samples[N_SAMPLES];
int sampleCounter = 0;
bool samplesReady = false; // 8192 points have been sampled, ready to be processed by FFT

/*
* FFT and LED variables and definitions
*/
#define numLeds 117
#define numLedsBy3 39 // number of leds divided by 3
#define numLedsBy3By3 13 // number of leds divided by 9
#define numLedsLowLimit 4 // If less than 5 leds are on in a region, that region of sound is considered to be quiet.
#define dataPin 14
#define sampleBias 1522 // the DC bias of the microphone, 1.25 V
#define maxPeak 1240 // Max peak AC signal, 1 V
#define sqrt_2 1.4142 // square root of 2
#define bassUpper 2500 // bass upper frequency in deciHz
#define midUpper 15000 // mid upper frequency in deciHz
#define trebleUpper 50000 // treble upper frequency in deciHz
#define fundamentalFreq 71 // fundamental frequency in deciHz
#define numRecordedLedValues 60

CRGB leds[numLeds];
q31_t bassAmplitude;
q31_t midAmplitude;
q31_t trebleAmplitude;
short bassLedsOn; // number of leds that are turned on in the bass range
short midLedsOn; // number of leds that are turned on in the bass range
short trebleLedsOn; // number of leds that are turned on in the bass range

int maxBass = 2000000; // max bass amplitude
int maxMid = 4000000; // max mid amplitude
int maxTreble = 4000000; // max treble amplitude

q15_t fftSamples[N_SAMPLES];
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
    pinMode(A1, INPUT);
    pinMode(dataPin, OUTPUT);

    LEDS.addLeds<WS2812SERIAL, dataPin, RGB>(leds, numLeds);
    LEDS.setBrightness(84);

    // setup the ADC
    ADC0.setReference(ADC_REFERENCE::REF_3V3);
    ADC0.setResolution(12); // resolution of 12 bits
    ADC0.setConversionSpeed(ADC_CONVERSION_SPEED::ADACK_20); // ADC asynchronous clock 20 MHz
    ADC0.setSamplingSpeed(ADC_SAMPLING_SPEED::LOW_MED_SPEED); // 16 ADCK cycles
    ADC0.setAveraging(8); // take the average of 8 readings
    ADC0.recalibrate();

    ADC0.setOffset(sampleBias, true); // remove sample bias from ADC result
    ADC0.enableInterrupts(readAdc, ADC_IR_Priority);
    ADC0.startContinuous(A1);
}

void loop() {
    // long micros1, micros2;
    // micros1 = micros();
    // Sample window = 75.4 ms, fundamental frequency 13.3 Hz (4 readings)
    // Sample window = 150.7 ms, fundamental frequency 6.63 Hz (8 readings)
    if (sampleCounter == N_SAMPLES - 1) {
        // ADC0.stopContinuous();
        sampleCounter = 0;

        // copy samples over to fft array
        for (int i = 0; i < N_SAMPLES; i++) {
            fftSamples[i] = samples[i];
        }

        arm_rfft_init_q15(&fftInstance, N_SAMPLES, 0, 1);
        arm_rfft_q15(&fftInstance, fftSamples, fftOutput); // Q13.3 output format

        // Add up fft real and complex magnitudes for base frequencies, < 300 Hz
        int iFFT; // counter
        bass[0] = 0;
        bass[1] = 0;
        mid[0] = 0;
        mid[1] = 0;
        treble[0] = 0;
        treble[1] = 0;
        for (iFFT = 2; (iFFT >> 1) * fundamentalFreq < bassUpper; iFFT += 2) {
            bass[0] += abs(fftOutput[iFFT]);
            bass[1] += abs(fftOutput[iFFT + 1]);
        }

        // Add up fft real and complex magnitudes for midrange frequencies, [300, 1500] Hz
        for (iFFT; (iFFT >> 1) * fundamentalFreq < midUpper; iFFT += 2) {
            mid[0] += abs(fftOutput[iFFT]);
            mid[1] += abs(fftOutput[iFFT + 1]);
        }

        // Add up fft real and complex magnitudes for treble frequencies, [1500, 5000] Hz
        for (iFFT; (iFFT >> 1) * fundamentalFreq < trebleUpper; iFFT += 2) {
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
        /*Serial.print("Bass: ");
        Serial.println(bassAmplitude);
        Serial.print("Mid: ");
        Serial.println(midAmplitude);
        Serial.print("Treble: ");
        Serial.println(trebleAmplitude);*/
        bassLedsOn = numLedsBy3 * bassAmplitude / maxBass;
        midLedsOn = numLedsBy3 * midAmplitude / maxMid;
        trebleLedsOn = numLedsBy3 * trebleAmplitude / maxTreble;
        if (bassLedsOn > numLedsBy3) bassLedsOn = numLedsBy3;
        if (midLedsOn > numLedsBy3) midLedsOn = numLedsBy3;
        if (trebleLedsOn > numLedsBy3) trebleLedsOn = numLedsBy3;

        //// Adjust the max values for bass, mid and treble amplitude 
        //// based on the last 60 values of bassLedsOn, midLedsOn and trebleLedsOn
        //if (lastBassLeds[lastLedsIndex] == numLedsBy3) numMaxBassValues--;
        //else if (lastBassLeds[lastLedsIndex] > numLedsBy3By3) numHighBassValues--;
        //if (lastMidLeds[lastLedsIndex] == numLedsBy3) numMaxMidValues--;
        //else if (lastMidLeds[lastLedsIndex] > numLedsBy3By3) numHighMidValues--;
        //if (lastTrebleLeds[lastLedsIndex] == numLedsBy3) numMaxTrebleValues--;
        //else if (lastTrebleLeds[lastLedsIndex] > numLedsBy3By3) numHighTrebleValues--;
        //
        //if (bassLedsOn == numLedsBy3) numMaxBassValues++;
        //else if (bassLedsOn > numLedsBy3By3 && bassLedsOn > numLedsLowLimit) numHighBassValues++;
        //if (midLedsOn == numLedsBy3) numMaxMidValues++;
        //else if (midLedsOn > numLedsBy3By3 && midLedsOn > numLedsLowLimit) numHighMidValues++;
        //if (trebleLedsOn == numLedsBy3) numMaxTrebleValues++;
        //else if (trebleLedsOn > numLedsBy3By3 && trebleLedsOn > numLedsLowLimit) numHighTrebleValues++;

        //Serial.println("LED values");
        ///*Serial.println(numHighBassValues);
        //Serial.println(numHighMidValues);
        //Serial.println(numHighTrebleValues);*/
        //Serial.println(maxBass);
        //Serial.println(maxMid);
        //Serial.println(maxTreble);

        //// Add latest values to the lists
        //lastBassLeds[lastLedsIndex] = bassLedsOn;
        //lastMidLeds[lastLedsIndex] = midLedsOn;
        //lastTrebleLeds[lastLedsIndex] = trebleLedsOn;

        //if (numMaxBassValues > 5) {
        //    maxBass <<= 1;
        //    numMaxBassValues = 0;
        //}
        //if (numMaxMidValues > 5) {
        //    maxMid <<= 1;
        //    numMaxMidValues = 0;
        //}
        //if (numMaxTrebleValues > 5) {
        //    maxTreble <<= 1;
        //    numMaxTrebleValues = 0;
        //}

        //if (numHighBassValues < 5 && lastLedsIndex == (numRecordedLedValues - 1)) {
        //    maxBass >>= 1;
        //}
        //if (numHighMidValues < 5 && lastLedsIndex == (numRecordedLedValues - 1)) {
        //    maxMid >>= 1;
        //}
        //if (numHighTrebleValues < 5 && lastLedsIndex == (numRecordedLedValues - 1)) {
        //    maxTreble >>= 1;
        //}

        //lastLedsIndex++;
        //if (lastLedsIndex == numRecordedLedValues) {
        //    lastLedsIndex = 0;
        //    lastLedsCycleCompleted = true;
        //}

        /*bassLedsOn = numLedsBy3 - log2(maxBass / bassAmplitude);
        midLedsOn = numLedsBy3 - log2(maxMid / midAmplitude);
        trebleLedsOn = numLedsBy3 - log2(maxTreble / trebleAmplitude);*/

        /*Serial.print("Bass leds: ");
        Serial.println(bassLedsOn);
        Serial.print("Mid leds: ");
        Serial.println(midLedsOn);
        Serial.print("Treble leds: ");
        Serial.println(trebleLedsOn);*/

        uint8_t hue = 100;
        int ledCounter;
        for (ledCounter = 0; ledCounter < bassLedsOn; ledCounter++) {
            leds[ledCounter] = CHSV(hue++, 255, 255);
        }
        for (; ledCounter < numLedsBy3; ledCounter++) {
            leds[ledCounter] = CHSV(hue++, 255, 0);
        }
        for (ledCounter = 0; ledCounter < midLedsOn; ledCounter++) {
            leds[ledCounter + numLedsBy3] = CHSV(hue++, 255, 255);
        }
        for (; ledCounter < numLedsBy3; ledCounter++) {
            leds[ledCounter + numLedsBy3] = CHSV(hue++, 255, 0);
        }
        for (ledCounter = 0; ledCounter < trebleLedsOn; ledCounter++) {
            leds[ledCounter + numLedsBy3 * 2] = CHSV(hue++, 255, 255);
        }
        for (; ledCounter < numLedsBy3; ledCounter++) {
            leds[ledCounter + numLedsBy3 * 2] = CHSV(hue++, 255, 0);
        }

        FastLED.show();
    }
    // micros2 = micros();
    // Serial.print("Time to compute fft: ");
    // Serial.println(micros2 - micros1);
}

/*
* ADC interrupt callback function. Executes when an ADC conversion has completed.
* Read the ADC sample and store it in an array.
*/
void readAdc(void) {
    samples[sampleCounter] = ADC0.analogReadContinuous() * 26; // scale samples to maximise resolution (* 26)
    sampleCounter++;
    asm("DSB");
}