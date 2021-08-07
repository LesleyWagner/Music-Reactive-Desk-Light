/*
 Name:		Teensy_ADC_Test.ino
 Created:	2/21/2021 10:27:22 PM
 Author:	lesley wagner

 Description: Test for directly addressing the Teensy ADC
*/

#include <ADC.h>
#include "My_ADC.h"

#define ADC_IR_Priority 64
#define NSamples 100 // 8192

void readAdc(void);

My_ADC ADC0(0);
int samples[NSamples];
int sampleCounter = 0;

// the setup function runs once when you press reset or power the board
void setup() {
	ADC0.setReference(ADC_REFERENCE::REF_3V3);
	ADC0.setResolution(10); // resolution of 10 bits
	ADC0.setConversionSpeed(ADC_CONVERSION_SPEED::ADACK_20); // ADC asynchronous clock 20 MHz
	ADC0.setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED); // 5 ADCK cycles
	ADC0.setAveraging(32); // take the average of 4 readings
	ADC0.recalibrate();

	// set offset	
	ADC0.enableInterrupts(readAdc, ADC_IR_Priority);
	ADC0.startContinuous(A1);
	// ADC0.startSingleRead(A1);
	//if (!ADC0.startContinuous(A1)) { // start continuous conversion
	//	// error, can't start continuous conversion.
	//	Serial.println("error");
	//}
	Serial.println("hello");
}

// the loop function runs over and over again until power down or reset
void loop() {
	if (sampleCounter == NSamples-1) {
		ADC0.stopContinuous();
		sampleCounter = 0;

		for (int i = 0; i < NSamples; i++) {
			Serial.printf("Sample %d = %d\n", i, samples[i]);
		}
	}
	// ADC0.startSingleRead(A1);
	// Serial.println(ADC0.readSingle());
	// delay(1000);
}

/*
* ADC interrupt callback function. Executes when an ADC conversion has completed.
* Read the ADC sample and store it in an array.
*/
void readAdc(void) {
	samples[sampleCounter] = ADC0.analogReadContinuous();
	sampleCounter++;
	asm("DSB");
}
