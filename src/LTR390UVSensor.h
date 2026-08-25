#pragma once

///////////////////////////////////////////////////////////////////////
// LTR390 Ambient Light and UV Sensor with I2C Interface
// 3.3V ONLY!
// Copyright (C) muman.ch + github/mumanchu, 2026.08.24
// 
/*
Soldering the tiny ant-sized LTR390 chip is almost impossible, so the
Adafruit breakout board was used.

The Adafruit library does not contain the LUX and UV Index calculations, 
and it requires the Adafruit BusIO library which makes the code much 
bigger.

This stand-alone mumanchu class has integer and/or float for LUX and 
UV Index calculations.

The LTR390 has two sensors, an ambient light sensor (ALS) and a UV 
sensor (UVS). Only one sensor can be active at a time, selected by 
setSensor().

Use setConfiguration() to configure the sensor's resolution (in bits), 
sample rate and the gain (1..18). For high LUX levels, a high gain 
may cause overflow. Reduce the gain for high light levels, increase 
the gain for low light levels. TODO: You could add an auto-gain-adjust 
feature.

Either floating point and/or integer calculations for the LUX and UV
index can be done. Add '#define LTR390_FLOAT' or '%define LTR390_INT' 
before #include "LTR390UVSensor.h" to select only float or only 
integer calculations. Using floating point will increase the code size 
if you are not using floats elsewhere.

All methods return 'false' if [communications or validation] failed. 
Data is returned via parameter pointers.

Note that the green Power LED on the Adafruit board can add a few LUX 
to very low light readings, so stick some black tape over the Power LED.

A nasty problem was found. The 'software reset' command hangs the I2C 
bus (the LTR390 does not issue the I2C ACK). This is handled by special 
code in begin().

LTR390 DATA SHEET
https://optoelectronics.liteon.com/upload/download/DS86-2015-0004/LTR-390UV_Final_%20DS_V1%201.pdf

ADAFRUIT LTR390 BREAKOUT BOARD
https://learn.adafruit.com/adafruit-ltr390-uv-sensor
https://github.com/adafruit/Adafruit_LTR390
*/

#define LOGERROR(s) { Serial.println(s); Serial.flush(); }	// for debug
//#define LOGERROR(s)	// for release, does nothing

// Provide both float and integer calculations by default
#if !defined(LTR390_FLOAT) && !defined(LTR390_INT)
#define LTR390_INT
#define LTR390_FLOAT
#endif


class LTR390UVSensor
{
protected:
	TwoWire* wire;
	byte i2cAdds;
	// shadow registers, saves read-before-write
	byte mainCtrlRegShadow;
	byte intCfgRegShadow;

	// pre-calculated values for LUX and UV Index calculations
	#ifdef LTR390_FLOAT
	float alsSensitivityF;	// ambient light sensor sensitivity
	float uvsSensitivityF;	// uv sensor sensitivity
	#endif
	#ifdef LTR390_INT
	ulong alsSensitivityI;	// ambient light sensor sensitivity
	ulong uvsSensitivityI;	// uv sensor sensitivity
	#endif
	ulong maxReading;		// maximum reading according to resolution

public:
	#ifdef LTR390_FLOAT
	// for float calculation
	// wfac : 1.0 for no window/glass
	//       >1.0 for device under tinted glass (needs calibration)
	float wfacF = 1.0f;
	#endif
	#ifdef LTR390_INT
	// for integer calculation
	// wfac x 1000 : e.g. 1.0 = 1000, 1.234 = 1234, 1.5 = 1500
	ulong wfacI = 1000;
	#endif

	typedef enum {
		ALS = 0,	// ambient light sensor channel
		UVS = 1		// uv sensor channel
	} CHANNEL;

