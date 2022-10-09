#include <Arduino.h>
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <vector>

#define USE_SERIAL Serial
#define INTERNAL_LED 2
#define TEMPERATURE_PRECISION 9
#define DO_PIN 39
#define TDS_PIN 36
#define PH_PIN 34
#define CSS_PIN 35
#define ONE_WIRE_BUS 25

const char *ssid = "Kembar Kost";   // Enter SSID
const char *password = "Blangpulo"; // Enter Password
const char SET_LOOP[] = "SET_LOOP";
const char GET_ALL[] = "GET_ALL";
const char GET_WATER_TEMP[] = "GET_WATER_TEMP";
const char GET_AIR_TEMP[] = "GET_AIR_TEMP";
const char GET_PH[] = "GET_PH";
const char GET_TDS[] = "GET_TDS";
const char GET_DO[] = "GET_DO";
const char GET_CSS[] = "GET_CSS";
const DeviceAddress water_temp_address = {0x28, 0x95, 0x1E, 0x45, 0x92, 0x16, 0x2, 0x66};
const DeviceAddress air_temp_address = {0x28, 0xA7, 0xCD, 0x45, 0x92, 0xC, 0x2, 0xFE};
const uint16_t DO_Table[41] = {
    14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
    11260, 11010, 10770, 10530, 10300, 10080, 9860, 9660, 9460, 9270,
    9080, 8900, 8730, 8570, 8410, 8250, 8110, 7960, 7820, 7690,
    7560, 7430, 7300, 7180, 7070, 6950, 6840, 6730, 6630, 6530, 6410};
char buffer[512];
boolean led_status = false;

WebSocketsServer webSocket = WebSocketsServer(81);

