#include "DHT.h"                   // Library for DHT sensors
#include <WiFi.h>                  // Library for WiFi functionality
#include <PubSubClient.h>          // Library for MQTT functionality
#include "LiquidCrystal_I2C.h"     // Library for I2C LCD
#include "Wire.h"                  // Library for I2C communication
#include <WiFiClientSecure.h>      // Library for secure WiFi client
#include <MFRC522.h>               // Library for RFID functionality
#include <afstandssensor.h>        // Custom library for distance sensor
#include <UniversalTelegramBot.h>  // Library for Telegram bot
#include <ArduinoJson.h>           // Library for JSON parsing
#include <SPI.h>                   // Library for SPI communication
#include <OneWire.h>               // Library for 1-Wire communication
#include <DallasTemperature.h>     // Library for Dallas Temperature sensors
#include <Preferences.h>           // Library for storing preferences
//Pinnen voor actuatoren / sensoren, DHT type
#define VentilTemp 25
#define VentilHum 33
#define Pomp 12
#define Verwarming 13
#define Lamp 26
#define ldrPin 35
#define bvPin 34
#define btPin 14
#define RST_PIN 32
#define SS_PIN 5
#define DHTPIN 4
#define DHTTYPE DHT11

#define CHAT_ID "..."
#define BOTtoken "..."
WiFiClientSecure client;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

const char* ssid = "...";
const char* password = "...";

const char* mqtt_server = "...";
const int mqtt_port = 1883;
const char* MQTT_USER = "...";
const char* MQTT_PASSWORD = "...";
const char* MQTT_CLIENT_ID = "MQTTclient";
//Waarden
const char* MQTT_TOPICtemp = "serre/waarden/temperature";
const char* MQTT_TOPIChum = "serre/waarden/humidity";
const char* MQTT_TOPIClight = "serre/waarden/light";
const char* MQTT_TOPICbv = "serre/waarden/bodemvocht";
const char* MQTT_TOPICbt = "serre/waarden/bodemtemp";
const char* MQTT_TOPICWater = "serre/waarden/waterres";
//Actuatoren
const char* MQTT_TOPICVentilTempTijd = "serre/actuatoren/VentilTempDraaitijd";
const char* MQTT_TOPICVentilHumTijd = "serre/actuatoren/VentilHumDraaitijd";
const char* MQTT_TOPICPompTijd = "serre/actuatoren/PompDraaitijd";
const char* MQTT_TOPICLichtTijd = "serre/actuatoren/LampDraaitijd";
const char* MQTT_TOPICVerwTijd = "serre/actuatoren/VerwDraaitijd";
//Parameters
const char* MQTT_TOPICParameterMaxTemp = "serre/parameters/MaxTemp";
const char* MQTT_TOPICParameterMinTemp = "serre/parameters/MinTemp";
const char* MQTT_TOPICParameterMaxHum = "serre/parameters/MaxHum";
const char* MQTT_TOPICParameterMinLight = "serre/parameters/MinLight";
const char* MQTT_TOPICParameterMinBv = "serre/parameters/MinBv";

const char* MQTT_TOPICCommunication = "serre/communication/communication";
const char* MQTT_REGEX = "serre/([^/]+)/([^/]+)";

LiquidCrystal_I2C lcd(0x27, 16, 4);
DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(btPin);
DallasTemperature sensors(&oneWire);
AfstandsSensor afstandssensor(17, 16);
UniversalTelegramBot bot(BOTtoken, client);
MFRC522 mfrc522(SS_PIN, RST_PIN);

unsigned long lastDisplayUpdateTime = 0;
const unsigned long displayUpdateInterval = 5000;  // Interval van 5 seconden voor het bijwerken van het display
const unsigned long waterUpdateInterval = 10000;
unsigned long lastWaardenUpdateTime = 0;
const unsigned long waardenUpdateInterval = 10000;  // Interval van 5 seconden voor het bijwerken van het display
unsigned long lastParametersUpdateTime = 0;
const unsigned long parametersUpdateInterval = 10000;
unsigned long lastActuatorsUpdateTime = 0;
const unsigned long actuatorsUpdateInterval = 3000;
unsigned long lastCommunicationUpdateTime = 0;
const unsigned long communicationUpdateInterval = 5000;
int botRequestDelay = 1000;
unsigned long lastTimeBotRan;
bool newRFIDScan = false;
bool displaySensorValues = true;