	bool begin(TwoWire* twoWire, byte i2cAddress);
	bool reset();
	bool getID(uint* partNumber, uint* revisionId);
	bool setConfiguration(uint resolution, uint sampleRate, uint gain);
	bool getConfiguration(uint* resolution, uint* sampleRate, uint* gain);
	bool configureIntPin(CHANNEL channel, uint persistence);
	bool enableIntPin(bool enable);
	bool setIntPinThreshold(ulong upperThreshold, ulong lowerThreshold);
	bool setChannel(CHANNEL channel);
	bool getChannel(CHANNEL* channel);
	bool enableSensor(bool enable);
	bool getStatus(bool* dataReady, bool* intTriggered = NULL, bool* powerOn = NULL);
	bool getReading(CHANNEL channel, ulong* reading);
	#ifdef LTR390_FLOAT
	float calculateLuxF(ulong alsReading);
	uint calculateUVIndexF(ulong uvsReading);
	#endif
	#ifdef LTR390_INT
	ulong calculateLuxI(ulong alsReading);
	uint calculateUVIndexI(ulong uvsReading);
	#endif

protected:
	void calculateSensitivity(uint gain, uint resolution);
	bool writeRegister(byte reg, byte value);
	bool readRegister(byte reg, byte* value);
};

// Returns false if the sensor was not found
bool LTR390UVSensor::begin(TwoWire* twoWire, byte i2cAddress)
{
	wire = twoWire;
	i2cAdds = i2cAddress;
	return reset();
}

// Software reset
// sets default values (see data sheet)
// the sensor remains in standby mode
bool LTR390UVSensor::reset()
{
	// set default values after reset
	mainCtrlRegShadow = 0;
	intCfgRegShadow = 0;

	// sensitivity for default gain and resolution
	calculateSensitivity(1, 2);

	// check it's the right chip
	uint partNumber, revisionId;
	if (!getID(&partNumber, &revisionId))
		return false;			// not responding
	if (partNumber != 11) {
		LOGERROR("invalid part number");
		return false;
	}
	// set reset bit
	bool ok = writeRegister(0x00, 0x10);
	delay(100);

	// the software reset has no ACK and hangs the I2C bus,
	// so we must reset the I2C bus
	wire->end();
	delay(100);
	//TODO >>>
	wire->setClock(400000);	// naughty bit, we don't know the I2C bus clock speed
	wire->setTimeout(100);
	//<<<
	wire->begin();

	// check it's still responding
	if (!readRegister(0x00, &mainCtrlRegShadow))
		return false;

	// check MAIN_CTRL register should be reset to zero
	return mainCtrlRegShadow == 0;
}

// partNumber = 0x0b (11), revisionId = silicon revision (2) 
bool LTR390UVSensor::getID(uint* partNumber, uint* revisionId)
{
	byte b;
	bool ok = readRegister(0x06, &b);
	// note: b is 0 if readRegister() fails, so both return values are 0
	*partNumber = b >> 4;
	*revisionId = b & 0x0f;
	return ok;
}

// resolution : 0=20bit 400ms, 1=19bit 200ms, 2=18bit 100ms (default), 
//              3=17bit 50ms, 4=16bit 25ms, (13-bit is not supported)
// sampleRate : 0=25ms, 1=50ms, 2=100ms (default), 3=200ms, 4=500ms, 5=1000ms
// gain       : 0=range 1, 1=range 3, 2=range 6, 3=range 9, 4=range 18
// Note: If the sampleRate is faster than the resolution speed, the rate is set
// to the resultion speed.
bool LTR390UVSensor::setConfiguration(uint resolution, uint sampleRate, uint gain)
{
	if (resolution > 4 || sampleRate > 5 || gain > 4) {
		LOGERROR("configure() bad parameter");
		return false;
	}
	calculateSensitivity(gain, resolution);
	return writeRegister(0x04, (byte)((resolution << 4) + sampleRate)) &&
		writeRegister(0x05, (byte)gain);
}

bool LTR390UVSensor::getConfiguration(uint* resolution, uint* rate, uint* gain)
{
	byte b1, b2;
	bool ok = readRegister(0x04, &b1) && readRegister(0x05, &b2);
	*resolution = (b1 >> 4) & 0x07;
	*rate = b1 & 0x07;
	*gain = b2 & 0x07;
	calculateSensitivity(*gain, *resolution);
	return ok;
}

// channel     : 0=ALS channel (default), 1=UVS channel
// persistence : number of consecutive readings minus 1, 0=1, 15=16 readings
bool LTR390UVSensor::configureIntPin(CHANNEL channel, uint persistence)
{
	intCfgRegShadow &= ~0x30;
	intCfgRegShadow |= channel ? 0x30 : 0x10;
	return writeRegister(0x19, intCfgRegShadow) && 
		writeRegister(0x1a, (byte)(persistence << 4));
}

