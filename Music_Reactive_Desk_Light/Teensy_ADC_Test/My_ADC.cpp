/*
 Name:		My_ADC.cpp
 Created:	23/02/2021 20:23
 Author:	lesley wagner

 Description: Direct access to the teensy 4.0 ADC module.
*/

#include "My_ADC.h"

// include the internal reference
#ifdef ADC_USE_INTERNAL_VREF
#include <VREF.h>
#endif

/* Constructor
*   Point the registers to the correct ADC module
*   Copy the correct channel2sc1a
*   Call init
*/
My_ADC::My_ADC(uint8_t ADC_number) : ADC_num(ADC_number), channel2sc1a(ADC_num ? channel2sc1aADC1 : channel2sc1aADC0),
adc_regs(ADC_num ? ADC1_START : ADC0_START)
#ifdef ADC_USE_PDB
	,
	PDB0_CHnC1(ADC_num ? PDB0_CH1C1 : PDB0_CH0C1)
#endif
	,
	XBAR_IN(ADC_num ? XBARA1_IN_QTIMER4_TIMER3 : XBARA1_IN_QTIMER4_TIMER0), XBAR_OUT(ADC_num ? XBARA1_OUT_ADC_ETC_TRIG10 : XBARA1_OUT_ADC_ETC_TRIG00), QTIMER4_INDEX(ADC_num ? 3 : 0), ADC_ETC_TRIGGER_INDEX(ADC_num ? 4 : 0), IRQ_ADC(ADC_num ? IRQ_NUMBER_t::IRQ_ADC2 : IRQ_NUMBER_t::IRQ_ADC1) {
	// call our init
	analog_init();
}

/* Initialize stuff: Set initial ADC settings via corresponding functions.
* 
*/
void My_ADC::analog_init() {
	calibrating = 0;
	fail_flag = ADC_ERROR::CLEAR; // clear all errors
}

// starts calibration
void My_ADC::calibrate() {

	__disable_irq();

	calibrating = 1;
	atomic::clearBitFlag(adc_regs.GS, ADC_GS_CALF);
	atomic::setBitFlag(adc_regs.GC, ADC_GC_CAL);

	__enable_irq();
}

/* Waits until calibration is finished and writes the corresponding registers
*
*/
void My_ADC::wait_for_cal(void) {

	// wait for calibration to finish
	while (atomic::getBitFlag(adc_regs.GC, ADC_GC_CAL)) { // Bit ADC_GC_CAL in register GC cleared when calib. finishes.
		yield();
	}
	if (atomic::getBitFlag(adc_regs.GS, ADC_GS_CALF)) { // calibration failed
		fail_flag |= ADC_ERROR::CALIB; // the user should know and recalibrate manually
	}

	calibrating = 0;
}

//! Starts the calibration sequence, waits until it's done and writes the results
/** Usually it's not necessary to call this function directly, but do it if the "environment" changed
*   significantly since the program was started.
*/
void My_ADC::recalibrate() {
	calibrate();
	wait_for_cal();
}

/////////////// METHODS TO SET/GET SETTINGS OF THE ADC ////////////////////

/* Set the voltage reference you prefer, default is 3.3V
*   It needs to recalibrate
*  Use ADC_REF_3V3, ADC_REF_1V2 (not for Teensy LC) or ADC_REF_EXT
*/
void My_ADC::setReference(ADC_REFERENCE type) {
	ADC_REF_SOURCE ref_type = static_cast<ADC_REF_SOURCE>(type); // cast to source type, that is, either internal or default

	if (analog_reference_internal == ref_type) { // don't need to change anything
		return;
	}

	if (ref_type == ADC_REF_SOURCE::REF_ALT) { // 1.2V ref for Teensy 3.x, 3.3 VDD for Teensy LC
// internal reference requested
#ifdef ADC_USE_INTERNAL_VREF
		VREF::start(); // enable VREF if Teensy 3.x
#endif

		analog_reference_internal = ADC_REF_SOURCE::REF_ALT;
	}
	else if (ref_type == ADC_REF_SOURCE::REF_DEFAULT) {   // ext ref for all Teensys, vcc also for Teensy 3.x
		// vcc or external reference requested

#ifdef ADC_USE_INTERNAL_VREF
		VREF::stop(); // disable 1.2V reference source when using the external ref (p. 102, 3.7.1.7)
#endif

		analog_reference_internal = ADC_REF_SOURCE::REF_DEFAULT;
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_REFSEL(3));
	}

	calibrate();
}

/* Change the resolution of the measurement
*  For single-ended measurements: 8, 10, 12 or 16 bits.
*  For differential measurements: 9, 11, 13 or 16 bits.
*  If you want something in between (11 bits single-ended for example) select the inmediate higher
*  and shift the result one to the right.
*
*  It doesn't recalibrate
*/
void My_ADC::setResolution(uint8_t bits) {

	if (analog_res_bits == bits) {
		return;
	}

	if (calibrating)
		wait_for_cal();

	// conversion resolution
	// single-ended 8 bits is the same as differential 9 bits, etc.
	if (bits == 8) {
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_MODE(3));
		analog_max_val = 255; // diff mode 9 bits has 1 bit for sign, so max value is the same as single 8 bits
	}
	else if (bits == 10) {
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_MODE(3), ADC_CFG_MODE(1));
		analog_max_val = 1023;
	}
	else if (bits == 12) {
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_MODE(3), ADC_CFG_MODE(2));
		analog_max_val = 4095;
	}
	else {
		// error
	}

	analog_res_bits = bits;

	// no recalibration is needed when changing the resolution, p. 619
}

