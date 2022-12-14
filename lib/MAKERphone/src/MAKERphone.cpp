/*
MIT License

Copyright (c) 2020 CircuitMess

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

#include "MAKERphone.h"

#ifdef MPMINIMAL
#pragma message(__FILE__ ": MPMINIMAL")
#endif

#ifndef MPMINIMAL
TaskHandle_t Task1;
void Task1code(void *pvParameters);
#endif

TaskHandle_t MeasuringTask;
extern MAKERphone mp;
//audio refresh task
volatile uint32_t voltageSample = 0;
volatile double voltage = 3700;
boolean btnHeld;
boolean btnHeldField[10];
volatile uint32_t _timesMeasured = 0;
uint16_t offset1Value = 1900;
volatile bool chargeDuringADCRead = 0;
uint32_t simBusyCounter = 0;
uint8_t _SDcounter = 0;
static esp_adc_cal_characteristics_t *adc_chars;
void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
	Serial.printf("Listing directory: %s\n", dirname);

	File root = fs.open(dirname);
	if (!root)
	{
		Serial.println("Failed to open directory");
		return;
	}
	if (!root.isDirectory())
	{
		Serial.println("Not a directory");
		return;
	}

	File file = root.openNextFile();
	while (file)
	{
		if (strcmp(file.name(), "/.core") != 0 && strcmp(file.name(), "/System Volume Information") != 0)
		{
			if (file.isDirectory())
			{
				Serial.print("  DIR : ");
				Serial.println(file.name());
				if (levels)
				{
					listDir(fs, file.name(), levels - 1);
				}
			}
			else
			{
				Serial.print("  FILE: ");
				Serial.print(file.name());
				Serial.print("  SIZE: ");
				Serial.println(file.size());
				_SDcounter++;
			}
		}
		file = root.openNextFile();
	}
}
String MAKERphone::waitForOK()
{
	char buffer[400];
	memset(buffer, 0, sizeof(buffer));
	char c;
	uint32_t temp = millis();
	while (strstr(buffer, "OK") == nullptr && millis() - temp < 10000)
		if (Serial1.available() && strlen(buffer) < 400)
		{
			c = Serial1.read();
			strncat(buffer, &c, 1);
			// Serial.println(buffer);
			if (strstr(buffer, "ERROR") != nullptr)
			{
				if (strstr(strstr(buffer, "ERROR"), "\r") != nullptr)
					break;
			}
		}
	return String(buffer);
}
void ADCmeasuring(void *parameters)
{
	while (1)
	{
		if (digitalRead(CHRG_INT)) //not charging
		{

			voltage = ADCrawRead();
			if (digitalRead(CHRG_INT))
			{
				voltage *= (float)(3600.0 / offset1Value);
				vTaskDelay(1000);
			}
			else
				vTaskDelay(1);
		}
		else
		{
			chargeDuringADCRead = 1;
			vTaskDelay(1);
		}
	}
}
uint32_t ADCrawRead(void)
{
	uint32_t voltagesum = 0;
	for (uint32_t measuringCounter = 0; measuringCounter < 100; measuringCounter++)
	{
		vTaskDelay(10);
		voltagesum += (adc1_get_raw(ADC1_CHANNEL_7));
		if (!digitalRead(CHRG_INT)) //charging
		{
			voltageSample = 0;
			chargeDuringADCRead = 1;
			break;
		}
		voltageSample++;
	}
	if (digitalRead(CHRG_INT)) //not charging
	{
		double _voltage = ((voltagesum) / 100);
		voltageSample = 0;
		_timesMeasured++;

		return _voltage;
	}
	else
		return voltage;
}
//core
void MAKERphone::begin(bool splash)
{
	String input = "";
	pinMode(soundSwitchPin, OUTPUT);
	digitalWrite(soundSwitchPin, LOW);
	pinMode(SIM800_DTR, OUTPUT);
	digitalWrite(SIM800_DTR, 0);
	pinMode(BTN_INT, INPUT_PULLUP);
	pinMode(SIM_INT, INPUT_PULLUP);
	pinMode(SD_INT, INPUT_PULLUP);
	pinMode(CHRG_INT, INPUT_PULLUP);
	pinMode(OFF_PIN, OUTPUT);
	digitalWrite(OFF_PIN, 0);
	esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0);
	//Initialize and start with the NeoPixels
	FastLED.addLeds<NEOPIXEL, PIXELPIN>(leds, 8);

	adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
	// int vRef = REG_GET_FIELD(EFUSE_BLK0_RDATA4_REG, EFUSE_RD_ADC_VREF);

	// if (vRef & 0x10)
	// 	vRef = -(vRef & 0x0F);
	// else
	// 	vRef = (vRef & 0x0F);
	// vRef *= 7;
	// vRef += 1100;
	// Serial.print("Vref: ");
	// Serial.println(vRef);
	esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_0db, ADC_WIDTH_BIT_12, 1100, adc_chars);
	adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_11db);
	adc1_config_width(ADC_WIDTH_BIT_12);
	if (REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_BLK3_PART_RESERVE) == 1) //calibrated?
	{
		Serial.println("Calibrated!");
		isCalibrated = 1;
		if (REG_GET_FIELD(EFUSE_BLK3_RDATA3_REG, EFUSE_RD_ADC2_TP_HIGH) == 0)
		{
			offset1Value = REG_GET_FIELD(EFUSE_BLK3_RDATA3_REG, EFUSE_RD_ADC1_TP_HIGH) << 7;
			offset1Value = offset1Value | REG_GET_FIELD(EFUSE_BLK3_RDATA3_REG, EFUSE_RD_ADC1_TP_LOW);
			Serial.print("offset: ");
			Serial.println(offset1Value);
		}
		else
			Serial.println("Bad calibration!");
	}
	else
	{
		Serial.println("Not calibrated!");
		isCalibrated = 0;
	}
	// if (splash == 1) {
	// 	for (uint8_t i = 0; i < NUMPIXELS; i++) {
	// 		for (uint8_t x = 0; x <= 128; x += 2) {
	// 			leds[i] = CRGB(x, 0, 0);
	// 			delay(1);
	// 			FastLED.show();
	// 		}
	// 	}
	// 	for (uint8_t i = 0; i < NUMPIXELS; i++) {
	// 		for (int16_t x = 128; x >= 0; x -= 2) {
	// 			leds[i] = CRGB(x, 0, 0);
	// 			delay(1);
	// 			FastLED.show();
	// 		}
	// 	}
	// }
	FastLED.clear();
	buttons.begin();

#ifndef MPMINIMAL
	RTC.begin();
#endif

	// Serial.println(batteryVoltage);
	// delay(50);

	//display initialization
	tft.init();
	tft.invertDisplay(0);
	tft.setRotation(3);
	display.setColorDepth(8); // Set colour depth of Sprite to 8 (or 16) bits
	display.createSprite(160, 128);
	display.setRotation(1);

	display.fillScreen(TFT_BLACK);
	display.pushSprite(0, 0);

	//PWM SETUP FOR ESP32
	delay(30);
	ledcSetup(0, 2000, 8);
	ledcSetup(LEDC_CHANNEL, LEDC_BASE_FREQ, LEDC_TIMER);
	ledcAttachPin(LCD_BL_PIN, LEDC_CHANNEL);
	ledcAnalogWrite(LEDC_CHANNEL, 255);

	for (uint8_t i = 255; i > actualBrightness; i--) {
		ledcAnalogWrite(LEDC_CHANNEL, i);
		delay(2);
	}

	ledcAnalogWrite(LEDC_CHANNEL, 0);

#ifndef MPMINIMAL
	//SD startup
	uint32_t tempMillis = millis();
	SDinsertedFlag = 1;
	while (!SD.begin(5, SPI, 8000000)) {
		Serial.println("SD ERROR");
		if (millis() - tempMillis > 10) {
			SDinsertedFlag = 0;
			break;
		}
	}

	if (digitalRead(SD_INT) && SDinsertedFlag) {
		_SDinterruptError = 1;
	}

	Serial.print("SD interrupt error: ");
	Serial.println(_SDinterruptError);
#endif

	if (splash == 1) {
		splashScreen(); //Show the main splash screen
	} else {
		delay(500);
	}

#ifndef MPMINIMAL
	//EEPROM setup for firmware_version
	EEPROM.begin(300);
	sim_module_version = EEPROM.readByte(GSM_MODULE_ADDRESS);
	Serial.print("Read sim value: ");
	Serial.println(sim_module_version);
	uint32_t temp = millis();
	bool found = 0;
	char myBuffer[200];
	memset(myBuffer, 0, sizeof(myBuffer));
	for (uint8_t i = 0; i < 200; i++)
		myBuffer[i] = 0;
	bool temperino = 1;
	Serial1.flush();
	if (sim_module_version == 0) {
		Serial1.begin(115200, SERIAL_8N1, 17, 16);
		pinMode(17, INPUT_PULLUP);
		delay(100);
		Serial1.println("AT");
		temperino = 1;
		uint32_t atMillis = millis();
		while (!found && millis() - temp < 8000) {
			if (millis() - atMillis > 1000) {
				Serial1.println("AT");
				Serial.println("at printed");
				atMillis = millis();
			}

			if (Serial1.available()) {
				char c = Serial1.read();
				strncat(myBuffer, &c, 1);
				Serial.println(myBuffer);
				Serial.println("-------------");
				atMillis = millis();
				temp = millis();
			}

			if (strstr(myBuffer, "OK") != nullptr || strstr(myBuffer, "RDY") != nullptr) {
				found = 1;
			}
		}

		if (!found) {
			Serial.println("not sim7600");
			Serial1.end();
			temp = millis();
			memset(myBuffer, 0, sizeof(myBuffer));
			Serial1.begin(9600, SERIAL_8N1, 17, 16);
			pinMode(17, INPUT_PULLUP);
			delay(100);
			Serial1.println("AT");
			temperino = 1;

			while (!found && millis() - temp < 1500) {
				if (temperino && millis() - temp > 1000) {
					Serial1.println("AT");
					temperino = 0;
				}

				if (Serial1.available()) {
					char c = Serial1.read();
					strncat(myBuffer, &c, 1);
					Serial.println(myBuffer);
					Serial.println("-------------");
					temp = millis();
				}

				if (strstr(myBuffer, "OK") != nullptr) {
					found = 1;
				}
			}
			if (found) {
				sim_module_version = 1; //SIM800
			} else {
				Serial.println("not sim800");
				sim_module_version = 255; //NO SIM MODULE
			}
		}
		else {
			sim_module_version = 0; //SIM7600
		}
	} else {
		Serial1.begin(9600, SERIAL_8N1, 17, 16);
		pinMode(17, INPUT_PULLUP);
		delay(100);
		Serial1.println("AT");
		temperino = 1;

		while (!found && millis() - temp < 1500) {
			if (temperino && millis() - temp > 1000) {
				Serial1.println("AT");
				temperino = 0;
			}

			if (Serial1.available()) {
				char c = Serial1.read();
				strncat(myBuffer, &c, 1);
				Serial.println(myBuffer);
				Serial.println("-------------");
				temp = millis();
			}

			if (strstr(myBuffer, "OK") != nullptr) {
				found = 1;
			}
		}

		if (!found) {
			Serial.println("not sim800");
			Serial1.end();
			temp = millis();
			memset(myBuffer, 0, sizeof(myBuffer));
			Serial1.begin(115200, SERIAL_8N1, 17, 16);
			pinMode(17, INPUT_PULLUP);
			delay(100);
			Serial1.println("AT");
			temperino = 1;
			uint32_t atMillis = millis();

			while (!found && millis() - temp < 8000) {
				if (millis() - atMillis > 1000) {
					Serial1.println("AT");
					Serial.println("at printed");
					atMillis = millis();
				}

				if (Serial1.available()) {
					char c = Serial1.read();
					strncat(myBuffer, &c, 1);
					Serial.println(myBuffer);
					Serial.println("-------------");
					atMillis = millis();
					temp = millis();
				}

				if (strstr(myBuffer, "OK") != nullptr || strstr(myBuffer, "RDY") != nullptr) {
					found = 1;
				}
			}

			if (found) {
				sim_module_version = 0; //SIM7600
			} else {
				Serial.println("not sim7600");

				sim_module_version = 255; //NO SIM MODULE
			}
		}
		else {
			sim_module_version = 1; //SIM800
		}
	}
	Serial1.end();

	if (sim_module_version == 1) {
		Serial1.begin(9600, SERIAL_8N1, 17, 16);
		pinMode(17, INPUT_PULLUP);
		micGain = 14;
	} else if (sim_module_version == 0) {
		Serial1.begin(115200, SERIAL_8N1, 17, 16);
		pinMode(17, INPUT_PULLUP);
		micGain = 1;
	}

	if (sim_module_version != 255) {
		Serial1.setRxBufferSize(1024);
		Serial1.println("ATI");
		String temp = waitForOK();
		Serial.println(temp);

		if (temp.indexOf("SIM800") != -1) {
			if (sim_module_version == 0) {
				Serial1.println("AT+IPR=9600");
				Serial1.end();
				Serial1.begin(9600, SERIAL_8N1, 17, 16);
				pinMode(17, INPUT_PULLUP);
			}

			sim_module_version = 1;
		} else if (temp.indexOf("SIM7600") != -1) {
			sim_module_version = 0;
		}

		checkSim();

		if (sim_module_version == 1) {
			Serial1.println("AT+CCALR?");
			uint32_t cregMillis = millis();
			String cregString = "";
			while (networkRegistered != 1 && networkRegistered != 5 && millis() - cregMillis < 1000)
			{
				if (cregString != "")
				{
					// Serial.println(cregString);
					if (cregString.indexOf("\n", cregString.indexOf("+CCALR:")) != -1)
					{
						uint16_t helper = cregString.indexOf(" ", cregString.indexOf("+CCALR:"));
						networkRegistered = cregString.substring(helper + 1, helper + 2).toInt();
					}
				}
				if (cregString != "" && networkRegistered != -1)
				{
					Serial1.println("AT+CCALR?");
				}
				cregString = waitForOK();
			}
		} else {
			Serial1.println("AT+CREG?");
			uint32_t cregMillis = millis();
			String cregString = waitForOK();
			while (networkRegistered != 1 && networkRegistered != 5 && millis() - cregMillis < 1000)
			{
				if (cregString != "")
				{
					// Serial.println(cregString);
					if (cregString.indexOf("\n", cregString.indexOf("+CREG:")) != -1)
					{
						uint16_t helper = cregString.indexOf(",", cregString.indexOf("+CREG:"));
						networkRegistered = cregString.substring(helper + 1, helper + 2).toInt();
					}
				}
				if (cregString != "" && networkRegistered != -1)
				{
					Serial1.println("AT+CREG?");
				}
				cregString = waitForOK();
			}
		}
	}

	if (EEPROM.readByte(GSM_MODULE_ADDRESS) != sim_module_version) {
		Serial.println("written module type");
		EEPROM.writeByte(GSM_MODULE_ADDRESS, sim_module_version);
		EEPROM.commit();
	}

	updateTimeRTC();
	//Audio
	initWavLib();
	xTaskCreatePinnedToCore(
		Task1code, /* Task function. */
		"Task1",   /* name of task. */
		10000,	   /* Stack size of task */
		NULL,	   /* parameter of the task */
		1,		   /* priority of the task */
		&Task1,
		0); /* Task handle to keep track of created task */
	addOscillator(osc);
#endif

	if (digitalRead(CHRG_INT)) {
		voltage = ADCrawRead() * (3600.0 / offset1Value);
	}

	xTaskCreatePinnedToCore(
		ADCmeasuring,	 /* Task function. */
		"MeasuringTask", /* name of task. */
		10000,			 /* Stack size of task */
		NULL,			 /* parameter of the task */
		1,				 /* priority of the task */
		&MeasuringTask,
		1); /* Task handle to keep track of created task */

#ifndef MPMINIMAL
	ringtone = nullptr;
	if (SDinsertedFlag) {
		Serial.println("SD inserted");
		loadSettings(1);
		loadAlarms();
		loadNotifications();
		ringtone = new MPTrack((char *)ringtone_path.c_str());
	} else {
		Serial.println("SD missing");

		if (sim_module_version == 0) {
			micGain = 1;
		} else {
			micGain = 14;
		}
	}

	applySettings();
	checkAlarms();

	if (sim_module_version != 255) {
		networkModuleInit();
	}

	networkInitialized = 1;
	sleepTimer = millis();
	networkDisconnectMillis = millis();
	if (digitalRead(CHRG_INT) && buttons.timeHeld(BTN_1) > 0 && buttons.timeHeld(BTN_3) > 0 && buttons.timeHeld(BTN_5) > 0 && buttons.timeHeld(BTN_7) > 0 && buttons.timeHeld(BTN_9) > 0 && buttons.timeHeld(BTN_0) > 0 && !isCalibrated) {
		Serial.println("Calibration will start"); //safety checks before calibrating
	} else {
		isCalibrated = 1;
	}

#endif
}