// Enables/disables the INT output
bool LTR390UVSensor::enableIntPin(bool enable)
{
	if (enable)
		intCfgRegShadow |= 0x04;
	else
		intCfgRegShadow &= ~0x04;
	return writeRegister(0x19, intCfgRegShadow);
}

// Upper and lower limits to set/clear INT output
bool LTR390UVSensor::setIntPinThreshold(ulong upperThreshold, ulong lowerThreshold)
{
	return writeRegister(0x21, (byte)upperThreshold) &&
		writeRegister(0x22, (byte)(upperThreshold >> 8)) &&
		writeRegister(0x23, (byte)(upperThreshold) >> 16) &&
		writeRegister(0x24, (byte)lowerThreshold) &&
		writeRegister(0x25, (byte)(lowerThreshold >> 8)) &&
		writeRegister(0x26, (byte)(lowerThreshold >> 16));
}

// channel : CHANNEL::ALS = Ambient Light sensor, CHANNEL::UVS = UV sensor
bool LTR390UVSensor::setChannel(CHANNEL channel)
{
	if (channel == CHANNEL::ALS)
		mainCtrlRegShadow &= ~0x08;
	else
		mainCtrlRegShadow |= 0x08;
	return writeRegister(0x00, mainCtrlRegShadow);
}

bool LTR390UVSensor::getChannel(CHANNEL* channel)
{
	bool ok = readRegister(0x00, &mainCtrlRegShadow);
	*channel = (mainCtrlRegShadow & 0x08) ? CHANNEL::UVS : CHANNEL::ALS;
	return ok;
}

// enable : 0=sensor in standby, 1=sensor active
// after reset() the sensor is in standby
bool LTR390UVSensor::enableSensor(bool enable)
{
	if (enable)
		mainCtrlRegShadow |= 0x02;
	else
		mainCtrlRegShadow &= ~0x02;
	return writeRegister(0, mainCtrlRegShadow);
}

// dataReady    : 0=old reading, 1=new reading ready
// intTriggered : 0=not triggered, 1=triggered
// powerOn      : 0=cleared when read, 1=power on has reset registers
bool LTR390UVSensor::getStatus(bool* dataReady, bool* intTriggered /*=NULL*/, 
	bool* powerOn /*=NULL*/)
{
	byte b;
	bool ok = readRegister(0x07, &b);
	*dataReady = b & 0x08 ? 1 : 0;
	if (intTriggered)
		*intTriggered = b & 0x10 ? 1 : 0;
	if (powerOn)
		*powerOn = b & 0x20 ? 1 : 0;
	return ok;
}

// Get the sensor's reading, ALS or UVS
// channel : 0=ALS channel (default), 1=UVS channel
// reading = 0 if the sensor channel is not active, see setChannel()
bool LTR390UVSensor::getReading(CHANNEL channel, ulong* reading)
{
	// UVS=register 0x10, ALS=register 0x0d 
	uint reg = channel ? 0x10 : 0x0d;
	byte buf[4];
	if (readRegister(reg++, buf + 0) &&
		readRegister(reg++, buf + 1) &&
		readRegister(reg, buf + 2)) {
		buf[3] = 0;
		*reading = *(ulong*)buf;
		return true;
	}
	*reading = 0;
	return false;
}

// LUX and UV Index calculations, see Data Sheet p24

// Pre-calculate sensor sensitivity values when configuration is changed
void LTR390UVSensor::calculateSensitivity(uint gain, uint resolution)
{
	static const uint gainsI[5] = { 1, 3, 6, 9, 18 };

	#ifdef LTR390_FLOAT
	static const float integrationTimesF[5] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };

	// ambient light sensor sensitivity
	float gainF = (float)gainsI[gain];
	alsSensitivityF = (0.6f * wfacF) / (gainF * integrationTimesF[resolution]);
	// uv sensor sensitivity
	uvsSensitivityF = wfacF / (2300.0f * (gainF / 18.0f) * (integrationTimesF[resolution] / 4.0f));
	#endif

	#ifdef LTR390_INT
	static const uint integrationTimesI[5] = { 4000, 2000, 1000, 500, 250 };

	// ambient light sensor sensitivity
	alsSensitivityI = (ulong)gainsI[gain] * (400 >> resolution);
	// uv sensor sensitivity x 1000
	uvsSensitivityI = (2300L * gainsI[gain] * integrationTimesI[resolution]) / 72;
	#endif

	// maximum sensor reading according to resolution
	maxReading = 0x000FFFFF >> resolution;
}