int maxTemp = 24;        // Maximale temperatuur standaard instelling
int minTemp = 18;        // Minimale temperatuur standaard instelling
int maxHum = 60;         // Maximale luchtvochtigheid standaard instelling
int minLight = 50;       // Minimale lichtintensiteit standaard instelling
int minBodemvocht = 30;  // Minimale bodemvochtigheid standaard instelling

float water;
TaskHandle_t Task1;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Verbinding maken met WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi verbonden");
  Serial.println("IP adres: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Verbinding maken met MQTT-broker...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Verbinden met");
    lcd.setCursor(0, 1);
    lcd.print("MQTT broker...");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println(" verbonden");
      mqttClient.subscribe(MQTT_TOPICCommunication);

    } else {
      Serial.print(" mislukt, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" opnieuw proberen in 5 seconden");
      delay(5000);
    }
  }
}

void clientPubTask(void* pvParameters) {
  while (true) {
    if (!mqttClient.connected()) {
      reconnect();
    }
    sensors.requestTemperatures();
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    float l = map(analogRead(ldrPin), 0, 4096, 100, 0);
    float b = map(analogRead(bvPin), 0, 4096, 100, 0);
    float bt = sensors.getTempCByIndex(0);

    unsigned long currentTime1 = millis();
    if (currentTime1 - lastActuatorsUpdateTime >= actuatorsUpdateInterval) {
      if (t >= maxTemp) {
        digitalWrite(VentilTemp, HIGH);
        digitalWrite(Verwarming, LOW);
        mqttClient.publish(MQTT_TOPICVentilTempTijd, static_cast<const char*>("1"));
        mqttClient.publish(MQTT_TOPICVerwTijd, static_cast<const char*>("0"));
      } else if (t >= minTemp && t < maxTemp) {
        digitalWrite(VentilTemp, LOW);
        digitalWrite(Verwarming, LOW);
        mqttClient.publish(MQTT_TOPICVentilTempTijd, static_cast<const char*>("0"));
        mqttClient.publish(MQTT_TOPICVerwTijd, static_cast<const char*>("0"));

      } else if (t < minTemp) {
        digitalWrite(Verwarming, HIGH);
        digitalWrite(VentilTemp, LOW);
        mqttClient.publish(MQTT_TOPICVerwTijd, static_cast<const char*>("1"));
        mqttClient.publish(MQTT_TOPICVentilTempTijd, static_cast<const char*>("0"));
      }
      if (h >= maxHum) {
        digitalWrite(VentilHum, HIGH);
        mqttClient.publish(MQTT_TOPICVentilHumTijd, static_cast<const char*>("1"));
      } else {
        digitalWrite(VentilHum, LOW);
        mqttClient.publish(MQTT_TOPICVentilHumTijd, static_cast<const char*>("0"));
      }
      if (l <= minLight) {
        digitalWrite(Lamp, HIGH);
        mqttClient.publish(MQTT_TOPICLichtTijd, static_cast<const char*>("1"));
      } else {
        digitalWrite(Lamp, LOW);
        mqttClient.publish(MQTT_TOPICLichtTijd, static_cast<const char*>("0"));
      }
      if (b <= minBodemvocht) {
        digitalWrite(Pomp, HIGH);
        mqttClient.publish(MQTT_TOPICPompTijd, static_cast<const char*>("1"));
      } else {
        digitalWrite(Pomp, LOW);
        mqttClient.publish(MQTT_TOPICPompTijd, static_cast<const char*>("0"));
      }
      Serial.println("status verzonden");
      lastActuatorsUpdateTime = currentTime1;
    }
    unsigned long currentTime2 = millis();
    if (currentTime2 - lastWaardenUpdateTime >= waardenUpdateInterval) {
      String ts = String(t);
      String hs = String(h);
      String ls = String(l);
      String bs = String(b);
      String bts = String(bt);
      mqttClient.publish(MQTT_TOPICtemp, ts.c_str());
      mqttClient.publish(MQTT_TOPIChum, hs.c_str());
      mqttClient.publish(MQTT_TOPIClight, ls.c_str());
      mqttClient.publish(MQTT_TOPICbv, bs.c_str());
      mqttClient.publish(MQTT_TOPICbt, bts.c_str());
      //Serial.println("waarden verzonden");
      lastWaardenUpdateTime = currentTime2;
    }

    unsigned long currentTime3 = millis();
    if (currentTime3 - lastParametersUpdateTime >= parametersUpdateInterval) {
      sendParameters();
      lastParametersUpdateTime = currentTime3;
    }

    if (displaySensorValues) {
      lcd.setCursor(0, 0);
      lcd.print("Omgevingsinfo:");
      lcd.setCursor(0, 1);
      lcd.print("Temp.:");
      lcd.print(t);
      lcd.print(" C");
      lcd.setCursor(-4, 2);
      lcd.print("Hum. :");
      lcd.print(h);
      lcd.print("%");
      lcd.setCursor(-4, 3);
      lcd.print("Licht:");
      lcd.print(l);
      lcd.print("%");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Bodeminfo:");
      lcd.setCursor(0, 1);
      lcd.print("BodemV.:");
      lcd.print(b);
      lcd.print("%");
      lcd.setCursor(-4, 2);
      lcd.print("BodemT.:");
      lcd.print(bt);
      lcd.print(" C");
      lcd.setCursor(-4, 3);
    }

    unsigned long currentTime4 = millis();
    if (currentTime4 - lastDisplayUpdateTime >= displayUpdateInterval) {
      lastDisplayUpdateTime = currentTime4;
      displaySensorValues = !displaySensorValues;
      lcd.clear();
    }

    if (newRFIDScan) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("New Parameters ");
      lcd.setCursor(0, 1);
      lcd.print("MaxTemp = ");
      lcd.print(maxTemp);
      lcd.print(" C");
      lcd.setCursor(-4, 2);
      lcd.print("MinTemp = ");
      lcd.print(minTemp);
      lcd.print(" C");
      lcd.setCursor(-4, 3);
      lcd.print("MaxHum  = ");
      lcd.print(maxHum);
      lcd.print(" %");
      delay(5000);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("New Parameters ");
      lcd.setCursor(0, 1);
      lcd.print("MinLicht   = ");
      lcd.print(minLight);
      lcd.print("%");
      lcd.setCursor(-4, 2);
      lcd.print("MinBodemV. = ");
      lcd.print(minBodemvocht);
      lcd.print("%");
      delay(5000);
      newRFIDScan = false;  // Reset de nieuwe RFID-scan status
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  unsigned long currentTime1 = millis();
  if (currentTime1 - lastCommunicationUpdateTime >= communicationUpdateInterval) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.print("Message: ");
    String bericht;
    for (int i = 0; i < length; i++) {
      bericht += ((char)payload[i]);
    }
    if (String(topic) == MQTT_TOPICCommunication) {
      if (bericht == "hallo") {
        Serial.println(bericht);
      }
    }
    lastCommunicationUpdateTime = currentTime1;
  }
}