bool MAKERphone::update()
{
#ifndef MPMINIMAL
	newMessage = 0;
	exitedLockscreen = 0;
	if (digitalRead(SD_INT) && (SDinsertedFlag || (!SDinsertedFlag && SDerror)) && !_SDinterruptError) {
		SDinsertedFlag = 0;
		SDerror = 0;
		for (int i = 0; i < 4; i++)
		{
			if (tracks[i] != nullptr)
			{
				removeTrack(tracks[i]);
				tracks[i] = nullptr;
			}
		}
		if (ringtone != nullptr)
		{
			removeTrack(ringtone);
			ringtone = nullptr;
		}
		SD.end();

		SDremovedPopup();
		// initWavLib();
	} else if (!digitalRead(SD_INT) && !SDinsertedFlag && !SDerror && !_SDinterruptError) {
		SDinsertedFlag = 1;
		SDinsertedPopup();
		uint32_t tempMillis = millis();
		while (!SD.begin(5, SPI, 8000000)) {
			Serial.println("SD ERROR");
			if (millis() - tempMillis > 50) {
				SDinsertedFlag = 0;
				SDerror = 1;
				break;
			}
		}

		if (SDinsertedFlag) {
			loadSettings();
			applySettings();
		}
	}

	if (screenshotFlag) {
		screenshotFlag = 0;
		takeScreenshot();
		homePopup(0);
	}
#endif

	buttonsPressed = 0;

	for (uint8_t i = 16; i < 22; i++) {
		if (buttons.pressed(i)) {
			buttonsPressed = 1;
			break;
		}
	}
#ifndef MPMINIMAL
	if (!buttonsPressed && digitalRead(BTN_INT) && sleepTime != 0 && !inCall && !inAlarmPopup) {
		if (millis() - sleepTimer >= sleepTimeActual * 1000) {
			buttons.update();
			sleepTimer = millis();
			sleep();
			if (!inLockScreen) {
				lockscreen();
				exitedLockscreen = 1;
			}
		}
	} else if ((buttonsPressed || !digitalRead(BTN_INT) || inCall || inAlarmPopup) && sleepTime) {

		sleepTimer = millis();
	}

	if (millis() > 7000) {
		simReady = 1;
	} else {
		simReady = 0;
	}

	//refreshing signal and battery info
	if (simReady && dataRefreshFlag) {
		if (sim_module_version == 1) {
			if (millis() - refreshMillis >= 5000) {
				refreshMillis = millis();
				updateBuffer = "";
				Serial1.println("AT+CBC");

				if (simInserted && !airplaneMode) {
					if (networkRegistered == 5 || networkRegistered == 1) {
						if (carrierName == "") {
							carrierNameCounter++;
							Serial1.println("AT+CSPN?");
						}
						Serial1.println("AT+CSQ");
					}
					Serial1.println("AT+CCALR?");
					if (clockYear % 100 < 19 || clockYear % 100 >= 40 || clockMonth < 1 || clockMonth > 12 ||
						clockHour > 24 || clockMinute >= 60) {
						Serial1.println("AT+CCLK?");
					}
				}
			}
		} else if (sim_module_version == 0) {
			if (millis() - refreshMillis >= 2000) {
				refreshMillis = millis();
				switch (alternatingRefresh) {
				case 0:
					if (simInserted && !airplaneMode && (networkRegistered == 5 || networkRegistered == 1)) {
						if (carrierName == "") {
							carrierNameCounter++;
							Serial1.println("AT+CSPN?");
						}
						else {
							refreshMillis = millis() + 2000;
						}
					} else {
						refreshMillis = millis() + 2000;
					}
					break;

				case 1:
					if (simInserted && !airplaneMode && (networkRegistered == 5 || networkRegistered == 1)) {
						Serial1.println("AT+CSQ");
					} else {
						refreshMillis = millis() + 2000;
					}
					break;

				case 2:
					if (simInserted && !airplaneMode && (networkRegistered == 5 || networkRegistered == 1)) {
						if (clockYear % 100 < 19 || clockYear % 100 >= 40 || clockMonth < 1 || clockMonth > 12 || clockHour > 24 || clockMinute >= 60) {
							Serial1.println("AT+CCLK?");
						} else {
							refreshMillis = millis() + 2000;
						}
					} else {
						refreshMillis = millis() + 2000;
					}
					break;
				case 3:
					Serial1.println("AT+CREG?");
					break;
				}
				updateBuffer = "";
				alternatingRefresh++;
				if (alternatingRefresh > 3) {
					alternatingRefresh = 0;
				}
			}
		}

		if (Serial1.available() && sim_module_version != 255) {
			c = Serial1.read();
			updateBuffer += c;
			Serial.println("--------------");
			Serial.println(updateBuffer);

			if (updateBuffer.indexOf("\r", updateBuffer.indexOf("OK")) != -1) {
				updateBuffer = "";
			}

			if (simInserted && !airplaneMode) {
				if (carrierName == "" && updateBuffer.indexOf("\n", updateBuffer.indexOf("+CSPN:")) != -1) {
					uint16_t helper = updateBuffer.indexOf("\"", updateBuffer.indexOf("+CSPN:")) + 1;
					carrierName = updateBuffer.substring(helper, updateBuffer.indexOf("\"", helper));
				}

				if (updateBuffer.indexOf("\n", updateBuffer.indexOf("+CSQ:")) != -1) {
					signalStrength = updateBuffer.substring(updateBuffer.indexOf(" ", updateBuffer.indexOf("+CSQ:")) + 1,
															updateBuffer.indexOf(",", updateBuffer.indexOf(" ", updateBuffer.indexOf("+CSQ:"))))
										 .toInt();
				}

				if (clockYear % 100 < 19 || clockYear % 100 >= 40 || clockMonth < 1 || clockMonth > 12 ||
					clockHour > 24 || clockMinute >= 60) {
					if (updateBuffer.indexOf("\n", updateBuffer.indexOf("+CCLK:")) != -1) {
						uint16_t index = updateBuffer.indexOf(F("+CCLK: \""));
						char c1, c2; //buffer for saving date and time numerals in form of characters
						c1 = updateBuffer.charAt(index + 8);
						c2 = updateBuffer.charAt(index + 9);
						clockYear = 2000 + ((c1 - '0') * 10) + (c2 - '0');

						c1 = updateBuffer.charAt(index + 11);
						c2 = updateBuffer.charAt(index + 12);
						clockMonth = ((c1 - '0') * 10) + (c2 - '0');

						c1 = updateBuffer.charAt(index + 14);
						c2 = updateBuffer.charAt(index + 15);
						clockDay = ((c1 - '0') * 10) + (c2 - '0');

						c1 = updateBuffer.charAt(index + 17);
						c2 = updateBuffer.charAt(index + 18);
						clockHour = ((c1 - '0') * 10) + (c2 - '0');

						c1 = updateBuffer.charAt(index + 20);
						c2 = updateBuffer.charAt(index + 21);
						clockMinute = ((c1 - '0') * 10) + (c2 - '0');

						c1 = updateBuffer.charAt(index + 23);
						c2 = updateBuffer.charAt(index + 24);
						clockSecond = ((c1 - '0') * 10) + (c2 - '0');

						//TO-DO: UPDATE THE RTC HERE
						DateTime now = DateTime(clockYear, clockMonth, clockDay,
												clockHour, clockMinute, clockSecond);
						RTC.adjust(now);
						if (clockYear % 100 != 4 && clockYear % 100 != 80 && clockMonth != 0 && clockMonth < 13 &&
							clockHour < 25 && clockMinute < 60) {
							// Serial1.println("AT+CNMP=2");
							// clockFallback = 1;
						}
						Serial.println(F("\nRTC TIME UPDATE OVER GSM DONE!"));
					}
				}
			}

			if (updateBuffer.indexOf("\n", updateBuffer.indexOf("+CBC:")) != -1) {
				uint16_t helper = updateBuffer.indexOf(",", updateBuffer.indexOf("+CBC:"));
				helper = updateBuffer.indexOf(",", helper + 1) + 1;
				uint16_t tempVoltage = updateBuffer.substring(helper, updateBuffer.indexOf("\n", helper)).toInt();
				if (tempVoltage > 3000 && tempVoltage < 5000)
					simVoltage = tempVoltage;
			}

			if (updateBuffer.indexOf("\n", updateBuffer.indexOf("+CCALR:")) != -1 && sim_module_version == 1) {
				uint16_t helper = updateBuffer.indexOf(" ", updateBuffer.indexOf("+CCALR:"));
				networkRegistered = updateBuffer.substring(helper + 1, helper + 2).toInt();
				if (networkRegistered != 5 && networkRegistered != 1)
					carrierNameCounter++;
				updateBuffer = "";
			}

			if (updateBuffer.indexOf("\n", updateBuffer.indexOf("+CREG:")) != -1 && sim_module_version == 0) {
				uint16_t helper = updateBuffer.indexOf(",", updateBuffer.indexOf("+CREG:"));
				networkRegistered = updateBuffer.substring(helper + 1, helper + 2).toInt();
			}
		}
	}

#endif
	batteryVoltage = voltage;

#ifndef MPMINIMAL
	if (digitalRead(CHRG_INT) && ((sim_module_version != 1 && batteryVoltage <= 3600) || (sim_module_version == 1 && simVoltage <= 3580)) && isCalibrated)
#else
	if (digitalRead(CHRG_INT) && batteryVoltage <= 3600 && isCalibrated)
#endif
	{
		if (timesMeasured != _timesMeasured)
			shutdownCounter++;
		if (shutdownCounter > 2 && timesMeasured != _timesMeasured)
		{
			tft.setTextColor(TFT_BLACK);
			tft.setTextSize(1);
			tft.setTextFont(2);
			tft.fillRect(12, 36, 138, 56, TFT_WHITE);
			tft.setCursor(37, 45);
			tft.print("Battery critical!");
			tft.setCursor(40, 61);
			tft.print("Turning off...");
			Serial.print("battery: ");
			Serial.println(batteryVoltage);
#ifndef MPMINIMAL
			Serial.print("SIM: ");
			Serial.println(simVoltage);
#endif
			delay(1500);

			//Serial1.println("AT+CFUN=4");
			//Serial1.println("AT+CSCLK=2");

			delay(750);
			Serial.println("TURN OFF");
			digitalWrite(SIM800_DTR, 1);
			FastLED.clear(1);
			ledcDetachPin(LCD_BL_PIN);
			pinMode(LCD_BL_PIN, OUTPUT);
			digitalWrite(LCD_BL_PIN, 1);
			digitalWrite(OFF_PIN, 1);

			rtc_gpio_isolate(GPIO_NUM_16);
			rtc_gpio_isolate(GPIO_NUM_17);
			rtc_gpio_isolate(GPIO_NUM_33);
			rtc_gpio_isolate(GPIO_NUM_34);
			rtc_gpio_isolate(GPIO_NUM_36);
			rtc_gpio_isolate(GPIO_NUM_39);

			digitalWrite(OFF_PIN, 1);
			ESP.deepSleep(0);
		}
		timesMeasured = _timesMeasured;
	}
	else
		shutdownCounter = 0;

#ifndef MPMINIMAL
	if (alarmCleared && millis() - alarmMillis > 60000)
		alarmCleared = 0;
	if (!digitalRead(SIM_INT) && !inCall && networkInitialized)
	{
		String temp = "";
		uint32_t curr_millis = millis();
		int32_t helper = -1;
		while ((temp.indexOf("\r", temp.indexOf("\r", helper + 1) + 1) == -1 || temp.indexOf("\r", helper + 1) == -1 || helper == -1) && temp.indexOf("\r", temp.indexOf("1,4,0,0,")) == -1 && millis() - curr_millis < 2500)
		{
			if (Serial1.available())
			{
				curr_millis = millis();
				char c = Serial1.read();
				temp += c;
				// Serial.println(temp);
				// Serial.println("-----------------");
				helper = temp.indexOf("+CMT:");
				if (helper > 0)
					temp = "+CMT:";
				// Serial.println(helper);
			}
		}
		if (temp.indexOf("\r", temp.indexOf("1,4,0,0,")) != -1)
		{
			inCall = 1;
			incomingCall(temp);
			inCall = 0;
		}
		else if (temp.indexOf("+CMT:") != -1)
		{
			playNotificationSound(notification);
			incomingMessage(temp);
		}
		else if (!inAlarmPopup && !alarmCleared && currentAlarm != 99)
		{
			inAlarmPopup = 1;
			alarmPopup();
			if (alarmRepeat[currentAlarm] == 0)
				alarmEnabled[currentAlarm] = 0;
			saveAlarms();
			if (ringtone->isPlaying())
				ringtone->stop();

			alarmCleared = 1;
			alarmMillis = millis();
			inAlarmPopup = 0;
		}
	}
	updateNotificationSound();

	if (buttons.released(13) && !inShutdownPopup)
	{
		buttons.update();
		if (HOME_POPUP_ENABLE && !inHomePopup)
		{
			inHomePopup = 1;
			homePopup();
			inHomePopup = 0;
		}
		else if (!HOME_POPUP_ENABLE)
		{
			homeButtonPressed = 1;
		}
	}

#endif
	buttons.update();
#ifndef MPMINIMAL

	if (SHUTDOWN_POPUP_ENABLE && buttons.released(14) && !wokeWithPWRBTN && !inCall)
	{
		buttons.update();
		sleep();
	}
#endif
	if (buttons.held(14, 40) && !inShutdownPopup && SHUTDOWN_POPUP_ENABLE && !wokeWithPWRBTN && !inCall)
	{
		inShutdownPopup = 1;
		shutdownPopup();
		inShutdownPopup = 0;
	}
	if (wokeWithPWRBTN && buttons.released(14))
	{
		buttons.update();
		wokeWithPWRBTN = 0;
	}

#ifndef MPMINIMAL
	if (sim_module_version != 255 && simInserted)
	{
		if (networkRegistered != 1 && millis() - networkDisconnectMillis > 30000 && networkDisconnectFlag && inLockScreen && sim_module_version != 0)
		{
			display.fillScreen(TFT_BLACK);
			display.setTextColor(TFT_WHITE);
			display.setTextSize(1);
			display.setCursor(0, display.height() / 2 - 20);
			display.setTextFont(2);
			display.printCenter(F("Registering to network"));
			display.setCursor(0, display.height() / 2);
			display.printCenter(F("Please wait..."));
			display.pushSprite(0, 0);
			while (Serial1.available())
				Serial1.read();
			Serial1.println("AT+CFUN=1,1");
			char buffer[300];
			bool found = 0;
			memset(buffer, 0, sizeof(buffer));
			Serial1.flush();
			uint32_t timer = millis();

			while (!found)
			{
				if (Serial1.available())
				{

					char test = (char)Serial1.read();
					strncat(buffer, &test, 1);
					Serial.println(buffer);
				}

				if (strstr(buffer, "RDY") != nullptr)
					found = 1;
				if ((millis() - timer > 8000 && sim_module_version == 1) ||
					(millis() - timer > 28000 && sim_module_version == 0))
					break;
			}
			delay(2000);
			checkSim();
			if (sim_module_version == 1)
			{
				Serial1.println("AT+CCALR?");
				uint32_t cregMillis = millis();
				String cregString = "";
				while (networkRegistered == -1 && millis() - cregMillis < 1000)
				{
					if (millis() - cregMillis > 500)
						Serial1.println("AT+CCALR?");
					if (cregString != "")
					{
						// Serial.println(cregString);
						if (cregString.indexOf("\n", cregString.indexOf("+CCALR:")) != -1)
						{
							uint16_t helper = cregString.indexOf(" ", cregString.indexOf("+CCALR:"));
							networkRegistered = cregString.substring(helper + 1, helper + 2).toInt();
						}
					}
					cregString = waitForOK();
				}
			}
			else
			{
				Serial1.println("AT+CREG?");
				uint32_t cregMillis = millis();
				String cregString = "";
				while (networkRegistered != 1 && networkRegistered != 5 && millis() - cregMillis < 1000)
				{
					if (cregString != "")
					{
						// Serial.println(cregString);
						if (cregString.indexOf("\n", cregString.indexOf("+CREG:")) != -1)
						{
							uint16_t helper = cregString.indexOf(",", cregString.indexOf("+CREG:"));
							networkRegistered = cregString.substring(helper + 1, helper + 2).toInt();
						}
					}
					if (cregString != "" && networkRegistered != -1)
					{
						Serial1.println("AT+CREG?");
					}
					cregString = waitForOK();
				}
			}
			networkModuleInit();
			networkDisconnectFlag = 0;
			carrierNameCounter = 0;
		}
		else if (networkRegistered == 1 || !dataRefreshFlag)
			networkDisconnectMillis = millis();
	}

#endif
	if (millis() - lastFrameCount >= frameSpeed || screenshotFlag)
	{
		lastFrameCount = millis();

		if (resolutionMode == 0) //native res mode
		{
#ifndef MPMINIMAL
			if (popupTimeLeft)
				updatePopup(); // else//halved res mode
			else
#endif
				display.pushSprite(0, 0);
		}
		FastLED.setBrightness(255 / 5 * pixelsBrightness);
		FastLED.show();
		delay(1);
		FastLED.clear();
		return true;
	}
	else
		return false;
}
void MAKERphone::splashScreen()
{
	char t[] = { 'A' + 0x16, 'A' + 0x46, 'A' + 0x05, 'A' + 0x17, 'A' + 0x52, 0 };
	for (int y = -35; y < -10; y++) {
		display.setTextColor(TFT_BLACK);
		display.setFreeFont(JP1);
		display.setTextSize(1);
		display.setCursor(0, 50 + y - 1);
		display.printCenter(t);

		display.setTextColor(TFT_WHITE);
		display.setCursor(0, 50 + y);
		display.printCenter(t);
		while (!update());
	}
	delay(100);
	display.setTextColor(TFT_WHITE);
	display.setTextFont(2);
	display.setTextSize(1);
	display.setCursor(0, 100);
	display.printCenter("Loading...");
	while (!update())
		;
	delay(2000);
}
void MAKERphone::setResolution(bool res)
{
	resolutionMode = res;
	spriteCreated = 0;
}
void MAKERphone::ledcAnalogWrite(uint8_t channel, uint32_t value, uint32_t valueMax)
{
	// calculate duty, 8191 from 2 ^ 13 - 1
	uint32_t duty = (8191 / 255) * _min(value, valueMax);
	// write duty to LEDC
	ledcWrite(channel, duty);
}
void MAKERphone::shutdownPopup(bool animation)
{
	if (animation)
	{
		for (int i = 2; i < 75; i++)
		{
			// tft.fillRect(tft.width()/2 - i, 34, i * 2 + 2, 60, TFT_BLACK);
			tft.drawRect(tft.width() / 2 - i, 28, i * 2 + 2, 72, TFT_BLACK);
			tft.drawRect(tft.width() / 2 - i + 1, 29, i * 2 + 1, 70, TFT_BLACK);
			tft.fillRect(tft.width() / 2 - i + 2, 30, i * 2 - 2, 68, TFT_WHITE);
			// update();
			// delayMicroseconds(2000);
		}
	}
	dataRefreshFlag = 1;
	// tft.fillRect(10, 34, 142, 60, TFT_BLACK);
	// tft.fillRect(12, 36, 138, 56, TFT_WHITE);

	uint8_t cursor = 0;
	uint32_t blinkMillis = millis();
	bool blinkState = 0;
	dataRefreshFlag = 1;
	// tft.fillRect(12, 36, 138, 56, TFT_WHITE);
	tft.setTextColor(TFT_BLACK);
	tft.drawBitmap(38, 45, powerButton, TFT_RED);
	tft.drawBitmap(107, 45, resetButton, TFT_GREEN);
	tft.setTextFont(2);
	tft.setTextSize(1);
	// tft.setCursor(55, 44);
	// tft.print("Turn off?");
	// tft.print("YES");
	// tft.setCursor(98, 61);
	// tft.print("NO");
	tft.setCursor(18, 68);
	tft.print("Power off");
	tft.setCursor(95, 68);
	tft.print("Restart");
	// tft.print("YES      NO");
	while (!buttons.released(BTN_B))
	{
		if (millis() - blinkMillis > 350)
		{
			blinkMillis = millis();
			blinkState = !blinkState;
		}
		switch (cursor)
		{
		case 0:
			tft.drawRect(13, 37, 71, 54, blinkState ? TFT_RED : TFT_WHITE);
			tft.drawRect(14, 38, 69, 52, blinkState ? TFT_RED : TFT_WHITE);
			break;
		case 1:
			tft.drawRect(86, 37, 60, 54, blinkState ? TFT_RED : TFT_WHITE);
			tft.drawRect(87, 38, 58, 52, blinkState ? TFT_RED : TFT_WHITE);
			break;
		}
		if (buttons.released(BTN_LEFT) && cursor == 1)
		{
			cursor = 0;
			blinkMillis = millis();
			blinkState = 1;
			tft.drawRect(86, 37, 60, 54, TFT_WHITE);
			tft.drawRect(87, 38, 58, 52, TFT_WHITE);
		}
		if (buttons.released(BTN_RIGHT) && cursor == 0)
		{
			cursor = 1;
			blinkMillis = millis();
			blinkState = 1;
			tft.drawRect(13, 37, 71, 54, TFT_WHITE);
			tft.drawRect(14, 38, 69, 52, TFT_WHITE);
		}
		if (buttons.released(BTN_A))
		{
			if (cursor == 1)
			{
				tft.fillRect(12, 36, 138, 56, TFT_WHITE);
				tft.setCursor(40, 51);
				tft.print("Restarting...");
#ifndef MPMINIMAL
				if (sim_module_version == 255)
					delay(1500);
				else
				{
					while (Serial1.available())
						Serial1.read();
					Serial1.println("AT+CFUN=1,1");
					char buffer[300];
					bool found = 0;
					memset(buffer, 0, sizeof(buffer));
					Serial1.flush();
					uint32_t timer = millis();
					while (!found)
					{
						if (Serial1.available())
						{

							char test = (char)Serial1.read();
							strncat(buffer, &test, 1);
							Serial.println(buffer);
						}

						if (strstr(buffer, "RDY") != nullptr)
							found = 1;
						if ((millis() - timer > 8000 && sim_module_version == 1) ||
							(millis() - timer > 28000 && sim_module_version == 0))
							break;
					}
				}
#endif
				ESP.restart();
			}
			else
			{
				tft.fillRect(12, 36, 138, 56, TFT_WHITE);
				tft.setCursor(40, 51);
				tft.print("Turning off...");
				// Serial1.println("AT+CFUN=1,1");
				// Serial1.println("AT+CFUN=4");
				// Serial1.println("AT+CSCLK=2");

				delay(750);
				Serial.println("TURN OFF");
				digitalWrite(SIM800_DTR, 1);
				FastLED.clear(1);
				ledcDetachPin(LCD_BL_PIN);
				pinMode(LCD_BL_PIN, OUTPUT);
				digitalWrite(LCD_BL_PIN, 1);
				// digitalWrite(OFF_PIN, 1);

				rtc_gpio_isolate(GPIO_NUM_16);
				rtc_gpio_isolate(GPIO_NUM_17);
				rtc_gpio_isolate(GPIO_NUM_33);
				rtc_gpio_isolate(GPIO_NUM_34);
				rtc_gpio_isolate(GPIO_NUM_36);
				rtc_gpio_isolate(GPIO_NUM_39);

				digitalWrite(OFF_PIN, 1);
				ESP.deepSleep(0);
			}
		}
		buttons.update();
	}
	while (!update())
		;
}
void MAKERphone::shutdownPopupEnable(bool enabled)
{
	SHUTDOWN_POPUP_ENABLE = enabled;
}

#ifndef MPMINIMAL

void MAKERphone::updateTimeRTC()
{
	currentTime = RTC.now();
	clockHour = currentTime.hour();
	clockMinute = currentTime.minute();
	clockSecond = currentTime.second();
	clockDay = currentTime.day();
	clockMonth = currentTime.month();
	clockYear = currentTime.year();

	//Serial.println(F("CLOCK HOUR:"));
	//Serial.println(clockHour);

	//Serial.println(F("CLOCK MINUTE:"));
	//Serial.println(clockMinute);

	//Serial.println(F("CLOCK SECOND:"));
	//Serial.println(clockSecond);

	//Serial.println(F("\nGLOBAL TIME UPDATE OVER RTC DONE!"));
}
void Task1code(void *pvParameters)
{
	while (true)
		updateWav();
}
void MAKERphone::sleep()
{
	// if(sim_module_version != 255)
	digitalWrite(SIM800_DTR, 1);
	FastLED.clear(1);
	ledcAnalogWrite(LEDC_CHANNEL, 255);
	for (uint8_t i = actualBrightness; i < 255; i++)
	{
		ledcAnalogWrite(LEDC_CHANNEL, i);
		delay(1);
	}

	ledcDetachPin(LCD_BL_PIN);
	pinMode(LCD_BL_PIN, OUTPUT);
	digitalWrite(LCD_BL_PIN, 1);

	buttons.activateInterrupt();
	uint8_t reason = 4;
	vTaskSuspend(MeasuringTask);
	for (uint8_t i = 0; i < 4; i++)
		currTracks[i] = NULL;
	bool pausedTracks[4] = {0, 0, 0, 0};
	for (int i = 0; i < 4; i++)
	{
		if (tracks[i] != nullptr)
		{
			Serial.printf("%d track is playing: %d\n", i, tracks[i]->isPlaying());
			if (tracks[i]->isPlaying())
			{
				currTracks[i] = tracks[i];
				currTracks[i]->seek(tracks[i]->getSamplePos());
				Serial.print("PLAYING:");
				Serial.println(i);
				pausedTracks[i] = 1;
				tracks[i]->pause();
				removeTrack(tracks[i]);
			}
			else
			{
				currTracks[i] = tracks[i];
				currTracks[i]->seek(tracks[i]->getSamplePos());
			}
		}
	}
	updateWav();
	vTaskSuspend(Task1);
	tft.writecommand(16); //send 16 for sleep in, 17 for sleep out
	esp_sleep_enable_timer_wakeup(SLEEP_WAKEUP_TIME * 1000000);
	esp_light_sleep_start();
	reason = esp_sleep_get_wakeup_cause();
	while (reason == 4)
	{
		reason = esp_sleep_get_wakeup_cause();
		if (reason != 4)
			break;
		Serial.println("Timer wakeup");
		if (digitalRead(CHRG_INT))
		{
			adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_11db);
			adc1_config_width(ADC_WIDTH_BIT_12);
			voltage = ADCrawRead() * (3600.0 / offset1Value);
			Serial.println(voltage);
			batteryVoltage = voltage;
		}
		if (simInserted && sim_module_version != 255)
		{
			digitalWrite(SIM800_DTR, 0);
			delay(100);
			Serial1.end();
			delay(100);
			// Serial.println("HERE");
			// delay(5);
			if (sim_module_version == 1)
				Serial1.begin(9600, SERIAL_8N1, 17, 16);
			else
				Serial1.begin(115200, SERIAL_8N1, 17, 16);
			// Serial.println("HERE");
			// delay(5);
			while (Serial1.available())
				Serial.print(Serial1.read());

			// Serial.println("HERE");
			// delay(5);
			uint32_t cregMillis = millis();
			String cregString = "";
			int8_t tempReg = -1;

			if (sim_module_version == 1)
			{
				Serial1.println("AT+CCALR?");
				while (tempReg == -1 && millis() - cregMillis < 1000)
				{
					cregString = waitForOK();
					// Serial.println(cregString);
					if (cregString != "" && cregString.indexOf("+CCALR:") != -1)
					{
						if (cregString.indexOf("\n", cregString.indexOf("+CCALR:")) != -1)
						{
							uint16_t helper = cregString.indexOf(" ", cregString.indexOf("+CCALR:"));
							tempReg = cregString.substring(helper + 1, helper + 2).toInt();
						}
					}
					if (cregString != "" && tempReg != 1 && tempReg != 5)
					{
						Serial1.println("AT+CCALR?");
					}
				}
			}
			else
			{
				Serial1.println("AT+CREG?");
				while (tempReg == -1 && millis() - cregMillis < 5000)
				{
					cregString = waitForOK();
					// Serial.println(cregString);
					if (cregString != "" && cregString.indexOf("+CREG:") != -1)
					{
						if (cregString.indexOf("\n", cregString.indexOf("+CREG:")) != -1)
						{
							uint16_t helper = cregString.indexOf(",", cregString.indexOf("+CREG:"));
							tempReg = cregString.substring(helper + 1, helper + 2).toInt();
						}
					}
					if (cregString != "" && tempReg != 1 && tempReg != 5)
					{
						Serial1.println("AT+CREG?");
					}
				}
			}
			Serial.print("call ready: ");
			Serial.println(tempReg);
			delay(5);
			if (tempReg == 0)
			{
				while (Serial1.available())
					Serial1.read();
				Serial1.println("AT+CFUN=1,1");
				char buffer[300];
				bool found = 0;
				memset(buffer, 0, sizeof(buffer));
				Serial1.flush();
				uint32_t timer = millis();

				while (!found)
				{
					if (Serial1.available())
					{

						char test = (char)Serial1.read();
						strncat(buffer, &test, 1);
						Serial.println(buffer);
					}
					if (strstr(buffer, "RDY") != nullptr)
						found = 1;
					if ((millis() - timer > 8000 && sim_module_version == 1) ||
						(millis() - timer > 28000 && sim_module_version == 0))
						break;
				}
				delay(2000);
				checkSim();
				if (sim_module_version == 1)
				{
					Serial1.println("AT+CCALR?");
					uint32_t cregMillis = millis();
					String cregString = "";
					int8_t tempReg = -1;
					while (tempReg == -1 && millis() - cregMillis < 10000)
					{
						if (millis() - cregMillis > 500)
							Serial1.println("AT+CCALR?");
						if (cregString != "")
						{
							// Serial.println(cregString);
							if (cregString.indexOf("\n", cregString.indexOf("+CCALR:")) != -1)
							{
								uint16_t helper = cregString.indexOf(" ", cregString.indexOf("+CCALR:"));
								tempReg = cregString.substring(helper + 1, helper + 2).toInt();
							}
						}
						cregString = waitForOK();
					}
				}
				else
				{
					Serial1.println("AT+CREG?");
					uint32_t cregMillis = millis();
					String cregString = "";
					int8_t tempReg = -1;
					while (tempReg == -1 && millis() - cregMillis < 1000)
					{
						cregString = waitForOK();
						// Serial.println(cregString);
						if (cregString != "" && cregString.indexOf("+CREG:") != -1)
						{
							if (cregString.indexOf("\n", cregString.indexOf("+CREG:")) != -1)
							{
								uint16_t helper = cregString.indexOf(",", cregString.indexOf("+CREG:"));
								tempReg = cregString.substring(helper + 1, helper + 2).toInt();
							}
						}
						if (cregString != "" && tempReg != 1 && tempReg != 5)
						{
							Serial1.println("AT+CREG?");
						}
					}
				}
				networkModuleInit();
				if (tempReg != -1)
					networkRegistered = tempReg;
			}
			if (sim_module_version == 1)
			{
				while (Serial1.available())
					Serial1.read();
				uint32_t voltMillis = millis();
				String voltString = "";
				uint16_t tempVolt = 0;
				Serial1.println("AT+CBC");
				while (tempVolt == 0 && millis() - voltMillis < 1000)
				{
					voltString = waitForOK();
					Serial.println(voltString);
					if (voltString.indexOf("+CBC:") != -1)
					{
						uint16_t helper = voltString.indexOf(",", voltString.indexOf("+CBC:"));
						helper = voltString.indexOf(",", helper + 1) + 1;
						tempVolt = voltString.substring(helper, voltString.indexOf("\n", helper)).toInt();
					}
					if (tempVolt == 0)
						Serial1.println("AT+CBC");
				}
				Serial.print("voltage: ");
				simVoltage = tempVolt;
				Serial.println(tempVolt);
			}
			digitalWrite(SIM800_DTR, 1);
		}
		if (digitalRead(CHRG_INT) && ((sim_module_version != 1 && batteryVoltage <= 3580) ||
									  (sim_module_version == 1 && simVoltage <= 3580)))
		{
			//Serial1.println("AT+CFUN=4");
			//Serial1.println("AT+CSCLK=2");

			delay(750);
			Serial.println("TURN OFF");
			digitalWrite(SIM800_DTR, 1);
			FastLED.clear(1);
			ledcDetachPin(LCD_BL_PIN);
			pinMode(LCD_BL_PIN, OUTPUT);
			digitalWrite(LCD_BL_PIN, 1);
			digitalWrite(OFF_PIN, 1);

			rtc_gpio_isolate(GPIO_NUM_16);
			rtc_gpio_isolate(GPIO_NUM_17);
			rtc_gpio_isolate(GPIO_NUM_33);
			rtc_gpio_isolate(GPIO_NUM_34);
			rtc_gpio_isolate(GPIO_NUM_36);
			rtc_gpio_isolate(GPIO_NUM_39);

			digitalWrite(OFF_PIN, 1);
			ESP.deepSleep(0);
		}
		buttons.activateInterrupt();
		esp_light_sleep_start();
	}

	networkDisconnectMillis = millis();
	sleepTimer = millis();
	Serial.println("buttons wakuep");
	// if(sim_module_version != 255)
	digitalWrite(SIM800_DTR, 0);
	voltage = batteryVoltage;
	voltageSample = 0;
	vTaskResume(MeasuringTask);
	vTaskResume(Task1);
	buttons.update();
	if (buttons.pressed(14))
		wokeWithPWRBTN = 1;
	if (!digitalRead(SIM_INT))
	{
		initWavLib();
		for (int i = 0; i < 4; i++)
		{
			if (currTracks[i] != nullptr)
			{
				addTrack(currTracks[i]);
				// currTracks[i]->play();
			}
		}
		ledcAttachPin(LCD_BL_PIN, LEDC_CHANNEL);
		ledcAnalogWrite(LEDC_CHANNEL, actualBrightness);
		tft.writecommand(17);
		delay(50);
		while (!update())
			;
		return;
	}
	// while(!update());
	// delay(1000);
	if (!inLockScreen)
	{
		display.fillScreen(backgroundColors[backgroundIndex]);

		display.setFreeFont(TT1);
		display.setTextSize(6);
		//Hour shadow
		display.setTextColor(TFT_DARKGREY);
		if (clockHour == 11)
			display.setCursor(10 * 2, 28 * 2);
		else if (clockHour % 10 == 1 || (int)clockHour / 10 == 1)
			display.setCursor(7 * 2, 28 * 2);
		else
			display.setCursor(4 * 2, 28 * 2);
		if (clockHour < 10)
			display.print("0");
		display.print(clockHour);

		//minute shadow
		display.setCursor(36 * 2, 28 * 2);
		if (clockMinute < 10)
			display.print("0");
		display.print(clockMinute);

		//Hour black
		display.setTextColor(TFT_BLACK);
		if (clockHour == 11)
			display.setCursor(9 * 2, 27 * 2);
		else if (clockHour % 10 == 1 || (int)clockHour / 10 == 1)
			display.setCursor(6 * 2, 27 * 2);
		else
			display.setCursor(3 * 2, 27 * 2);
		if (clockHour < 10)
			display.print("0");
		display.print(clockHour);

		//Minute black
		display.setCursor(35 * 2, 27 * 2);
		if (clockMinute < 10)
			display.print("0");
		display.print(clockMinute);

		display.setTextSize(1 * 2);
		display.setCursor(60 * 2, 19 * 2);
		display.setTextWrap(false);
		if (clockDay < 10)
			display.print("0");
		display.print(clockDay);
		display.print("/");
		if (clockMonth < 10)
			display.print("0");
		display.print(clockMonth);
		display.setCursor(62 * 2, 25 * 2);
		display.print(clockYear);
		/*display.setTextSize(2);
		display.setCursor(10, 50);
		display.print("12:00");*/
		uint8_t helper = 11;
		if (sim_module_version == 255)
			helper = 1;
		if (simInserted && !airplaneMode && sim_module_version != 255)
		{
			if (signalStrength <= 3)
				display.drawBitmap(2, 2, noSignalIcon, TFT_BLACK, 2);
			else if (signalStrength > 3 && signalStrength <= 10)
				display.drawBitmap(2, 2, signalLowIcon, TFT_BLACK, 2);
			else if (signalStrength > 10 && signalStrength <= 20)
				display.drawBitmap(2, 2, signalHighIcon, TFT_BLACK, 2);
			else if (signalStrength > 20 && signalStrength <= 31)
				display.drawBitmap(2, 2, signalFullIcon, TFT_BLACK, 2);
			else if (signalStrength == 99)
				display.drawBitmap(2, 2, signalErrorIcon, TFT_BLACK, 2);
		}
		else if (!simInserted && !airplaneMode && sim_module_version != 255)
			display.drawBitmap(2, 2, noSimIcon, TFT_BLACK, 2);
		else if (airplaneMode)
		{
			display.drawBitmap(2, 2, airplaneModeIcon, TFT_BLACK, 2);
		}
		if (ringVolume == 0)
		{
			display.drawBitmap(helper * 2, 2, silentModeIcon, TFT_BLACK, 2);
			helper += 10;
		}
		//display.drawBitmap(31, 1, missedcall);
		//display.drawBitmap(41, 1, newtext);
		// if (!airplaneMode)
		// {
		// 	if (wifi == 1)
		// 		display.drawBitmap(helper * 2, 2, wifiOnIcon, TFT_BLACK, 2);
		// 	else
		// 		display.drawBitmap(helper * 2, 2, wifiOffIcon, TFT_BLACK, 2);
		// 	helper += 10;
		// 	if (bt)
		// 		display.drawBitmap(helper * 2, 2, BTonIcon, TFT_BLACK, 2);
		// 	else
		// 		display.drawBitmap(helper * 2, 2, BToffIcon, TFT_BLACK, 2);
		// 	helper += 10;
		// }

		if (!SDinsertedFlag)
		{
			display.drawBitmap(helper * 2, 2, noSDIcon, TFT_BLACK, 2);
			helper += 10;
		}
		display.setTextFont(2);
		display.setTextSize(1);
		display.setCursor(helper * 2, 4);
		if (carrierName != "" && (networkRegistered == 1 || networkRegistered == 5))
		{
			// display.setFreeFont(TT1);
			// display.setTextSize(2);
			// display.setCursor(helper*2, 15);

			display.print(carrierName);
		}
		else if (simInserted && !airplaneMode && (carrierName == "" || !(networkRegistered == 1 || networkRegistered == 5)))
			display.print("loading...");
		else if (carrierName == "" && !simInserted && sim_module_version == 255)
			display.print("No module");

		if (!digitalRead(CHRG_INT))
			display.drawBitmap(148, 2, batteryChargingIcon, TFT_BLACK, 2);
		else
		{
			display.setTextFont(2);
			display.setTextSize(1);
			display.setTextColor(TFT_BLACK);
			display.setCursor(120, 2);
			//y = -316139 + 250.3763*x - 0.06612874*x^2 + 0.000005825959*x^3
			//y - percentage(%), x - voltage(V)
			/*
			double percentage = -316139 + (250.3763 * batteryVoltage) - (0.06612874 * batteryVoltage * batteryVoltage) + (0.000005825959 * batteryVoltage * batteryVoltage * batteryVoltage);
			if (percentage < 101)
			{
				display.printf("%d", (int)percentage);
				display.print("%");
			}
			if (percentage < 0)
				percentage = 0;
			*/
			// if (batteryVoltage > 4100)
			// 	display.drawBitmap(148, 2, batteryChargingIcon, TFT_BLACK, 2);
			if (batteryVoltage >= 3850)
				display.drawBitmap(148, 2, batteryFullIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3850 && batteryVoltage >= 3750)
				display.drawBitmap(148, 2, batteryHighIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3750 && batteryVoltage >= 3650)
				display.drawBitmap(148, 2, batteryMidIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3650 && batteryVoltage >= 3600)
				display.drawBitmap(148, 2, batteryLowIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3600)
				display.drawBitmap(148, 2, batteryEmptyIcon, TFT_BLACK, 2);
		}
		uint8_t temp = sizeof(notificationTypeList);
		for (int i = 0; i < sizeof(notificationTypeList); i++)
		{
			if (notificationTypeList[i] == 0)
			{
				temp = i;
				break;
			}
		}
		if (temp > 0)
			drawNotificationWindow(64, temp - 1);
		if (temp > 1)
			drawNotificationWindow(88, temp - 2);
		display.setFreeFont(TT1);
		display.setTextSize(4);
		display.setCursor(2, 111);
		display.setTextFont(2);
		display.setTextSize(1);
		display.setTextColor(TFT_BLACK);
		display.print("Hold \"#\" to unlock");
		display.setTextSize(6);
		display.setTextColor(TFT_DARKGREY);
		display.setFreeFont(TT1);
		display.setCursor(58, 56);
		display.print(":");
		display.setTextColor(TFT_BLACK);
		display.setCursor(56, 54);
		display.print(":");
		while (!update())
			;
	}
	tft.writecommand(17);
	delay(50);
	display.pushSprite(0, 0);
	initWavLib();
	for (int i = 0; i < 4; i++)
	{
		if (currTracks[i] != nullptr)
		{
			addTrack(currTracks[i]);
			// currTracks[i]->play();
		}
	}

	ledcAttachPin(LCD_BL_PIN, LEDC_CHANNEL);
	ledcAnalogWrite(LEDC_CHANNEL, 255);
	for (uint8_t i = 255; i > actualBrightness; i--)
	{
		ledcAnalogWrite(LEDC_CHANNEL, i);
		delay(1);
	}
	delay(2);
	FastLED.clear();
	updateTimeRTC();
	while (!update())
		;

	if (!inLockScreen)
	{
		lockscreen();
		exitedLockscreen = 1;
	}
	for (int i = 0; i < 4; i++)
	{
		if (pausedTracks[i])
		{
			Serial.printf("%d was playing\n", i);
			tracks[i]->play();
		}
	}
}
void MAKERphone::tone2(int pin, int freq, int duration)
{
	ledcWriteTone(0, freq);
	delay(duration);
	ledcWriteTone(0, 0);
}
void MAKERphone::loader()
{
	display.fillScreen(TFT_BLACK);
	display.setCursor(0, display.height() / 2 - 12);
	display.setTextColor(TFT_WHITE);
	display.setTextFont(2);
	display.setTextSize(1);
	display.printCenter("LOADING...");
	while (!update())
		;
	const esp_partition_t *partition;
	partition = esp_ota_get_running_partition();
	const esp_partition_t *partition2;
	partition2 = esp_ota_get_next_update_partition(partition);
	esp_ota_set_boot_partition(partition2);
	ESP.restart();
}
void MAKERphone::updateTimeGSM()
{
	// if(sim_module_version == 0)
	// {
	// 	Serial1.println("AT+CNETSTOP");
	// 	Serial1.println("AT+CNMP=13");
	// 	Serial1.println("AT+CNETSTART");
	// }
	Serial1.write("AT+CCLK?\r");
	//Serial1.flush();
	//delay(500);
	String reply;
	uint16_t temp = 0;
	while (!Serial1.available())
		update();
	while (Serial1.available())
	{
		reply += (char)Serial1.read();

		//if (Serial1.read() == '"') {
		//  char c1 = Serial1.read();
		//  Serial.println(c1);
		//  char c2 = Serial1.read();
		//  Serial.println(c2);
		//  clockYear = 2000 + ((c1 - '0') * 10) + (c2 - '0');
		//}
		//Serial.println(clockYear);
	}
	Serial.print(F("REPLY:"));
	Serial.println(reply);

	uint16_t index = reply.indexOf(F("+CCLK: \""));
	Serial.println(F("INDEX:"));
	Serial.println(index);
	Serial.println(reply.charAt(index));

	char c1, c2; //buffer for saving date and time numerals in form of characters
	c1 = reply.charAt(index + 8);
	c2 = reply.charAt(index + 9);
	temp = 2000 + ((c1 - '0') * 10) + (c2 - '0');
	if (temp % 100 < 19 || temp % 100 >= 40)
		return;
	else
		clockYear = temp;
	Serial.println(F("CLOCK YEAR:"));
	Serial.println(clockYear);

	c1 = reply.charAt(index + 11);
	c2 = reply.charAt(index + 12);
	temp = ((c1 - '0') * 10) + (c2 - '0');
	if (temp < 1 || temp > 12)
		return;
	else
		clockMonth = temp;
	Serial.println(F("CLOCK MONTH:"));
	Serial.println(clockMonth);

	c1 = reply.charAt(index + 14);
	c2 = reply.charAt(index + 15);
	temp = ((c1 - '0') * 10) + (c2 - '0');
	if (temp < 1 || temp > 31)
		return;
	else
		clockDay = temp;
	Serial.println(F("CLOCK DAY:"));
	Serial.println(clockDay);

	c1 = reply.charAt(index + 17);
	c2 = reply.charAt(index + 18);
	temp = ((c1 - '0') * 10) + (c2 - '0');
	if (temp > 24)
		return;
	else
		clockHour = temp;
	Serial.println(F("CLOCK HOUR:"));
	Serial.println(clockHour);

	c1 = reply.charAt(index + 20);
	c2 = reply.charAt(index + 21);
	temp = ((c1 - '0') * 10) + (c2 - '0');
	if (temp > 59)
		return;
	else
		clockMinute = temp;
	Serial.println(F("CLOCK MINUTE:"));
	Serial.println(clockMinute);

	c1 = reply.charAt(index + 23);
	c2 = reply.charAt(index + 24);
	temp = ((c1 - '0') * 10) + (c2 - '0');
	if (temp > 59)
		return;
	else
		clockSecond = temp;
	Serial.println(F("CLOCK SECOND:"));
	Serial.println(clockSecond);

	DateTime now = DateTime(clockYear, clockMonth, clockDay,
							clockHour, clockMinute, clockSecond);
	RTC.adjust(now);
	Serial.println(F("\nRTC TIME UPDATE OVER GSM DONE!"));
	delay(5);
}