struct sensorData
{
  char *name;
  float value;
};
std::vector<sensorData> sensor_array;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void ledBlink(int times)
{
  for (size_t i = 0; i < times * 2; i++)
  {
    if (led_status)
    {
      digitalWrite(INTERNAL_LED, LOW);
      led_status = false;
    }
    else
    {
      digitalWrite(INTERNAL_LED, HIGH);
      led_status = true;
    }
    delay(100);
  }
}
boolean compare(uint8_t *first, const char *second)
{
  return (memcmp(first, second, strlen(second)) == 0);
}
float average_analogue_read(int pin, int mode = 0)
{
  int average;
  int buffer_arr[10], temp;
  for (int i = 0; i < 10; i++)
  {
    buffer_arr[i] = analogRead(pin);
    delay(10);
  }
  for (int i = 0; i < 9; i++)
  {
    for (int j = i + 1; j < 10; j++)
    {
      if (buffer_arr[i] > buffer_arr[j])
      {
        temp = buffer_arr[i];
        buffer_arr[i] = buffer_arr[j];
        buffer_arr[j] = temp;
      }
    }
  }
  average = 0;
  for (int i = 2; i < 8; i++)
    average += buffer_arr[i];
  if (mode == 0)
  {
    return average / 6;
    USE_SERIAL.println(average / 6);
  }
  else
  {
    return average / 6 * 3.3 / 4096.0;
    USE_SERIAL.println(average / 6 * 3.3 / 4096.0);
  }
}
void pack_sensor_array()
{
  StaticJsonDocument<500> doc;
  doc["status"] = "DONE";
  JsonArray data = doc.createNestedArray("data");
  for (auto &d : sensor_array)
  {
    StaticJsonDocument<64> var;
    var["name"] = d.name;
    var["value"] = d.value;
    data.add(var);
  }
  serializeJson(doc, buffer, measureJson(doc));
}
float retrive_value(const char *name)
{
  float retval = 0.0;
  for (auto &d : sensor_array)
  {
    if (d.name == name)
    {
      retval = d.value;
      break;
    }
  }
  return retval;
}
char *jsonify_status(const char *status)
{
  StaticJsonDocument<48> doc;
  doc["status"] = status;
  const size_t strsize = measureJson(doc);
  serializeJson(doc, buffer, strsize + 1);
  return buffer;
}
void get_air_temp()
{
  sensors.requestTemperatures();
  sensor_array.push_back({(char *)"air_temp", sensors.getTempC(air_temp_address)});
}
void get_water_temp()
{
  sensors.requestTemperatures();
  sensor_array.push_back({(char *)"water_temp", sensors.getTempC(water_temp_address)});
}
void get_ph()
{
  float ph_act;
  float calibration_value = 20.24 - 0.7;
  float volt = average_analogue_read(PH_PIN, 1);
  ph_act = -5.70 * volt + calibration_value;
  sensor_array.push_back({(char *)"ph", ph_act});
}
void get_tds()
{
  float temperature = retrive_value("water_temp");
  if (temperature == 0)
  {
    get_water_temp();
  }
  float compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
  float compensationVoltage = average_analogue_read(TDS_PIN, 1) / compensationCoefficient;
  float tdsValue = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage - 255.86 * compensationVoltage * compensationVoltage + 857.39 * compensationVoltage) * 0.5;
  sensor_array.push_back({(char *)"tds", tdsValue});
}
void get_do()
{
  int CAL1_V = 131; // mv
  int CAL1_T = 25;  //℃
  float temperature = retrive_value("water_temp");
  if (temperature == 0)
  {
    get_water_temp();
  }
  uint16_t V_saturation = (uint32_t)CAL1_V + (uint32_t)35 * temperature - (uint32_t)CAL1_T * 35;
  float do_value = (average_analogue_read(DO_PIN, 1) * DO_Table[(int)temperature] / V_saturation);
  sensor_array.push_back({(char *)"do", do_value});
}
void get_css()
{
  const int AirValue = 3620;   // you need to replace this value with Value_1
  const int WaterValue = 1680; // you need to replace this value with Value_2
  float moisturePercentage = map(analogRead(CSS_PIN), AirValue, WaterValue, 0, 100);
  sensor_array.push_back({(char *)"css", moisturePercentage});
}
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  char *data;
  switch (type)
  {
  case WStype_DISCONNECTED:
    ledBlink(4);
    USE_SERIAL.printf("[%u] Disconnected!\n", num);
    break;
  case WStype_CONNECTED:
  {
    ledBlink(2);
    IPAddress ip = webSocket.remoteIP(num);
    USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
    webSocket.sendTXT(num, jsonify_status("CONNECTED"));
  }
  break;
  case WStype_TEXT:
    ledBlink(1);
    webSocket.sendTXT(num, jsonify_status("RECEIVED"));
    if (compare(payload, GET_WATER_TEMP))
      get_water_temp();
    if (compare(payload, GET_AIR_TEMP))
      get_air_temp();
    if (compare(payload, GET_ALL))
    {
      get_water_temp();
      get_air_temp();
      get_ph();
      get_tds();
      get_do();
      get_css();
    }
    pack_sensor_array();
    webSocket.sendTXT(num, buffer);
    break;
  case WStype_BIN:
    break;
  case WStype_ERROR:
    break;
  case WStype_FRAGMENT_TEXT_START:
    break;
  case WStype_FRAGMENT_BIN_START:
    break;
  case WStype_FRAGMENT:
    break;
  case WStype_FRAGMENT_FIN:
    break;
  }
}

void wifiConnect()
{
  USE_SERIAL.println("CONNECTING TO WIFI");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    USE_SERIAL.print(WiFi.status());
    ledBlink(2);
    delay(600);
  }
  IPAddress failed_ip = IPAddress(0, 0, 0, 0);
  if (WiFi.localIP() == failed_ip)
  {
    USE_SERIAL.println();
    USE_SERIAL.println("Wifi not connected");
    USE_SERIAL.println();
    delay(500);
    ledBlink(5);
    delay(500);
    ESP.restart();
  }
  USE_SERIAL.println("");
  USE_SERIAL.println("WiFi connected");
  USE_SERIAL.println("IP address: ");
  USE_SERIAL.println(WiFi.localIP());
  ledBlink(5);
}

void setup()
{
  USE_SERIAL.begin(115200);
  pinMode(INTERNAL_LED, OUTPUT);
  pinMode(PH_PIN, INPUT);
  pinMode(DO_PIN, INPUT);
  pinMode(CSS_PIN, INPUT);
  pinMode(TDS_PIN, INPUT);
  USE_SERIAL.setDebugOutput(true);
  USE_SERIAL.println();

  // Start up the library
  sensors.begin();

  // locate devices on the bus
  Serial.println("Locating thermometer");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");

  sensors.setResolution(water_temp_address, TEMPERATURE_PRECISION);
  sensors.setResolution(air_temp_address, TEMPERATURE_PRECISION);

  wifiConnect();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}
void loop()
{
  memset(&buffer, 0, sizeof(buffer));
  sensor_array.clear();
  webSocket.loop();
}