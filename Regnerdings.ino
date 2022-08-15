#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "include/WiFiSecret.h"
#include "include/lcd.h"
#include "include/th_sensor.h"

#define NUMPORTS 5
const char m_CompileDate[] = __DATE__ " " __TIME__;

WiFiClient espClient;
PubSubClient client(espClient);

// D1 Pinout guide https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
// relay/switch config
const byte m_Ports[NUMPORTS] = {D1, D2, D5, D6, D7};
bool m_SwitchState[NUMPORTS];
const unsigned long m_MaxOnDuration = 3600 * 1000; // max one hour
unsigned long m_LastOnTime[NUMPORTS];

// temperature/humidity sensor config
const byte m_i2cPorts[] = {D3, D4}; // SDA, SCL: GPIO0, GPIO2, must be HIGH during boot
unsigned long m_LastTempTransmit = 0;

void setup()
{
    Serial.begin(115200);

    sensor_init(m_i2cPorts[0], m_i2cPorts[1]);
    setupLcd();

    wifi_setup();
    mqtt_setup();

    txSwitchState();
    txTemperatureState();
}

void loop()
{
    handleLcd();

    if (!client.loop())
    {
        mqtt_setup();
    }

    // transmit temperature and humidity
    if (millis() > m_LastTempTransmit + (60 * 1000))
    {
        txTemperatureState();
        m_LastTempTransmit = millis();
    }

    // safeguard max on duration
    for (int i = 1; i <= NUMPORTS; i++)
    {
        if ((getSwitch(i) == "ON") && (millis() > (m_LastOnTime[i - 1] + m_MaxOnDuration)))
        {
            setSwitch(i, false);
            Serial.printf("Emergency shutoff on port: %d!\n", i);
        }
    }
}

void wifi_setup()
{
    // Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.hostname("Regnerdings");
    WiFi.begin(m_Ssid, m_Pass);

    Serial.println("Connecting to WiFi...");
    m_Lcd.print("WiFi connecting");
    m_Lcd.setCursor(0, 1);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(100);
    }

    Serial.printf("WiFi Connected: %s, RSSI: %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    m_Lcd.print("WiFi Connected!");
    delay(2000);
    m_Lcd.clear();
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
            m_Lcd.print("MQTT connecting");
            m_Lcd.setCursor(0, 1);
            client.connect("Regnerdings", "mosquitto", "mosquitto", "/home/state", 1, true, "offline");
            delay(100);
        }
        Serial.println("Connected to MQTT");
        m_Lcd.print("MQTT connected!");
        delay(2000);
        m_Lcd.clear();

        char topic[100];
        char cfgMsg[1024];
        m_Lcd.print("HA config...");
        m_Lcd.setCursor(0, 1);

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
            setSwitch(i, false);
        }

        Serial.println("Config message sent, client state: " + String(client.state()));
        m_Lcd.print("HA config sent!");
        delay(2000);
        m_Lcd.clear();
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
        setSwitch(port, true);
    }
    else if (strcmp(msg, "OFF") == 0)
    {
        setSwitch(port, false);
    }
    else
    {
        Serial.printf("Unknown message: %s\n", msg);
    }
}

// transmit temperature and humidity to HA
void txTemperatureState()
{
    String mqtt_state = "";
    char lcd_state[17];

    int temperature = read_temperature();
    int humidity = read_humidity();

    mqtt_state = "{ \"temperature\" : " + String(temperature) + ", \"humidity\" : " + String(humidity) + " }";
    sprintf(lcd_state, "  T:%d%cC H:%d%%", temperature, 0xDF, humidity);

    Serial.printf("Temp/Humid state: %s\n", mqtt_state.c_str());

    m_Lcd.setCursor(0, 0);
    m_Lcd.write(lcd_state);
    // enableLcdBacklight();

    client.publish("disc/sensor/Regnerdings/state", mqtt_state.c_str(), false);
}

// transmit state of all switches to HA
void txSwitchState()
{
    String mqtt_state = "{";
    String lcd_state = "Ports: ";

    for (int i = 1; i <= NUMPORTS; i++)
    {
        mqtt_state += "\"P" + String(i) + "\" : \"" + getSwitch(i) + "\", ";
        lcd_state += (getSwitch(i) == "ON" ? "> " : "- ");
    }
    mqtt_state = mqtt_state.substring(0, mqtt_state.length() - 2);
    mqtt_state += "}";

    Serial.printf("Switch state: %s\n", mqtt_state.c_str());

    m_Lcd.setCursor(0, 1);
    m_Lcd.write(lcd_state.c_str());
    enableLcdBacklight();

    client.publish("disc/switch/Regnerdings/state", mqtt_state.c_str(), false);
}

// get the state of a switch
String getSwitch(int port)
{
    return m_SwitchState[port - 1] ? "ON" : "OFF";
}

// operate the switch on the given port
void setSwitch(int port, bool state)
{
    if (state)
    {
        digitalWrite(m_Ports[port - 1], HIGH); // ON
        m_LastOnTime[port - 1] = millis();
    }
    else
    {
        digitalWrite(m_Ports[port - 1], LOW); // OFF
        m_LastOnTime[port - 1] = 0;
    }

    m_SwitchState[port - 1] = state;
    txSwitchState();
}

// parse port from topic
int parsePort(String msg)
{
    return msg.substring(msg.lastIndexOf("dings/P") + 7, msg.lastIndexOf("/")).toInt();
}