void MAKERphone::performUpdate(Stream &updateSource, size_t updateSize)
{
	if (Update.begin(updateSize))
	{
		size_t written = Update.writeStream(updateSource);
		if (written == updateSize)
		{
			Serial.println("Written : " + String(written) + " successfully");
		}
		else
		{
			Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
		}
		if (Update.end())
		{
			Serial.println("OTA done!");
			if (Update.isFinished())
			{
				Serial.println("Update successfully completed. Rebooting.");
				ESP.restart();
			}
			else
			{
				Serial.println("Update not finished? Something went wrong!");
			}
		}
		else
		{
			Serial.println("Error Occurred. Error #: " + String(Update.getError()));
		}
	}
	else
	{
		Serial.println("Not enough space to begin OTA");
	}
}
void MAKERphone::updateFromFS(String FilePath)
{
	Serial.println(FilePath);
	// SD.end();
	// SD.end();
	// while(!SDFAT.begin(5, SD_SCK_MHZ(8)))
	// Serial.println("SdFat error");
	File updateBin = SD.open(FilePath);
	if (updateBin)
	{
		if (updateBin.isDirectory())
		{
			Serial.println("Error, update.bin is not a file");
			updateBin.close();
			return;
		}

		size_t updateSize = updateBin.size();

		if (updateSize > 0)
		{
			Serial.println("Try to start update");
			performUpdate(updateBin, updateSize);
		}
		else
		{
			Serial.println("Error, file is empty");
		}

		updateBin.close();
	}
	else
	{
		Serial.println("Could not load update.bin from sd root");
	}
}
void MAKERphone::incomingCall(String _serialData)
{
	bool state = 0;
	String number = "";
	uint8_t tempNotification = notification;
	uint32_t callMillis = millis();
	uint16_t tmp_time = 0;
	String localBuffer = _serialData;
	String buffer = "";
	uint16_t foo = localBuffer.indexOf("\"+", localBuffer.indexOf(",1,4,0,0")) + 1;
	if (!foo)
		foo = localBuffer.indexOf("\"", localBuffer.indexOf(",1,4,0,0")) + 1;
	number = localBuffer.substring(foo, localBuffer.indexOf("\"", foo));
	if (number == "")
		number = "BLOCKED";
	else if (number[0] != '+')
		number = '+' + number;
	String contact = checkContact(number);
	int32_t centerCursor = 0;
	localBuffer = "";
	bool played = 0;
	char c;
	tft.setTextFont(2);
	tft.setTextColor(TFT_BLACK);
	tft.setTextSize(1);
	tft.fillRect(0, 0, 160, 128, TFT_WHITE);
	tft.setCursor(40, 9);
	Serial.println("ringing");
	tft.print("Incoming call");
	tft.drawBitmap(29 * 2, 24 * 2, call_icon, TFT_DARKGREY, 2);
	FastLED.clear(1);
	if (contact == "")
	{
		centerCursor = (160 - tft.textWidth(number, 2)) / 2;
		Serial.println(centerCursor);
		tft.setCursor(centerCursor, 28);
		tft.print(number);
	}
	else
	{
		display.setCursor(0, -10);
		Serial.print("before: ");
		Serial.println(display.getCursorX());
		// display.setTextWrap(0);

		// for(int i = 0; i < contact.length(); i++)
		// {
		// 	display.print(contact[i]);
		// 	Serial.print("after: ");
		// 	Serial.println(display.getCursorX());
		// }
		centerCursor = (160 - tft.textWidth(contact, 2)) / 2;
		Serial.println(centerCursor);
		tft.setCursor(centerCursor, 28);
		tft.print(contact);
	}

	tft.fillRect(90, 51 * 2, 70, 13 * 2, TFT_RED);
	tft.setCursor(105, 109);
	tft.print("Hang up");
	tft.fillRect(0, 51 * 2, 70, 13 * 2, TFT_GREEN);
	tft.setCursor(10, 109);
	tft.print("Answer");
	Serial.print("SD inserted:");
	Serial.println(SDinsertedFlag);
	Serial.print("Ringtone exists: ");
	Serial.print(ringtone_path);
	Serial.println(SD.exists(ringtone_path));
	MPTrack *tempTrack = nullptr;
	uint8_t callIdNumber = 0;
	uint16_t helper = _serialData.indexOf("+CLCC: ") + 7;
	if (helper > 6)
		callIdNumber = _serialData.substring(helper, helper + 1).toInt();
	else
	{
		helper = _serialData.indexOf(",1,4,0,0");
		callIdNumber = _serialData.substring(helper - 1, helper).toInt();
	}

	Serial.print("call id: ");
	Serial.println(callIdNumber);
	if (!inHomePopup && !tracksSaved)
	{
		for (int i = 0; i < 4; i++)
		{
			if (tracks[i] != nullptr)
			{
				Serial.printf("%d track is playing: %d\n", i, tracks[i]->isPlaying());
				if (tracks[i]->isPlaying())
				{
					currTracks[i] = tracks[i];
					currTracks[i]->seek(tracks[i]->getSamplePos());
					Serial.print("PLAYING:");
					Serial.println(i);
					pausedTracks[i] = 1;
					tracks[i]->pause();
					removeTrack(tracks[i]);
				}
				else
				{
					currTracks[i] = tracks[i];
					currTracks[i]->seek(tracks[i]->getSamplePos());
					removeTrack(tracks[i]);
				}
			}
		}
		tracksSaved = 1;
	}
	bool goOut = 0;
	uint32_t checkMillis = millis();
	while (1)
	{
		if (millis() - checkMillis >= 8000 && !Serial1.available())
		{
			checkMillis = millis();
			Serial1.println("AT+CLCC");
			buffer = waitForOK();
			Serial.println(buffer);
			if (buffer.indexOf("+CLCC") == -1)
				localBuffer = String(String(callIdNumber) + ",1,6,0,0");
		}
		if (Serial1.available())
		{
			c = (char)Serial1.read();
			buffer += c;
			// Serial.println(buffer);
		}
		if (buffer.indexOf(String("CLCC: " + String(callIdNumber))) != -1 &&
			buffer.indexOf("\r", buffer.indexOf(String("CLCC: " + String(callIdNumber)))) != -1)
		{
			localBuffer = buffer;
			buffer = "";
		}
		if (buttons.released(BTN_FUN_LEFT))
			break;
		if (buttons.released(BTN_FUN_RIGHT) || localBuffer.indexOf(String(String(callIdNumber) + ",1,6,0,0")) != -1 || (digitalRead(SIM_INT) && sim_module_version == 1))
		{
			Serial.print("Interrupt: ");
			Serial.println(digitalRead(SIM_INT));
			if (SDinsertedFlag && SD.exists(ringtone_path))
			{
				ringtone->stop();
				removeTrack(ringtone);
			}
			tft.fillRect(0, 0, 160, 128, TFT_WHITE);
			tft.setCursor(60, 9);
			tft.print("00:00");

			tft.drawBitmap(29 * 2, 24 * 2, call_icon, TFT_RED, 2);
			tft.setCursor(centerCursor, 28);
			if (contact == "")
				tft.print(number);
			else
				tft.print(contact);
			tft.fillRect(0, 51 * 2, 80 * 2, 13 * 2, TFT_RED);
			tft.setCursor(10, 110);
			tft.print("Call ended");
			Serial.println("ENDED");
			if (localBuffer.indexOf(String(String(callIdNumber) + ",1,6,0,0")) == -1 && ((!digitalRead(SIM_INT) && sim_module_version == 1) || sim_module_version != 1))
			{
				Serial1.println("ATH");
				if (sim_module_version == 1)
				{
					while (!digitalRead(SIM_INT))
					{
						while (Serial1.available())
						{
							if (digitalRead(SIM_INT))
								break;
							Serial1.read();
						}
						Serial1.println("ATH");
						delay(200);
					}
				}
				else
				{
					uint32_t curr_millis = millis();
					while (buffer.indexOf(",1,6,") == -1 && millis() - curr_millis < 2000)
					{
						Serial1.println("ATH");
						buffer = waitForOK();
						Serial.println(buffer);
						// buffer+=(char)Serial.read();
					}
				}
			}

			// update();
			updateTimeRTC();
			if (SDinsertedFlag)
				addCall(number, checkContact(number), RTC.now().unixtime(), 0, 0);
			String temp = checkContact(number);
			if (temp == "")
				addNotification(1, number, RTC.now());
			else
				addNotification(1, temp, RTC.now());
			// delay(1000);
			goOut = 1;
			break;
		}

		if (!SDinsertedFlag || (SDinsertedFlag && !SD.exists(ringtone_path)))
		{
			if (millis() - callMillis >= 1000)
			{
				state = 1;
				callMillis = millis();
			}
			if (state)
			{
				playNotificationSound(4);
				state = 0;
			}
			notification = tempNotification;
		}

		else if (!played)
		{
			if (!addTrack(ringtone))
			{
				tempTrack = tracks[3];
				removeTrack(tempTrack);
				addTrack(ringtone);
			}
			Serial.println("PLAYED");
			if (ringVolume == 0)
				ringtone->setVolume(0);
			else
				ringtone->setVolume(map(ringVolume, 0, 14, 100, 300));
			ringtone->setRepeat(1);
			ringtone->play();
			played = 1;
		}
		updateNotificationSound();
		buttons.update();
	}
	if (SDinsertedFlag && SD.exists(ringtone_path))
	{
		ringtone->stop();
		removeTrack(ringtone);
		if (tempTrack != nullptr)
			addTrack(tempTrack);
	}
	if (goOut)
	{
		if (!inHomePopup)
		{
			for (int i = 0; i < 4; i++)
			{
				if (currTracks[i] != nullptr)
				{
					addTrack(currTracks[i]);
					if (pausedTracks[i])
						currTracks[i]->play();
				}
				currTracks[i] = nullptr;
				pausedTracks[i] = 0;
			}
			tracksSaved = 0;
		}

		buttons.update();
		return;
	}
	Serial.print("interrupt: ");
	Serial.println(digitalRead(SIM_INT));
	Serial1.println("ATA");
	long long curr_millis = millis();
	if (sim_module_version == 1)
	{
		while (!digitalRead(SIM_INT) && millis() - curr_millis < 2000)
		{
			Serial1.println("ATA");
			// localBuffer = waitForOK();
			// Serial.println(localBuffer);
			delay(200);
		}
		delay(1000);
	}
	else
	{
		localBuffer = waitForOK();
		long long curr_millis = millis();
		while (localBuffer.indexOf(String(String(callIdNumber) + ",1,0,0,0")) == -1 && millis() - curr_millis < 2000)
		{
			Serial1.println("ATA");
			localBuffer = waitForOK();
			Serial.println(localBuffer);
			delay(200);
		}
	}
	// while (!digitalRead(SIM_INT))
	// {
	// 	if (Serial1.available())
	// 		Serial1.read();
	// 	Serial1.println("ATA");
	// 	delay(100);
	// }
	Serial.print("interrupt: ");
	Serial.println(digitalRead(SIM_INT));
	// delay(2000);
	tft.setTextColor(TFT_BLACK);
	bool firstPass = 1;
	uint32_t timeOffset = 0;
	uint8_t scale;
	tmp_time = 0;
	String temp;
	scale = 2;
	tft.setTextFont(2);
	tft.setTextSize(1);
	tft.fillRect(0, 0, 160, 128, TFT_WHITE);
	tft.drawBitmap(29 * scale, 24 * scale, call_icon, TFT_GREEN, scale);
	tft.setCursor(centerCursor, 28);
	if (contact == "")
		tft.print(number);
	else
		tft.print(contact);
	tft.fillRect(0, 51 * scale, 80 * scale, 13 * scale, TFT_RED);
	tft.setCursor(100, 109);
	tft.print("Hang up");
	tft.setCursor(5, 109);
	tft.print("Volume: ");
	tft.setCursor(59, 109);
	tft.print(callSpeakerVolume);
	digitalWrite(soundSwitchPin, 1);
	int8_t written = -1;
	uint16_t prevTime = 0;
	// localBuffer = "";
	while (Serial1.available())
		Serial1.read();
	if (sim_module_version == 0)
	{
		Serial1.println("AT+CECH=0x0000");
		Serial.println(waitForOK());
	}
	Serial1.println("AT+CLCC");
	buffer = waitForOK();
	// uint32_t statusCheckMillis = millis();
	while (1)
	{
		if (Serial1.available())
		{
			c = Serial1.read();
			buffer += c;
			// Serial.println("----------");
			// Serial.println(buffer);
		}
		// else
		// {
		// 	if(millis() - statusCheckMillis >= 2000)
		// 	{
		// 		statusCheckMillis = millis();
		// 		Serial1.println("AT+CLCC");
		// 	}
		// }
		if (buffer.indexOf(String("CLCC: " + String(callIdNumber))) != -1 &&
			buffer.indexOf("\r", buffer.indexOf(String("CLCC: " + String(callIdNumber)))) != -1)
		{
			localBuffer = buffer;
			buffer = "";
		}
		if (buffer.indexOf("OK", buffer.indexOf("AT+CLVL=")) != -1)
			buffer = "";
		if (buffer.indexOf("\r") != -1)
			buffer = "";

		// delay(1);
		if (localBuffer.indexOf(String("CLCC: " + String(callIdNumber))) != -1 || localBuffer.indexOf("AT+CLVL") != -1)
		{
			if ((localBuffer.indexOf(String(String(callIdNumber) + ",1,0,0,0")) != -1 || localBuffer.indexOf("AT+CLVL") != -1) && (written != 0 || prevTime != tmp_time))
			{
				if (firstPass == 1)
				{
					timeOffset = millis();
					firstPass = 0;
				}
				temp = "";
				if ((int((millis() - timeOffset) / 1000) / 60) > 9)
					temp += (int((millis() - timeOffset) / 1000) / 60);
				else
				{
					temp += "0";
					temp += (int((millis() - timeOffset) / 1000) / 60);
				}
				temp += ":";
				if (int((millis() - timeOffset) / 1000) % 60 > 9)
					temp += (int((millis() - timeOffset) / 1000) % 60);
				else
				{
					temp += "0";
					temp += (int((millis() - timeOffset) / 1000) % 60);
				}
				tft.setCursor(55, 9);
				tft.fillRect(50, 5, 40, 20, TFT_WHITE);
				tft.print(temp);
				prevTime = tmp_time;
				written = 0;
			}
			else if (localBuffer.indexOf(String(String(callIdNumber) + ",1,6,0,0")) != -1)
			{

				tft.fillRect(0, 0, 160, 128, TFT_WHITE);
				tft.setCursor(55, 9);
				if (timeOffset == 0)
				{
					tft.print("00:00");
					tmp_time = 0;
				}
				else
				{
					temp = "";
					if ((int((millis() - timeOffset) / 1000) / 60) > 9)
						temp += (int((millis() - timeOffset) / 1000) / 60);
					else
					{
						temp += "0";
						temp += (int((millis() - timeOffset) / 1000) / 60);
					}
					temp += ":";
					if (int((millis() - timeOffset) / 1000) % 60 > 9)
						temp += (int((millis() - timeOffset) / 1000) % 60);
					else
					{
						temp += "0";
						temp += (int((millis() - timeOffset) / 1000) % 60);
					}
					tft.print(temp);
				}
				tft.drawBitmap(29 * scale, 24 * scale, call_icon, TFT_RED, scale);
				tft.setCursor(centerCursor, 28);
				if (contact == "")
					tft.print(number);
				else
					tft.print(contact);
				tft.fillRect(0, 51 * scale, 80 * scale, 13 * scale, TFT_RED);
				tft.setCursor(10, 110);
				tft.print("Call ended");
				Serial.println("ENDED");
				// update();

				updateTimeRTC();
				if (SDinsertedFlag)
				{
					addCall(number, checkContact(number), RTC.now().unixtime(), tmp_time, 2);
					saveSettings();
				}

				delay(1000);
				break;
			}
		}

		else if (localBuffer.indexOf(String("CLCC: " + String(callIdNumber))) == -1)
		{
			if (localBuffer.indexOf("ERROR") != -1)
			{

				tft.setCursor(10, 9);
				tft.print("Error");
				tft.drawBitmap(29 * scale, 24 * scale, call_icon, TFT_RED, scale);
				tft.setCursor(centerCursor, 28);
				if (contact == "")
					tft.print(number);
				else
					tft.print(contact);
				tft.fillRect(0, 51 * scale, 80 * scale, 13 * scale, TFT_RED);
				tft.setCursor(2, 100);
				tft.print("Invalid number or");
				tft.setCursor(2, 112);
				tft.print("SIM card missing!");
				while (!buttons.released(BTN_B))
					buttons.update();
				break;
			}
		}
		if (buttons.pressed(BTN_FUN_RIGHT)) // hanging up
		{
			Serial.println("B PRESSED");
			if (sim_module_version == 1)
			{
				Serial1.println("ATH");
				while (!digitalRead(SIM_INT))
				{
					while (Serial1.available())
					{
						if (digitalRead(SIM_INT))
							break;
						Serial1.read();
					}
					Serial1.println("ATH");
					delay(200);
				}
			}
			else
			{
				long long curr_millis = millis();
				while (Serial1.readString().indexOf(",1,6,") == -1 && millis() - curr_millis < 2000)
				{
					Serial1.println("ATH");
				}
			}

			tft.fillRect(0, 0, 160, 128, TFT_WHITE);
			tft.setCursor(55, 9);
			if (timeOffset == 0)
			{
				tft.print("00:00");
				tmp_time = 0;
			}
			else
			{
				temp = "";
				if ((int((millis() - timeOffset) / 1000) / 60) > 9)
					temp += (int((millis() - timeOffset) / 1000) / 60);
				else
				{
					temp += "0";
					temp += (int((millis() - timeOffset) / 1000) / 60);
				}
				temp += ":";
				if (int((millis() - timeOffset) / 1000) % 60 > 9)
					temp += (int((millis() - timeOffset) / 1000) % 60);
				else
				{
					temp += "0";
					temp += (int((millis() - timeOffset) / 1000) % 60);
				}
				tft.print(temp);
			}
			tft.drawBitmap(29 * scale, 24 * scale, call_icon, TFT_RED, scale);
			tft.setCursor(centerCursor, 28);
			if (contact == "")
				tft.print(number);
			else
				tft.print(contact);
			tft.fillRect(0, 51 * scale, 80 * scale, 13 * scale, TFT_RED);
			tft.setCursor(10, 110);
			tft.print("Call ended");
			Serial.println("ENDED");
			// update();

			updateTimeRTC();
			if (SDinsertedFlag)
			{
				addCall(number, checkContact(number), RTC.now().unixtime(), tmp_time, 2);
				saveSettings();
			}

			delay(1000);
			break;
		}
		if (buttons.released(BTN_UP) && callSpeakerVolume < 5)
		{
			callSpeakerVolume++;
			if (sim_module_version == 1)
				Serial1.printf("AT+CLVL=%d\r", callSpeakerVolume * 20);
			else if (sim_module_version == 0)
				Serial1.printf("AT+CLVL=%d\r", callSpeakerVolume);
			written = -1;
			tft.fillRect(56, 111, 20, 15, TFT_RED);
			tft.setCursor(59, 109);
			tft.print(callSpeakerVolume);
		}
		if (buttons.released(BTN_DOWN) && callSpeakerVolume > 0)
		{
			callSpeakerVolume--;
			if (sim_module_version == 1)
				Serial1.printf("AT+CLVL=%d\r", callSpeakerVolume * 20);
			else if (sim_module_version == 0)
				Serial1.printf("AT+CLVL=%d\r", callSpeakerVolume);
			written = -1;
			tft.fillRect(56, 111, 20, 15, TFT_RED);
			tft.setCursor(59, 109);
			tft.print(callSpeakerVolume);
		}
		tmp_time = int((millis() - timeOffset) / 1000);
		for (int i = 0; i < 12; i++)
		{

			if (buttons.released(i))
			{
				if (i == 9)
					Serial1.println("AT+VTS=*");
				else if (i == 10)
					Serial1.println("AT+VTS=0");
				else if (i == 11)
					Serial1.println("AT+VTS=#");
				else
				{
					Serial1.print("AT+VTS=");
					Serial1.println(i + 1);
				}
				break;
			}
		}
		buttons.update();
	}
	digitalWrite(soundSwitchPin, 0);
	osc->setVolume(oscillatorVolumeList[ringVolume]);
	sleepTimer = millis();
	if (!inHomePopup)
	{
		for (int i = 0; i < 4; i++)
		{
			if (currTracks[i] != nullptr)
			{
				addTrack(currTracks[i]);
				if (pausedTracks[i])
					currTracks[i]->play();
			}
			currTracks[i] = nullptr;
			pausedTracks[i] = 0;
		}
		tracksSaved = 0;
	}
	buttons.update();
}
void MAKERphone::incomingMessage(String _serialData)
{
	Serial.println("incoming message");
	Serial.println(_serialData);
	FastLED.clear(1);

	int32_t helper = 0;
	// helper = _serialData.indexOf("\"", _serialData.indexOf("+CMT:"));
	// String number = _serialData.substring(helper + 1, _serialData.indexOf("\"", helper+1));
	// helper+=number.length() + 1;
	// helper = _serialData.indexOf("\"\r", helper);
	helper = _serialData.indexOf("\r", _serialData.indexOf("+CMT")) + 1;
	const char *pdu_text = _serialData.substring(helper + 1, _serialData.indexOf("\r", helper + 2)).c_str();
	pduDecode(pdu_text);
	size_t masterTextLength;
	if (_concatSMSCounter > 1)
		masterTextLength = _concatSMSCodingScheme ? _concatSMSCounter * 68 : _concatSMSCounter * 160;
	else
		masterTextLength = 160;
	char masterText[masterTextLength] = "";
	// memset(masterText, 0, sizeof(masterText));
	Serial.print("current SMS index: ");
	Serial.println(_currentConcatSMS);
	Serial.println("sms text: ");
	Serial.println(_smsText);
	if (_concatSMSCounter > 1)
	{
		if (_concatSMSCodingScheme)
			strcpy(&masterText[(_currentConcatSMS - 1) * 67], _smsText);
		else
			strcpy(&masterText[(_currentConcatSMS - 1) * 153], _smsText);
	}
	else
		strncat(masterText, _smsText, sizeof(masterText));
	// Serial.print("master text: ");
	// for(int i = 0; i < sizeof(masterText); i++)
	Serial.print("length of message: ");
	Serial.println(_concatSMSCounter);
	uint8_t lengthOfSMS = _concatSMSCounter;
	if (lengthOfSMS == 0)
		lengthOfSMS = 1;
	// 	Serial.println((uint8_t)masterText[i]);
	if (!inHomePopup && !tracksSaved)
	{
		for (int i = 0; i < 4; i++)
		{
			if (tracks[i] != nullptr)
			{
				Serial.printf("%d track is playing: %d\n", i, tracks[i]->isPlaying());
				if (tracks[i]->isPlaying())
				{
					currTracks[i] = tracks[i];
					currTracks[i]->seek(tracks[i]->getSamplePos());
					Serial.print("PLAYING:");
					Serial.println(i);
					pausedTracks[i] = 1;
					tracks[i]->pause();
					removeTrack(tracks[i]);
				}
				else
				{
					currTracks[i] = tracks[i];
					currTracks[i]->seek(tracks[i]->getSamplePos());
					removeTrack(tracks[i]);
				}
			}
		}
		tracksSaved = 1;
	}
	if (_concatSMSCounter > 1)
	{
		tft.setTextColor(TFT_WHITE);
		tft.fillRect(0, 0, 160, 128, TFT_BLACK);
		tft.setTextFont(2);
		tft.setTextSize(1);
		tft.setCursor(2, 45);
		tft.print("Receiving additional SMS");
		tft.setCursor(35, 64);
		tft.print("Please wait...");
		while (_concatSMSCounter > 1)
		{
			tft.fillRect(50, 80, 50, 30, TFT_BLACK);
			tft.setCursor(68, 83);
			tft.printf("%d/%d", lengthOfSMS - _concatSMSCounter + 1, lengthOfSMS);
			String temp = "";
			long long curr_millis = millis();

			while ((temp.indexOf("\r", temp.indexOf("\r", helper + 1) + 1) == -1 || temp.indexOf("\r", helper + 1) == -1 || helper == -1) && millis() - curr_millis < 60000)
			{
				if (Serial1.available())
				{
					curr_millis = millis();
					char c = Serial1.read();
					temp += c;
					helper = temp.indexOf("+CMT:");
					if (helper > 0)
						temp = "+CMT:";
				}
			}
			if (temp.indexOf("+CMT:") != -1)
			{
				helper = temp.indexOf("\r", temp.indexOf("+CMT")) + 1;
				const char *pdu_text = temp.substring(helper + 1, temp.indexOf("\r", helper + 2)).c_str();
				pduDecode(pdu_text);
				Serial.print("current SMS index: ");
				Serial.println(_currentConcatSMS);
				Serial.println("sms text: ");
				Serial.println(_smsText);
				// strncat(masterText, _smsText, _concatSMSCounter*160);
				if (_concatSMSCodingScheme)
					strcpy(&masterText[(_currentConcatSMS - 1) * 67], _smsText);
				else
					strcpy(&masterText[(_currentConcatSMS - 1) * 153], _smsText);
				// for(int i = 0; i < sizeof(masterText); i++)
				// 	Serial.println((uint8_t)masterText[i]);
				_concatSMSCounter--;
			}
			else
				break;
		}
	}
	if (_concatSMSCounter > 1)
	{
		_currentConcatSMS = 0;
		_concatSMSCounter = 0;
		return;
	}
	Serial.print("final text: ");
	Serial.println(masterText);
	_currentConcatSMS = 0;
	_concatSMSCounter = 0;
	jb.createArray();
	bool fullStack = 0;
	if (SDinsertedFlag)
	{
		File file = SD.open("/.core/messages.json", "r");
		if (file.size() < 2)
		{ // empty -> FILL
			Serial.println("Override");
			file.close();
			// JsonArray& jarr = jb.parseArray("[{\"number\":\"123123\", \"dateTime\":\"2018-12-12 12:12:12\", \"text\":\"asd asd asd asd\"}]");
			// JsonArray& jarr = jb.parseArray("[{\"number\":\"123123\", \"dateTime\":\"2018-12-12 12:12:12\", \"text\":\"asd asd asd asd\"}, {\"number\":\"09123\", \"dateTime\":\"2018-12-12 12:12:12\", \"text\":\"Some other text\"}, {\"number\":\"911\", \"dateTime\":\"2018-03-12 12:12:12\", \"text\":\"Help\"}]");
			JsonArray &jarr = jb.createArray();
			delay(10);
			File file1 = SD.open("/.core/messages.json", "w");
			jarr.prettyPrintTo(file1);
			file1.close();
			file = SD.open("/.core/messages.json", "r");
			while (!file)
				Serial.println("Messages ERROR");
			jb.clear();
		}
		jb.clear();
		JsonArray &jarr = jb.parseArray(file);
		updateTimeRTC();
		file.close();

		if (!jarr.success())
		{
			Serial.println("MESSAGE NOT SAVED");
			// Serial.println(readFile("/.core/messages.json"));
		}
		else
		{
			// jarr.prettyPrintTo(Serial);
			uint8_t messagesSize = jarr.size();
			for (int i = 0; i < jarr.size(); i++)
			{
				if (strlen(jarr[i]["text"].as<char *>()) > 160)
					messagesSize += strlen(jarr[i]["text"].as<char *>()) / 160;
			}
			Serial.println(messagesSize);
			if (messagesSize > SMS_LIMIT - lengthOfSMS)
				fullStack = 1;
			String temp = checkContact(_smsNumber);
			DateTime now = RTC.now();
			if (temp == "")
			{
				saveMessage(masterText, "", _smsNumber, 0, 1, now);
				addNotification(2, _smsNumber, now);
			}
			else
			{
				saveMessage(masterText, temp, _smsNumber, 0, 1, now);
				addNotification(2, temp, now);
			}
		}
	}
	// popup(String(number + ": " + text), 50);
	tft.setTextColor(TFT_BLACK);
	tft.fillRect(0, 0, 160, 128, TFT_WHITE);
	tft.fillRect(0, 108, 160, 20, 0xA534);
	tft.setTextFont(2);
	tft.setTextSize(1);
	tft.setCursor(40, 7);
	tft.print("NEW MESSAGE!");
	tft.drawBitmap(10, 5, incomingMessageIcon, TFT_BLUE, 2);
	tft.setCursor(10, 26);
	tft.print("From: ");
	String tempCname = checkContact(_smsNumber);
	if (tempCname != "")
		tft.print(tempCname);
	else
		tft.print(_smsNumber);
	tft.setCursor(5, 43);
	tft.setTextWrap(1);
	for (int i = 0; i < strlen(masterText); i++)
	{
		//if(tft.getCursorY() > 75) break;
		if (tft.getCursorX() < 5)
			tft.setCursor(5, tft.getCursorY());
		if (tft.getCursorX() > 146)
		{
			if (masterText[i] != ' ' && masterText[i - 1] != ' ')
				tft.print('-');
			tft.println();
			tft.setCursor(5, tft.getCursorY());
		}
		if (tft.getCursorX() == 5 && masterText[i] == ' ')
			continue;
		tft.print(masterText[i]);
		if ((tft.getCursorY() > 78 && tft.getCursorX() > 125) || (masterText[i + 1] == '\n' && tft.getCursorY() > 78))
		{
			tft.print("...");
			break;
		}
	}
	// tft.print(text);
	tft.setCursor(3, 110);
	tft.print("Press \"A\" to continue");
	sleepTimer = millis();
	while (!buttons.released(BTN_A))
	{
		updateNotificationSound();
		if (!digitalRead(SIM_INT) && !inCall)
		{
			String temp = "";
			uint32_t curr_millis = millis();
			helper = -1;
			while ((temp.indexOf("\r", temp.indexOf("\r", helper + 1) + 1) == -1 || temp.indexOf("\r", helper + 1) == -1 || helper == -1) && temp.indexOf("\r", temp.indexOf("1,4,0,0,")) == -1 && millis() - curr_millis < 500)
			{
				if (Serial1.available())
				{
					curr_millis = millis();
					char c = Serial1.read();
					temp += c;
					// Serial.println(temp);
					// Serial.println("-----------------");
					helper = temp.indexOf("+CMT:");
					if (helper > 0)
						temp = "+CMT:";
					// Serial.println(helper);
				}
			}
			if (temp.indexOf("\r", temp.indexOf("1,4,0,0,")) != -1)
			{
				inCall = 1;
				incomingCall(temp);
				inCall = 0;
				break;
			}
			else if (temp.indexOf("+CMT:") != -1)
			{
				playNotificationSound(notification);
				incomingMessage(temp);
				break;
			}
			else if (!inAlarmPopup && !alarmCleared && currentAlarm != 99)
			{
				inAlarmPopup = 1;
				alarmPopup();
				if (alarmRepeat[currentAlarm] == 0)
					alarmEnabled[currentAlarm] = 0;
				saveAlarms();
				if (ringtone->isPlaying())
					ringtone->stop();

				alarmCleared = 1;
				alarmMillis = millis();
				inAlarmPopup = 0;
				break;
			}
		}
		if (millis() - sleepTimer > sleepTimeActual * 1000 && sleepTime != 0)
			break;
		buttons.update();
	}
	if (fullStack)
	{
		/*
		tft.setTextColor(TFT_BLACK);
		tft.fillRect(0, 0, 160, 128, TFT_WHITE);
		tft.setTextFont(2);
		tft.setTextSize(1);
		tft.setCursor(10, 30);
		tft.print("Couldn't save message!");
		tft.setCursor(35, 50);
		tft.print("No more space");
		tft.setCursor(10, 70);
		tft.print("Deleting oldest SMS...");
		*/
		uint32_t tempMillis = millis();
		while (millis() < tempMillis + 2000)
		{
			buttons.update();
			if (buttons.pressed(BTN_A) || buttons.pressed(BTN_B))
			{
				while (!buttons.released(BTN_A) && !buttons.released(BTN_B))
				{
					if (!digitalRead(SIM_INT) && !inCall)
					{
						String temp = "";
						uint32_t curr_millis = millis();
						helper = -1;
						while ((temp.indexOf("\r", temp.indexOf("\r", helper + 1) + 1) == -1 || temp.indexOf("\r", helper + 1) == -1 || helper == -1) && temp.indexOf("\r", temp.indexOf("1,4,0,0,")) == -1 && millis() - curr_millis < 500)
						{
							if (Serial1.available())
							{
								curr_millis = millis();
								char c = Serial1.read();
								temp += c;
								// Serial.println(temp);
								// Serial.println("-----------------");
								helper = temp.indexOf("+CMT:");
								if (helper > 0)
									temp = "+CMT:";
								// Serial.println(helper);
							}
						}
						if (temp.indexOf("\r", temp.indexOf("1,4,0,0,")) != -1)
						{
							inCall = 1;
							incomingCall(temp);
							inCall = 0;
							break;
						}
						else if (temp.indexOf("+CMT:") != -1)
						{
							playNotificationSound(notification);
							incomingMessage(temp);
							break;
						}
						else if (!inAlarmPopup && !alarmCleared && currentAlarm != 99)
						{
							inAlarmPopup = 1;
							alarmPopup();
							if (alarmRepeat[currentAlarm] == 0)
								alarmEnabled[currentAlarm] = 0;
							saveAlarms();
							if (ringtone->isPlaying())
								ringtone->stop();

							alarmCleared = 1;
							alarmMillis = millis();
							inAlarmPopup = 0;
							break;
						}
					}
					buttons.update();
				}
				break;
			}
		}
	}
	newMessage = 1;
	osc->setVolume(oscillatorVolumeList[mediaVolume]);
	if (!inHomePopup)
	{
		for (int i = 0; i < 4; i++)
		{
			if (currTracks[i] != nullptr)
			{
				addTrack(currTracks[i]);
				if (pausedTracks[i])
					currTracks[i]->play();
			}
			currTracks[i] = nullptr;
			pausedTracks[i] = 0;
		}
		tracksSaved = 0;
	}
}
void MAKERphone::addCall(String number, String contact, uint32_t dateTime, int duration, uint8_t direction)
{
	File file = SD.open("/.core/call_log.json", "r");
	Serial.print("Direction of call: ");
	Serial.println(direction);
	if (file.size() < 2)
	{
		file.close();
		jb.clear();
		JsonArray &jarr = jb.createArray();
		delay(10);
		File file1 = SD.open("/.core/call_log.json", "w");
		jarr.prettyPrintTo(file1);
		file1.close();
		file = SD.open("/.core/call_log.json", "r");
		while (!file)
		{
			file = SD.open("/.core/call_log.json", "r");
		}
	}

	jb.clear();
	JsonArray &jarr = jb.parseArray(file);
	file.close();
	if (jarr.size() > 39)
	{
		Serial.println("call log limit reached");
		jarr.remove(0);
		File file = SD.open("/.core/call_log.json", "w");
		jarr.prettyPrintTo(file);
		file.close();
	}

	JsonObject &new_item = jb.createObject();
	new_item["number"] = number.c_str();
	new_item["contact"] = contact.c_str();
	new_item["dateTime"] = dateTime;
	new_item["duration"] = duration;
	new_item["direction"] = direction; //0 - missed, 1 - outgoing, 2 - incoming
	jarr.add(new_item);

	File file1 = SD.open("/.core/call_log.json", "w");
	jarr.prettyPrintTo(file1);
	file1.close();
}
void MAKERphone::saveMessage(char *text, String contact, String number, bool isRead, bool direction, DateTime time)
{
	File file = SD.open("/.core/messages.json", "r");
	while (!file)
		Serial.println("Messages ERROR");
	jb.clear();
	JsonArray &messages = jb.parseArray(file);
	file.close();
	uint8_t messagesSize = messages.size();
	for (int i = 0; i < messages.size(); i++)
	{
		if (strlen(messages[i]["text"].as<char *>()) > 160)
			messagesSize += strlen(messages[i]["text"].as<char *>()) / 160;
	}
	uint8_t thisMessageSize = 1;
	if (strlen(text) > 160)
		thisMessageSize += strlen(text) / 160;
	if (messagesSize > SMS_LIMIT - thisMessageSize)
	{
		Serial.println("SMS limit reached");
		while (messagesSize > SMS_LIMIT - thisMessageSize)
		{
			messages.remove(0);
			messagesSize = messages.size();
			for (int i = 0; i < messages.size(); i++)
			{
				if (strlen(messages[i]["text"].as<char *>()) > 160)
					messagesSize += strlen(messages[i]["text"].as<char *>()) / 160;
			}
			Serial.println(messagesSize);
		}
		File file = SD.open("/.core/messages.json", "w");
		messages.prettyPrintTo(file);
		file.close();
	}
	// messages.prettyPrintTo(Serial);
	delay(1);
	JsonObject &new_item = jb.createObject();
	updateTimeRTC();
	new_item["number"] = number;
	new_item["contact"] = contact;
	new_item["text"] = text;
	new_item["dateTime"] = time.unixtime();
	new_item["read"] = isRead;
	new_item["direction"] = direction; //0 - outgoing, 1 - incoming

	messages.add(new_item);

	File file1 = SD.open("/.core/messages.json", "w");
	messages.prettyPrintTo(file1);
	// messages.prettyPrintTo(Serial);
	file1.close();
}
void MAKERphone::checkSim()
{
	Serial.println("checking sim");
	String input = "";
	Serial1.println(F("AT+CPIN?"));
	input = waitForOK();
	Serial.println(input);
	uint32_t timeoutMillis = millis();
	while (input.indexOf("READY", input.indexOf("+CPIN:")) == -1 && input.indexOf("SIM PIN", input.indexOf("+CPIN:")) == -1 && input.indexOf("SIM PUK", input.indexOf("+CPIN:")) == -1)
	{
		if (millis() - timeoutMillis >= 2500)
		{
			simInserted = 0;
			return;
		}
		// input = Serial1.readString();
		Serial1.println(F("AT+CPIN?"));
		Serial.println("printed at+cpin?");
		input = waitForOK();
		Serial.println(input);
		// if(input.indexOf("busy") != -1)
		// 	break;
	}
	if (input.indexOf("busy") != -1 && simBusyCounter < 5)
	{
		simBusyCounter++;
		delay(100);
		checkSim();
		return;
	}
	if (input.indexOf("NOT READY", input.indexOf("+CPIN:")) != -1 || (input.indexOf("ERROR") != -1 && input.indexOf("+CPIN:") == -1) || input.indexOf("NOT INSERTED") != -1 || input.indexOf("not inserted", input.indexOf("+CPIN")) != -1 || input.indexOf("failure") != -1)
		simInserted = 0;
	else
	{
		simInserted = 1;
		if (input.indexOf("SIM PIN") != -1)
			enterPin();
		if (input.indexOf("SIM PUK") != -1)
			enterPUK();
	}
	simBusyCounter = 0;
}
void MAKERphone::enterPin()
{
	shutdownPopupEnable(1);
	char key = NO_KEY;
	String pinBuffer = "";
	String reply = "";
	uint32_t color = TFT_BLACK;
	if (backgroundColors[backgroundIndex] == TFT_BLACK)
		color = TFT_WHITE;
	while (reply.indexOf("+SPIC:") == -1)
	{
		Serial1.println("AT+SPIC");
		reply = Serial1.readString();
	}
	uint8_t foo = reply.indexOf(" ", reply.indexOf("+SPIC:"));
	timesRemaining = reply.substring(foo, reply.indexOf(",", foo)).toInt();

	while (1)
	{
		display.setTextColor(color);
		display.setTextSize(1);
		display.setTextFont(2);
		display.fillScreen(backgroundColors[backgroundIndex]);
		display.setCursor(5, 7);
		display.printCenter("Enter PIN:");
		display.drawRect(14, 45, 134, 30, color);
		display.drawRect(13, 44, 136, 32, color);
		display.setCursor(47, 52);
		display.printCenter(pinBuffer);

		display.setCursor(1, 112);
		display.print("Erase");
		display.setCursor(110, 112);
		display.print("Confirm");
		display.setCursor(5, 85);
		String temp = "Remaining attempts: ";
		temp += timesRemaining;
		display.printCenter(temp);

		key = buttons.getKey(); //BUTTONSREFRESH
		if (buttons.released(BTN_FUN_LEFT) && pinBuffer != "")
			pinBuffer.remove(pinBuffer.length() - 1);
		else if (key != NO_KEY && isDigit(key) && pinBuffer.length() != 4)
			pinBuffer += key;

		if ((buttons.released(BTN_A) || buttons.released(BTN_FUN_RIGHT)) && pinBuffer.length() == 4) //enter PIN
		{
			reply = "";
			Serial1.print(F("AT+CPIN=\""));
			Serial1.print(pinBuffer);
			Serial1.println("\"");
			while (!Serial1.available())
				;
			while (reply.indexOf("+CPIN: READY") == -1 && reply.indexOf("ERROR") == -1)
				reply = Serial1.readString();
			display.fillScreen(backgroundColors[backgroundIndex]);
			display.setCursor(28, 52);
			display.setTextFont(2);
			if (reply.indexOf("+CPIN: READY") != -1)
			{
				display.printCenter("PIN OK :)");
				while (!update())
					;
				delay(1000);
				break;
			}
			else if (reply.indexOf("ERROR") != -1)
			{
				timesRemaining--;
				pinBuffer = "";
				display.printCenter("Wrong PIN :(");
				while (!update())
					;
				delay(2000);
				if (timesRemaining == 0)
				{
					enterPUK();
					break;
				}
			}
		}
		sleepTimer = millis();
		update();
	}
	shutdownPopupEnable(0);
}
void MAKERphone::enterPUK()
{
	shutdownPopupEnable(1);
	char key = NO_KEY;
	String pukBuffer = "";
	String reply = "";
	uint32_t color = TFT_BLACK;
	if (backgroundColors[backgroundIndex] == TFT_BLACK)
		color = TFT_WHITE;
	while (reply.indexOf("+SPIC:") == -1)
	{
		Serial1.println("AT+SPIC");
		reply = Serial1.readString();
	}
	uint8_t foo = 0;
	if (sim_module_version == 1)
	{
		foo = reply.indexOf(",", reply.indexOf("+SPIC:")) + 3;
		timesRemaining = reply.substring(foo, reply.indexOf(",", foo + 1)).toInt();
	}
	else if (sim_module_version == 0)
	{
		foo = reply.indexOf(",", reply.indexOf("+SPIC:")) + 1;
		timesRemaining = reply.substring(foo, reply.indexOf(",", foo)).toInt();
	}
	Serial.println(reply);
	Serial.println(reply.substring(foo));
	delay(5);
	while (1)
	{
		display.setTextColor(color);
		display.setTextSize(1);
		display.setTextFont(2);
		display.fillScreen(backgroundColors[backgroundIndex]);
		display.setCursor(5, 7);
		display.printCenter("Enter PUK:");
		display.drawRect(14, 45, 134, 30, color);
		display.drawRect(13, 44, 136, 32, color);
		display.setCursor(47, 52);
		display.printCenter(pukBuffer);

		display.setCursor(1, 112);
		display.print("Erase");
		display.setCursor(110, 112);
		display.print("Confirm");
		display.setCursor(5, 85);
		String temp = "Remaining attempts: ";
		temp += timesRemaining;
		display.printCenter(temp);

		key = buttons.getKey(); //BUTTONSREFRESH
		if (buttons.released(BTN_FUN_LEFT) && pukBuffer != "")
			pukBuffer.remove(pukBuffer.length() - 1);
		else if (key != NO_KEY && isDigit(key) && pukBuffer.length() != 8)
			pukBuffer += key;

		if ((buttons.released(BTN_A) || buttons.released(BTN_FUN_RIGHT)) && pukBuffer.length() == 8) //enter PUK
		{
			reply = "";
			Serial1.print(F("AT+CPIN=\""));
			Serial1.print(pukBuffer);
			Serial1.println("\", \"0000\"");
			uint32_t timeoutMillis = millis();
			bool goOut = 0;
			while (!goOut)
			{
				while (reply.indexOf("+CPIN: READY") == -1 && reply.indexOf("ERROR") == -1 && reply.indexOf("AT+CPIN=") == -1 && reply.indexOf("OK", reply.indexOf("AT+CPIN")) == -1 && millis() - timeoutMillis < 3000)
				{
					reply = Serial1.readString();
					Serial.println(reply);
				}
				if (reply.indexOf("+CPIN: READY") != -1 || reply.indexOf("ERROR") != -1 || (reply.indexOf("AT+CPIN=") != -1 && reply.indexOf("OK", reply.indexOf("AT+CPIN")) != -1))
					goOut = 1;
				else
					timeoutMillis = millis();
			}
			display.fillScreen(backgroundColors[backgroundIndex]);
			display.setCursor(28, 52);
			display.setTextFont(2);
			if (reply.indexOf("+CPIN: READY") != -1 || (reply.indexOf("AT+CPIN=") != -1 && reply.indexOf("OK", reply.indexOf("AT+CPIN")) != -1))
			{
				display.printCenter("PUK OK :)");
				while (!update())
					;
				delay(1000);
				String pinBuffer = "";
				while (1)
				{
					display.setTextColor(color);
					display.setTextSize(1);
					display.setTextFont(2);
					display.fillScreen(backgroundColors[backgroundIndex]);
					display.setCursor(5, 7);
					display.printCenter("Enter new PIN:");
					display.drawRect(14, 45, 134, 30, color);
					display.drawRect(13, 44, 136, 32, color);
					display.setCursor(47, 52);
					display.printCenter(pinBuffer);
					display.setCursor(1, 112);
					display.print("Erase");
					display.setCursor(110, 112);
					display.print("Confirm");

					key = buttons.getKey(); //BUTTONSREFRESH
					if (buttons.released(BTN_FUN_LEFT) && pinBuffer != "")
						pinBuffer.remove(pinBuffer.length() - 1);
					else if (key != NO_KEY && isDigit(key) && pinBuffer.length() != 4)
						pinBuffer += key;

					if ((buttons.released(BTN_A) || buttons.released(BTN_FUN_RIGHT)) && pinBuffer.length() == 4) //enter PIN
					{
						reply = "";
						Serial1.print(F("AT+CPWD= \"SC\",\"0000\", \""));
						Serial1.print(pinBuffer);
						Serial1.println("\"");
						while (!Serial1.available())
							;
						while (reply.indexOf("OK", reply.indexOf("AT+CPWD")) == -1)
							reply = Serial1.readString();
						break;
					}
					update();
				}

				break;
			}
			else if (reply.indexOf("ERROR") != -1)
			{
				timesRemaining--;
				pukBuffer = "";
				display.printCenter("Wrong PUK :(");
				while (!update())
					;
				delay(2000);
			}
		}
		sleepTimer = millis();
		update();
	}
	shutdownPopupEnable(0);
}
String MAKERphone::textInput(String buffer, int16_t length)
{
	int ret = 0;
	byte key = buttons.getKey(); // Get a key press from the keypad

	if (buttons.released(BTN_FUN_LEFT) && buffer != "")
	{
		if (textPointer == buffer.length())
		{
			textPointer--;
			btnHeld = false;
		}
		buffer.remove(buffer.length() - 1);
	}
	else if (key == '*')
	{
		if (textPointer != buffer.length())
			textPointer++;
		textPointer++;
		buffer += ' ';
	}

	if (!textLimitFlag && !(buffer.length() == length) && !btnHeld)
	{
		if (buttons.held(10, 25))
		{
			btnHeld = true;
			btnHeldField[9] = true;
			buffer += '+';
			textPointer++;
		}
		else
			for (int i = 0; i < 9; i++)
			{
				if (buttons.held(i, 25))
				{
					buffer += i + 1;
					textPointer++;
					btnHeld = true;
					btnHeldField[i] = true;
					break;
				}
			}
		if (buffer.length() == length)
			textLimitFlag = 1;
	}

	if (textLimitFlag && buffer.length() == length)
		return buffer;
	else
		textLimitFlag = 0;

	if ((length == -1 || length >= buffer.length()) && !btnHeld)
	{
		ret = multi_tap(key); // Feed the key press to the multi_tap function.
		if ((ret & 256) != 0) // If this is non-zero, we got a key. Handle some special keys or just print the key on screen
		{
			if (buffer.length() == length)
			{
				textLimitFlag = 1;
				return buffer;
			}
			else
				textPointer++;
		}
		else if (ret) // We don't have a key but the user is still cycling through characters on one key so we need to update the screen
		{
			if (textPointer == buffer.length())
				buffer += char(lowByte(ret));
			else
				buffer[buffer.length() - 1] = char(lowByte(ret));
		}
	}

	if (btnHeld)
	{
		if (btnHeldField[9] && buttons.released(10))
		{
			btnHeldField[9] = false;
			btnHeld = false;
		}
		else
			for (int i = 0; i < 9; i++)
			{
				if (btnHeldField[i] && buttons.released(i))
				{
					btnHeldField[i] = false;
					btnHeld = false;
					break;
				}
			}
	}

	if (buffer.length() > length)
	{
		buffer = buffer.substring(0, length);
		textPointer--;
	}
	return buffer;
}
int MAKERphone::multi_tap(byte key)
{
	static boolean upperCase = true;
	static byte prevKeyPress = NO_KEY, cyclicPtr = 0;
	static const char multi_tap_mapping[10][map_width] = {
		{'0', '#', '$', '.', '?', '"', '&'},
		{'1', '+', '-', '*', '/', '\'', ':'},
		{'A', 'B', 'C', '2', '!', ';', '<'},
		{'D', 'E', 'F', '3', '%', '[', '='},
		{'G', 'H', 'I', '4', '(', '\\', '>'},
		{'J', 'K', 'L', '5', ')', ']', '^'},
		{'M', 'N', 'O', '6', '@', '_', '`'},
		{'P', 'Q', 'R', 'S', '7', '{', '|'},
		{'T', 'U', 'V', '8', ',', '}', '~'},
		{'W', 'X', 'Y', 'Z', '9', ' ', 0}};
	if (key == RESET_MTP) // Received reset command. Flush everything and get ready for restart.
	{
		upperCase = true;
		prevKeyPress = NO_KEY;
		cyclicPtr = 0;
		return 0;
	}

	if (key != NO_KEY && key != 'B' && key != 'C') // A key is pressed at this iteration.
	{
		// buttons.update();
		if (key == 'D' || key == 'A')
		{
			prevKeyPress = NO_KEY;
			cyclicPtr = 0;
			return 0;
		}
		if (key == '*')
		{
			prevKeyPress = NO_KEY;
			prevKeyMillis = 0;
			cyclicPtr = 0;
			return 0;
		}
		if ((key > '9') || (key < '0')) // Function keys
		{
			if ((key == 1) || (key == '#')) // Up for case change
			{
				upperCase = !upperCase;
				return 0;
			}
			else // Other function keys. These keys produce characters so they need to terminate the last keypress.
			{
				if (prevKeyPress != NO_KEY)
				{
					char temp1 = multi_tap_mapping[prevKeyPress - '0'][cyclicPtr];
					if ((!upperCase) && (temp1 >= 'A') && (temp1 <= 'Z'))
						temp1 += 'a' - 'A';
					cyclicPtr = 0;
					prevKeyMillis = 0;
					switch (key)
					{
					case 2:
						// Call symbol list
						return 0; // Clear the buffer.
						break;
					case 3:
						prevKeyPress = '\b';
						break;
					case 5:
						prevKeyPress = '\n';
						break;
					case 6:
						prevKeyPress = NO_KEY; // Clear the buffer.
						break;
					}
					return (256 + (unsigned int)(temp1));
				}
				else
				{
					prevKeyPress = NO_KEY;
					cyclicPtr = 0;
					prevKeyMillis = 0;
					switch (key)
					{
					case 2:
						// Call symbol list
						return 0; // Clear the buffer.
						break;
					case 3:
						return (256 + (unsigned int)('\b'));
						break;
					case 4:
						return (256 + (unsigned int)(' '));
						break;
					case 5:
						return (256 + (unsigned int)('\n'));
						break;
					case 6:
						return 0; // Clear the buffer.
						break;
					}
				}
			}
		}
		if (prevKeyPress != NO_KEY)
		{
			if (prevKeyPress == key)
			{
				char temp1;
				cyclicPtr++;
				if ((multi_tap_mapping[key - '0'][cyclicPtr] == 0) || (cyclicPtr == map_width))
					cyclicPtr = 0; //Cycle key
				prevKeyMillis = millis();
				temp1 = multi_tap_mapping[key - '0'][cyclicPtr];
				if ((!upperCase) && (temp1 >= 'A') && (temp1 <= 'Z'))
					temp1 += 'a' - 'A';
				return ((unsigned int)(temp1));
			}
			else
			{
				char temp1 = multi_tap_mapping[prevKeyPress - '0'][cyclicPtr];
				if ((!upperCase) && (temp1 >= 'A') && (temp1 <= 'Z'))
					temp1 += 'a' - 'A';
				prevKeyPress = key;
				cyclicPtr = 0;
				prevKeyMillis = millis();
				//Print key on cursor+1
				return (256 + (unsigned int)(temp1));
			}
		}
		else
		{
			char temp1 = multi_tap_mapping[key - '0'][cyclicPtr];
			if ((!upperCase) && (temp1 >= 'A') && (temp1 <= 'Z'))
				temp1 += 'a' - 'A';
			prevKeyPress = key;
			prevKeyMillis = millis();
			cyclicPtr = 0;
			return ((unsigned int)(temp1));
		}
	}
	else // No key is pressed at this iteration.
	{
		if (prevKeyPress == NO_KEY)
			return 0;											 // No key was previously pressed.
		else if (millis() - prevKeyMillis < multi_tap_threshold) // Key was pressed previously but within threshold
		{
			char temp1 = multi_tap_mapping[prevKeyPress - '0'][cyclicPtr];
			if ((!upperCase) && (temp1 >= 'A') && (temp1 <= 'Z'))
				temp1 += 'a' - 'A';
			return ((unsigned int)(temp1));
		}
		else // Key was pressed previously and threshold has passed
		{
			char temp1 = multi_tap_mapping[prevKeyPress - '0'][cyclicPtr];
			if ((!upperCase) && (temp1 >= 'A') && (temp1 <= 'Z'))
				temp1 += 'a' - 'A';
			prevKeyPress = NO_KEY;
			cyclicPtr = 0;
			return (256 + (unsigned int)(temp1));
		}
	}
	return 0;
}
void MAKERphone::lockscreen()
{
	dataRefreshFlag = 1;
	bool blinkState = 0;
	uint32_t elapsedMillis = millis();
	// FastLED.clear();
	inLockScreen = 1;
	uint32_t measure = _timesMeasured;
	uint8_t measureCounter = 0;
	uint32_t meanMeasure = 0;
	uint32_t measureSum = 0;
	if (!isCalibrated)
	{
		display.fillScreen(TFT_BLACK);
		display.setTextColor(TFT_WHITE);
		display.setTextSize(1);
		display.setTextFont(2);
		display.setCursor(30, 50);
		display.printCenter("SD checking");
		while (!mp.update())
			;
		if (digitalRead(CHRG_INT) && buttons.timeHeld(BTN_1) > 0 && buttons.timeHeld(BTN_3) > 0 && buttons.timeHeld(BTN_5) > 0 && buttons.timeHeld(BTN_7) > 0 && buttons.timeHeld(BTN_9) > 0 && buttons.timeHeld(BTN_0) > 0) //safety checks before calibrating
		{

			//SD startup
			uint32_t tempMillis = millis();
			SDinsertedFlag = 1;
			while (!SD.begin(5, SPI, 8000000))
			{
				Serial.println("SD ERROR");
				if (millis() - tempMillis > 10)
				{
					SDinsertedFlag = 0;
					break;
				}
			}
			if (!SDinsertedFlag)
			{
				_SDinterruptError = 1;
				display.fillScreen(TFT_BLACK);
				display.setTextColor(TFT_WHITE);
				display.setTextSize(1);
				display.setTextFont(2);
				display.setCursor(30, 50);
				display.printCenter("SD missing!");
				while (!mp.update())
					;
				for (int i = 0; i < NUMPIXELS; i++)
					leds[i] = CRGB(50, 0, 0);
				FastLED.show();
				delay(2000);
				while (1)
					;
			}
			if (digitalRead(SD_INT) && SDinsertedFlag)
			{
				_SDinterruptError = 1;
				display.fillScreen(TFT_BLACK);
				display.setTextColor(TFT_WHITE);
				display.setTextSize(1);
				display.setTextFont(2);
				display.setCursor(30, 50);
				display.printCenter("SD socket");
				display.setCursor(30, 69);
				display.printCenter("interrupt error");
				while (!mp.update())
					;
				for (int i = 0; i < NUMPIXELS; i++)
					leds[i] = CRGB(50, 0, 0);
				FastLED.show();
				delay(2000);
				while (1)
					;
			}

			listDir(SD, "/", 3);
			if (_SDcounter != 29)
			{
				display.fillScreen(TFT_BLACK);
				display.setTextColor(TFT_WHITE);
				display.setTextSize(1);
				display.setTextFont(2);
				display.setCursor(30, 50);
				display.printCenter("SD content error");
				while (!mp.update())
					;
				for (int i = 0; i < NUMPIXELS; i++)
					leds[i] = CRGB(50, 0, 0);
				FastLED.show();
				delay(2000);
				while (1)
					;
			}

			display.fillScreen(TFT_BLACK);
			display.setTextColor(TFT_WHITE);
			display.setTextSize(1);
			display.setTextFont(2);
			display.setCursor(30, 50);
			display.printCenter("WiFi checking");
			while (!mp.update())
				;

			// Set WiFi to station mode and disconnect from an AP if it was previously connected
			WiFi.mode(WIFI_STA);
			WiFi.disconnect();
			delay(1000);
			// WiFi.scanNetworks will return the number of networks found
			int n = WiFi.scanNetworks();
			if (n == 0)
			{
				display.fillScreen(TFT_BLACK);
				display.setTextColor(TFT_WHITE);
				display.setTextSize(1);
				display.setTextFont(2);
				display.setCursor(30, 50);
				display.printCenter("WiFi error");
				while (!mp.update())
					;
				for (int i = 0; i < NUMPIXELS; i++)
					leds[i] = CRGB(50, 0, 0);
				FastLED.show();
				delay(2000);
				while (1)
					;
			}
			display.fillScreen(TFT_BLACK);
			display.setTextColor(TFT_WHITE);
			display.setTextSize(1);
			display.setTextFont(2);
			display.setCursor(30, 50);
			display.printCenter("RTC checking");
			while (!mp.update())
				;
			DateTime now = DateTime(2017, 4, 2, 23, 59, 0);
			RTC.adjust(now);
			delay(1000);
			char buf[100];
			strncpy(buf, "DD.MM.YYYY hh:mm:ss\0", 100);
			Serial.println(RTC.now().format(buf));
			if (RTC.now() != now)
				Serial.println("RTC OK!");
			else
			{
				Serial.println("RTC error");
				display.fillScreen(TFT_BLACK);
				display.setTextColor(TFT_WHITE);
				display.setTextSize(1);
				display.setTextFont(2);
				display.setCursor(30, 50);
				display.printCenter("RTC error");
				while (!mp.update())
					;
				for (int i = 0; i < NUMPIXELS; i++)
					leds[i] = CRGB(50, 0, 0);
				FastLED.show();
				delay(2000);
				while (1)
					;
			}
		}
		display.fillScreen(TFT_BLACK);
		display.setTextColor(TFT_WHITE);
		display.setTextSize(1);
		display.setTextFont(2);
		display.setCursor(30, 50);
		display.printCenter("Calibration start");
		while (!mp.update())
			;
		delay(500);
	}
	while (1)
	{
		if (!isCalibrated && _timesMeasured > measure)
		{
			measure = _timesMeasured;
			measureSum += ADCrawRead();
			measureCounter++;
			sleepTimer = millis();
			if (measureCounter == 5)
			{
				meanMeasure = measureSum / 5;
				int32_t temp = 1889 - meanMeasure;
				if (digitalRead(CHRG_INT) && (abs(temp) < 250) && buttons.timeHeld(BTN_1) > 0 && buttons.timeHeld(BTN_3) > 0 && buttons.timeHeld(BTN_5) > 0 && buttons.timeHeld(BTN_7) > 0 && buttons.timeHeld(BTN_9) > 0 && buttons.timeHeld(BTN_0) > 0) //safety checks before calibrating
				{
					uint16_t adc1low = 0;
					uint16_t adc1high = 0;
					adc1low = meanMeasure & 0b01111111;
					adc1high = meanMeasure >> 7;
					// while (!SD.begin(5, SPI, 8000000))
					// 	Serial.println("SD error");
					// Serial.println("SD OK");
					// SD.remove("/kalibracija.cal");
					// File file = SD.open("/kalibracija.cal", "w");
					// file.print("ADC high reg: ");
					// file.println(adc1high, BIN);
					// file.print("ADC low reg: ");
					// file.println(adc1low, BIN);
					// file.print("value: ");
					// file.println(meanMeasure);
					// file.close();
					REG_SET_FIELD(EFUSE_BLK0_WDATA3_REG, EFUSE_BLK3_PART_RESERVE, 1);
					REG_SET_FIELD(EFUSE_BLK3_WDATA3_REG, EFUSE_ADC1_TP_LOW, adc1low);
					REG_SET_FIELD(EFUSE_BLK3_WDATA3_REG, EFUSE_ADC1_TP_HIGH, adc1high);
					esp_efuse_burn_new_values();
					esp_efuse_reset();

					display.fillScreen(TFT_BLACK);
					display.setTextColor(TFT_WHITE);
					display.setTextSize(1);
					display.setTextFont(2);
					display.setCursor(30, 50);
					display.printCenter("Calibration OK");
					while (!mp.update())
						;
					for (int i = 0; i < NUMPIXELS; i++)
						leds[i] = CRGB(0, 50, 0);
					FastLED.show();
					SD.remove("/.core/settings.json");

					delay(2000);

					while (1)
						;
				}
				else
				{

					display.fillScreen(TFT_BLACK);
					display.setTextColor(TFT_WHITE);
					display.setTextSize(1);
					display.setTextFont(2);
					display.setCursor(30, 50);
					display.printCenter("Calibration error");
					display.setCursor(30, 69);
					display.printCenter(meanMeasure);
					while (!mp.update())
						;
					for (int i = 0; i < NUMPIXELS; i++)
						leds[i] = CRGB(50, 0, 0);
					FastLED.show();
					delay(2000);
					measure = _timesMeasured;
					measureCounter = 0;
					meanMeasure = 0;
					measureSum = 0;
					while (1)
						;
				}
			}
		}
		// drawJpeg("/Images/box1.jpg", 0, 0);
		// display.drawBmp("/Images/screenshot_02.bmp", 0,0);
		display.fillScreen(backgroundColors[backgroundIndex]);

		display.setFreeFont(TT1);
		display.setTextSize(6);
		//Hour shadow
		display.setTextColor(TFT_DARKGREY);
		if (clockHour == 11)
			display.setCursor(10 * 2, 28 * 2);
		else if (clockHour % 10 == 1 || (int)clockHour / 10 == 1)
			display.setCursor(7 * 2, 28 * 2);
		else
			display.setCursor(4 * 2, 28 * 2);
		if (clockHour < 10)
			display.print("0");
		display.print(clockHour);

		//minute shadow
		display.setCursor(36 * 2, 28 * 2);
		if (clockMinute < 10)
			display.print("0");
		display.print(clockMinute);

		//Hour black
		display.setTextColor(TFT_BLACK);
		if (clockHour == 11)
			display.setCursor(9 * 2, 27 * 2);
		else if (clockHour % 10 == 1 || (int)clockHour / 10 == 1)
			display.setCursor(6 * 2, 27 * 2);
		else
			display.setCursor(3 * 2, 27 * 2);
		if (clockHour < 10)
			display.print("0");
		display.print(clockHour);

		//Minute black
		display.setCursor(35 * 2, 27 * 2);
		if (clockMinute < 10)
			display.print("0");
		display.print(clockMinute);

		display.setTextSize(1 * 2);
		display.setCursor(60 * 2, 19 * 2);
		display.setTextWrap(false);
		if (clockDay < 10)
			display.print("0");
		display.print(clockDay);
		display.print("/");
		if (clockMonth < 10)
			display.print("0");
		display.print(clockMonth);
		display.setCursor(62 * 2, 25 * 2);
		display.print(clockYear);
		/*display.setTextSize(2);
		display.setCursor(10, 50);
		display.print("12:00");*/
		uint8_t helper = 11;
		if (sim_module_version == 255)
			helper = 1;
		if (simInserted && !airplaneMode && sim_module_version != 255)
		{
			if (networkRegistered == 1 || networkRegistered == 5)
			{

				if (signalStrength <= 3)
					display.drawBitmap(2, 2, noSignalIcon, TFT_BLACK, 2);
				else if (signalStrength > 3 && signalStrength <= 10)
					display.drawBitmap(2, 2, signalLowIcon, TFT_BLACK, 2);
				else if (signalStrength > 10 && signalStrength <= 20)
					display.drawBitmap(2, 2, signalHighIcon, TFT_BLACK, 2);
				else if (signalStrength > 20 && signalStrength <= 31)
					display.drawBitmap(2, 2, signalFullIcon, TFT_BLACK, 2);
				else if (signalStrength == 99)
					display.drawBitmap(2, 2, signalErrorIcon, TFT_BLACK, 2);
			}
			else
				display.drawBitmap(2, 2, signalErrorIcon, TFT_BLACK, 2);
		}
		else if (!simInserted && !airplaneMode && sim_module_version != 255)
			display.drawBitmap(2, 2, noSimIcon, TFT_BLACK, 2);
		else if (airplaneMode)
		{
			display.drawBitmap(2, 2, airplaneModeIcon, TFT_BLACK, 2);
		}
		if (ringVolume == 0)
		{
			display.drawBitmap(helper * 2, 2, silentModeIcon, TFT_BLACK, 2);
			helper += 10;
		}
		//display.drawBitmap(31, 1, missedcall);
		//display.drawBitmap(41, 1, newtext);
		// if (!airplaneMode)
		// {
		// 	if (wifi == 1)
		// 		display.drawBitmap(helper * 2, 2, wifiOnIcon, TFT_BLACK, 2);
		// 	else
		// 		display.drawBitmap(helper * 2, 2, wifiOffIcon, TFT_BLACK, 2);
		// 	helper += 10;
		// 	if (bt)
		// 		display.drawBitmap(helper * 2, 2, BTonIcon, TFT_BLACK, 2);
		// 	else
		// 		display.drawBitmap(helper * 2, 2, BToffIcon, TFT_BLACK, 2);
		// 	helper += 10;
		// }

		if (!SDinsertedFlag)
		{
			display.drawBitmap(helper * 2, 2, noSDIcon, TFT_BLACK, 2);
			helper += 10;
		}
		display.setTextFont(2);
		display.setTextSize(1);
		display.setCursor(helper * 2, 4);
		if (carrierName != "" && (networkRegistered == 1 || networkRegistered == 5))
		{
			// display.setFreeFont(TT1);
			// display.setTextSize(2);
			// display.setCursor(helper*2, 15);

			display.print(carrierName);
		}
		else if (simInserted && !airplaneMode && carrierNameCounter < 5 && (carrierName == "" || (networkRegistered != 1 && networkRegistered != 5)))
			display.print("loading...");
		else if (carrierName == "" && !simInserted && sim_module_version == 255)
			display.print("No module");
		if (!digitalRead(CHRG_INT)) {
			display.drawBitmap(148, 2, batteryChargingIcon, TFT_BLACK, 2);
		} else {
			if (batteryVoltage >= 3850)
				display.drawBitmap(148, 2, batteryFullIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3850 && batteryVoltage >= 3750)
				display.drawBitmap(148, 2, batteryHighIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3750 && batteryVoltage >= 3650)
				display.drawBitmap(148, 2, batteryMidIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3650 && batteryVoltage >= 3625)
				display.drawBitmap(148, 2, batteryLowIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3625)
				display.drawBitmap(148, 2, batteryEmptyIcon, TFT_BLACK, 2);
		}
		uint8_t temp = sizeof(notificationTypeList);
		for (int i = 0; i < sizeof(notificationTypeList); i++) {
			if (notificationTypeList[i] == 0) {
				temp = i;
				break;
			}
		}

		if (temp > 0) {
			drawNotificationWindow(64, temp - 1);
		}

		if (temp > 1) {
			drawNotificationWindow(88, temp - 2);
		}

		display.setFreeFont(TT1);
		display.setTextSize(4);
		if (millis() - elapsedMillis >= 500) {
			elapsedMillis = millis();
			blinkState = !blinkState;
			if (clockYear % 100 != 4 && clockYear % 100 != 80)
				updateTimeRTC();
		}

		display.setCursor(10, 80);
		display.setFreeFont(JP1);
		display.setTextSize(1);
		display.setTextColor(TFT_BLACK);
		char t[] = { 'A' + 0x16, 'A' + 0x46, 'A' + 0x05, 'A' + 0x17, 'A' + 0x52, 0 };
		display.print(t);

		if (blinkState == 1)
		{
			display.setCursor(2, 111);
			display.setTextFont(2);
			display.setTextSize(1);
			display.setTextColor(TFT_BLACK);

			display.print("Hold \"#\" to unlock");
			display.setTextSize(6);
			display.setTextColor(TFT_DARKGREY);
			display.setFreeFont(TT1);
			display.setCursor(58, 56);
			display.print(":");
			display.setTextColor(TFT_BLACK);
			display.setCursor(56, 54);
			display.print(":");
		}
		if (buttons.timeHeld(BTN_HASHTAG) < 5 && buttons.timeHeld(BTN_HASHTAG) > 0)
		{
			display.setTextFont(2);
			display.setTextSize(1);
			display.fillRect(0, 112, display.width(), 16, backgroundColors[backgroundIndex]);
			display.setCursor(2, 111);
			display.print("Unlocking");
		}
		if (buttons.timeHeld(BTN_HASHTAG) >= 5 && buttons.timeHeld(BTN_HASHTAG) < 12)
		{
			display.fillRect(0, 112, display.width(), 16, backgroundColors[backgroundIndex]);
			display.setCursor(2, 111);
			display.setTextFont(2);
			display.setTextSize(1);
			display.print("Unlocking *");

			leds[0] = CRGB::Red;
			leds[7] = CRGB::Red;
			// FastLED.show();
		}
		else if (buttons.timeHeld(BTN_HASHTAG) >= 12 && buttons.timeHeld(BTN_HASHTAG) < 18)
		{
			display.fillRect(0, 112, display.width(), 16, backgroundColors[backgroundIndex]);
			display.setCursor(2, 111);
			display.setTextFont(2);
			display.setTextSize(1);
			display.print("Unlocking * *");
			leds[0] = CRGB::Red;
			leds[7] = CRGB::Red;
			leds[1] = CRGB::Red;
			leds[6] = CRGB::Red;
		}
		else if (buttons.timeHeld(BTN_HASHTAG) >= 18 && buttons.timeHeld(BTN_HASHTAG) < 24)
		{
			display.fillRect(0, 112, display.width(), 16, backgroundColors[backgroundIndex]);
			display.setCursor(2, 111);
			display.setTextFont(2);
			display.setTextSize(1);
			display.print("Unlocking * * *");
			leds[0] = CRGB::Red;
			leds[7] = CRGB::Red;
			leds[1] = CRGB::Red;
			leds[6] = CRGB::Red;
			leds[2] = CRGB::Red;
			leds[5] = CRGB::Red;
		}
		else if (buttons.timeHeld(BTN_HASHTAG) >= 24 && buttons.timeHeld(BTN_HASHTAG) < 30)
		{
			display.fillRect(0, 112, display.width(), 16, backgroundColors[backgroundIndex]);
			display.setCursor(2, 111);
			display.setTextFont(2);
			display.setTextSize(1);
			display.print("Unlocking * * * *");
			leds[0] = CRGB::Red;
			leds[7] = CRGB::Red;
			leds[1] = CRGB::Red;
			leds[6] = CRGB::Red;
			leds[2] = CRGB::Red;
			leds[5] = CRGB::Red;
			leds[3] = CRGB::Red;
			leds[4] = CRGB::Red;
		}
		else if (buttons.timeHeld(BTN_HASHTAG) >= 30)
		{
			// while(!update());
			dataRefreshFlag = 0;
			buttons.holdForUnlock = 1;
			inLockScreen = 0;
			display.setTextSize(1);
			display.setTextFont(2);
			return;
		}

		update();
	}
}
void MAKERphone::networkModuleInit()
{
	Serial.println("module inserted");
	if (simInserted)
		Serial.println("SIM INSERTED");
	else
		Serial.println("SIM NOT INSERTED");

	// updateTimeGSM();
	Serial1.println(F("AT+CMEE=2"));
	waitForOK();
	if (sim_module_version == 1)
	{
		Serial1.println(F("AT+CRSL=100"));
		waitForOK();
		Serial1.println(F("AT+CLVL=100"));
		waitForOK();
		Serial1.println(F("AT+CLTS=1")); //Enable local Timestamp mode (used for syncrhonising RTC with GSM time
		waitForOK();
		Serial1.println(F("AT+IPR=9600"));
		waitForOK();
	}
	else if (sim_module_version == 0)
	{
		Serial1.println("AT+COUTGAIN=8");
		waitForOK();
		Serial1.println("AT+CVHU=0");
		waitForOK();
		Serial1.println("AT+CTZU=1");
		waitForOK();
		Serial1.println("AT+CNMP=2");
		waitForOK();
		Serial1.println("AT+CGATT=0");
		waitForOK();
		// Serial1.println("AT+CPMVT=1,0,5000");
		// waitForOK();
	}
	Serial1.println(F("AT+CMGF=0"));
	waitForOK();
	Serial1.println(F("AT+CNMI=1,2,0,0,0"));
	waitForOK();
	// waitForOK();
	// Serial1.println(F("AT+CPMS=\"SM\",\"SM\",\"SM\""));
	// waitForOK();
	Serial1.println(F("AT+CLCC=1"));
	waitForOK();
	Serial1.println(F("AT+CSCLK=1"));
	waitForOK();
	Serial1.println(F("AT&W"));
	waitForOK();
	Serial1.println(F("AT+CREG=0"));
	waitForOK();
	Serial.println("Serial1 up and running...");
}
//JPEG drawing
void MAKERphone::drawJpeg(String filename, int xpos, int ypos)
{

	Serial.println(F("==========================="));
	Serial.print(F("Drawing file: "));
	Serial.println(filename);
	Serial.println(F("==========================="));
	// Open the named file (the Jpeg decoder library will close it after rendering image)
	File jpegFile = SD.open(filename); // File handle reference for SPIFFS
	//  File jpegFile = SD.open( filename, FILE_READ);  // or, file handle reference for SD library
	if (!jpegFile)
	{
		Serial.print(F("ERROR: File \""));
		Serial.print(filename);
		Serial.println("\" not found!");
		return;
	}

	// Use one of the three following methods to initialise the decoder:
	//boolean decoded = JpegDec.decodeFsFile(jpegFile); // Pass a SPIFFS file handle to the decoder,
	//boolean decoded = JpegDec.decodeSdFile(jpegFile); // or pass the SD file handle to the decoder,
	boolean decoded = JpegDec.decodeSdFile(jpegFile); // or pass the filename (leading / distinguishes SPIFFS files)
	// Note: the filename can be a String or character array type
	if (decoded)
	{
		// print information about the image to the serial port
		jpegInfo();

		// render the image onto the screen at given coordinates
		jpegRender(xpos, ypos);
	}
	else
	{
		Serial.println(F("Jpeg file format not supported!"));
	}
	jpegFile.close();
}
void MAKERphone::jpegRender(int xpos, int ypos)
{
	Serial.println("JPEG render");
	delay(5);

	// retrieve infomration about the image
	uint16_t *pImg;
	uint16_t mcu_w = JpegDec.MCUWidth;
	uint16_t mcu_h = JpegDec.MCUHeight;
	uint32_t max_x = JpegDec.width;
	uint32_t max_y = JpegDec.height;

	// Jpeg images are drawn as a set of image block (tiles) called Minimum Coding Units (MCUs)
	// Typically these MCUs are 16x16 pixel blocks
	// Determine the width and height of the right and bottom edge image blocks
	Serial.println("Here");
	delay(5);
	uint32_t min_w = min(mcu_w, max_x % mcu_w);

	uint32_t min_h = min(mcu_h, max_y % mcu_h);

	// save the current image block size
	uint32_t win_w = mcu_w;
	uint32_t win_h = mcu_h;

	// record the current time so we can measure how long it takes to draw an image
	uint32_t drawTime = millis();

	// save the coordinate of the right and bottom edges to assist image cropping
	// to the screen size
	max_x += xpos;
	max_y += ypos;

	// read each MCU block until there are no more
	while (JpegDec.readSwappedBytes())
	{ // Swap byte order so the SPI buffer can be used

		// save a pointer to the image block
		pImg = JpegDec.pImage;

		// calculate where the image block should be drawn on the screen
		int mcu_x = JpegDec.MCUx * mcu_w + xpos; // Calculate coordinates of top left corner of current MCU
		int mcu_y = JpegDec.MCUy * mcu_h + ypos;

		// check if the image block size needs to be changed for the right edge
		if (mcu_x + mcu_w <= max_x)
			win_w = mcu_w;
		else
			win_w = min_w;

		// check if the image block size needs to be changed for the bottom edge
		if (mcu_y + mcu_h <= max_y)
			win_h = mcu_h;
		else
			win_h = min_h;

		// copy pixels into a contiguous block
		if (win_w != mcu_w)
		{
			uint16_t *cImg;
			int p = 0;
			cImg = pImg + win_w;
			for (int h = 1; h < win_h; h++)
			{
				p += mcu_w;
				for (int w = 0; w < win_w; w++)
				{
					*cImg = *(pImg + w + p);
					cImg++;
				}
			}
		}

		// draw image MCU block only if it will fit on the screen
		if ((mcu_x + win_w) <= display.width() && (mcu_y + win_h) <= display.height())
		{
			display.setSwapBytes(1);
			display.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
		}

		else if ((mcu_y + win_h) >= display.height())
			JpegDec.abort();
	}

	// calculate how long it took to draw the image
	drawTime = millis() - drawTime; // Calculate the time it took

	// print the results to the serial port
	Serial.print("Total render time was    : ");
	Serial.print(drawTime);
	Serial.println(" ms");
	Serial.println("=====================================");
}
void MAKERphone::jpegInfo()
{

	Serial.println(F("==============="));
	Serial.println(F("JPEG image info"));
	Serial.println(F("==============="));
	Serial.print(F("Width      :"));
	Serial.println(JpegDec.width);
	Serial.print(F("Height     :"));
	Serial.println(JpegDec.height);
	Serial.print(F("Components :"));
	Serial.println(JpegDec.comps);
	Serial.print(F("MCU / row  :"));
	Serial.println(JpegDec.MCUSPerRow);
	Serial.print(F("MCU / col  :"));
	Serial.println(JpegDec.MCUSPerCol);
	Serial.print(F("Scan type  :"));
	Serial.println(JpegDec.scanType);
	Serial.print(F("MCU width  :"));
	Serial.println(JpegDec.MCUWidth);
	Serial.print(F("MCU height :"));
	Serial.println(JpegDec.MCUHeight);
	Serial.println(F("==============="));
	Serial.println();
}