/* Returns the resolution of the ADC
*
*/
uint8_t My_ADC::getResolution() {
	return analog_res_bits;
}

/* Returns the maximum value for a measurement, that is: 2^resolution-1
*
*/
uint32_t My_ADC::getMaxValue() {
	return analog_max_val;
}

// Sets the conversion speed
/* Increase the sampling speed for low impedance sources, decrease it for higher impedance ones.
* \param speed can be any of the ADC_SAMPLING_SPEED enum: VERY_LOW_SPEED, LOW_SPEED, MED_SPEED, HIGH_SPEED or VERY_HIGH_SPEED.
*
* VERY_LOW_SPEED is the lowest possible sampling speed (+24 ADCK).
* LOW_SPEED adds +16 ADCK.
* MED_SPEED adds +10 ADCK.
* HIGH_SPEED adds +6 ADCK.
* VERY_HIGH_SPEED is the highest possible sampling speed (0 ADCK added).
*/
void My_ADC::setConversionSpeed(ADC_CONVERSION_SPEED speed) {

	if (speed == conversion_speed) { // no change
		return;
	}

	//if (calibrating) wait_for_cal();

	bool is_adack = false;
	uint32_t ADC_CFG1_speed = 0; // store the clock and divisor (set to 0 to avoid warnings)

	switch (speed) {
		// normal bus clock
	case ADC_CONVERSION_SPEED::LOW_SPEED:
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADHSC);
		atomic::setBitFlag(adc_regs.CFG, ADC_CFG_ADLPC);
		// ADC_CFG1_speed = ADC_CFG1_LOW_SPEED;
		ADC_CFG1_speed = get_CFG_LOW_SPEED(ADC_F_BUS);
		break;
	case ADC_CONVERSION_SPEED::MED_SPEED:
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADHSC);
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADLPC);
		ADC_CFG1_speed = get_CFG_MEDIUM_SPEED(ADC_F_BUS);
		break;
	case ADC_CONVERSION_SPEED::HIGH_SPEED:
		atomic::setBitFlag(adc_regs.CFG, ADC_CFG_ADHSC);
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADLPC);
		ADC_CFG1_speed = get_CFG_HIGH_SPEED(ADC_F_BUS);
		break;
		// adack - async clock source, independent of the bus clock
// fADK = 10 or 20 MHz
	case ADC_CONVERSION_SPEED::ADACK_10:
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADHSC);
		is_adack = true;
		break;
	case ADC_CONVERSION_SPEED::ADACK_20:
		atomic::setBitFlag(adc_regs.CFG, ADC_CFG_ADHSC);
		is_adack = true;
		break;

	default:
		fail_flag |= ADC_ERROR::OTHER;
		return;
	}

	if (is_adack) {
		// async clock source, independent of the bus clock
		atomic::setBitFlag(adc_regs.CFG, ADC_CFG_ADICLK(3)); // select ADACK as clock source
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADIV(3)); // select no dividers
		atomic::setBitFlag(adc_regs.GC, ADC_GC_ADACKEN);
	}
	else {
		// normal bus clock used - disable the internal asynchronous clock
		// total speed can be: bus, bus/2, bus/4, bus/8 or bus/16.
		atomic::clearBitFlag(adc_regs.GC, ADC_GC_ADACKEN);                                          // disable async
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADICLK(3), ADC_CFG1_speed & ADC_CFG_ADICLK(3)); // bus or bus/2
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADIV(3), ADC_CFG1_speed & ADC_CFG_ADIV(3));     // divisor for the clock source
	}

	conversion_speed = speed;
}

