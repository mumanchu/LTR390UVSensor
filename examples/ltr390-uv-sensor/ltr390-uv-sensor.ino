/////////////////////////////////////////////////////////////////////
// Test sketch for the mumanchu LTR390UVSensror library
// 2026.08.24
// Preliminary version only tested on STM32 Nucleo F103RB

#include <Wire.h>

#include "LTR390UVSensor.h"
LTR390UVSensor uvSensor;

uint resolution = 0;	// 0..4
uint sampleRate = 2;	// 0..5
uint gain = 0;			// 0..4
int readings = 0;

void setup() 
{
	// use different pins for RX/TX
	// this is only for the STM32 Nucleo-64 boards
	Serial.setTx(PC_10);
	Serial.setRx(PC_11);

	Serial.begin(115200);
	delay(3000);

	// PuTTY clear screen and scrollback
	Serial.print("\033[2J\033[H\033[3J");

	Serial.println("\n\rStarted\n\r");
	Serial.flush();

	pinMode(LED_BUILTIN, OUTPUT);

	Wire.begin();
	Wire.setClock(400000);
	// sometimes the timeout is infinite?
	Wire.setTimeout(100);

	if (!uvSensor.begin(&Wire, 0x53)) {
		Serial.println("uvSensor.begin() failed");
		Serial.flush();
		while (1) yield();
	}
	uint partNumber, revisionId;
	if (!uvSensor.getID(&partNumber, &revisionId)) {
		Serial.println("uvSensor.getId() failed");
		Serial.flush();
		while (1) yield();
	}
	else {
		char buf[32];
		sprintf(buf, "partNumber=%u  revisionId=%u", partNumber, revisionId);
		Serial.println(buf);
		Serial.flush();
	}

	if (!uvSensor.setConfiguration(resolution, sampleRate, gain)) {
		Serial.println("uvSensor.setConfiguration() failed");
		Serial.flush();
		while (1) yield();
	}

	uint resolution1, sampleRate1, gain1;
	if (!uvSensor.getConfiguration(&resolution1, &sampleRate1, &gain1)) {
		Serial.println("uvSensor.getConfiguration() failed");
		Serial.flush();
		while (1) yield();
	}
	
	// select AL sensor
	uvSensor.setChannel(uvSensor.ALS);

	// select UV sensor
	//uvSensor.setChannel(uvSensor.UVS);

	// start taking readings
	uvSensor.enableSensor(1);
}

void loop() 
{
	// scheduler
	ulong t = millis();
	static ulong t1 = 0;
	if (1) {//t - t1 > 10) {
		t1 = t;

		// flash the LED so we know it's running
		digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

		// if a new reading is ready, read it
		bool dataReady;
		uvSensor.getStatus(&dataReady);
		if (dataReady) {
			ulong v;
			char buf[64];

			LTR390UVSensor::CHANNEL channel;
			uvSensor.getChannel(&channel);
			
			// ambient light sensor is active
			if (channel == uvSensor.ALS) {
				uvSensor.getReading(uvSensor.ALS, &v);
				float lux = uvSensor.calculateLuxF(v);
				
				ulong luxI = uvSensor.calculateLuxI(v);
				Serial.printf("luxI=%lu\n\r", luxI);

				sprintf(buf, "luxF=%.02f\tv=%lu (%08lX)", lux, v, v);
			}

			// UV sensor is active
			else if (channel == uvSensor.UVS) {
				uvSensor.getReading(uvSensor.UVS, &v);
				uint uvi = uvSensor.calculateUVIndexF(v);

				uint uviI = uvSensor.calculateUVIndexI(v);
				Serial.printf("uviI=%lu\n\r", uviI);

				sprintf(buf, "uviF=%u\tv=%lu", uvi, v);
			}
			Serial.println(buf);
			Serial.flush();

			// step through the settings
			// TODO edit as appropriate 
			// >>>>
			readings = 0;
			Serial.println();

			if (++gain > 4) {
				gain = 0;
				if (++resolution > 4) 
					resolution = 0;

			}

			//if (++sampleRate > 5) {
			//	sampleRate = 0;

			//	if (++gain > 4)
			//		gain = 0;
			//}

			//if (++gain > 4)
			//	gain = 0;

			// <<<<

			Serial.printf("resolution = %u\r\n", resolution);
			Serial.printf("sampleRate = %u\r\n", sampleRate);
			Serial.printf("gain = %u\r\n", gain);
			Serial.flush();

			uvSensor.setConfiguration(resolution, sampleRate, gain);

		}
	}
}
