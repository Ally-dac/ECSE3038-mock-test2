#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHTesp.h>
#include <PubSubClient.h>
#include <cmath>
#include <cstdlib>
#include "secrets.h"

const int DHT_PIN = 15;
const int LED_PIN = 2;
const unsigned long READ_INTERVAL = 10000;

const char* API_URL = "http://172.183.49.30/readings";
const char* BROKER = "135.232.203.144";
const char* LIMIT_TOPIC = "ecse3038/alliyah/alarm/max";
const char* STATE_TOPIC = "ecse3038/alliyah/alarm/state";
const char* STATUS_TOPIC = "ecse3038/alliyah/status";

DHTesp dht;
WiFiClient mqttNetwork;
PubSubClient mqtt(mqttNetwork);

String mac;
String clientId;
float temperature = NAN;
float alarmLimit = NAN;
bool ledOn = false;
bool statePending = false;
unsigned long lastRead = 0;
unsigned long lastConnectAttempt = 0;

void updateAlarm() {
    if (!std::isfinite(temperature) || !std::isfinite(alarmLimit)) return;

    bool shouldBeOn = temperature > alarmLimit;
    if (shouldBeOn != ledOn) {
        ledOn = shouldBeOn;
        digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
        statePending = true;
        Serial.println(ledOn ? "LED: on" : "LED: off");
    }
}

void receiveLimit(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, LIMIT_TOPIC) != 0) return;

    String message;
    for (unsigned int i = 0; i < length; i++) {
        if (payload[i] == 0) return;
        message += static_cast<char>(payload[i]);
    }
    message.trim();

    char* end;
    const char* start = message.c_str();
    float value = strtof(start, &end);
    if (end == start || *end != '\0' || !std::isfinite(value)) {
        Serial.println("Ignored invalid alarm limit");
        return;
    }

    alarmLimit = value;
    Serial.printf("Alarm limit: %.1f C\n", alarmLimit);
    updateAlarm();
}

void connectMqtt() {
    // The broker publishes retained "offline" if the board disconnects unexpectedly.
    if (mqtt.connect(clientId.c_str(), STATUS_TOPIC, 1, true, "offline")) {
        mqtt.publish(STATUS_TOPIC, "online", true);
        mqtt.subscribe(LIMIT_TOPIC);
        Serial.println("MQTT connected");
    }
}

void sendReading() {
    float sample = dht.getTemperature();
    if (!std::isfinite(sample)) {
        Serial.println("DHT22 reading failed");
        return;
    }

    temperature = sample;
    updateAlarm();

    WiFiClient apiNetwork;
    HTTPClient http;
    if (!http.begin(apiNetwork, API_URL)) return;

    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", DEVICE_KEY);

    String body = "{\"mac\":\"" + mac + "\",\"temp\":" +
                  String(temperature, 1) + "}";
    int code = http.POST(body);
    Serial.printf("Temperature: %.1f C | POST /readings: %d\n", temperature, code);
    http.end();
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    dht.setup(DHT_PIN, DHTesp::DHT22);

    WiFi.mode(WIFI_STA);
    WiFi.begin("Wokwi-GUEST", "", 6);
    while (WiFi.status() != WL_CONNECTED) delay(100);

    mac = WiFi.macAddress();
    clientId = "alliyah-coldroom-" + mac;
    Serial.println("Board MAC: " + mac);

    mqtt.setServer(BROKER, 1883);
    mqtt.setCallback(receiveLimit);
    mqtt.setKeepAlive(15);
    connectMqtt();
    lastConnectAttempt = millis();
}

void loop() {
    unsigned long now = millis();

    if (!mqtt.connected() && now - lastConnectAttempt >= 3000) {
        lastConnectAttempt = now;
        connectMqtt();
    }
    mqtt.loop();

    if (now - lastRead >= READ_INTERVAL) {
        lastRead = now;
        sendReading();
    }

    if (statePending && mqtt.connected()) {
        if (mqtt.publish(STATE_TOPIC, ledOn ? "on" : "off", true)) {
            statePending = false;
        }
    }
}