// Sets the sampling speed
/* Increase the sampling speed for low impedance sources, decrease it for higher impedance ones.
* \param speed can be any of the ADC_SAMPLING_SPEED enum: VERY_LOW_SPEED, LOW_SPEED, MED_SPEED, HIGH_SPEED or VERY_HIGH_SPEED.
*
* VERY_LOW_SPEED is the lowest possible sampling speed (25 ADCK).
* LOW_SPEED adds takes 21 ADCK.
* LOW_MED_SPEED takes 17 ADCK.
* MED_SPEED takes 13 ADCK.
* MED_HIGH_SPEED takes 9 ADCK.
* HIGH_SPEED takes 7 ADCK.
* HIGH_VERY_HIGH_SPEED takes 5 ADCK
* VERY_HIGH_SPEED is the highest possible sampling speed (3 ADCK)
*/
void My_ADC::setSamplingSpeed(ADC_SAMPLING_SPEED speed) {
	if (calibrating)
		wait_for_cal();

	switch (speed) {
	case ADC_SAMPLING_SPEED::VERY_LOW_SPEED:
		atomic::setBitFlag(adc_regs.CFG, ADC_CFG_ADLSMP); // long sampling time enable
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADSTS(3), ADC_CFG_ADSTS(3));
		break;
	case ADC_SAMPLING_SPEED::LOW_SPEED:
		atomic::setBitFlag(adc_regs.CFG, ADC_CFG_ADLSMP); // long sampling time enable
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADSTS(3), ADC_CFG_ADSTS(2));
		break;
	case ADC_SAMPLING_SPEED::LOW_MED_SPEED:
		atomic::setBitFlag(adc_regs.CFG, ADC_CFG_ADLSMP); // long sampling time enable
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADSTS(3), ADC_CFG_ADSTS(1));
		break;
	case ADC_SAMPLING_SPEED::MED_SPEED:
		atomic::setBitFlag(adc_regs.CFG, ADC_CFG_ADLSMP); // long sampling time enable
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADSTS(3), ADC_CFG_ADSTS(0));
		break;
	case ADC_SAMPLING_SPEED::MED_HIGH_SPEED:
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADLSMP); // long sampling time disabled
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADSTS(3), ADC_CFG_ADSTS(3));
		break;
	case ADC_SAMPLING_SPEED::HIGH_SPEED:
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADLSMP); // long sampling time disabled
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADSTS(3), ADC_CFG_ADSTS(2));
		break;
	case ADC_SAMPLING_SPEED::HIGH_VERY_HIGH_SPEED:
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADLSMP); // long sampling time disabled
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADSTS(3), ADC_CFG_ADSTS(1));
		break;
	case ADC_SAMPLING_SPEED::VERY_HIGH_SPEED:
		atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_ADLSMP); // long sampling time disabled
		atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_ADSTS(3), ADC_CFG_ADSTS(0));
		break;
	}
	sampling_speed = speed;
}

/* Set the number of averages: 0, 4, 8, 16 or 32.
*
*/
void My_ADC::setAveraging(uint8_t num) {

	if (calibrating)
		wait_for_cal();

	if (num <= 1) {
		num = 0;
		// ADC_SC3_avge = 0;
		atomic::clearBitFlag(adc_regs.GC, ADC_GC_AVGE);
	}
	else {
		// ADC_SC3_avge = 1;
		atomic::setBitFlag(adc_regs.GC, ADC_GC_AVGE);

		if (num <= 4) {
			num = 4;
			// ADC_SC3_avgs0 = 0;
			// ADC_SC3_avgs1 = 0;
			atomic::clearBitFlag(adc_regs.CFG, ADC_CFG_AVGS(3));
		}
		else if (num <= 8) {
			num = 8;
			// ADC_SC3_avgs0 = 1;
			// ADC_SC3_avgs1 = 0;
			atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_AVGS(3), ADC_CFG_AVGS(1));
		}
		else if (num <= 16) {
			num = 16;
			// ADC_SC3_avgs0 = 0;
			// ADC_SC3_avgs1 = 1;
			atomic::changeBitFlag(adc_regs.CFG, ADC_CFG_AVGS(3), ADC_CFG_AVGS(2));
		}
		else {
			num = 32;
			// ADC_SC3_avgs0 = 1;
			// ADC_SC3_avgs1 = 1;
			atomic::setBitFlag(adc_regs.CFG, ADC_CFG_AVGS(3));
		}
	}
	analog_num_average = num;
}

/* Enable interrupts: An ADC Interrupt will be raised when the conversion is completed
*  (including hardware averages and if the comparison (if any) is true).
*/
void My_ADC::enableInterrupts(void (*isr)(void), uint8_t priority) {
	if (calibrating)
		wait_for_cal();

	atomic::setBitFlag(adc_regs.HC0, ADC_HC_AIEN);
	interrupts_enabled = true;

	attachInterruptVector(IRQ_ADC, isr);
	NVIC_SET_PRIORITY(IRQ_ADC, priority);
	NVIC_ENABLE_IRQ(IRQ_ADC);
}

/* Disable interrupts
*
*/
void My_ADC::disableInterrupts() {
	// ADC_SC1A_aien = 0;
	atomic::clearBitFlag(adc_regs.HC0, ADC_HC_AIEN);
	interrupts_enabled = false;

	NVIC_DISABLE_IRQ(IRQ_ADC);
}

#ifdef ADC_USE_DMA
/* Enable DMA request: An ADC DMA request will be raised when the conversion is completed
*  (including hardware averages and if the comparison (if any) is true).
*/
void My_ADC::enableDMA() {

	if (calibrating)
		wait_for_cal();

	// ADC_SC2_dma = 1;
	atomic::setBitFlag(adc_regs.GC, ADC_GC_DMAEN);
}

/* Disable ADC DMA request
*
*/
void My_ADC::disableDMA() {

	// ADC_SC2_dma = 0;
	atomic::clearBitFlag(adc_regs.GC, ADC_GC_DMAEN);
}
#endif