//settings operations
void MAKERphone::saveSettings(bool debug)
{
	const char *path = "/.core/settings.json";
	Serial.println("");
	if (!SD.exists(path))
	{
		if (sim_module_version == 0)
			micGain = 1;
		else
			micGain = 14;
	}
	SD.remove(path);
	jb.clear();
	JsonObject &settings = jb.createObject();
	// Serial.print("MIC GAIN: ");
	// Serial.println(micGain);
	if (settings.success())
	{
		if (debug)
		{
			// Serial.print("wifi: ");
			// Serial.println(settings["wifi"].as<bool>());
			// Serial.print("bluetooth: ");
			// Serial.println(settings["bluetooth"].as<bool>());
			Serial.print("airplane_mode: ");
			Serial.println(settings["airplane_mode"].as<bool>());
			Serial.print("brightness: ");
			Serial.println(settings["brightness"].as<int>());
			Serial.print("sleep_time: ");
			Serial.println(settings["sleep_time"].as<int>());
			Serial.print("background_color: ");
			Serial.println(settings["background_color"].as<int>());
		}

		// settings["wifi"] = wifi;
		// settings["bluetooth"] = bt;
		settings["airplane_mode"] = airplaneMode;
		settings["brightness"] = brightness;
		settings["sleep_time"] = sleepTime;
		settings["background_color"] = backgroundIndex;
		settings["notification"] = notification;
		settings["ringtone"] = ringtone_path.c_str();
		settings["ringVolume"] = ringVolume;
		settings["mediaVolume"] = mediaVolume;
		settings["micGain"] = micGain;
		settings["callVolume"] = callSpeakerVolume;
		File file1 = SD.open(path, "w");
		settings.prettyPrintTo(file1);
		file1.close();
	}
	else
	{
		Serial.println("Error saving new settings");
	}
}
void MAKERphone::loadSettings(bool debug)
{
	//create default system folders if not present
	if (!SD.exists("/Music"))
		SD.mkdir("/Music");
	if (!SD.exists("/Images"))
		SD.mkdir("/Images");
	if (!SD.exists("/Video"))
		SD.mkdir("/Video");
	if (!SD.exists("/Ringtones"))
		SD.mkdir("/Ringtones");
	if (!SD.exists("/.core"))
		SD.mkdir("/.core");
	const char *path = "/.core/settings.json";
	Serial.println("");
	if (SD.exists(path))
	{
		File file = SD.open(path);
		jb.clear();
		JsonObject &settings = jb.parseObject(file);
		file.close();

		if (settings.success())
		{
			if (debug)
			{
				// Serial.print("wifi: ");
				// Serial.println(settings["wifi"].as<bool>());
				// Serial.print("bluetooth: ");
				// Serial.println(settings["bluetooth"].as<bool>());
				Serial.print("airplane_mode: ");
				Serial.println(settings["airplane_mode"].as<bool>());
				Serial.print("brightness: ");
				Serial.println(settings["brightness"].as<int>());
				Serial.print("sleep_time: ");
				Serial.println(settings["sleep_time"].as<int>());
				Serial.print("background_color: ");
				Serial.println(settings["background_color"].as<int>());
				Serial.print("notification: ");
				Serial.println(settings["notification"].as<int>());
				Serial.print("ringtone: ");
				Serial.println(settings["ringtone"].as<char *>());
			}
			// wifi = settings["wifi"];
			// bt = settings["bluetooth"];
			airplaneMode = settings["airplane_mode"];
			brightness = settings["brightness"];
			sleepTime = settings["sleep_time"];
			backgroundIndex = settings["background_color"];
			notification = settings["notification"];
			ringtone_path = String(settings["ringtone"].as<char *>());
			ringVolume = settings["ringVolume"];
			mediaVolume = settings["mediaVolume"];
			micGain = settings["micGain"];
			callSpeakerVolume = settings["callVolume"];
		}
	}
	else
	{
		if (sim_module_version == 1)
			micGain = 14;
		else
			micGain = 1;

		Serial.println("Error loading new settings");
		saveSettings();
	}
}
void MAKERphone::applySettings()
{
	// if(wifi)
	// {
	// delay(200);
	// WiFi.begin();
	// WiFi.mode(WIFI_STA);
	// WiFi.disconnect();
	// delay(200);
	// }
	// else
	// {
	WiFi.disconnect(true);
	delay(10); // disable WIFI altogether
	WiFi.mode(WIFI_MODE_NULL);
	delay(10);
	// }

	// if(bt)
	// 	btStart();
	// else
	btStop();

	if (brightness == 0)
		actualBrightness = 230;
	else
		actualBrightness = (5 - brightness) * 51;
	ledcAnalogWrite(LEDC_CHANNEL, actualBrightness);
	switch (sleepTime)
	{ //interpreting value into actual numbers
	case 0:
		sleepTimeActual = 0;
		break;
	case 1:
		sleepTimeActual = 10;
		break;
	case 2:
		sleepTimeActual = 30;
		break;
	case 3:
		sleepTimeActual = 60;
		break;
	case 4:
		sleepTimeActual = 600;
		break;
	case 5:
		sleepTimeActual = 1800;
		break;
	}
	osc->setVolume(oscillatorVolumeList[mediaVolume]);
	if (SDinsertedFlag)
	{
		if (ringtone == nullptr)
			ringtone = new MPTrack((char *)ringtone_path.c_str());
		else
			ringtone->reloadFile(((char *)ringtone_path.c_str()));
		if (ringVolume == 0)
			ringtone->setVolume(0);
		else
			ringtone->setVolume(map(ringVolume, 0, 14, 100, 300));
	}
	if (sim_module_version != 255)
	{
		if (sim_module_version == 1)
		{
			Serial1.print(F("AT+CMIC=0,"));
			Serial1.println(micGain);
			waitForOK();
			Serial1.printf("AT+CLVL=%d\r", mp.callSpeakerVolume * 20);
			waitForOK();
		}
		else if (sim_module_version == 0)
		{
			if (micGain > 8)
				micGain = 8;
			Serial1.print(F("AT+CMICGAIN="));
			Serial1.println(micGain);
			waitForOK();
			Serial1.printf("AT+CLVL=%d\r", mp.callSpeakerVolume);
			waitForOK();
		}
		while (Serial1.available())
			Serial1.read();
		Serial1.println("AT+CFUN?");
		Serial.println("CFUN checking");
		String readOutput = waitForOK();
		Serial.println(readOutput);
		uint32_t timeoutMillis = millis();
		while (readOutput.indexOf("+CFUN:") == -1 && millis() - timeoutMillis < 2000)
		{
			Serial1.println("AT+CFUN?");
			Serial.println("printed AT+CFUN?");
			readOutput = waitForOK();

			Serial.println(readOutput);
			delay(100);
		}
		if (airplaneMode && readOutput.indexOf("+CFUN: 4") == -1)
		{
			Serial1.println("AT+CFUN=4");
			waitForOK();
		}
		else if (!airplaneMode && readOutput.indexOf("+CFUN: 1") == -1)
		{
			Serial1.println("AT+CFUN=1");
			waitForOK();
		}
	}
	for (int i = 0; i < 4; i++)
	{
		if (tracks[i] != nullptr)
		{
			if (mediaVolume == 0)
				tracks[i]->setVolume(0);
			else
				tracks[i]->setVolume(map(mediaVolume, 0, 14, 100, 300));
		}
		if (oscs[i] != nullptr)
			oscs[i]->setVolume(oscillatorVolumeList[mediaVolume]);
	}
}