void setup() {
  Serial.begin(9600);
  pinMode(25, OUTPUT);
  pinMode(33, OUTPUT);
  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(26, OUTPUT);
  pinMode(27, OUTPUT);
  pinMode(ldrPin, INPUT);
  pinMode(bvPin, INPUT);
  pinMode(btPin, INPUT);

  dht.begin();
  setup_wifi();
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callback);
  reconnect();

  lcd.init();
  lcd.backlight();
  Wire.begin(SDA, SCL);
  SPI.begin();
  mfrc522.PCD_Init();
  sensors.begin();
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  preferences.begin("serre", false);  // Beginnen met de voorkeuren met de naam "serre"
  // Lezen van opgeslagen waarden of standaardwaarden gebruiken als er geen opgeslagen waarden zijn
  maxTemp = preferences.getInt("maxTemp", 24);
  minTemp = preferences.getInt("minTemp", 18);
  maxHum = preferences.getInt("maxHum", 60);
  minLight = preferences.getInt("minLight", 50);
  minBodemvocht = preferences.getInt("minBodemvocht", 30);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Verbonden met");
  lcd.setCursor(0, 1);
  lcd.print("MQTT broker");
  delay(3000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Used Parameters ");
  lcd.setCursor(0, 1);
  lcd.print("MaxTemp = ");
  lcd.print(maxTemp);
  lcd.print(" C");
  lcd.setCursor(-4, 2);
  lcd.print("MinTemp = ");
  lcd.print(minTemp);
  lcd.print(" C");
  lcd.setCursor(-4, 3);
  lcd.print("MaxHum  = ");
  lcd.print(maxHum);
  lcd.print(" %");
  delay(5000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Used Parameters ");
  lcd.setCursor(0, 1);
  lcd.print("MinLicht   = ");
  lcd.print(minLight);
  lcd.print("%");
  lcd.setCursor(-4, 2);
  lcd.print("MinBodemV. = ");
  lcd.print(minBodemvocht);
  lcd.print("%");
  delay(5000);

  xTaskCreatePinnedToCore(
    clientPubTask,
    "clientPubTask",
    10000,
    NULL,
    1,
    NULL,
    0);
}

void RFIDtagLezen() {
  static String Tag = "";  // Variabele om de vorige RFID-tag op te slaan
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  // Select one of the cards
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }
  Serial.print("UID tag :");
  String content = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
    content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""));
    content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  Serial.println();
  Serial.print("Message : ");
  content.toUpperCase();
  Serial.println(content.substring(1));

  Tag = content.substring(1);

  if (Tag == "1CD9A1D") {
    // Parameters voor blauwe RFID-tag
    maxTemp = 26;
    minTemp = 20;
    maxHum = 75;
    minLight = 60;
    minBodemvocht = 35;
  } else if (Tag == "3149DE7") {
    // Parameters voor witte RFID-tag
    maxTemp = 22;
    minTemp = 16;
    maxHum = 65;
    minLight = 40;
    minBodemvocht = 30;
  }
  preferences.putInt("maxTemp", maxTemp);
  preferences.putInt("minTemp", minTemp);
  preferences.putInt("maxHum", maxHum);
  preferences.putInt("minLight", minLight);
  preferences.putInt("minBodemvocht", minBodemvocht);
  sendParameters();
  newRFIDScan = true;
  tone(27, 1000);
  delay(1000);
  noTone(27);
}