/* Enable the compare function: A conversion will be completed only when the ADC value
*  is >= compValue (greaterThan=1) or < compValue (greaterThan=0)
*  Call it after changing the resolution
*  Use with interrupts or poll conversion completion with isADC_Complete()
*/
void My_ADC::enableCompare(int16_t compValue, bool greaterThan) {

	if (calibrating)
		wait_for_cal(); // if we modify the adc's registers when calibrating, it will fail

// ADC_SC2_cfe = 1; // enable compare
// ADC_SC2_cfgt = (int32_t)greaterThan; // greater or less than?
	atomic::setBitFlag(adc_regs.GC, ADC_GC_ACFE);
	atomic::changeBitFlag(adc_regs.GC, ADC_GC_ACFGT, ADC_GC_ACFGT * greaterThan);
	adc_regs.CV = ADC_CV_CV1(compValue);
}

/* Enable the compare function: A conversion will be completed only when the ADC value
*  is inside (insideRange=1) or outside (=0) the range given by (lowerLimit, upperLimit),
*  including (inclusive=1) the limits or not (inclusive=0).
*  See Table 31-78, p. 617 of the freescale manual.
*  Call it after changing the resolution
*/
void My_ADC::enableCompareRange(int16_t lowerLimit, int16_t upperLimit, bool insideRange, bool inclusive) {

	if (calibrating)
		wait_for_cal(); // if we modify the adc's registers when calibrating, it will fail

// ADC_SC2_cfe = 1; // enable compare
// ADC_SC2_cren = 1; // enable compare range
	atomic::setBitFlag(adc_regs.GC, ADC_GC_ACFE);
	atomic::setBitFlag(adc_regs.GC, ADC_GC_ACREN);

	if (insideRange && inclusive) { // True if value is inside the range, including the limits. CV1 <= CV2 and ACFGT=1
// ADC_SC2_cfgt = 1;
		atomic::setBitFlag(adc_regs.GC, ADC_GC_ACFGT);
		adc_regs.CV = ADC_CV_CV1(lowerLimit) | ADC_CV_CV2(upperLimit);
	}
	else if (insideRange && !inclusive) { // True if value is inside the range, excluding the limits. CV1 > CV2 and ACFGT=0
// ADC_SC2_cfgt = 0;
		atomic::clearBitFlag(adc_regs.GC, ADC_GC_ACFGT);
		adc_regs.CV = ADC_CV_CV2(lowerLimit) | ADC_CV_CV1(upperLimit);
	}
	else if (!insideRange && inclusive) { // True if value is outside of range or is equal to either limit. CV1 > CV2 and ACFGT=1
// ADC_SC2_cfgt = 1;
		atomic::setBitFlag(adc_regs.GC, ADC_GC_ACFGT);
		adc_regs.CV = ADC_CV_CV2(lowerLimit) | ADC_CV_CV1(upperLimit);

	}
	else if (!insideRange && !inclusive) { // True if value is outside of range and not equal to either limit. CV1 > CV2 and ACFGT=0
// ADC_SC2_cfgt = 0;
		atomic::clearBitFlag(adc_regs.GC, ADC_GC_ACFGT);
		adc_regs.CV = ADC_CV_CV1(lowerLimit) | ADC_CV_CV2(upperLimit);
	}
}

/* Disable the compare function
*
*/
void My_ADC::disableCompare() {

	// ADC_SC2_cfe = 0;
	atomic::clearBitFlag(adc_regs.GC, ADC_GC_ACFE);

}

//! Set offset to ADC result.
	/** Subtracts from or adds a value (offset) to the ADC result.
	*   Should be set before conversion is started.
	*   @param offset: value to be subtracted from or added to the ADC result.
	*	@param subtract: true when the offset is subtracted; false when it is added.
	*/
void My_ADC::setOffset(int16_t offset, bool subtract) {
	adc_regs.OFS = ADC_OFS_OFS(offset) | (subtract << 12);
}

#ifdef ADC_USE_PGA
/* Enables the PGA and sets the gain
*   Use only for signals lower than 1.2 V
*   \param gain can be 1, 2, 4, 8, 16 32 or 64
*
*/
void My_ADC::enablePGA(uint8_t gain) {
	if (calibrating)
		wait_for_cal();

	uint8_t setting;
	if (gain <= 1) {
		setting = 0;
	}
	else if (gain <= 2) {
		setting = 1;
	}
	else if (gain <= 4) {
		setting = 2;
	}
	else if (gain <= 8) {
		setting = 3;
	}
	else if (gain <= 16) {
		setting = 4;
	}
	else if (gain <= 32) {
		setting = 5;
	}
	else { // 64
		setting = 6;
	}

	adc_regs.PGA = ADC_PGA_PGAEN | ADC_PGA_PGAG(setting);
	pga_value = 1 << setting;
}

/* Returns the PGA level
*  PGA level = from 0 to 64
*/
uint8_t My_ADC::getPGA() {
	return pga_value;
}

//! Disable PGA
void My_ADC::disablePGA() {
	// ADC_PGA_pgaen = 0;
	atomic::clearBitFlag(adc_regs.PGA, ADC_PGA_PGAEN);
	pga_value = 1;
}
#endif

//////////////// INFORMATION ABOUT VALID PINS //////////////////