//Collision
bool MAKERphone::collideRectRect(int16_t x1, int16_t y1, int16_t w1, int16_t h1, int16_t x2, int16_t y2, int16_t w2, int16_t h2)
{
	return (x2 < x1 + w1 && x2 + w2 > x1 && y2 < y1 + h1 && y2 + h2 > y1);
}
bool MAKERphone::collidePointRect(int16_t pointX, int16_t pointY, uint16_t rectX, uint16_t rectY, uint16_t rectW, uint16_t rectH)
{
	return (pointX >= rectX && pointX < rectX + rectW && pointY >= rectY && pointY < rectY + rectH);
}
bool MAKERphone::collideCircleCircle(int16_t centerX1, int16_t centerY1, int16_t r1, int16_t centerX2, int16_t centerY2, int16_t r2)
{
	r1 = abs(r1);
	r2 = abs(r2);
	return (((centerX1 - centerX2) * (centerX1 - centerX2) + (centerY1 - centerY2) * (centerY1 - centerY2)) < (r1 + r2) * (r1 + r2));
}
bool MAKERphone::collidePointCircle(int16_t pointX, int16_t pointY, int16_t centerX, int16_t centerY, int16_t r)
{
	return (((pointX - centerX) * (pointX - centerX) + (pointY - centerY) * (pointY - centerY)) < r * r);
}