// Float versions
#ifdef LTR390_FLOAT

// LUX Calculation
// Typical LUX values
// 0.1              Moonless or cloudy night sky
// 1                Full moon on a clear night
// 30               A dark winter's night in The Kings Arms
// 500              Recommended level for a bright indoor office
// 10000..20000     Daylight under open shade or a clear blue sky
// 100000+          Direct unobstructed sunlight at noon
// 999999           Overflow, reduce the gain
float LTR390UVSensor::calculateLuxF(ulong alsReading)
{
	// overflow, reduce the gain
	if (alsReading >= maxReading)
		return 999999.0f;

	return alsReading * alsSensitivityF;
}

// UV Index Calculation
// 1..2     Low
// 3..5     Moderate
// 6..7     High
// 8..10    Very high
// 11+      Extreme!
// 9999     Overflow, reduce the gain
uint LTR390UVSensor::calculateUVIndexF(ulong uvsReading)
{
	// overflow, reduce the gain
	if (uvsReading >= maxReading)
		return 9999;

	float uvi = uvsReading * uvsSensitivityF;
	uvi += 0.5f;		// round up for uint return value
	uint uviI = (uint)uvi;
	return uviI == 0 ? 1 : uviI;
}
#endif

// Integer versions
#ifdef LTR390_INT

// LUX Calculation
// Typical LUX values
// 0.1              Moonless or cloudy night sky
// 1                Full moon on a clear night
// 30               The Kings Arms on a dark winter night
// 500              Recommended level for a bright indoor office
// 10000..20000     Daylight under open shade or a clear blue sky
// 100000+          Direct unobstructed sunlight at noon
// 999999           Overflow, reduce the gain
ulong LTR390UVSensor::calculateLuxI(ulong alsReading)
{
	// overflow, reduce the gain
	if (alsReading >= maxReading)
		return 999999;

	ulong luxI = (600 * alsReading) / alsSensitivityI;
	// + 5000 to round up to nearest int
	return ((luxI * wfacI) + 5000) / 10000;
}

// UV Index Calculation
// 1..2     Low
// 3..5     Moderate
// 6..7     High
// 8..10    Very high
// 11+      Extreme, take cover!
// 9999     Overflow, reduce the gain
uint LTR390UVSensor::calculateUVIndexI(ulong uvsReading)
{
	// overflow, reduce the gain
	if (uvsReading >= maxReading)
		return 9999;

	// round up with '+ (uvsSensitivityI >> 1)'
	uint uviI = ((uvsReading * wfacI) + (uvsSensitivityI >> 1)) / uvsSensitivityI;
	return uviI == 0 ? 1 : uviI;
}
#endif


// Internal Methods

bool LTR390UVSensor::writeRegister(byte reg, byte value)
{
	wire->beginTransmission(i2cAdds);
	wire->write(reg);
	wire->write(value);
	if (wire->endTransmission() != 0) {
		// soft reset has no ACK
		if (!(reg == 0x00 && (value & 0x10)))
			LOGERROR("write endTransmission() failed");
		return false;
	}
	return true;
}

// note: value = 0 if readRegister() fails
bool LTR390UVSensor::readRegister(byte reg, byte* value)
{
	wire->beginTransmission(i2cAdds);
	wire->write(reg);
	if (wire->endTransmission() != 0) {
		LOGERROR("read endTransmission() failed");
		*value = 0;
		return false;
	}
	wire->requestFrom(i2cAdds, (size_t)1);
	if (wire->readBytes(value, 1) != 1) {
		LOGERROR("readBytes() failed");
		*value = 0;
		return false;
	}
	return true;
}