// check whether the pin is a valid analog pin
bool My_ADC::checkPin(uint8_t pin) {

	if (pin > ADC_MAX_PIN) {
		return false; // all others are invalid
	}

	// translate pin number to SC1A number, that also contains MUX a or b info.
	const uint8_t sc1a_pin = channel2sc1a[pin];

	// check for valid pin
	if ((sc1a_pin & ADC_SC1A_CHANNELS) == ADC_SC1A_PIN_INVALID) {
		return false; // all others are invalid
	}

	return true;
}

#if ADC_DIFF_PAIRS > 0
// check whether the pins are a valid analog differential pins (including PGA if enabled)
bool My_ADC::checkDifferentialPins(uint8_t pinP, uint8_t pinN) {
	if (pinP > ADC_MAX_PIN) {
		return false; // all others are invalid
	}

	// translate pinP number to SC1A number, to make sure it's differential
	uint8_t sc1a_pin = channel2sc1a[pinP];

	if (!(sc1a_pin & ADC_SC1A_PIN_DIFF)) {
		return false; // all others are invalid
	}

	// get SC1A number, also whether it can do PGA
	sc1a_pin = getDifferentialPair(pinP);

	// the pair can't be measured with this ADC
	if ((sc1a_pin & ADC_SC1A_CHANNELS) == ADC_SC1A_PIN_INVALID) {
		return false; // all others are invalid
	}

#ifdef ADC_USE_PGA
	// check if PGA is enabled, and whether the pin has access to it in this ADC module
	if (isPGAEnabled() && !(sc1a_pin & ADC_SC1A_PIN_PGA)) {
		return false;
	}
#endif // ADC_USE_PGA

	return true;
}
#endif

//////////////// HELPER METHODS FOR CONVERSION /////////////////

// Starts a single-ended conversion on the pin (sets the mux correctly)
// Doesn't do any of the checks on the pin
// It doesn't change the continuous conversion bit
void My_ADC::startReadFast(uint8_t pin) {

	// translate pin number to SC1A number, that also contains MUX a or b info.
	const uint8_t sc1a_pin = channel2sc1a[pin];

	// select pin for single-ended mode and start conversion, enable interrupts if requested
	__disable_irq();
	adc_regs.HC0 = (sc1a_pin & ADC_SC1A_CHANNELS) + interrupts_enabled * ADC_HC_AIEN;

	__enable_irq();
}

#if ADC_DIFF_PAIRS > 0
// Starts a differential conversion on the pair of pins
// Doesn't do any of the checks on the pins
// It doesn't change the continuous conversion bit
void My_ADC::startDifferentialFast(uint8_t pinP, uint8_t pinN) {

	// get SC1A number
	uint8_t sc1a_pin = getDifferentialPair(pinP);

#ifdef ADC_USE_PGA
	// check if PGA is enabled
	if (isPGAEnabled()) {
		sc1a_pin = 0x2; // PGA always uses DAD2
	}
#endif // ADC_USE_PGA

	__disable_irq();
	adc_regs.SC1A = ADC_SC1_DIFF + (sc1a_pin & ADC_SC1A_CHANNELS) + atomic::getBitFlag(adc_regs.SC1A, ADC_SC1_AIEN) * ADC_SC1_AIEN;
	__enable_irq();
}
#endif

//////////////// BLOCKING CONVERSION METHODS //////////////////
/*
	This methods are implemented like this:

	1. Check that the pin is correct
	2. if calibrating, wait for it to finish before modifiying any ADC register
	3. Check if we're interrupting a measurement, if so store the settings.
	4. Disable continuous conversion mode and start the current measurement
	5. Wait until it's done, and check whether the comparison (if any) was succesful.
	6. Get the result.
	7. If step 3. is true, restore the previous ADC settings

*/

/* Reads the analog value of the pin.
* It waits until the value is read and then returns the result.
* If a comparison has been set up and fails, it will return ADC_ERROR_VALUE.
* Set the resolution, number of averages and voltage reference using the appropriate functions.
*/
int My_ADC::analogRead(uint8_t pin) {

	//digitalWriteFast(LED_BUILTIN, HIGH);

	// check whether the pin is correct
	if (!checkPin(pin)) {
		fail_flag |= ADC_ERROR::WRONG_PIN;
		return ADC_ERROR_VALUE;
	}

	// increase the counter of measurements
	num_measurements++;

	//digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN));

	if (calibrating)
		wait_for_cal();

	//digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN));

	// check if we are interrupting a measurement, store setting if so.
	// vars to save the current state of the ADC in case it's in use
	ADC_Config old_config = {};
	const uint8_t wasADCInUse = isConverting(); // is the ADC running now?

	if (wasADCInUse) { // this means we're interrupting a conversion
		// save the current conversion config, we don't want any other interrupts messing up the configs
		__disable_irq();
		//digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN) );
		saveConfig(&old_config);
		__enable_irq();
	}

	// no continuous mode
	singleMode();

	startReadFast(pin); // start single read

	// wait for the ADC to finish
	while (isConverting()) {
		yield();
	}

	// it's done, check if the comparison (if any) was true
	int32_t result;
	__disable_irq(); // make sure nothing interrupts this part
	if (isComplete()) { // conversion succeded
		result = (uint16_t)readSingle();
	}
	else { // comparison was false
		fail_flag |= ADC_ERROR::COMPARISON;
		result = ADC_ERROR_VALUE;
	}
	__enable_irq();

	// if we interrupted a conversion, set it again
	if (wasADCInUse) {
		//digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN) );
		__disable_irq();
		loadConfig(&old_config);
		__enable_irq();
	}

	num_measurements--;
	return result;

} // analogRead