//SD operations
void MAKERphone::writeFile(const char *path, const char *message)
{
	while (!SD.begin(5, SPI, 8000000))
		;
	Serial.printf("Writing file: %s\n", path);

	File file = SD.open(path);
	if (!file)
	{
		Serial.println("Failed to open file for writing");
		return;
	}
	if (file.print(message))
	{
		Serial.println("File written");
	}
	else
	{
		Serial.println("Write failed");
	}
	file.close();
}
void MAKERphone::appendFile(const char *path, const char *message)
{
	Serial.printf("Appending to file: %s\n", path);

	File file = SD.open(path);
	if (!file)
	{
		Serial.println("Failed to open file for appending");
		return;
	}
	if (file.print(message))
	{
		Serial.println("Message appended");
		delay(5);
	}
	else
	{
		Serial.println("Append failed");
		delay(5);
	}
	file.close();
}
String MAKERphone::readFile(const char *path)
{
	while (!SD.begin(5, SPI, 8000000))
		;
	Serial.printf("Reading file: %s\n", path);
	String helper = "";
	File file = SD.open(path);
	if (!file)
	{
		Serial.println("Failed to open file for reading");
		return "";
	}

	Serial.print("Read from file: ");
	while (file.available())
	{
		helper += (char)file.read();
	}
	file.close();

	return helper;
}
void MAKERphone::takeScreenshot()
{
	Serial.println("HERE");
	for (int i = 0; i < 4; i++)
	{
		if (tracks[i] != nullptr)
		{
			Serial.printf("%d track is playing: %d\n", i, tracks[i]->isPlaying());
			if (tracks[i]->isPlaying())
			{
				currTracks[i] = tracks[i];
				currTracks[i]->seek(tracks[i]->getSamplePos());
				Serial.print("PLAYING:");
				Serial.println(i);
				pausedTracks[i] = 1;
				tracks[i]->pause();
				removeTrack(tracks[i]);
			}
			else
			{
				currTracks[i] = tracks[i];
				currTracks[i]->seek(tracks[i]->getSamplePos());
				removeTrack(tracks[i]);
			}
		}
	}
	// display.setTextColor(TFT_BLACK);
	// display.setTextSize(1);
	// display.setTextFont(2);
	// display.drawRect(14, 45, 134, 38, TFT_BLACK);
	// display.drawRect(13, 44, 136, 40, TFT_BLACK);
	// display.fillRect(15, 46, 132, 36, 0xC59F);
	// display.setCursor(47, 55);
	// display.printCenter("Taking screenshot");
	while (!update())
		;

	char name[] = "/Images/screenshot_00.bmp";
	while (!SD.begin(5, SPI, 8000000))
		Serial.println("SD ERROR");
	for (int i = 0; i < 100; i++)
	{
		name[20] = i % 10 + '0';
		name[19] = i / 10 + '0';
		if (!SD.exists(name))
			break;
	}
	Serial.println(name);
	delay(5);
	File file = SD.open(name, "w");
	if (!file)
	{
		Serial.println("SD file error!");
		return;
	}

	uint8_t w = 160;
	uint8_t h = 128;
	//int px[] = {255, 0, 255, 0, 255, 0};
	//bool debugPrint = 1;
	unsigned char *img = NULL; // image data
	//  int filesize = 54 + 3 * w * h;      //  w is image width, h is image height
	int filesize = 54 + 4 * w * h; //  w is image width, h is image height
	if (img)
	{
		free(img);
	}
	img = (unsigned char *)malloc(3 * w);
	Serial.println(ESP.getFreeHeap());
	delay(5);
	memset(img, 0, 3 * w); // not sure if I really need this; runs fine without...
	Serial.println(ESP.getFreeHeap());
	delay(5);
	// for (int y=0; y<h; y++) {
	// 	for (int x=0; x<w; x++) {
	// 		byte red, green, blue;
	// 		unsigned long rgb = display.readPixel(x, y);
	// 		red = rgb >> 16;
	// 		green = (rgb & 0x00ff00) >> 8;
	// 		blue = (rgb & 0x0000ff);
	// 		rgb = 0;
	// 		rgb |= red <<16;
	// 		rgb |= blue <<8;
	// 		rgb |=green;
	// 		img[(y*w + x)*3+2] = red;
	// 		img[(y*w + x)*3+1] = green;
	// 		img[(y*w + x)*3+0] = blue;
	// 		// Serial.printf("x: %d   y: %d\n", x, y);
	// 		// delay(5);
	// 		// int colorVal = px[y*w + x];
	// 		img[(y*w + x)*3+2] = (unsigned char)(red);
	// 		img[(y*w + x)*3+1] = (unsigned char)(green);
	// 		img[(y*w + x)*3+0] = (unsigned char)(blue);
	// 	}
	// }
	// print px and img data for debugging
	// if (debugPrint) {
	// Serial.print("Writing \"");
	// Serial.print(name);
	// Serial.print("\" to file...\n");
	// for (int i=0; i<w*h; i++) {
	// 	Serial.print(px[i]);
	// 	Serial.print("  ");
	// }
	// }

	// create file headers (also taken from above example)
	unsigned char bmpFileHeader[14] = {
		'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0};
	unsigned char bmpInfoHeader[40] = {
		40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 24, 0};
	//unsigned char bmpPad[3] = {
	//	0, 0, 0};

	bmpFileHeader[2] = (unsigned char)(filesize);
	bmpFileHeader[3] = (unsigned char)(filesize >> 8);
	bmpFileHeader[4] = (unsigned char)(filesize >> 16);
	bmpFileHeader[5] = (unsigned char)(filesize >> 24);

	bmpInfoHeader[4] = (unsigned char)(w);
	bmpInfoHeader[5] = (unsigned char)(w >> 8);
	bmpInfoHeader[6] = (unsigned char)(w >> 16);
	bmpInfoHeader[7] = (unsigned char)(w >> 24);
	bmpInfoHeader[8] = (unsigned char)(h);
	bmpInfoHeader[9] = (unsigned char)(h >> 8);
	bmpInfoHeader[10] = (unsigned char)(h >> 16);
	bmpInfoHeader[11] = (unsigned char)(h >> 24);

	// write the file (thanks forum!)
	file.write(bmpFileHeader, sizeof(bmpFileHeader)); // write file header
	file.write(bmpInfoHeader, sizeof(bmpInfoHeader)); // " info header

	for (int i = h - 1; i >= 0; i--) // iterate image array
	{
		// memset(img, 0, sizeof(3 * w));        // not sure if I really need this; runs fine without...
		for (int x = 0; x < w; x++)
		{
			// uint8_t rgb[3];
			uint16_t rgb = display.readPixelRGB(x, i);
			// Serial.println(rgb);
			// Serial.println(display.readPixelRGB(x, i)*257);
			// delay(5);
			// byte red, green, blue;
			// display.readPixelRGB(x, i, &rgb[0]);
			// display.readRectRGB(x, i, 1, 1, rgb);
			// uint8_t r = ((rgb >> 11) & 0x1F);
			// uint8_t g = ((rgb >> 5) & 0x3F);
			// uint8_t b = (rgb & 0x1F);

			uint8_t r = (rgb & 0xF800) >> 8;
			uint8_t g = (rgb & 0x07E0) >> 3;
			uint8_t b = (rgb & 0x1F) << 3;

			// r = (r * 255) / 31;
			// g = (g * 255) / 63;
			// b = (b * 255) / 31;

			//  r = rgb >> 16;
			//  g = (rgb & 0x00ff00) >> 8;
			//  b = (rgb & 0x0000ff);

			// Serial.println(r);
			// Serial.println(g);
			// Serial.println(b);

			// delay(1000);

			// rgb = 0;
			// rgb |= red <<16;
			// rgb |= blue <<8;
			// rgb |= green;
			// Serial.printf("x: %d   y: %d\n", x, y);
			// delay(5);
			// int colorVal = px[y*w + x];
			// img[x*3+2] = (unsigned char)(r);
			// img[x*3+1] = (unsigned char)(g);
			// img[x*3+0] = (unsigned char)(b);
			file.write(b);
			file.write(g);
			file.write(r);
			// file.write(rgb[0]);
			// Serial.println(rgb[0]);
			// file.write(rgb[1]);
			// Serial.println(rgb[1]);
			// file.write(rgb[2]);                // write px data
			// Serial.println(rgb[2]);
			// delay(5);
		}
		// file.write(img + (w * (h - i - 1) * 3), 3 * w);	// write px data
		// file.write(bmpPad, (4-(w*3)%4)%4);                 // and padding as needed
	}
	file.close();
	free(img);
	display.setTextColor(TFT_BLACK);
	display.setTextSize(1);
	display.setTextFont(2);
	display.drawRect(14, 45, 134, 38, TFT_BLACK);
	display.drawRect(13, 44, 136, 40, TFT_BLACK);
	display.fillRect(15, 46, 132, 36, 0xC59F);
	display.setCursor(47, 48);
	display.printCenter(&name[8]);
	display.setCursor(47, 61);
	display.printCenter("saved to SD!");
	uint32_t tempMillis = millis();
	while (millis() < tempMillis + 3000)
	{
		update();
		if (buttons.pressed(BTN_A) || buttons.pressed(BTN_B))
		{
			while (!buttons.released(BTN_A) && !buttons.released(BTN_B))
				update();
			break;
		}
	}
	while (!update())
		;
	for (int i = 0; i < 4; i++)
	{
		if (currTracks[i] != nullptr)
		{
			addTrack(currTracks[i]);
			if (pausedTracks[i])
				currTracks[i]->play();
		}
		currTracks[i] = nullptr;
		pausedTracks[i] = 0;
	}
}

//Popups
void MAKERphone::popup(String text, uint16_t duration)
{
	popupText = text;
	popupTotalTime = popupTimeLeft = duration + 16;
}

void MAKERphone::updatePopup()
{
	if (!popupTimeLeft)
	{
		return;
	}
	uint8_t yOffset = 0;
	if (popupTimeLeft >= popupTotalTime - 18)
	{
		yOffset = (19 - (popupTotalTime - popupTimeLeft));
	}
	if (popupTimeLeft < 18)
	{
		yOffset = (19 - popupTimeLeft);
	}

	popupSprite.setTextColor(TFT_WHITE);
	popupSprite.fillScreen(TFT_BLACK);
	popupSprite.fillRect(0, yOffset, display.width(), 18, TFT_DARKGREY);
	// popupSprite.fillRect(0, 18 - yOffset, display.width(), 2, TFT_BLACK);
	if (yOffset == 0)
	{
		popupSprite.setTextFont(2);
		popupSprite.setTextSize(1);
		popupSprite.setCursor(2, yOffset + 2);
		popupSprite.print(popupText);
	}
	popupSprite.pushSprite(0, 110, TFT_BLACK);

	popupTimeLeft--;
}
void MAKERphone::homePopup(bool animation)
{
	Serial.println("HERE");
	for (int i = 0; i < 4; i++)
	{
		if (tracks[i] != nullptr)
		{
			Serial.printf("%d track is playing: %d\n", i, tracks[i]->isPlaying());
			if (tracks[i]->isPlaying())
			{
				currTracks[i] = tracks[i];
				currTracks[i]->seek(tracks[i]->getSamplePos());
				Serial.print("PLAYING:");
				Serial.println(i);
				pausedTracks[i] = 1;
				tracks[i]->pause();
				removeTrack(tracks[i]);
			}
			else
			{
				currTracks[i] = tracks[i];
				currTracks[i]->seek(tracks[i]->getSamplePos());
				removeTrack(tracks[i]);
			}
		}
	}
	uint8_t _tempFont = display.getTextFont();
	GFXfont *_tempFreeFont = display.getFreeFont();
	uint16_t _tempColor = display.getTextColor();
	uint8_t _tempSize = display.getTextSize();

	// for(uint8_t i = 0 ; i < 4;i++)
	// {
	// 	if(tracks[i] != nullptr)
	// 	{
	// 		if(tracks[i]->isPlaying())
	// 		{
	// 			tracks[i]->pause();
	// 			tempArray[i] = 1;
	// 		}
	// 		else
	// 			tempArray[i] = 0;
	// 	}
	// 	else
	// 		tempArray[i] = 0;
	// }
	// for(uint8_t i = 0 ; i < 4;i++)
	// {
	// 	if(oscs[i] != nullptr)
	// 		oscs[i]->stop();
	// }
	if (animation)
	{
		for (int i = 0; i < display.height(); i++)
		{
			display.drawFastHLine(0, i, display.width(), TFT_WHITE);
			if (i % 5 == 0)
				display.pushSprite(0, 0);
			// delayMicroseconds(750);
		}
	}
	dataRefreshFlag = 1;
	display.fillScreen(TFT_WHITE);
	display.drawIcon(popupVolume, 12, 25, 20, 20, 2);
	display.drawIcon(popupExit, 60, 25, 20, 20, 2);
	display.drawIcon(popupScreenBrightness, 108, 25, 20, 20, 2);
	display.drawIcon(popupScreenshot, 12, 70, 20, 20, 2);
	display.drawIcon(popupPixelBrightness, 108, 70, 20, 20, 2);
	uint8_t cursor = 1;
	uint32_t blinkMillis = millis();
	uint32_t notificationMillis = millis();
	uint8_t cursorState = 0;
	dataRefreshFlag = 1;
	String temp;
	while (!buttons.released(BTN_B))
	{
		display.fillScreen(TFT_WHITE);
		display.drawIcon(popupVolume, 12, 25, 20, 20, 2);
		display.drawIcon(popupExit, 60, 25, 20, 20, 2);
		display.drawIcon(popupScreenBrightness, 108, 25, 20, 20, 2);
		display.drawIcon(popupScreenshot, 12, 70, 20, 20, 2);
		display.drawIcon(popupPixelBrightness, 108, 70, 20, 20, 2);
		display.fillRect(0, 0, 160, 18, TFT_WHITE);
		display.fillRect(0, 114, 160, 20, TFT_WHITE);

		//drawing the top icons
		uint8_t helper = 11;
		if (sim_module_version == 255)
			helper = 1;
		if (simInserted && !airplaneMode && sim_module_version != 255)
		{
			if (networkRegistered == 1 || networkRegistered == 5)
			{

				if (signalStrength <= 3)
					display.drawBitmap(2, 2, noSignalIcon, TFT_BLACK, 2);
				else if (signalStrength > 3 && signalStrength <= 10)
					display.drawBitmap(2, 2, signalLowIcon, TFT_BLACK, 2);
				else if (signalStrength > 10 && signalStrength <= 20)
					display.drawBitmap(2, 2, signalHighIcon, TFT_BLACK, 2);
				else if (signalStrength > 20 && signalStrength <= 31)
					display.drawBitmap(2, 2, signalFullIcon, TFT_BLACK, 2);
				else if (signalStrength == 99)
					display.drawBitmap(2, 2, signalErrorIcon, TFT_BLACK, 2);
			}
			else
				display.drawBitmap(2, 2, signalErrorIcon, TFT_BLACK, 2);
		}
		else if (!simInserted && !airplaneMode && sim_module_version != 255)
			display.drawBitmap(2, 2, noSimIcon, TFT_BLACK, 2);
		else if (airplaneMode)
		{
			display.drawBitmap(2, 2, airplaneModeIcon, TFT_BLACK, 2);
		}
		if (ringVolume == 0)
		{
			display.drawBitmap(helper * 2, 2, silentModeIcon, TFT_BLACK, 2);
			helper += 10;
		}
		//display.drawBitmap(31, 1, missedcall);
		//display.drawBitmap(41, 1, newtext);
		// if (!airplaneMode)
		// {
		// 	if (wifi == 1)
		// 		display.drawBitmap(helper * 2, 2, wifiOnIcon, TFT_BLACK, 2);
		// 	else
		// 		display.drawBitmap(helper * 2, 2, wifiOffIcon, TFT_BLACK, 2);
		// 	helper += 10;
		// 	if (bt)
		// 		display.drawBitmap(helper * 2, 2, BTonIcon, TFT_BLACK, 2);
		// 	else
		// 		display.drawBitmap(helper * 2, 2, BToffIcon, TFT_BLACK, 2);
		// 	helper += 10;
		// }

		if (!SDinsertedFlag)
		{
			display.drawBitmap(helper * 2, 2, noSDIcon, TFT_BLACK, 2);
			helper += 10;
		}
		display.setTextFont(2);
		display.setTextSize(1);
		display.setTextColor(TFT_BLACK);
		display.setCursor(helper * 2, 4);
		if (carrierName != "" && (networkRegistered == 1 || networkRegistered == 5))
		{
			// display.setFreeFont(TT1);
			// display.setTextSize(2);
			// display.setCursor(helper*2, 15);

			display.print(carrierName);
		}
		else if (simInserted && !airplaneMode && (carrierName == "" || !(networkRegistered == 1 || networkRegistered == 5)))
			display.print("loading...");
		else if (carrierName == "" && !simInserted && sim_module_version == 255)
			display.print("No module");
		// display.setCursor(60, 2);
		// display.println(simVoltage);
		if (!digitalRead(CHRG_INT))
			display.drawBitmap(148, 2, batteryChargingIcon, TFT_BLACK, 2);
		else
		{
			/*
			//y = -316139 + 250.3763*x - 0.06612874*x^2 + 0.000005825959*x^3
			//y - percentage(%), x - voltage(V)
			double percentage = -316139 + (250.3763*batteryVoltage) - (0.06612874*batteryVoltage*batteryVoltage) + (0.000005825959*batteryVoltage*batteryVoltage*batteryVoltage);
			
			if(percentage > 100)
			{
				// Serial.print("Voltage: ");
				
				percentage = 100;
			}
			if (percentage < 0)
				percentage = 0;
			display.setTextFont(2);
			display.setTextSize(1);
			display.setTextColor(TFT_BLACK);
			display.setCursor(120, 2);
			percentage = round(percentage/10);
			if(percentage == 10)
				display.setCursor(110, 2);

			display.printf("%d", (int)percentage * 10);
			display.print("%");
			*/
			// if (batteryVoltage > 4100)
			// 	display.drawBitmap(148, 2, batteryChargingIcon, TFT_BLACK, 2);
			if (batteryVoltage >= 3850)
				display.drawBitmap(148, 2, batteryFullIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3850 && batteryVoltage >= 3750)
				display.drawBitmap(148, 2, batteryHighIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3750 && batteryVoltage >= 3650)
				display.drawBitmap(148, 2, batteryMidIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3650 && batteryVoltage >= 3600)
				display.drawBitmap(148, 2, batteryLowIcon, TFT_BLACK, 2);
			else if (batteryVoltage < 3600)
				display.drawBitmap(148, 2, batteryEmptyIcon, TFT_BLACK, 2);
		}

		if (millis() - blinkMillis >= 250)
		{
			blinkMillis = millis();
			cursorState = !cursorState;
		}

		if (millis() - notificationMillis >= 2000)
			notificationMillis = millis();
		display.setCursor(2, 112);
		if (millis() - notificationMillis > 1000 && millis() - notificationMillis < 2000)
		{
			display.printCenter("Notifications   ");
			display.drawBitmap(display.cursor_x - 10, display.cursor_y, arrowDown, TFT_BLACK, 2);
		}
		else
			display.printCenter(popupHomeItems[cursor]);

		// date and time
		updateTimeRTC();
		display.fillRect(60, 70, 40, 40, 0x963F);
		display.setFreeFont(TT1);
		display.setTextSize(2);
		display.setCursor(63, 85);
		temp = "";
		if (clockHour < 10)
			temp.concat("0");
		temp.concat(clockHour);
		temp.concat(":");
		if (clockMinute < 10)
			temp.concat("0");
		temp.concat(clockMinute);
		display.printCenter(temp);
		display.setCursor(63, 105);
		temp = "";
		if (clockDay < 10)
			temp.concat("0");
		temp.concat(clockDay);
		temp.concat("/");
		if (clockMonth < 10)
			temp.concat("0");
		temp.concat(clockMonth);
		display.printCenter(temp);
		display.setTextSize(1);
		display.setTextFont(2);

		if (buttons.released(BTN_UP))
		{
			osc->note(75, 0.05);
			osc->play();
			display.drawRect(12 + cursor % 3 * 48 - 2, 25 + 45 * (int)(cursor / 3) - 2, 44, 44, TFT_WHITE);
			display.drawRect(12 + cursor % 3 * 48 - 1, 25 + 45 * (int)(cursor / 3) - 1, 42, 42, TFT_WHITE);
			cursorState = 1;
			blinkMillis = millis();
			if (cursor < 3)
				cursor += 3;
			else
				cursor -= 3;
			notificationMillis = millis();
			buttons.update();
		}
		if (buttons.released(BTN_DOWN))
		{
			osc->note(75, 0.05);
			osc->play();
			display.drawRect(12 + cursor % 3 * 48 - 2, 25 + 45 * (int)(cursor / 3) - 2, 44, 44, TFT_WHITE);
			display.drawRect(12 + cursor % 3 * 48 - 1, 25 + 45 * (int)(cursor / 3) - 1, 42, 42, TFT_WHITE);
			cursorState = 1;
			blinkMillis = millis();
			if (cursor < 3)
				cursor += 3;
			else
			{
				notificationView();
			}
			notificationMillis = millis();

			buttons.update();
		}
		if (buttons.released(BTN_LEFT))
		{
			osc->note(75, 0.05);
			osc->play();
			display.drawRect(12 + cursor % 3 * 48 - 2, 25 + 45 * (int)(cursor / 3) - 2, 44, 44, TFT_WHITE);
			display.drawRect(12 + cursor % 3 * 48 - 1, 25 + 45 * (int)(cursor / 3) - 1, 42, 42, TFT_WHITE);
			cursorState = 1;
			blinkMillis = millis();
			if (cursor % 3 == 0)
				cursor += 2;
			else
				cursor -= 1;
			notificationMillis = millis();
			buttons.update();
		}
		if (buttons.released(BTN_RIGHT))
		{
			osc->note(75, 0.05);
			osc->play();
			display.drawRect(12 + cursor % 3 * 48 - 2, 25 + 45 * (int)(cursor / 3) - 2, 44, 44, TFT_WHITE);
			display.drawRect(12 + cursor % 3 * 48 - 1, 25 + 45 * (int)(cursor / 3) - 1, 42, 42, TFT_WHITE);
			cursorState = 1;
			blinkMillis = millis();
			if (cursor % 3 == 2)
				cursor -= 2;
			else
				cursor += 1;
			notificationMillis = millis();
			buttons.update();
		}
		if (buttons.released(BTN_A))
		{
			uint8_t blinkStateSound = 0;
			uint8_t soundCursor = 0;
			buttons.update();
			switch (cursor)
			{
			case 0: //volume
				while (!buttons.released(BTN_B) && !buttons.released(BTN_A))
				{
					if (buttons.released(BTN_DOWN))
						soundCursor = 1;
					if (buttons.released(BTN_UP))
						soundCursor = 0;
					if (millis() - blinkMillis >= 350)
					{
						blinkMillis = millis();
						blinkStateSound = !blinkStateSound;
					}
					display.drawRect(14, 30, 134, 70, TFT_BLACK);
					display.drawRect(13, 29, 136, 72, TFT_BLACK);
					display.fillRect(15, 31, 132, 68, 0x9FFE);
					display.drawRect(37, 79, 86, 10, TFT_BLACK);
					display.drawRect(36, 78, 88, 12, TFT_BLACK);
					display.fillRect(38, 80, ringVolume * 6, 8, TFT_BLACK);
					if (soundCursor)
					{
						mp.display.drawBitmap(18, 44, noSound, TFT_BLACK, 2);
						mp.display.drawBitmap(126, 44, fullSound, TFT_BLACK, 2);
						mp.display.drawBitmap(18, 76, noSound, blinkStateSound ? TFT_BLACK : 0x9FFE, 2);
						mp.display.drawBitmap(126, 76, fullSound, blinkStateSound ? TFT_BLACK : 0x9FFE, 2);
					}
					else
					{
						mp.display.drawBitmap(18, 76, noSound, TFT_BLACK, 2);
						mp.display.drawBitmap(126, 76, fullSound, TFT_BLACK, 2);
						mp.display.drawBitmap(18, 44, noSound, blinkStateSound ? TFT_BLACK : 0x9FFE, 2);
						mp.display.drawBitmap(126, 44, fullSound, blinkStateSound ? TFT_BLACK : 0x9FFE, 2);
					}
					display.drawRect(37, 47, 86, 10, TFT_BLACK);
					display.drawRect(36, 46, 88, 12, TFT_BLACK);
					display.fillRect(38, 48, mediaVolume * 6, 8, TFT_BLACK);

					mp.display.setCursor(30, 31, 2);
					mp.display.printCenter("Media volume");
					mp.display.setCursor(30, 61, 2);
					mp.display.printCenter("Ring volume");
					//osc->setVolume(oscillatorVolumeList[mediaVolume]);
					if (buttons.repeat(BTN_LEFT, 7) && (mediaVolume > 0 || ringVolume > 0))
					{
						if (!soundCursor)
						{
							if (mediaVolume > 0)
								mediaVolume--;
							for (uint8_t i = 0; i < 4; i++)
							{
								if (tracks[i] != nullptr)
								{
									if (mediaVolume == 0)
										tracks[i]->setVolume(0);
									else
										tracks[i]->setVolume(map(mediaVolume, 0, 14, 100, 300));
								}
							}
							for (uint8_t i = 0; i < 4; i++)
							{
								if (oscs[i] != nullptr)
									oscs[i]->setVolume(oscillatorVolumeList[mediaVolume]);
							}
							osc->setVolume(oscillatorVolumeList[mediaVolume]);
						}
						if (soundCursor)
						{
							if (ringVolume > 0)
								ringVolume--;
							osc->setVolume(oscillatorVolumeList[ringVolume]);
						}
						osc->note(75, 0.05);
						osc->play();
						if (mediaVolume < 0)
							mediaVolume = 0;
						if (ringVolume < 0)
							ringVolume = 0;
						//update();
						//while (!update());
					}
					if (buttons.repeat(BTN_RIGHT, 7) && (mediaVolume < 14 || ringVolume < 14))
					{
						if (!soundCursor)
						{
							if (mediaVolume < 14)
								mediaVolume++;
							for (uint8_t i = 0; i < 4; i++)
							{
								if (tracks[i] != nullptr)
								{
									if (mediaVolume == 0)
										tracks[i]->setVolume(0);
									else
										tracks[i]->setVolume(map(mediaVolume, 0, 14, 100, 300));
								}
							}
							for (uint8_t i = 0; i < 4; i++)
							{
								if (oscs[i] != nullptr)
									oscs[i]->setVolume(oscillatorVolumeList[mediaVolume]);
							}
							osc->setVolume(oscillatorVolumeList[mediaVolume]);
						}
						if (soundCursor)
						{
							if (ringVolume < 14)
								ringVolume++;
							osc->setVolume(oscillatorVolumeList[ringVolume]);
						}
						osc->note(75, 0.05);
						osc->play();
						if (mediaVolume > 14)
							mediaVolume = 14;
						if (ringVolume > 14)
							ringVolume = 14;
					}
					update();
				}
				osc->setVolume(oscillatorVolumeList[mediaVolume]);
				break;

			case 1: //exit
				loader();
				break;

			case 2: //screen brightness
				while (!buttons.released(BTN_B) && !buttons.released(BTN_A))
				{
					display.drawRect(13, 49, 136, 30, TFT_BLACK);
					display.drawRect(14, 50, 134, 28, TFT_BLACK);
					display.fillRect(15, 51, 132, 26, 0xFF92);
					display.drawRect(33, 58, 89, 12, TFT_BLACK);
					display.drawRect(34, 59, 87, 10, TFT_BLACK);
					display.fillRect(35, 60, brightness * 17, 8, TFT_BLACK);
					display.drawBitmap(18, 59, noBrightness, TFT_BLACK, 2);
					display.drawBitmap(125, 53, fullBrightness, TFT_BLACK, 2);
					if (buttons.released(BTN_LEFT) && brightness > 0)
					{
						brightness--;
						osc->note(75, 0.05);
						osc->play();
						buttons.update();
					}
					if (buttons.released(BTN_RIGHT) && brightness < 5)
					{
						brightness++;
						osc->note(75, 0.05);
						osc->play();
						buttons.update();
					}
					if (brightness == 0)
						ledcAnalogWrite(LEDC_CHANNEL, 230);
					else
						ledcAnalogWrite(LEDC_CHANNEL, (5 - brightness) * 51);

					update();
				}
				break;

			case 3: //screenshot
				screenshotFlag = 1;
				display.setTextColor(_tempColor);
				display.setTextFont(_tempFont);
				display.setTextSize(_tempSize);
				return;
				break;

			case 4: //clock
			{
				uint32_t timer = millis();
				bool blinkState = 0;
				String temp = "";
				String monthsList[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

				while (!buttons.released(BTN_B) && !buttons.released(BTN_A))
				{
					display.fillScreen(0x963F);
					// date and time
					updateTimeRTC();
					display.setTextFont(2);
					display.setTextSize(2);
					display.setCursor(15, 25);
					temp = "";
					if (clockHour < 10)
						temp.concat("0");
					temp.concat(clockHour);
					temp.concat(":");
					if (clockMinute < 10)
						temp.concat("0");
					temp.concat(clockMinute);
					temp.concat(":");
					if (clockSecond < 10)
						temp.concat("0");
					temp.concat(clockSecond);

					display.printCenter(temp);
					display.setTextSize(1);
					display.setCursor(63, 85);
					temp = "";
					if (clockDay < 10)
						temp.concat("0");
					temp.concat(clockDay);
					if (clockDay < 20 && clockDay > 10)
						temp.concat("th");
					else if (clockDay % 10 == 1)
						temp.concat("st");
					else if (clockDay % 10 == 2)
						temp.concat("nd");
					else if (clockDay % 10 == 3)
						temp.concat("rd");
					else
						temp.concat("th");
					temp.concat(" of ");
					temp.concat(monthsList[clockMonth - 1]);

					display.printCenter(temp);
					display.setCursor(0, 100);
					display.printCenter(clockYear);

					if (millis() - timer >= 1000)
					{
						blinkState = !blinkState;
						timer = millis();
					}
					update();
				}
			}
			break;

			case 5: //LED brightness
			{
				while (!buttons.released(BTN_B) && !buttons.released(BTN_A))
				{
					for (int i = 0; i < 8; i++)
						leds[i] = CRGB::Red;
					display.drawRect(13, 49, 136, 30, TFT_BLACK);
					display.drawRect(14, 50, 134, 28, TFT_BLACK);
					display.fillRect(15, 51, 132, 26, 0xA794);
					display.drawRect(33, 58, 89, 12, TFT_BLACK);
					display.drawRect(34, 59, 87, 10, TFT_BLACK);
					display.fillRect(35, 60, pixelsBrightness * 17, 8, TFT_BLACK);
					display.drawBitmap(18, 59, noBrightness, TFT_BLACK, 2);
					display.drawBitmap(125, 53, fullBrightness, TFT_BLACK, 2);
					if (buttons.released(BTN_LEFT) && pixelsBrightness > 0)
					{
						pixelsBrightness--;
						osc->note(75, 0.05);
						osc->play();
						buttons.update();
					}
					if (buttons.released(BTN_RIGHT) && pixelsBrightness < 5)
					{
						pixelsBrightness++;
						osc->note(75, 0.05);
						osc->play();
						buttons.update();
					}
					update();
				}
			}
			break;
			}
			buttons.update();
			display.fillScreen(TFT_WHITE);
			display.drawIcon(popupVolume, 12, 25, 20, 20, 2);
			display.drawIcon(popupExit, 60, 25, 20, 20, 2);
			display.drawIcon(popupScreenBrightness, 108, 25, 20, 20, 2);
			display.drawIcon(popupScreenshot, 12, 70, 20, 20, 2);
			display.drawIcon(popupPixelBrightness, 108, 70, 20, 20, 2);
		}
		display.drawRect(12 + cursor % 3 * 48 - 1, 25 + 45 * (int)(cursor / 3) - 1, 42, 42, cursorState ? TFT_RED : TFT_WHITE);
		display.drawRect(12 + cursor % 3 * 48 - 2, 25 + 45 * (int)(cursor / 3) - 2, 44, 44, cursorState ? TFT_RED : TFT_WHITE);
		update();
	}
	buttons.update();
	if (SDinsertedFlag)
		saveSettings();
	for (int i = 0; i < 4; i++)
	{
		if (currTracks[i] != nullptr)
		{
			addTrack(currTracks[i]);
			if (pausedTracks[i])
				currTracks[i]->play();
		}
		currTracks[i] = nullptr;
		pausedTracks[i] = 0;
	}
	display.fillScreen(TFT_BLACK);
	display.setTextColor(TFT_WHITE);
	display.setTextFont(2);
	display.setCursor(0, display.height() / 2 - 22);
	display.printCenter("Applying settings...");
	display.setCursor(0, display.height() / 2 - 2);
	display.printCenter("Please wait");
	while (!update())
		;
	applySettings();
	// for(uint8_t i = 0 ; i < 4;i++)
	// {
	// 	if(tracks[i] != nullptr && tempArray[i])
	// 		tracks[i]->resume();
	// }
	// for(uint8_t i = 0 ; i < 4;i++)
	// {
	// 	if(oscs[i] != nullptr)
	// 		oscs[i]->play();
	// }
	display.setTextColor(_tempColor);
	if (_tempFreeFont != NULL)
		display.setFreeFont(_tempFreeFont);
	else
		display.setTextFont(_tempFont);
	display.setTextSize(_tempSize);
	dataRefreshFlag = 0;
}
void MAKERphone::drawNotificationWindow(uint8_t y, uint8_t index)
{
	display.setTextFont(2);
	display.setTextSize(1);
	display.setCursor(22, y + 2);
	display.fillRoundRect(2, y + 2, 154, 20, 2, TFT_DARKGREY);
	display.fillRoundRect(4, y, 154, 20, 2, TFT_WHITE);
	display.print(notificationDescriptionList[index]);
	switch (notificationTypeList[index])
	{
	case 1:
		display.drawBitmap(5, y + 2, missedCallIcon, TFT_RED, 2);
		break;
	case 2:
		display.drawBitmap(5, y + 2, newMessageIcon, TFT_BLACK, 2);
		break;
	case 3:
		display.drawBitmap(5, y + 2, systemNotification, TFT_RED, 2);
		break;
	}
	display.setTextFont(1);
	display.setCursor(129, y + 2);
	String temp = "";
	DateTime now = notificationTimeList[index];
	if (now.hour() < 10)
		temp.concat("0");
	temp.concat(now.hour());
	display.print(temp);
	display.setCursor(display.cursor_x - 2, display.cursor_y);
	display.print(":");
	display.setCursor(display.cursor_x - 2, display.cursor_y);
	temp = "";
	if (now.minute() < 10)
		temp.concat("0");
	temp.concat(now.minute());
	display.print(temp);
	display.setCursor(125, y + 11);
	temp = "";
	display.print(monthsList[now.month() - 1]);
	display.setCursor(display.cursor_x + 2, display.cursor_y);
	if (now.day() < 10)
		temp.concat("0");
	temp.concat(now.day());
	display.print(temp);
}
void MAKERphone::homePopupEnable(bool enabled)
{
	HOME_POPUP_ENABLE = enabled;
}
void MAKERphone::playNotificationSound(uint8_t _notification)
{
	notification = _notification;
	osc->stop();
	osc->setWaveform(SINE);
	notificationMillis = 0;
	playingNotification = 1;
	notesIndex = 0;

	notificationSoundDuration = notificationNotesDuration[notification][notesIndex];
}
void MAKERphone::updateNotificationSound()
{
	if (playingNotification && millis() - notificationMillis >= notificationSoundDuration * 1000 + 125)
	{
		osc->setVolume(oscillatorVolumeList[ringVolume]);
		notificationSoundDuration = notificationNotesDuration[notification][notesIndex];
		notificationSoundNote = notificationNotes[notification][notesIndex];
		if (notificationSoundDuration == 0 || notesIndex == 5)
		{
			playingNotification = 0;
			// osc->stop();
			return;
		}
		if (notificationSoundNote == 0)
		{
			notesIndex++;
			return;
		}
		notificationMillis = millis();
		osc->note(notificationSoundNote, notificationSoundDuration);

		notesIndex++;
	}
}
void MAKERphone::addNotification(uint8_t _type, String _description, DateTime _time)
{
	for (int i = 0; i < sizeof(notificationTypeList); i++)
	{
		if (notificationTypeList[i] == 0)
		{
			notificationTypeList[i] = _type;
			notificationDescriptionList[i] = _description;
			notificationTimeList[i] = _time;
			break;
		}
	}
	saveNotifications();
}
void MAKERphone::removeNotification(uint8_t index)
{
	notificationTypeList[index] = 0;
	notificationDescriptionList[index] = "";
	notificationTimeList[index] = DateTime((uint32_t)0);
	saveNotifications();
}
void MAKERphone::loadNotifications(bool debug)
{
	const char *path = "/.core/notifications.json";
	File file = SD.open(path);
	jb.clear();
	JsonArray &notifications = jb.parseArray(file);
	file.close();

	if (notifications.success())
	{
		int i = 0;
		// notifications.prettyPrintTo(Serial);
		for (JsonObject &tempNotification : notifications)
		{
			notificationTypeList[i] = tempNotification["Type"];
			notificationDescriptionList[i] = tempNotification["Description"].as<String>();
			notificationTimeList[i] = DateTime(tempNotification["DateTime"].as<uint32_t>());
			i++;
		}
	}
	else
	{
		saveNotifications();
		Serial.println("Error loading new notifications");
	}

	// notifications.prettyPrintTo(Serial);
}
void MAKERphone::saveNotifications(bool debug)
{
	const char *path = "/.core/notifications.json";
	Serial.println("");
	SD.remove(path);
	jb.clear();
	JsonArray &notifications = jb.createArray();

	if (notifications.success())
	{
		for (int i = 0; i < sizeof(notificationTypeList); i++)
		{
			JsonObject &tempNotification = jb.createObject();
			tempNotification["Type"] = notificationTypeList[i];
			tempNotification["Description"] = notificationDescriptionList[i].c_str();
			tempNotification["DateTime"] = notificationTimeList[i].unixtime();
			notifications.add(tempNotification);
		}

		File file1 = SD.open(path, "w");
		notifications.prettyPrintTo(file1);
		// notifications.prettyPrintTo(Serial);
		file1.close();
	}
	else
	{
		Serial.println("Error saving notifications data");
	}
}
void MAKERphone::notificationView()
{
	while (!buttons.released(BTN_B) && !buttons.released(BTN_UP))
	{
		display.fillScreen(TFT_WHITE);
		display.setCursor(0, 2);
		display.setTextFont(2);
		display.setTextSize(1);
		display.printCenter("Notifications");
		display.setCursor(3, 114);
		display.print("Clear all");

		uint8_t temp = 0;
		bool anyNotifications = 0;
		for (int i = sizeof(notificationTypeList); i > -1; i--)
		{
			if (notificationTypeList[i] != 0)
			{
				temp++;
				if (temp == 1)
					drawNotificationWindow(20, i);
				else if (temp == 2)
					drawNotificationWindow(44, i);
				else if (temp == 3)
					drawNotificationWindow(68, i);
				else if (temp == 4)
					drawNotificationWindow(92, i);
				anyNotifications = 1;
			}
		}
		if (!anyNotifications)
		{
			display.setCursor(0, display.height() / 2 - 16);
			display.printCenter("No notifications...");
		}
		if (buttons.released(BTN_FUN_LEFT))
		{
			for (int i = 0; i < sizeof(notificationTypeList); i++)
				notificationTypeList[i] = 0;
			if (SDinsertedFlag)
				saveNotifications();
		}
		update();
	}
	while (!update())
		;
}
String MAKERphone::currentDateTime()
{
	updateTimeRTC();
	// 2019-04-18 12:00:00
	String dateTime = String(clockYear);
	dateTime += "-";
	if (clockMonth < 10)
	{
		dateTime += "0";
	}
	dateTime += String(clockMonth);
	dateTime += "-";
	if (clockDay < 10)
	{
		dateTime += "0";
	}
	dateTime += String(clockDay);
	dateTime += " ";

	if (clockHour < 10)
	{
		dateTime += "0";
	}
	dateTime += String(clockHour);
	dateTime += ":";
	if (clockMinute < 10)
	{
		dateTime += "0";
	}
	dateTime += String(clockMinute);
	dateTime += ":";
	if (clockSecond < 10)
	{
		dateTime += "0";
	}
	dateTime += String(clockSecond);
	return dateTime;
}

void MAKERphone::alarmPopup(bool animation)
{
	// if(animation)
	// {
	// 	for (int i = 0; i < display.height(); i+=1)
	// 	{
	// 		display.drawFastHLine(0, i, display.width(), TFT_WHITE);
	// 		if(i%5 == 0)update();
	// 		// delayMicroseconds(750);
	// 	}
	// }
	MPTrack *temp = nullptr;
	updateTimeRTC();
	DateTime now = RTC.now();
	char buf[100];
	strncpy(buf, "hh:mm\0", 100);
	tft.fillRect(0, 0, 160, 128, TFT_WHITE);
	tft.setCursor(38, 12);
	tft.setTextFont(1);
	tft.setTextSize(3);
	tft.setTextColor(TFT_BLACK);
	tft.print(now.format(buf));
	tft.setTextFont(2);
	tft.setTextSize(2);
	tft.setCursor(50, 50);
	tft.print("Alarm");
	tft.setTextSize(1);
	tft.setCursor(2, 111);
	tft.setTextFont(2);
	tft.print("Press \"A\" to turn off");
	bool blinkState = 0;
	uint32_t blinkMillis = millis();
	Serial.println(currentAlarm);

	if (!SDinsertedFlag || (SDinsertedFlag && !SD.exists(ringtone_path)))
	{

		bool state = 0;
		uint8_t tempNotification = notification;
		uint32_t callMillis = millis();
		while (1)
		{
			if (millis() - callMillis >= 1000)
			{
				state = 1;
				callMillis = millis();
			}
			if (state)
			{

				playNotificationSound(4);
				state = 0;
			}
			if (millis() - blinkMillis >= 350)
			{
				blinkMillis = millis();
				blinkState = !blinkState;
			}

			if (buttons.released(BTN_A))
			{
				buttons.update();
				break;
			}
			buttons.update();
			updateNotificationSound();
		}
		notification = tempNotification;
	}
	else
	{
		if (!addTrack(ringtone))
		{
			temp = tracks[3];
			removeTrack(temp);
			addTrack(ringtone);
		}
		Serial.println(ringtone_path);
		if (ringVolume == 0)
			ringtone->setVolume(0);
		else
			ringtone->setVolume(map(ringVolume, 0, 14, 100, 300));
		ringtone->setRepeat(1);
		ringtone->play();

		while (1)
		{
			if (buttons.released(BTN_A))
			{
				buttons.update();
				break;
			}
			buttons.update();
		}
		ringtone->stop();
		removeTrack(ringtone);
		if (temp != nullptr)
			addTrack(temp);
	}
}
void MAKERphone::loadAlarms()
{
	const char *path = "/.core/alarms.json";
	File file = SD.open(path);
	jb.clear();
	JsonArray &alarms = jb.parseArray(file);
	file.close();

	if (alarms.success())
	{
		int i = 0;
		for (JsonObject &tempAlarm : alarms)
		{
			alarmHours[i] = tempAlarm["hours"];
			alarmMins[i] = tempAlarm["mins"];
			alarmEnabled[i] = tempAlarm["enabled"];
			alarmRepeat[i] = tempAlarm["repeat"];
			JsonArray &days = tempAlarm["days"];
			for (int x = 0; x < 7; x++)
			{
				alarmRepeatDays[i][x] = days[x];
			}
			alarmTrack[i] = String(tempAlarm["track"].as<char *>());
			i++;
		}
	}
	else
	{
		saveAlarms();
		Serial.println("Error loading new alarms");
	}
}
void MAKERphone::saveAlarms()
{
	const char *path = "/.core/alarms.json";
	Serial.println("");
	SD.remove(path);
	jb.clear();
	JsonArray &alarms = jb.createArray();

	if (alarms.success())
	{
		for (int i = 0; i < 5; i++)
		{
			JsonObject &tempAlarm = jb.createObject();
			tempAlarm["hours"] = alarmHours[i];
			tempAlarm["mins"] = alarmMins[i];
			tempAlarm["enabled"] = alarmEnabled[i];
			tempAlarm["repeat"] = alarmRepeat[i];
			JsonArray &days = jb.createArray();
			for (int x = 0; x < 7; x++)
			{
				days.add(alarmRepeatDays[i][x]);
			}
			tempAlarm["days"] = days;
			tempAlarm["track"] = alarmTrack[i];
			alarms.add(tempAlarm);
		}

		File file1 = SD.open(path, "w");
		alarms.prettyPrintTo(file1);
		// alarms.prettyPrintTo(Serial);
		file1.close();
	}
	else
	{
		Serial.println("Error saving alarm data");
	}
	checkAlarms();
}
void MAKERphone::checkAlarms()
{
	updateTimeRTC();
	uint8_t next_alarm = 99;
	int i = currentTime.dayOfWeek();
	for (int x = 0; x < 6; x++)
	{
		for (int y = 0; y < 5; y++)
		{
			// if(i == currentTime.dayOfWeek())
			// {
			// 	Serial.print(y);Serial.println(".");
			// 	Serial.print("alarmEnabled: "); Serial.println(alarmEnabled[y]);
			// 	Serial.print("alarmRepeat: ");Serial.println(alarmRepeat[y]);
			// 	Serial.print("alarmRepeatToday: ");Serial.println(alarmRepeatDays[y][i]);
			// 	Serial.println("-----------");
			// 	delay(10);
			// }
			if (alarmEnabled[y] == 1 && alarmRepeat[y] && alarmRepeatDays[y][i] &&
				((DateTime(clockYear, clockMonth, clockDay, alarmHours[y], alarmMins[y], 0) > currentTime &&
				  i == currentTime.dayOfWeek()) ||
				 i != currentTime.dayOfWeek()))
			{
				if (next_alarm == 99)
					next_alarm = y;
				else if (DateTime(clockYear, clockMonth, clockDay, alarmHours[y], alarmMins[y], 0) <
						 DateTime(clockYear, clockMonth, clockDay, alarmHours[next_alarm], alarmMins[next_alarm]))
					next_alarm = y;
			}
		}
		i++;
		if (i > 7)
			i = 1;
	}

	for (int x = 0; x < 5; x++)
	{
		DateTime tempAlarm = DateTime(clockYear, clockMonth, clockDay, alarmHours[x], alarmMins[x], 0);
		DateTime nextAlarm;
		if (next_alarm != 99)
			nextAlarm = DateTime(clockYear, clockMonth, clockDay, alarmHours[next_alarm], alarmMins[next_alarm], 0);
		if (alarmEnabled[x] == 1 && !alarmRepeat[x])
		{
			if (next_alarm == 99)
				next_alarm = x;
			else if ((((!alarmRepeat[next_alarm] || (alarmRepeat[next_alarm] &&
													 alarmRepeatDays[next_alarm][currentTime.dayOfWeek()])) &&
					   (tempAlarm > RTC.now() && tempAlarm > nextAlarm)) ||
					  (tempAlarm < RTC.now() && nextAlarm < RTC.now() && tempAlarm < nextAlarm)) ||
					 (alarmRepeat[next_alarm] && !alarmRepeatDays[next_alarm][currentTime.dayOfWeek()]))
				next_alarm = x;
		}
	}
	Serial.print("next alarm: ");
	Serial.println(next_alarm);
	currentAlarm = next_alarm;
	if (next_alarm == 99)
		RTC.off_alarm();
	else
	{
		uint8_t z = 0;
		if (alarmRepeat[next_alarm])
		{
			uint8_t x = currentTime.dayOfWeek();

			for (z = 0; z < 7; z++)
			{
				if (alarmRepeatDays[next_alarm][x])
					break;
				x++;
				if (x > 6)
					x = 0;
			}
		}
		DateTime alarm = DateTime(clockYear, clockMonth, clockDay, alarmHours[next_alarm], alarmMins[next_alarm], 0);
		alarm = DateTime(alarm.unixtime() + z * 3600 * 24);
		alarm_flags flags;
		flags.hour = 1;
		flags.minute = 1;
		flags.day = alarmRepeat[next_alarm];
		flags.wday = 0;
		RTC.set_alarm(alarm, flags);
		RTC.on_alarm();
	}
}
void MAKERphone::SDremovedPopup()
{
	for (int i = 2; i < 72; i++)
	{
		tft.drawRect(tft.width() / 2 - i, 46, i * 2 + 2, 36, TFT_BLACK);
		tft.drawRect(tft.width() / 2 - i + 1, 47, i * 2 + 1, 34, TFT_BLACK);
		tft.fillRect(tft.width() / 2 - i + 2, 48, i * 2 - 2, 32, TFT_WHITE);
	}
	tft.drawBitmap(14, 52, SDcardIcon, TFT_BLACK, 2);
	tft.setCursor(38, 55);
	tft.setTextColor(TFT_BLACK);
	tft.setTextFont(2);
	tft.setTextSize(1);
	tft.print("SD card removed!");
	delay(1500);
}
void MAKERphone::SDinsertedPopup()
{
	for (int i = 2; i < 72; i++)
	{
		tft.drawRect(tft.width() / 2 - i, 46, i * 2 + 2, 36, TFT_BLACK);
		tft.drawRect(tft.width() / 2 - i + 1, 47, i * 2 + 1, 34, TFT_BLACK);
		tft.fillRect(tft.width() / 2 - i + 2, 48, i * 2 - 2, 32, TFT_WHITE);
	}
	tft.drawBitmap(14, 52, SDcardIcon, TFT_BLACK, 2);
	tft.setCursor(38, 55);
	tft.setTextColor(TFT_BLACK);
	tft.setTextFont(2);
	tft.setTextSize(1);
	tft.print("SD card inserted!");
	delay(1250);
}

//Misc
void MAKERphone::deallocateAudio()
{
	vTaskDelete(Task1);
}
void MAKERphone::reallocateAudio()
{
	xTaskCreatePinnedToCore(
		Task1code, /* Task function. */
		"Task1",   /* name of task. */
		10000,	   /* Stack size of task */
		NULL,	   /* parameter of the task */
		1,		   /* priority of the task */
		&Task1,
		0); /* Task handle to keep track of created task */
}
String MAKERphone::checkContact(String contactNumber)
{
	File file = SD.open("/.core/contacts.json", "r");

	if (file.size() < 2)
	{ // empty -> FILL
		Serial.println("Override");
		file.close();
		jb.clear();
		JsonArray &jarr = jb.createArray();
		delay(10);
		File file1 = SD.open("/.core/contacts.json", "w");
		jarr.prettyPrintTo(file1);
		file1.close();
		file = SD.open("/.core/contacts.json", "r");
		if (!file)
			Serial.println("CONTACTS ERROR");
	}
	jb.clear();
	JsonArray &jarr = jb.parseArray(file);
	file.close();

	if (!jarr.success())
		Serial.println("Error loading contacts");
	else
	{
		for (JsonObject &elem : jarr)
		{
			char *tempNumber = (char *)elem["number"].as<char *>();
			if (contactNumber == String(tempNumber))
				return String(elem["name"].as<char *>());
		}
	}

	return "";
}
void MAKERphone::pduDecode(const char *PDU)
{
	uint16_t cursor = 0;
	//07 91 839515001000 04 0C 91 839535685687 0008 91806291402080 04 0076 010D
	// +CMT: "",25
	//07 91 839515001000 04 0C 91 839535771645 0000 91 80 90 61 90 92 80 06    32 5A CD 76 C3 01
	/*
		07 - length of SMSC info in octets
		91 - type of address (91 international)
		next 6 octets are the service center number
		04 - PDU type (further parsing needed)
		0C - address (phone number) length in digits (not octets!)
		91 - type of address
		next 12 digits are the phone number
		0000 - protocol identifier and data coding scheme

		next 7 octets are the timestamp: 9th of August 2019, 16:09:29, GMT+2
		91 - last two digits of the year (2019)
		80 - month (august)
		90 - day (9th)
		61 - 4 PM
		90 - 9 minutes
		92 - 29 seconds
		80 - timezone: 0b1000 0000
		bit 3 decides +/- timezone - in this case +
		the remaining number 80 & 1111 0111 = 80
		80 represents number 08, the timezone offset is 8*15 minutes = 120min = 2 hours

		06 - number of characters in message
	 */
	//07 91 839515001000 60 09 D0 D432BB2C03 00F191909151116080A00500033C03019C61502CE782E55C32582C07AA8362341D0C06AABBEBF4B01C440FCBD3E6324898D43A9353507199843A83A0743B4C2F83DCE5F7591E76A7C7657719D44EBB4169D0B43905C1DFF2FABA0CA2974131986CE6C2819A421708B47CCBD373777A9C6E874173103CBC2ED3DF6D90B0A8154A9F4AD0510882CBD3EBB0BEAE2E83E66510D95DB6BBC3
	char subcharBuffer[3];
	memset(subcharBuffer, 0, 3);
	subchar(PDU, cursor, 2, subcharBuffer);
	uint8_t SMSC_length = strtol(subcharBuffer, nullptr, 16);
	cursor += 4; //one octet for SMSC length, one for type-of-address
	Serial.print("SMSC length: ");
	Serial.println(SMSC_length);
	if (SMSC_length > 1)
		SMSC_length--;
	char serviceNumber[16];
	memset(serviceNumber, 0, 16);
	for (int i = 0; i < SMSC_length; i++)
	{
		subchar(PDU, cursor, 2, subcharBuffer);
		if (charReverse(subcharBuffer)[1] == 'F') //remove trailing F in case number can't be split in even octets
			strncat(serviceNumber, &subcharBuffer[0], 1);
		else
			strncat(serviceNumber, subcharBuffer, 2);
		cursor += 2;
	}
	subchar(PDU, cursor, 2, subcharBuffer);
	uint8_t PDUType = strtol(subcharBuffer, nullptr, 16);
	bool dataHeaderIndicator = bitRead(PDUType, 6);
	cursor += 2; //skip first octet of SMS-DELIVER
	subchar(PDU, cursor, 2, subcharBuffer);
	uint8_t addressLength = strtol(subcharBuffer, nullptr, 16);
	if (addressLength % 2 == 1)
		addressLength++;
	cursor += 2; //address length and type-of-address
	subchar(PDU, cursor, 2, subcharBuffer);
	uint8_t typeOfAddress = strtol(subcharBuffer, nullptr, 16);
	typeOfAddress = (typeOfAddress >> 4) & 0b00000111;
	cursor += 2;
	memset(_smsNumber, 0, 20);

	if (typeOfAddress != 0b00000101)
	{

		strncat(_smsNumber, "+", 1);
		for (int i = 0; i < addressLength / 2; i++)
		{
			subchar(PDU, cursor, 2, subcharBuffer);
			if (charReverse(subcharBuffer)[1] == 'F') //remove trailing F in case number can't be split in even octets
				strncat(_smsNumber, &subcharBuffer[0], 1);
			else
				strncat(_smsNumber, subcharBuffer, 2);
			cursor += 2;
		}
	}
	else
	{
		char buffer[(addressLength) + 1];
		memset(buffer, 0, (addressLength) + 1);
		subchar(PDU, cursor, (addressLength), buffer);
		myDecode(buffer, addressLength / 2, _smsNumber, 0);
		cursor += addressLength;
	}
	cursor += 2; //PID

	bool codingScheme = 0; // 0 - GSM 7 bit, 1 - UCS2
	subchar(PDU, cursor, 2, subcharBuffer);
	if (strtol(subcharBuffer, nullptr, 16) != 0 && strtol(subcharBuffer, nullptr, 16) != 0xF1)
		codingScheme = 1;
	cursor += 2;
	subchar(PDU, cursor, 2, subcharBuffer);
	clockYear = 2000 + strtol(charReverse(subcharBuffer), nullptr, 10);
	cursor += 2;
	subchar(PDU, cursor, 2, subcharBuffer);
	clockMonth = strtol(charReverse(subcharBuffer), nullptr, 10);
	cursor += 2;
	subchar(PDU, cursor, 2, subcharBuffer);
	clockDay = strtol(charReverse(subcharBuffer), nullptr, 10);
	cursor += 2;
	// Serial.println(clockYear);
	// Serial.println(clockMonth);
	// Serial.println(clockDay);
	subchar(PDU, cursor, 2, subcharBuffer);
	clockHour = strtol(charReverse(subcharBuffer), nullptr, 10);
	cursor += 2;
	subchar(PDU, cursor, 2, subcharBuffer);
	clockMinute = strtol(charReverse(subcharBuffer), nullptr, 10);
	cursor += 2;
	subchar(PDU, cursor, 2, subcharBuffer);
	clockSecond = strtol(charReverse(subcharBuffer), nullptr, 10);
	cursor += 2;
	// Serial.println(clockHour);
	// Serial.println(clockMinute);
	// Serial.println(clockSecond);
	subchar(PDU, cursor, 2, subcharBuffer);
	uint16_t timeZone = strtol(subcharBuffer, nullptr, 16);
	//bool plusMinus = bitRead(timeZone, 3);
	timeZone = timeZone & 0b11110111;
	char zoneHelper[3];
	sprintf(zoneHelper, "%X", timeZone);
	timeZone = strtol(charReverse(zoneHelper), nullptr, 10) / 4;
	cursor += 2;
	_smsDatetime = DateTime(clockYear, clockMonth, clockDay, clockHour, clockMinute, clockSecond);

	subchar(PDU, cursor, 2, subcharBuffer);
	uint16_t userDataLength = strtol(subcharBuffer, nullptr, 16);
	cursor += 2;
	uint8_t zerosPadding = 0;
	// if(userDataLength == 160 || _concatSMSCounter > 0)
	if (dataHeaderIndicator)
	{
		if (!codingScheme)
			userDataLength -= 7;
		subchar(PDU, cursor, 2, subcharBuffer);
		uint8_t concatHeaderLength = strtol(subcharBuffer, nullptr, 16);
		// Serial.print("concat header length: ");
		// Serial.println(concatHeaderLength);
		cursor += (concatHeaderLength - 1) * 2;
		subchar(PDU, cursor, 2, subcharBuffer);
		uint8_t totalParts = strtol(subcharBuffer, nullptr, 16);
		// Serial.print("totalParts ");
		// Serial.println(totalParts);
		cursor += 2;
		subchar(PDU, cursor, 2, subcharBuffer);
		uint8_t partIndex = strtol(subcharBuffer, nullptr, 16);
		// Serial.print("partIndex: ");
		// Serial.println(partIndex);
		_currentConcatSMS = partIndex;
		if (_concatSMSCounter == 0)
			_concatSMSCounter = totalParts;
		cursor += 2;
		zerosPadding = 7 - (((concatHeaderLength + 1) * 8) % 7);
		// Serial.print("zeros padding: ");
		// Serial.println(zerosPadding);
		_concatSMSCodingScheme = codingScheme;
	}
	// if(codingScheme)
	memset(_smsText, 0, userDataLength + 1);

	if (!codingScheme)
	{
		// Serial.println(subchar(PDU, cursor, (userDataLength)*2));
		// Serial.println(userDataLength);
		char buffer[(userDataLength)*2 + 1];
		subchar(PDU, cursor, (userDataLength)*2, buffer);
		// Serial.println(buffer);
		myDecode(buffer, userDataLength, _smsText, zerosPadding);

		// delete [] received;

		// for(int i = 0; i < userDataLength; i++)
		// {
		// 	char snippet[3];
		// 	strncpy(&snippet[0], subchar(PDU, cursor, 2), 3);
		// 	// snippet[2] = '\0';
		// 	// Serial.print("snip ");
		// 	// Serial.println(snippet);
		// 	// Serial.println(my_map[17].GSM7bitValue);
		// 	// for(int i = 0; i < 3 ; i++)
		// 	// {
		// 	// 	Serial.print(i);
		// 	// 	Serial.println(my_map[17].GSM7bitValue[i] == snippet[i]);
		// 	// }
		// 	// for(int y = 0; y < 93; y++)
		// 	// {
		// 	// 	// Serial.println(y);
		// 	// 	if(strcmp(my_map[y].GSM7bitValue, snippet) == 0)
		// 	// 	{
		// 	// 		content+=my_map[y].AsciiValue;
		// 	// 		break;
		// 	// 	}
		// 	// }
		// 	content+=char(strtol(snippet, nullptr, 16) & 0x7F);
		// 	cursor+=2;
		// }
	}
	else
	{
		char newText[162];
		memset(newText, 0, 162);
		for (int i = 0; i <= userDataLength * 2 - 4; i += 4)
		{
			char snip[5] = "";
			subchar(PDU, cursor + i, 4, snip);
			// strncpy(snip, subchar(PDU, cursor + i, 4), 5);
			int charCode = strtol(snip, nullptr, 16);
			char c = '*';
			if (charCode <= 128)
				c = charCode;
			strncat(newText, &c, 1);
		}
		strncpy(_smsText, newText, sizeof(newText));
		// content = newText;
	}
	// Serial.print("content: ");
	// Serial.println(_smsText);
	// Serial.print("data length: ");
	// Serial.println(userDataLength);
	// Serial.print("coding scheme: ");
	// Serial.println(codingScheme);
	// Serial.print(plusMinus ? "-" : "+");
	// Serial.println(timeZone);
	// Serial.println(_smsNumber);
	// free(content);
}
bool MAKERphone::setCallVolume(uint8_t volume) //0-100
{
	bool ret = false;
	String command = "";
	if (volume <= 100 && volume >= 0)
	{
		command = "AT+CLVL=" + String(volume);
		Serial.println(command);
		Serial1.println(command); //send command
		Serial.println("WAIT=1"); //wait a bit
		while (!Serial1.available())
			; //wait for response
		if (!(Serial1.readString().indexOf("OK") == -1))
			ret = false;
		else
			ret = true;
	}
	return ret;
}

#endif