String getWater() {
  water = afstandssensor.afstandCM();
  Serial.print("Afstand: ");
  Serial.print(water);
  Serial.println("cm");
  String message = "Afstand: " + String(water) + "cm \n";
  return message;
  bot.sendMessage(CHAT_ID, message, "");
}

void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }
    String text = bot.messages[i].text;
    Serial.println(text);
    String from_name = bot.messages[i].from_name;
    if (text == "/start") {
      String welcome = "Welcome, " + from_name + ".\n";
      welcome += "Use the following command to get current readings.\n\n";
      welcome += "/readings \n";
      bot.sendMessage(chat_id, welcome, "");
    }
    if (text == "/readings") {
      String readings = getWater();
      bot.sendMessage(chat_id, readings, "");
    }
  }
}

void waterReservoir() {
  static unsigned long lastWaterUpdate = 0;
  unsigned long currentTime = millis();
  // Controleer of er 10 seconden verstreken zijn sinds de laatste keer dat de waarde 'water' is verzonden
  if (currentTime - lastWaterUpdate >= waterUpdateInterval) {
    float water = afstandssensor.afstandCM();
    // Verstuur de waarde 'water' als deze meer dan 10 cm is
    if (water > 5 && water < 400) {
      String waters = String(water);
      Serial.println(water);
      mqttClient.publish(MQTT_TOPICWater, waters.c_str());
      lastWaterUpdate = currentTime;
      if (water < 10) {
        String alertMessage = "Waarschuwing: Waterreservoir is bijna leeg!";
        bot.sendMessage(CHAT_ID, alertMessage, "");
        lastWaterUpdate = currentTime;  // Update de tijd van de laatste verzending
      }
      // Handel de bot-updates af zoals eerder
      if (millis() > lastTimeBotRan + botRequestDelay) {
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        while (numNewMessages) {
          Serial.println("got response");
          handleNewMessages(numNewMessages);
          numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        }
        lastTimeBotRan = millis();
      }
    }
  }
}

void sendParameters() {
  String maxTemps = String(maxTemp);
  String minTemps = String(minTemp);
  String maxHums = String(maxHum);
  String minLights = String(minLight);
  String minBodemvochts = String(minBodemvocht);
  mqttClient.publish(MQTT_TOPICParameterMaxTemp, maxTemps.c_str());
  mqttClient.publish(MQTT_TOPICParameterMinTemp, minTemps.c_str());
  mqttClient.publish(MQTT_TOPICParameterMaxHum, maxHums.c_str());
  mqttClient.publish(MQTT_TOPICParameterMinLight, minLights.c_str());
  mqttClient.publish(MQTT_TOPICParameterMinBv, minBodemvochts.c_str());
  Serial.println("parameters verzonden");
}

void loop() {
  RFIDtagLezen();
  waterReservoir();
  mqttClient.loop();
}