#if ADC_DIFF_PAIRS > 0
/* Reads the differential analog value of two pins (pinP - pinN)
* It waits until the value is read and then returns the result
* If a comparison has been set up and fails, it will return ADC_ERROR_DIFF_VALUE
* Set the resolution, number of averages and voltage reference using the appropriate functions
*/
int My_ADC::analogReadDifferential(uint8_t pinP, uint8_t pinN) {

	if (!checkDifferentialPins(pinP, pinN)) {
		fail_flag |= ADC_ERROR::WRONG_PIN;
		return ADC_ERROR_VALUE; // all others are invalid
	}

	// increase the counter of measurements
	num_measurements++;

	// check for calibration before setting channels,
	// because conversion will start as soon as we write to adc_regs.SC1A
	if (calibrating)
		wait_for_cal();

	uint8_t res = getResolution();

	// vars to saved the current state of the ADC in case it's in use
	ADC_Config old_config = {};
	uint8_t wasADCInUse = isConverting(); // is the ADC running now?

	if (wasADCInUse) { // this means we're interrupting a conversion
		// save the current conversion config, we don't want any other interrupts messing up the configs
		__disable_irq();
		saveConfig(&old_config);
		__enable_irq();
	}

	// no continuous mode
	singleMode();

	startDifferentialFast(pinP, pinN); // start conversion

	// wait for the ADC to finish
	while (isConverting()) {
		yield();
		//digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN) );
	}

	// it's done, check if the comparison (if any) was true
	int32_t result;
	__disable_irq(); // make sure nothing interrupts this part
	if (isComplete()) {                                            // conversion succeded
		result = (int16_t)(int32_t)readSingle(); // cast to 32 bits
		if (res == 16) {                // 16 bit differential is actually 15 bit + 1 bit sign
			result *= 2; // multiply by 2 as if it were really 16 bits, so that getMaxValue gives a correct value.
		}
	}
	else { // comparison was false
		result = ADC_ERROR_VALUE;
		fail_flag |= ADC_ERROR::COMPARISON;
	}
	__enable_irq();

	// if we interrupted a conversion, set it again
	if (wasADCInUse) {
		__disable_irq();
		loadConfig(&old_config);
		__enable_irq();
	}

	num_measurements--;
	return result;

} // analogReadDifferential
#endif

/////////////// NON-BLOCKING CONVERSION METHODS //////////////
/*
	This methods are implemented like this:

	1. Check that the pin is correct
	2. if calibrating, wait for it to finish before modifiying any ADC register
	3. Check if we're interrupting a measurement, if so store the settings (in a member of the class, so it can be accessed).
	4. Disable continuous conversion mode and start the current measurement

	The fast methods only do step 4.

*/

/* Starts an analog measurement on the pin.
*  It returns inmediately, read value with readSingle().
*  If the pin is incorrect it returns false.
*/
bool My_ADC::startSingleRead(uint8_t pin) {

	// check whether the pin is correct
	if (!checkPin(pin)) {
		fail_flag |= ADC_ERROR::WRONG_PIN;
		return false;
	}

	if (calibrating)
		wait_for_cal();

	// save the current state of the ADC in case it's in use
	adcWasInUse = isConverting(); // is the ADC running now?

	if (adcWasInUse) { // this means we're interrupting a conversion
		// save the current conversion config, the adc isr will restore the adc
		__disable_irq();
		saveConfig(&adc_config);
		__enable_irq();
	}

	// no continuous mode
	singleMode();

	// start measurement
	startReadFast(pin);

	return true;
}

#if ADC_DIFF_PAIRS > 0
/* Start a differential conversion between two pins (pinP - pinN).
* It returns inmediately, get value with readSingle().
* Incorrect pins will return false.
* Set the resolution, number of averages and voltage reference using the appropriate functions
*/
bool My_ADC::startSingleDifferential(uint8_t pinP, uint8_t pinN) {

	if (!checkDifferentialPins(pinP, pinN)) {
		fail_flag |= ADC_ERROR::WRONG_PIN;
		return false; // all others are invalid
	}

	// check for calibration before setting channels,
	// because conversion will start as soon as we write to adc_regs.SC1A
	if (calibrating)
		wait_for_cal();

	// vars to saved the current state of the ADC in case it's in use
	adcWasInUse = isConverting(); // is the ADC running now?

	if (adcWasInUse) { // this means we're interrupting a conversion
		// save the current conversion config, we don't want any other interrupts messing up the configs
		__disable_irq();
		saveConfig(&adc_config);
		__enable_irq();
	}

	// no continuous mode
	singleMode();

	// start the conversion
	startDifferentialFast(pinP, pinN);

	return true;
}
#endif

