/**
 * Using a Wemos D1 to demo a simple IOT implementation, for a remote temperature sensor with Wifi connectivity. More details on https://www.pocketmagic.net/simple-iot/
 * Including hardware assembly, firmware and server backend.
 * 
 * Author: Radu Motisan, radhoo.tech@gmail.com
 * Web: www.uradmonitor.com
 * 
 * Licence: Open Source , under GPL v3
 * 
 */

// EEPROM
#include <EEPROM.h>

// DS18B20 libraries
#include <OneWire.h> 
#include <DallasTemperature.h>

// ESP library
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// sensor settings
#define SENSOR_BUS D2               // the DS18B20 has the sensor connected to D2
OneWire oneWire(SENSOR_BUS);        // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs) 
DallasTemperature sensor(&oneWire); // Pass our oneWire reference to Dallas Temperature. 

// wifi settings, set the AP to connect to
#define AP_SSID ""
#define AP_KEY ""
// get valid user id and user key from www.uradmonitor.com/dashboard and insert it below:
#define USER_ID ""
#define USER_KEY ""

// backend API
#define URADMONITOR_SERVER "http://data.uradmonitor.com/api/v1/upload/exp/"
#define DEV_CLASS 0x13
#define VER_SW 100
#define VER_HW 100
#define SEND_INTERVAL 60 // seconds


// eeprom / device ID
#define EEPROM_ADDR_DEVID 0x0
uint32_t deviceID = 0x0;

// expProtocol
#define ID_TIME_SECONDS "01"        // compulsory: local time in seconds
#define ID_TEMPERATURE_CELSIUS "02" // optional: temperature in degrees celsius
#define ID_PRESSURE_PASCALS "03"    // optional: barometric pressure in pascals
#define ID_HUMIDITY_RH "04"         // optional: humidity as relative humidity in percentage %
#define ID_LUMINOSITY_RL "05"       // optional: luminosity as relative luminosity in percentage ‰
#define ID_VOC_OHM "06"             // optional: volatile organic compounds in ohms
#define ID_CO2_PPM "07"             // optional: carbon dioxide in ppm
#define ID_CH2O_PPM "08"            // optional: formaldehyde in ppm
#define ID_PM25_UGCM "09"           // optional: particulate matter in micro grams per cubic meter
#define ID_BATTERY_VOLTS "0A"       // optional: device battery voltage in volts
#define ID_GEIGER_CPM "0B"          // optional: radiation measured on geiger tube in cpm
#define ID_INVERTERVOLTAGE_VOLTS "0C" // optional: high voltage geiger tube inverter voltage in volts
#define ID_INVERTERDUTY_PM "0D"     // optional: high voltage geiger tube inverter duty in ‰
#define ID_VERSION_HW "0E"          // optional: hardware version
#define ID_VERSION_SW "0F"          // optional: software firmware version
#define ID_TUBE "10"                // optional: tube type ID

// helper function to read a uint32_t from the EEPROM
uint32_t eeprom_read_dword(uint32_t address) {
  uint32_t value = 0;
  for (int i = 0; i < 4; i++)
    value |= (uint32_t)EEPROM.read(address + i) << (i * 8);
  return value;
}

// helper function to write a uint32_t to the EEPROM
void eeprom_write_dword(uint32_t address, uint32_t value) {
  for (int i = 0; i < 4; i++) {
    EEPROM.write(address + i, (value >> (i * 8)) & 0xFF);
  } 
}

// helper function to read a uint32_t from the EEPROM
void wifiConnect(char *ssid, char *key) {
  Serial.print("Connecting to AP "); Serial.print(ssid);
  WiFi.begin(ssid, key);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println(""); Serial.println("WiFi connected");  
}

// helper macro to return the minimum out of two values
#define MIN(x,y) (x<y)?x:y

// helper function to parse the json server response: finds a key and copies its value to the value output pointer
bool jsonKeyFind(char *response, char *key, char *value, uint8_t size) {
  char *s1 = strstr(response, key);
  uint8_t len = strlen(key);
  if (s1 && len) {
    char *s2 = strstr(s1 + len + 3, "\"");
    if (s2) {
      strncpy(value, s1 + len + 3, MIN(s2 - s1 - len - 3, size) );
      return true;
    }
  }
  return false;
}

