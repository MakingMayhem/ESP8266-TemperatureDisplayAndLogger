/*
 The MIT License (MIT)

 Copyright (c) 2015 David L Kinney <david@makingmayhem.net>

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

#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>


////////////////////////////////////////////////////////////////////////////////
// USER-ADJUSTABLE SETTINGS

/**
 * Name of the WiFi network the ESP8266 should join.
 */
char const * const WiFiSSID     = "YourWiFiNetworkName";

/**
 * Password for the WiFi network.
 */
char const * const WiFiPassword = "YourWiFiPassword";

/**
 * Your StatHat EZ key -- usually the email address for your account. 
 */
char const * const StatHatEZKey = "alice@example.com";

/**
 * Name of the stat. This is how it will appear in StatHat. 
 */
char const * const StatHatStat  = "temperature";

/**
 * Minimum number of milliseconds between sending updates to StatHat.
 * 
 * On the free tier, StatHat has 15-minute granularity, so the values it 
 * receives over the course of a 15 minute period are averaged into a single 
 * number. Thus, there is little value in having a short interval between 
 * sending updates to StatHat -- once every few minutes is quite acceptable and 
 * reduces power consumption (for implementations using batteries).
 */
#define TEMPERATURE_POST_INTERVAL 180000

/**
 * Minimum number of milliseconds between temperature readings and display 
 * updates. 
 * 
 * The Dallas Temperature sensor requires some time between readings -- see the 
 * data sheet for that value. 
 * 
 * Practically speaking, room temperatures do not change so quickly that they 
 * need to be read more than a few times a minute. 
 */
#define TEMPERATURE_READ_INTERVAL  15000

/**
 * The number of readings to use when calculating a rolling average of 
 * temperature. 
 * 
 * A rolling average is used to determine the value to display and the value to 
 * send to StatHat so that anomalous readings are smoothed out. I pick a value 
 * so that TEMPERATURE_READ_INTERVAL * TEMPERATURE_ROLLING_COUNT is about one 
 * minute because it makes it easy to explain: "that the average temperature 
 * over the last minute". For example, if TEMPERATURE_READ_INTERVAL is 15000, I 
 * would set TEMPERATURE_ROLLING_COUNT to 4: 15000ms * 4 = 60000ms = 1 minute.  
 */
#define TEMPERATURE_ROLLING_COUNT      4

////////////////////////////////////////////////////////////////////////////////
// 

char const * const StatHatHost = "api.stathat.com";
int const StatHatPort = 80;
char const * const StatHatHeaders = "POST /ez HTTP/1.1\r\nHost: api.stathat.com\r\nConnection: close\r\nContent-Type: application/json\r\n";

// Data wire is plugged into pin 2 on the Arduino
#define ONE_WIRE_BUS 2
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS); 
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature tempSensors(&oneWire);

Adafruit_AlphaNum4 alpha4 = Adafruit_AlphaNum4();

unsigned long currentMillis  = 0;
unsigned long lastReadMillis = 0;
unsigned long lastPostMillis = 0;
unsigned long elapsedMillisSinceReading  = 0;
unsigned long elapsedMillisSincePosting  = 0;

int rollingTempF[TEMPERATURE_ROLLING_COUNT];


////////////////////////////////////////////////////////////////////////////////
// HELPERS

int takeTempFReading() {
    // call sensors.requestTemperatures() to issue a global temperature
    // request to all devices on the bus
    tempSensors.requestTemperatures(); // Send the command to get temperatures
    int tempF = tempSensors.getTempFByIndex(0); // Why "byIndex"? You can have more than one IC on the same bus. 0 refers to the first IC on the wire
    return tempF;
}

void storeTempFReading(int tempF) {
  #define LAST_INDEX (TEMPERATURE_ROLLING_COUNT - 1)
  
  for (int i = 0; i < LAST_INDEX; ++i) {
    rollingTempF[i] = rollingTempF[i+1];
  }
  rollingTempF[LAST_INDEX] = tempF;
}

int averageTempF() {
  int sum = 0;
  for (int i = 0; i < TEMPERATURE_ROLLING_COUNT; ++i) {
    sum += rollingTempF[i];
  }
  int avg = sum / TEMPERATURE_ROLLING_COUNT;
  return avg;
}

void displayTempFReadingOnAlpha(int tempF) {
  String tempFStr = String(tempF);
  alpha4.writeDigitAscii(0, tempFStr.charAt(0));
  alpha4.writeDigitAscii(1, tempFStr.charAt(1));
  alpha4.writeDigitRaw(2, 0x00E3);
  alpha4.writeDigitAscii(3, 'F');
  alpha4.writeDisplay();
}

void postTempFReadingToStatHat(int tempF) {
  WiFiClient client;
  bool success = client.connect(StatHatHost, StatHatPort);
  if (! success) {
    return;
  }

  String bodyJSON = String("{\"ezkey\":\"");
  bodyJSON += StatHatEZKey;
  bodyJSON += "\", \"data\":[";
  bodyJSON += "{\"stat\":";
  bodyJSON += "\"";
  bodyJSON += StatHatStat;
  bodyJSON += "\",\"value\":";
  bodyJSON += tempF;
  bodyJSON += "}";
  bodyJSON += "]}";
  int contentLength = bodyJSON.length();

  client.print(StatHatHeaders);
  client.print("Content-Length: ");
  client.print(contentLength);
  client.print("\r\n\r\n");
  client.print(bodyJSON);
  
  while(client.available()) {
    client.read();
  }
}

////////////////////////////////////////////////////////////////////////////////
// SETUP & LOOP

void setup() {
  alpha4.begin(0x70);  // pass in the I2C address, 0x70 is default
  alpha4.writeDigitAscii(0, 'W');
  alpha4.writeDigitAscii(1, 'I');
  alpha4.writeDigitAscii(2, 'F');
  alpha4.writeDigitAscii(3, 'I');
  alpha4.writeDisplay();

  WiFi.begin(WiFiSSID, WiFiPassword);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  tempSensors.begin();
  tempSensors.setResolution(12); // IC Default 9 bit. If you have troubles consider upping it 12. Ups the delay giving the IC more time to process the temperature measurement
  delay(500);
  
  // fill rolling temperature readings with current temperature
  int tempF = takeTempFReading();
  for (int i = 0; i < TEMPERATURE_ROLLING_COUNT; ++i) {
    rollingTempF[i] = tempF;
  }

  // update display to show first temp reading
  displayTempFReadingOnAlpha(tempF);
}

void loop() {
  delay(500);
  currentMillis = millis();
  
  elapsedMillisSinceReading = currentMillis - lastReadMillis;
  if (elapsedMillisSinceReading >= TEMPERATURE_READ_INTERVAL) {
    int tempF = takeTempFReading();
    storeTempFReading(tempF);

    int avgTempF = averageTempF();
    displayTempFReadingOnAlpha(avgTempF);
    
    lastReadMillis = currentMillis;
  }

  elapsedMillisSincePosting = currentMillis - lastPostMillis;
  if (elapsedMillisSincePosting >= TEMPERATURE_POST_INTERVAL) {
    int avgTempF = averageTempF();
    postTempFReadingToStatHat(avgTempF);
    
    lastPostMillis = currentMillis;
  }
}