///////////// CONTINUOUS CONVERSION METHODS ////////////
/*
	This methods are implemented like this:

	1. Check that the pin is correct
	2. If calibrating, wait for it to finish before modifiying any ADC register
	4. Enable continuous conversion mode and start the current measurement

*/

/* Starts continuous conversion on the pin
 * It returns as soon as the ADC is set, use analogReadContinuous() to read the values
 * Set the resolution, number of averages and voltage reference using the appropriate functions BEFORE calling this function
*/
bool My_ADC::startContinuous(uint8_t pin) {

	// check whether the pin is correct
	if (!checkPin(pin)) {
		fail_flag |= ADC_ERROR::WRONG_PIN;
		return false;
	}

	// check for calibration before setting channels,
	if (calibrating)
		wait_for_cal();

	// increase the counter of measurements
	num_measurements++;

	// set continuous conversion flag
	continuousMode();

	startReadFast(pin);

	return true;
}

#if ADC_DIFF_PAIRS > 0
/* Starts continuous and differential conversion between the pins (pinP-pinN)
 * It returns as soon as the ADC is set, use analogReadContinuous() to read the value
 * Set the resolution, number of averages and voltage reference using the appropriate functions BEFORE calling this function
*/
bool My_ADC::startContinuousDifferential(uint8_t pinP, uint8_t pinN) {

	if (!checkDifferentialPins(pinP, pinN)) {
		fail_flag |= ADC_ERROR::WRONG_PIN;
		return false; // all others are invalid
	}

	// increase the counter of measurements
	num_measurements++;

	// check for calibration before setting channels,
	// because conversion will start as soon as we write to adc_regs.SC1A
	if (calibrating)
		wait_for_cal();

	// save the current state of the ADC in case it's in use
	uint8_t wasADCInUse = isConverting(); // is the ADC running now?

	if (wasADCInUse) { // this means we're interrupting a conversion
		// save the current conversion config, we don't want any other interrupts messing up the configs
		__disable_irq();
		saveConfig(&adc_config);
		__enable_irq();
	}

	// set continuous mode
	continuousMode();

	// start conversions
	startDifferentialFast(pinP, pinN);

	return true;
}
#endif

/* Stops continuous conversion
*/
void My_ADC::stopContinuous() {

	// set channel select to all 1's (31) to stop it.
#ifdef ADC_TEENSY_4
	adc_regs.HC0 = ADC_SC1A_PIN_INVALID + interrupts_enabled * ADC_HC_AIEN;
#else
	adc_regs.SC1A = ADC_SC1A_PIN_INVALID + atomic::getBitFlag(adc_regs.SC1A, ADC_SC1_AIEN) * ADC_SC1_AIEN;
#endif

	// decrease the counter of measurements (unless it's 0)
	if (num_measurements > 0) {
		num_measurements--;
	}

	return;
}

//////////// FREQUENCY METHODS ////////

#ifdef ADC_USE_QUAD_TIMER
// try to use some teensy core functions...
// mainly out of pwm.c
extern "C" {
	extern void xbar_connect(unsigned int input, unsigned int output);
	extern void quadtimer_init(IMXRT_TMR_t* p);
	extern void quadtimerWrite(IMXRT_TMR_t* p, unsigned int submodule, uint16_t val);
	extern void quadtimerFrequency(IMXRT_TMR_t* p, unsigned int submodule, float frequency);
}

