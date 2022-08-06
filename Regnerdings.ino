#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "include/WiFiSecret.h"

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi()
{
    // Connect to WiFi
    WiFi.begin(m_Ssid, m_Pass);

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Connecting to WiFi..");
        delay(100);
    }

    Serial.print("WiFi Connected: ");
    Serial.println(WiFi.localIP());
}

void setup_mqtt()
{
    client.setServer("homeassistant", 1883);
    client.setCallback(callback);

    // connect to MQTT broker
    if (!client.connected())
    {
        while (!client.connected())
        {
            Serial.println("Connecting to MQTT...");
            client.connect("Regnerdings", "mosquitto", "mosquitto");
            delay(100);
        }
        Serial.println("Connected to MQTT");

        client.subscribe("/home/test");

        client.publish("/home/data", "Hello World");
    }
}

void setup()
{
    Serial.begin(115200);
    setup_wifi();
    setup_mqtt();
}

void loop()
{
    if (!client.loop())
    {
        setup_mqtt();
    }
}

void callback(char *topic, byte *payload, unsigned int length)
{
    String msg;
    for (byte i = 0; i < length; i++)
    {
        char tmp = char(payload[i]);
        msg += tmp;
    }
    Serial.print("Message received, topic '");
    Serial.print(topic);
    Serial.print("': ");
    Serial.println(msg);
}