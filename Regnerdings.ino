#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "include/WiFiSecret.h"

WiFiClient espClient;
PubSubClient client(espClient);

#define NUMPORTS 5
byte m_Ports[NUMPORTS] = {D0, D5, D6, D7, D8};

const char m_CompileDate[] = __DATE__ " " __TIME__;
bool m_SwitchState[NUMPORTS];

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

    wifi_setup();
    mqtt_setup();
}

void loop()
{
    if (!client.loop())
    {
        mqtt_setup();
    }
}

void wifi_setup()
{
    // Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.hostname("Regnerdings");
    WiFi.begin(m_Ssid, m_Pass);

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Connecting to WiFi..");
        delay(100);
    }

    Serial.printf("WiFi Connected: %s, RSSI: %d\n", WiFi.localIP().toString(), WiFi.RSSI());
}

void mqtt_setup()
{
    client.setServer("homeassistant", 1883);
    client.setCallback(mqtt_callback);
    client.setBufferSize(1024);

    // connect to MQTT broker
    if (!client.connected())
    {
        while (!client.connected())
        {
            Serial.println("Connecting to MQTT...");
            client.connect("Regnerdings", "mosquitto", "mosquitto", "/home/state", 1, true, "offline");
            delay(100);
        }
        Serial.println("Connected to MQTT");

        char topic[100];
        char cfgMsg[1024];

        // configure device temp
        strcpy(topic, "disc/sensor/Regnerdings/T/config");
        strcpy(cfgMsg, "{");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"name\":          \"Temperature\",");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"unique_id\":     \"Temperature\",");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"device\":        { \"ids\" : [\"Regnerdings\"], \"name\" : \"Regnerdings\", \"mf\": \"xmirakulix\", \"mdl\": \"Regnerdings\", \"sw\": \"%s\" },", m_CompileDate);
        sprintf(cfgMsg + strlen(cfgMsg), "  \"device_class\":  \"temperature\",");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"value_template\":\"{{ value_json.temperature}}\",");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"state_topic\":   \"disc/sensor/Regnerdings/state\"");
        sprintf(cfgMsg + strlen(cfgMsg), "}");

        Serial.println("Sending temperature config message to HA");
        client.publish(topic, cfgMsg, true);

        // configure device humidity
        strcpy(topic, "disc/sensor/Regnerdings/H/config");
        strcpy(cfgMsg, "{");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"name\":          \"Humidity\",");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"unique_id\":     \"Humidity\",");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"device\":        { \"identifiers\" : [\"Regnerdings\"], \"name\" : \"Regnerdings\" },");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"device_class\":  \"humidity\",");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"value_template\":\"{{ value_json.humidity}}\",");
        sprintf(cfgMsg + strlen(cfgMsg), "  \"state_topic\":   \"disc/sensor/Regnerdings/state\"");
        sprintf(cfgMsg + strlen(cfgMsg), "}");

        Serial.println("Sending humidity config message to HA");
        client.publish(topic, cfgMsg, true);

        for (int i = 1; i <= NUMPORTS; i++)
        {
            char cmd_topic[50];

            // configure Ports
            sprintf(topic, "disc/switch/Regnerdings/P%d/config", i);
            sprintf(cmd_topic, "disc/switch/Regnerdings/P%d/set", i);

            strcpy(cfgMsg, "{");
            sprintf(cfgMsg + strlen(cfgMsg), "  \"name\":          \"Regnerdings P%d\",", i);
            sprintf(cfgMsg + strlen(cfgMsg), "  \"unique_id\":     \"Regnerdings_P%d\",", i);
            sprintf(cfgMsg + strlen(cfgMsg), "  \"device\":        { \"identifiers\" : [\"Regnerdings\"], \"name\" : \"Regnerdings\" },");
            sprintf(cfgMsg + strlen(cfgMsg), "  \"device_class\":  \"switch\",");
            sprintf(cfgMsg + strlen(cfgMsg), "  \"icon\":          \"mdi:sprinkler\",");
            sprintf(cfgMsg + strlen(cfgMsg), "  \"value_template\":\"{{ value_json.P%d}}\",", i);
            sprintf(cfgMsg + strlen(cfgMsg), "  \"state_topic\":   \"disc/switch/Regnerdings/state\",");
            sprintf(cfgMsg + strlen(cfgMsg), "  \"command_topic\": \"%s\"", cmd_topic);
            sprintf(cfgMsg + strlen(cfgMsg), "}");

            Serial.printf("Sending P%d config message to HA\n", i);
            client.publish(topic, cfgMsg, true);
            client.subscribe(cmd_topic);

            pinMode(m_Ports[i - 1], OUTPUT);
            setState(i, false);
        }

        txSwitchState();
        Serial.println("Config message sent, state: " + String(client.state()));
    }
}

void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
    char msg[50] = "";
    for (byte i = 0; i < length; i++)
    {
        msg[i] = (char)payload[i];
    }

    int port = parsePort(topic);
    Serial.printf("Message received on port '%d': %s\n", port, msg);

    if (strcmp(msg, "ON") == 0)
    {
        setState(port, true);
    }
    else if (strcmp(msg, "OFF") == 0)
    {
        setState(port, false);
    }
    else
    {
        Serial.printf("Unknown message: %s\n", msg);
    }
}

// transmit state of all switches to HA
void txSwitchState()
{
    String state = "{";

    for (int i = 1; i <= NUMPORTS; i++)
    {
        state += "\"P" + String(i) + "\" : \"" + getSwitch(i) + "\", ";
    }
    state = state.substring(0, state.length() - 2);
    state += "}";

    Serial.printf("State: %s\n", state.c_str());
    client.publish("disc/switch/Regnerdings/state", state.c_str(), false);
}

// get the state of a switch
String getSwitch(int port)
{
    return m_SwitchState[port - 1] ? "ON" : "OFF";
}

// operate the switch on the given port
void setState(int port, bool state)
{
    if (state)
    {
        digitalWrite(LED_BUILTIN, LOW);
        digitalWrite(m_Ports[port - 1], HIGH);
    }
    else
    {
        digitalWrite(LED_BUILTIN, HIGH);
        digitalWrite(m_Ports[port - 1], LOW);
    }

    m_SwitchState[port - 1] = state;
}

// parse port from topic
int parsePort(String msg)
{
    return msg.substring(msg.lastIndexOf("dings/P") + 7, msg.lastIndexOf("/")).toInt();
}