void My_ADC::startQuadTimer(uint32_t freq) {
	// First lets setup the XBAR
	CCM_CCGR2 |= CCM_CCGR2_XBAR1(CCM_CCGR_ON); //turn clock on for xbara1
	xbar_connect(XBAR_IN, XBAR_OUT);

	// Update the ADC
	uint8_t adc_pin_channel = adc_regs.HC0 & 0x1f; // remember the trigger that was set
	setHardwareTrigger();                          // set the hardware trigger
	adc_regs.HC0 = (adc_regs.HC0 & ~0x1f) | 16;    // ADC_ETC channel remember other states...
	singleMode();                                  // make sure continuous is turned off as you want the trigger to di it.

	// setup adc_etc - BUGBUG have not used the preset values yet.
	if (IMXRT_ADC_ETC.CTRL & ADC_ETC_CTRL_SOFTRST) { // SOFTRST
		// Soft reset
		atomic::clearBitFlag(IMXRT_ADC_ETC.CTRL, ADC_ETC_CTRL_SOFTRST);
		delay(5); // give some time to be sure it is init
	}
	if (ADC_num == 0) { // BUGBUG - in real code, should probably know we init ADC or not..
		IMXRT_ADC_ETC.CTRL |=
			(ADC_ETC_CTRL_TSC_BYPASS | ADC_ETC_CTRL_DMA_MODE_SEL | ADC_ETC_CTRL_TRIG_ENABLE(1 << ADC_ETC_TRIGGER_INDEX)); // 0x40000001;  // start with trigger 0
		IMXRT_ADC_ETC.TRIG[ADC_ETC_TRIGGER_INDEX].CTRL = ADC_ETC_TRIG_CTRL_TRIG_CHAIN(0);                                 // chainlength -1 only us
		IMXRT_ADC_ETC.TRIG[ADC_ETC_TRIGGER_INDEX].CHAIN_1_0 =
			ADC_ETC_TRIG_CHAIN_IE0(1) /*| ADC_ETC_TRIG_CHAIN_B2B0 */
			| ADC_ETC_TRIG_CHAIN_HWTS0(1) | ADC_ETC_TRIG_CHAIN_CSEL0(adc_pin_channel);

		if (interrupts_enabled) {
			// Not sure yet?
		}
		if (adc_regs.GC & ADC_GC_DMAEN) {
			IMXRT_ADC_ETC.DMA_CTRL |= ADC_ETC_DMA_CTRL_TRIQ_ENABLE(ADC_ETC_TRIGGER_INDEX);
		}
	}
	else {
		// This is our second one... Try second trigger?
		// Remove the BYPASS?
		IMXRT_ADC_ETC.CTRL &= ~(ADC_ETC_CTRL_TSC_BYPASS);                                                       // 0x40000001;  // start with trigger 0
		IMXRT_ADC_ETC.CTRL |= ADC_ETC_CTRL_DMA_MODE_SEL | ADC_ETC_CTRL_TRIG_ENABLE(1 << ADC_ETC_TRIGGER_INDEX); // Add trigger
		IMXRT_ADC_ETC.TRIG[ADC_ETC_TRIGGER_INDEX].CTRL = ADC_ETC_TRIG_CTRL_TRIG_CHAIN(0);                       // chainlength -1 only us
		IMXRT_ADC_ETC.TRIG[ADC_ETC_TRIGGER_INDEX].CHAIN_1_0 =
			ADC_ETC_TRIG_CHAIN_IE0(1) /*| ADC_ETC_TRIG_CHAIN_B2B0 */
			| ADC_ETC_TRIG_CHAIN_HWTS0(1) | ADC_ETC_TRIG_CHAIN_CSEL0(adc_pin_channel);

		if (adc_regs.GC & ADC_GC_DMAEN) {
			IMXRT_ADC_ETC.DMA_CTRL |= ADC_ETC_DMA_CTRL_TRIQ_ENABLE(ADC_ETC_TRIGGER_INDEX);
		}
	}

	// Now init the QTimer.
	// Extracted from quadtimer_init in pwm.c but only the one channel...
	// Maybe see if we have to do this every time we call this.  But how often is that?
	IMXRT_TMR4.CH[QTIMER4_INDEX].CTRL = 0; // stop timer
	IMXRT_TMR4.CH[QTIMER4_INDEX].CNTR = 0;
	IMXRT_TMR4.CH[QTIMER4_INDEX].SCTRL = TMR_SCTRL_OEN | TMR_SCTRL_OPS | TMR_SCTRL_VAL | TMR_SCTRL_FORCE;
	IMXRT_TMR4.CH[QTIMER4_INDEX].CSCTRL = TMR_CSCTRL_CL1(1) | TMR_CSCTRL_ALT_LOAD;
	// COMP must be less than LOAD - otherwise output is always low
	IMXRT_TMR4.CH[QTIMER4_INDEX].LOAD = 24000; // low time  (65537 - x) -
	IMXRT_TMR4.CH[QTIMER4_INDEX].COMP1 = 0;    // high time (0 = always low, max = LOAD-1)
	IMXRT_TMR4.CH[QTIMER4_INDEX].CMPLD1 = 0;
	IMXRT_TMR4.CH[QTIMER4_INDEX].CTRL = TMR_CTRL_CM(1) | TMR_CTRL_PCS(8) |
		TMR_CTRL_LENGTH | TMR_CTRL_OUTMODE(6);

	quadtimerFrequency(&IMXRT_TMR4, QTIMER4_INDEX, freq);
	quadtimerWrite(&IMXRT_TMR4, QTIMER4_INDEX, 5);
}

//! Stop the PDB
void My_ADC::stopQuadTimer() {
	quadtimerWrite(&IMXRT_TMR4, QTIMER4_INDEX, 0);
	setSoftwareTrigger();
}

//! Return the PDB's frequency
uint32_t My_ADC::getQuadTimerFrequency() {
	// Can I reverse the calculations of quad timer set frequency?
	uint32_t high = IMXRT_TMR4.CH[QTIMER4_INDEX].CMPLD1;
	uint32_t low = 65537 - IMXRT_TMR4.CH[QTIMER4_INDEX].LOAD;
	uint32_t highPlusLow = high + low; //
	if (highPlusLow == 0)
		return 0; //

	uint8_t pcs = (IMXRT_TMR4.CH[QTIMER4_INDEX].CTRL >> 9) & 0x7;
	uint32_t freq = (F_BUS_ACTUAL >> pcs) / highPlusLow;
	//Serial.printf("My_ADC::getTimerFrequency H:%u L:%u H+L=%u pcs:%u freq:%u\n", high, low, highPlusLow, pcs, freq);
	return freq;
}
#endif // ADC_USE_QUAD_TIMER