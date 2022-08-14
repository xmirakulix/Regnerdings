#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "include/WiFiSecret.h"
#include "include/th_sensor.h"

#define NUMPORTS 5
const char m_CompileDate[] = __DATE__ " " __TIME__;

WiFiClient espClient;
PubSubClient client(espClient);

// D1 Pinout guide https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
// relay/switch config
byte m_Ports[NUMPORTS] = {D1, D2, D5, D6, D7};
bool m_SwitchState[NUMPORTS];

// temperature/humidity sensor config
byte m_i2cPorts[] = {D3, D4}; // SDA, SCL: GPIO0, GPIO2, must be HIGH during boot
unsigned long m_LastTempTransmit = 0;

void setup()
{
    Serial.begin(115200);

    sensor_init(m_i2cPorts[0], m_i2cPorts[1]);

    wifi_setup();
    mqtt_setup();

    txSwitchState();
    txTemperatureState();
}

void loop()
{
    if (!client.loop())
    {
        mqtt_setup();
    }

    if (millis() > m_LastTempTransmit + (60 * 1000))
    {
        txTemperatureState();
        m_LastTempTransmit = millis();
    }
}

void wifi_setup()
{
    // Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.hostname("Regnerdings");
    WiFi.begin(m_Ssid, m_Pass);

    Serial.println("Connecting to WiFi..");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(100);
    }

    Serial.printf("WiFi Connected: %s, RSSI: %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
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
        sprintf(cfgMsg + strlen(cfgMsg), "  \"unit_of_measurement\":  \"°C\",");
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
        sprintf(cfgMsg + strlen(cfgMsg), "  \"unit_of_measurement\":  \"%%\",");
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

        Serial.println("Config message sent, client state: " + String(client.state()));
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

// transmit temperature and humidity to HA
void txTemperatureState()
{
    String state = "";

    state = "{ \"temperature\" : " + String(read_temperature()) + ", \"humidity\" : " + String(read_humidity()) + " }";

    Serial.printf("Temp/Humid state: %s\n", state.c_str());
    client.publish("disc/sensor/Regnerdings/state", state.c_str(), false);
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

    Serial.printf("Switch state: %s\n", state.c_str());
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
        digitalWrite(m_Ports[port - 1], HIGH);
    }
    else
    {
        digitalWrite(m_Ports[port - 1], LOW);
    }

    m_SwitchState[port - 1] = state;
}

// parse port from topic
int parsePort(String msg)
{
    return msg.substring(msg.lastIndexOf("dings/P") + 7, msg.lastIndexOf("/")).toInt();
}