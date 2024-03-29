#include <ArduinoOTA.h>

/* WIFI AP */
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

// WiFi settings
const char* ssid = "Hack5";
const char* password = "#Zline159";
/* Put IP Address details */
/*IPAddress local_ip(192, 168, 254, 253);
IPAddress gateway(192, 168, 254, 254);
IPAddress subnet(255, 255, 255, 0);*/
ESP8266WebServer server(80);
WiFiClient espClient;

// MQTT Broker
#include <PubSubClient.h>
const char* mqtt_broker = "broker.hivemq.com";
const char* mqtt_username = "";
const char* mqtt_password = "";
const int mqtt_port = 1883;
PubSubClient mqttClient(espClient);
String journal = "";

#define mqtt_topic(topic) ((String) "qfd564dsf654qsdf/" + topic).c_str()

// NTP server
#include <NTPClient.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time.google.com", 3600);
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
int default_cycle;  // 21600 Feed every 6 Hour
unsigned long alarm;

// // Servo motor
#include <Servo.h>
Servo myservo;       // create servo object to control a servo
int open = 0;        // variable to store the servo position
int close = 180;     // variable to store the servo position
int servo_pin = D5;  // for ESP8266 microcontroller
int qteFood;

// DHT Sensor
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
uint8_t DHTPin = D7;
DHT dht(DHTPin, DHT11);
float Temperature = 0;
float Humidity = 0;
float oldTemperature = 0;
float oldHumidity = 0;
float offsetTemperature = -8.4;
float offsetHumidity = 25;

// EEPROM
#include "EEPROM.h"
#define adr_qteFood 0x00
#define adr_timer 0x02

//variabls for blinking an LED with Millis
const int led = D0;                // ESP8266 Pin to which onboard LED is connected
unsigned long previousMillis = 0;  // will store last time LED was updated
const long interval = 1000;        // interval at which to blink (milliseconds)
int ledState = LOW;                // ledState used to set the LED

String stringDateTime = "";

void handle_OnConnect();
void handle_feed();
void handle_dht();
void handle_NotFound();
String SendHTML();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttIdle();
void feed();
void console(String msg);
int getQuality();

void setup() {
  pinMode(led, OUTPUT);

  pinMode(DHTPin, INPUT);
  dht.begin();

  myservo.attach(servo_pin);  // attaches the servo on GIO2 to the servo object
  myservo.write(close);

  Serial.begin(115200);
  Serial.println("\r\nBooting");
  WiFi.mode(WIFI_STA);
  WiFi.hostname("CAT-Feeder");
  WiFi.begin(ssid, password);
  //WiFi.config(local_ip, gateway, subnet);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("Cat_feeder");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else  // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handle_OnConnect);
  server.on("/feed", handle_feed);
  server.on("/dht", handle_dht);
  server.onNotFound(handle_NotFound);
  server.begin();
  Serial.println("HTTP server started");

  timeClient.begin();
  timeClient.update();

  mqttClient.setServer(mqtt_broker, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  mqttIdle();

  EEPROM.begin(10);

  qteFood = EEPROM.read(adr_qteFood);
  default_cycle = (EEPROM.read(adr_timer) << 8) | EEPROM.read(adr_timer + 1);

  alarm = timeClient.getEpochTime() + default_cycle;

  mqttClient.publish(mqtt_topic("qteFood"), String(qteFood).c_str());
  mqttClient.publish(mqtt_topic("timer"), String(alarm - timeClient.getEpochTime()).c_str());
  mqttClient.publish(mqtt_topic("log"), journal.c_str());
  mqttClient.publish(mqtt_topic("rssi"), String(WiFi.RSSI()).c_str());

  console("Start");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  mqttIdle();
  timeClient.update();

  //loop to blink without delay
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    // save the last time you blinked the LED
    previousMillis = currentMillis;
    // if the LED is off turn it on and vice-versa:
    ledState = not(ledState);
    // set the LED with thtime.google.come ledState of the variable:
    digitalWrite(led, ledState);

    if (ledState) {
      Temperature = dht.readTemperature() + offsetTemperature;  // Gets the values of the temperature
      if (Temperature != oldTemperature) {
        oldTemperature = Temperature;
        String T = String(Temperature, 1);
        mqttClient.publish(mqtt_topic("Temperature"), T.c_str());
        String txt = "Temperature ";
        txt += T.c_str();
        txt += "°C";
        console(txt);
      }

      Humidity = dht.readHumidity() + offsetHumidity;  // Gets the values of the humidity
      if (Humidity != oldHumidity) {
        oldHumidity = Humidity;
        String H = String(Humidity, 0);
        mqttClient.publish(mqtt_topic("Humidity"), H.c_str());
        String txt = "Humidity ";
        txt += H.c_str();
        txt += "%";
        console(txt);
      }

      if (alarm < timeClient.getEpochTime()) {
        feed();
        alarm = timeClient.getEpochTime() + default_cycle;
      } else {
        mqttClient.publish(mqtt_topic("timer"), String(alarm - timeClient.getEpochTime()).c_str());
      }

      mqttClient.publish(mqtt_topic("rssi"), String(WiFi.RSSI()).c_str());
    }
  }
}