// helper takes a hex string and converts it to a 32bit number (max 8 hex digits)
uint32_t hex2int(char *hex) {
    uint32_t val = 0;
     while (*hex) {
      // get current character then increment
      uint8_t byte = *hex++;
      // transform hex character to the 4bit equivalent number, using the ascii table indexes
      if (byte >= '0' && byte <= '9') byte = byte - '0';
      else if (byte >= 'a' && byte <='f') byte = byte - 'a' + 10;
      else if (byte >= 'A' && byte <='F') byte = byte - 'A' + 10;
      // shift 4 to make space for new digit, and add the 4 bits of the new digit
      val = (val << 4) | (byte & 0xF);
    }
    return val;
}


// send a HTTP Post to the backend, using the EXP protocol
bool sendSensorData (uint32_t seconds, float temperature, String userId, String userKey, uint32_t devId) {
  Serial.println("sendSensorData " + String(seconds) + " " + String(temperature) + " " + userId + " device:" + String(devId, HEX));
  
  // prepare post data
  String postUrl = String(ID_TIME_SECONDS) + "/" + String(seconds, DEC) + "/" + String(ID_VERSION_HW) + "/" + String(VER_HW, DEC) + "/" +  String(ID_VERSION_SW) + "/" + String(VER_SW, DEC) + "/" + String(ID_TEMPERATURE_CELSIUS) + "/" + String(temperature);
  Serial.println(postUrl);
  // prepare http client to do the post
  HTTPClient http;
  // data is sent as URL / RESTful API
  http.begin(URADMONITOR_SERVER + postUrl);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  // the expProtocol requires these customs headers
  http.addHeader("X-User-id", userId);
  http.addHeader("X-User-hash", userKey);
  http.addHeader("X-Device-id", String(devId, HEX));
  int httpCode = http.POST("");
  // check server results
  if (httpCode != 200) {
    Serial.println("not successful");
    http.end();
    return false;
  } else {
    char buffer[200] = {0};
    http.getString().toCharArray(buffer, 200);
    Serial.print("Server response:"); Serial.println(buffer);
    // check response
    char value[10] = {0}; // this has to be inited, or when parsing we could have extra junk screwing results
    if (jsonKeyFind(buffer, "setid", value, 10)) {
      Serial.print("Server allocated new ID:"); Serial.println(value);
      deviceID = hex2int(value);
      eeprom_write_dword(EEPROM_ADDR_DEVID, deviceID);
      EEPROM.commit();
    } 
    http.end();
    return true;
  }
}


// put your setup code here, to run once:
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  
  Serial.begin(9600);               // start serial port 
  Serial.print("read device number from EEPROM:");
  EEPROM.begin(4); // init eeprom with size 4bytes
  deviceID = ((uint32_t) DEV_CLASS << 24) | ((uint32_t) eeprom_read_dword(EEPROM_ADDR_DEVID) & 0x00FFFFFF);
  Serial.println(String(deviceID, HEX));
  
  sensor.begin();                   // Start up the sensor library 
  wifiConnect(AP_SSID, AP_KEY);
  
  Serial.println("Send interval:" + String(SEND_INTERVAL, DEC));
}



// put your main code here, to run repeatedly:
void loop() {
  if (millis() % (SEND_INTERVAL * 1000) == 0) {
    // check connection
    if (WiFi.status() != WL_CONNECTED) 
      wifiConnect(AP_SSID, AP_KEY);
     
    // read temperature
    digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level  but actually the LED is on; this is because it is acive low on the ESP)
    sensor.requestTemperatures(); // Send the command to get temperature readings 
    float temperature = sensor.getTempCByIndex(0);
    Serial.print("Temperature:"); Serial.println(temperature);
    
    // send temperature
    sendSensorData(millis() / 1000, temperature, USER_ID, USER_KEY, deviceID);       
    
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH

    // you could sent ESP to sleep here , to save power
  }

}