void handle_OnConnect() {
  server.send(200, "text/html", SendHTML());
}

void handle_feed() {
  feed();
  server.send(200, "text/html", SendHTML());
}

void handle_dht() {
  String dht = "{\"Temperature\":";
  dht += Temperature;
  dht += ", \"Humidity\":";
  dht += Humidity;
  dht += ", \"NTPClient\":\"";
  dht += timeClient.getFormattedTime();
  dht += "\"}";

  server.send(200, "application/json", dht);
}

void handle_NotFound() {
  server.send(404, "text/html", SendHTML());
}

String SendHTML() {
  String ptr = "<!DOCTYPE time.google.comhtml> <html>\n";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<link href=\"https://fonts.googleapis.com/css?family=Open+Sans:300,400,600\" rel=\"stylesheet\">\n";
  ptr += "<title>ESP8266 Weather Report</title>\n";
  ptr += "<style>html { font-family: 'Open Sans', sans-serif; display: block; margin: 0px auto; text-align: center;color: #333333;}\n";
  ptr += "body{margin-top: 50px;}\n";
  ptr += "h1 {margin: 50px auto 30px;}\n";
  ptr += ".side-by-side{display: inline-block;vertical-align: middle;position: relative;}\n";
  ptr += ".humidity-icon{background-color: #3498db;width: 30px;height: 30px;border-radius: 50%;line-height: 36px;}\n";
  ptr += ".humidity-text{font-weight: 600;padding-left: 15px;font-size: 19px;width: 160px;text-align: left;}\n";
  ptr += ".humidity{font-weight: 300;font-size: 60px;color: #3498db;}\n";
  ptr += ".temperature-icon{background-color: #f39c12;width: 30px;height: 30px;border-radius: 50%;line-height: 40px;}\n";
  ptr += ".temperature-text{font-weight: 600;padding-left: 15px;font-size: 19px;width: 160px;text-align: left;}\n";
  ptr += ".temperature{font-weight: 300;font-size: 60px;color: #f39c12;}\n";
  ptr += ".superscript{font-size: 17px;font-weight: 600;position: absolute;right: -20px;top: 15px;}\n";
  ptr += ".data{padding: 10px;}\n";
  ptr += ".button {display: block;width: auto;background-color: #1abc9c;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr += ".button-on {background-color: #1abc9c;}\n";
  ptr += ".button-on:active {background-color: #16a085;}\n";
  ptr += ".button-off {background-color: #34495e;}\n";
  ptr += ".button-off:active {background-color: #2c3e50;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";

  ptr += "<div>\n";

  ptr += "<h1>ESP8266 CAT FEEDER</h1>\n";

  ptr += "<div class=\"data\">\n";
  ptr += "<div class=\"side-by-side temperature-icon\">\n";
  ptr += "<svg version=\"1.1\" id=\"Layer_1\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" x=\"0px\" y=\"0px\"\n";
  ptr += "width=\"9.915px\" height=\"22px\" viewBox=\"0 0 9.915 22\" enable-background=\"new 0 0 9.915 22\" xml:space=\"preserve\">\n";
  ptr += "<path fill=\"#FFFFFF\" d=\"M3.498,0.53c0.377-0.331,0.877-0.501,1.374-0.527C5.697-0.04,6.522,0.421,6.924,1.142\n";
  ptr += "c0.237,0.399,0.315,0.871,0.311,1.33C7.229,5.856,7.245,9.24,7.227,12.625c1.019,0.539,1.855,1.424,2.301,2.491\n";
  ptr += "c0.491,1.163,0.518,2.514,0.062,3.693c-0.414,1.102-1.24,2.038-2.276,2.594c-1.056,0.583-2.331,0.743-3.501,0.463\n";
  ptr += "c-1.417-0.323-2.659-1.314-3.3-2.617C0.014,18.26-0.115,17.104,0.1,16.022c0.296-1.443,1.274-2.717,2.58-3.394\n";
  ptr += "c0.013-3.44,0-6.881,0.007-10.322C2.674,1.634,2.974,0.955,3.498,0.53z\"/>\n";
  ptr += "</svg>\n";
  ptr += "</div>\n";
  ptr += "<div class=\"side-by-side temperature-text\">Temperature</div>\n";
  ptr += "<div class=\"side-by-side temperature\">";
  ptr += "<div id=\"Temperature\">0</div>";
  ptr += "<span class=\"superscript\">&deg;C</span></div>\n";
  ptr += "</div>\n";

  ptr += "<div class=\"data\">\n";
  ptr += "<div class=\"side-by-side humidity-icon\">\n";
  ptr += "<svg version=\"1.1\" id=\"Layer_2\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" x=\"0px\" y=\"0px\"\n\"; width=\"12px\" height=\"17.955px\" viewBox=\"0 0 13 17.955\" enable-background=\"new 0 0 13 17.955\" xml:space=\"preserve\">\n";
  ptr += "<path fill=\"#FFFFFF\" d=\"M1.819,6.217C3.139,4.064,6.5,0,6.5,0s3.363,4.064,4.681,6.217c1.793,2.926,2.133,5.05,1.571,7.057\n";
  ptr += "c-0.438,1.574-2.264,4.681-6.252,4.681c-3.988,0-5.813-3.107-6.252-4.681C-0.313,11.267,0.026,9.143,1.819,6.217\"></path>\n";
  ptr += "</svg>\n";
  ptr += "</div>\n";
  ptr += "<div class=\"side-by-side humidity-text\">Humidity</div>\n";
  ptr += "<div class=\"side-by-side humidity\">";
  ptr += "<div id=\"Humidity\">0</div>";
  ptr += "<span class=\"superscript\">%</span></div>\n";
  ptr += "</div>\n";

  ptr += "<div class=\"data\">\n";
  ptr += "<div class=\"side-by-side humidity\">";
  ptr += "<div id=\"Time\">00:00:00</div>";
  ptr += "</div>\n";

  ptr += "<p>Press button to feed the cat</p><a class=\"button button-off\" href=\"/feed\">FEED</a>\n";

  ptr += "</div>\n";

  ptr += "<script>\n";
  ptr += "setInterval(loadDoc,1000);\n";
  ptr += "function loadDoc() {\n";
  ptr += "var xhttp = new XMLHttpRequest();\n";
  ptr += "xhttp.onreadystatechange = function() {\n";
  ptr += "if (this.readyState == 4 && this.status == 200) {\n";
  ptr += "let dht = JSON.parse(this.responseText);\n";
  ptr += "document.getElementById(\"Temperature\").innerHTML =dht.Temperature.toFixed(0);\n";
  ptr += "document.getElementById(\"Humidity\").innerHTML =dht.Humidity.toFixed(0);\n";
  ptr += "document.getElementById(\"Time\").innerHTML =dht.NTPClient;\n";
  ptr += "console.log(dht)}\n";
  ptr += "};\n";
  ptr += "xhttp.open(\"GET\", \"/dht\", true);\n";
  ptr += "xhttp.send();\n";
  ptr += "}\n";

  ptr += "</script>\n";
  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String txt = "Message arrived [";
  txt += topic;
  txt += "] ";
  console(txt);

  String buffer = "";
  for (int i = 0; i < length; i++) {
    buffer += (char)payload[i];
  }
  Serial.println(buffer);

  if (buffer == "feed") {
    feed();

    // } else if (buffer.indexOf(":") != -1) {  // feed at hh:mm
    //   int triggerH = buffer.substring(buffer.indexOf(":")).toInt();
    //   int triggerM = buffer.substring(buffer.indexOf(":") + 1, buffer.length()).toInt();

    //   txt = "Feed at ";
    //   txt += triggerH;
    //   txt += ":";
    //   txt += triggerM;
    //   console(txt);

    //   alarm = timeClient.getEpochTime() + (triggerH * 3600) + (triggerM * 60);
    //   int h = triggerH - timeClient.getHours();

  } else if (buffer[0] == 'q') {  // qte of food
    qteFood = buffer.substring(1, buffer.length()).toInt();

    txt = "qteFood = ";
    txt += qteFood;
    console(txt);
    mqttClient.publish(mqtt_topic("qteFood"), String(qteFood).c_str());
    EEPROM.write(adr_qteFood, qteFood);
    EEPROM.commit();

  } else if (buffer.toInt() != 0) {  // feed after x sec
    default_cycle = buffer.toInt();
    alarm = timeClient.getEpochTime() + default_cycle;

    txt = "Feed every ";
    txt += default_cycle;
    txt += "s";
    console(txt);

    EEPROM.write(adr_timer, default_cycle >> 8);
    EEPROM.write(adr_timer + 1, default_cycle);
    EEPROM.commit();
  }
}

void mqttIdle() {
  if (!mqttClient.loop()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect("CAT_Feeder", mqtt_username, mqtt_password)) {
      Serial.println("connected");
      mqttClient.publish(mqtt_topic("connect"), "connected");
      mqttClient.subscribe(mqtt_topic("cmd"));
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void feed() {
  myservo.write(open);
  delay(qteFood * 100);
  myservo.write(close);

  journal = "";
  journal += "Feed at ";
  journal += daysOfTheWeek[timeClient.getDay()];
  journal += " ";
  journal += timeClient.getFormattedTime();
  journal += " Qte: ";
  journal += String(qteFood);
  journal += "\r\n";
  mqttClient.publish(mqtt_topic("log"), journal.c_str());

  console("Feed now Qte: " + String(qteFood));
}

void console(String msg) {
  String text = "";
  text += timeClient.getFormattedTime();
  text += " ";
  text += msg;
  Serial.println(text);
}

int getQuality() {
  if (WiFi.status() != WL_CONNECTED)
    return -1;
  int dBm = WiFi.RSSI();
  if (dBm <= -100)
    return 0;
  if (dBm >= -50)
    return 100;
  return 2 * (dBm + 100);
}