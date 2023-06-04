#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "include/WiFiSecret.h"
#include "include/lcd.h"
#include "include/th_sensor.h"

#define NUMPORTS 5
const char m_CompileDate[] = __DATE__ " " __TIME__;

WiFiClient espClient;
PubSubClient client(espClient);

// activity LED config
const byte m_ActivityLed = D8;

// D1 Pinout guide https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
// relay/switch config
const byte m_Ports[NUMPORTS] = {D1, D2, D5, D6, D7};
bool m_SwitchState[NUMPORTS];
const unsigned long m_MaxOnDuration = 3600 * 1000; // max one hour
unsigned long m_LastOnTime[NUMPORTS];

// temperature/humidity sensor config
const byte m_i2cPorts[] = {D3, D4}; // SDA, SCL: GPIO0, GPIO2, must be HIGH during boot
unsigned long m_LastTempTransmit = 0;
int m_LastTemperature = 0;
int m_LastHumidity = 0;

// button config
const byte m_Button = D0;
unsigned long m_LastButtonPressed = 0;
bool m_ButtonPressed = false;
enum buttonAction
{
    HANDLED = -1,
    NONE = 0,
    SHORT = 1,
    LONG = 2
};
int m_LastButtonAction = NONE;

// screen config
unsigned long m_LastScreenUpdate = 0;
int m_EditPort = 0;

void setup()
{
    Serial.begin(115200);

    sensor_init(m_i2cPorts[0], m_i2cPorts[1]);
    setupLcd();
    button_setup();
    activityLed_setup();

    wifi_setup();
    mqtt_setup();

    txSwitchState();
    txTemperatureState();

    Serial.println("Setup complete, starting loop...");
}

void loop()
{
    handleScreen();
    handleButton();

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

void button_setup()
{
    pinMode(m_Button, INPUT);
}

void activityLed_setup()
{
    pinMode(m_ActivityLed, OUTPUT);
    digitalWrite(m_ActivityLed, LOW);
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

    Serial.printf("WiFi connected: %s, RSSI: %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    m_Lcd.print("WiFi connected!");
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
        enableLcdBacklight();
        m_Lcd.print("HA config sent!");
        delay(2000);
        m_Lcd.clear();
    }
}

void handleScreen()
{
    handleLcdBacklight();

    if (m_LastButtonAction == SHORT)
    {
        m_LastButtonAction = HANDLED;
        enableLcdBacklight();

        if (++m_EditPort > NUMPORTS)
        {
            m_EditPort = 1;
        }
        Serial.printf("Screen handling SHORT, port: %d\n", m_EditPort);
        m_LastScreenUpdate = 0;
    }
    else if (m_LastButtonAction == LONG)
    {
        m_LastButtonAction = HANDLED;
        enableLcdBacklight();

        if (m_EditPort > 0)
        {
            if (getSwitch(m_EditPort) == "ON")
            {
                setSwitch(m_EditPort, false);
            }
            else
            {
                setSwitch(m_EditPort, true);
            }
        }
        Serial.printf("Screen handling LONG, port %d: %s\n", m_EditPort, getSwitch(m_EditPort).c_str());
        m_LastScreenUpdate = 0;
    }

    if ((m_EditPort != 0) && (millis() > (m_LastButtonPressed + 10 * 1000)))
    {
        m_EditPort = 0;
    }

    if (millis() > (m_LastScreenUpdate + 300))
    {
        m_LastScreenUpdate = millis();
        char line[17];
        // m_Lcd.clear();
        m_Lcd.setCursor(0, 0);

        sprintf(line, "  T:%d%cC H:%d%% ", m_LastTemperature, 0xDF, m_LastHumidity);
        m_Lcd.print(line);

        m_Lcd.setCursor(0, 1);
        strcpy(line, "Ports:");

        for (int i = 1; i <= NUMPORTS; i++)
        {
            bool act = getSwitch(i) == "ON" ? true : false;
            strcat(line, " ");

            if (m_EditPort == i)
            {
                enableLcdBacklight();

                char c = getEditChar();

                if (c == 'x')
                {
                    sprintf(line + strlen(line), "%c", act ? '>' : '-');
                }
                else
                {
                    sprintf(line + strlen(line), "%c", c);
                }
            }
            else
            {
                sprintf(line + strlen(line), "%c", act ? '>' : '-');
            }
        }
        m_Lcd.print(line);
    }
}

void handleButton()
{
    // still unhandled button press active
    if (m_LastButtonAction > 0)
    {
        return;
    }

    int newState = digitalRead(m_Button);

    // new button press detected
    if (!m_ButtonPressed && (newState == LOW))
    {
        m_LastButtonPressed = millis();
        m_ButtonPressed = true;
        Serial.println("Button pressed");
        return;
    }

    // button no longer presssed and was short
    if (m_ButtonPressed && (newState == HIGH) && ((millis() - m_LastButtonPressed) < 500) && (m_LastButtonAction == NONE))
    {
        m_LastButtonAction = SHORT;
        m_ButtonPressed = false;
        Serial.println("Button action short detected");
    }
    // button still pressed, but already long
    else if (m_ButtonPressed && (millis() > (m_LastButtonPressed + 1000)) && (m_LastButtonAction == NONE))
    {
        m_LastButtonAction = LONG;
        Serial.println("Button action long detected");
    }
    // button no longer pressed
    else if (m_ButtonPressed && (newState == HIGH))
    {
        m_ButtonPressed = false;
        Serial.println("Button no longer pressed, no action detected");
    }

    if (!m_ButtonPressed && (m_LastButtonAction == HANDLED))
    {
        m_LastButtonAction = NONE;
        Serial.println("Button not pressed and handle finished");
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
    m_LastTemperature = read_temperature();
    m_LastHumidity = read_humidity();

    char mqtt_state[100];
    sprintf(mqtt_state, "{ \"temperature\": %d, \"humidity\": %d }", m_LastTemperature, m_LastHumidity);

    Serial.printf("Temp/Humid state: %s\n", mqtt_state);
    client.publish("disc/sensor/Regnerdings/state", mqtt_state, false);
}

// transmit state of all switches to HA
void txSwitchState()
{
    String mqtt_state = "{";

    for (int i = 1; i <= NUMPORTS; i++)
    {
        mqtt_state += "\"P" + String(i) + "\" : \"" + getSwitch(i) + "\", ";
    }
    mqtt_state = mqtt_state.substring(0, mqtt_state.length() - 2);
    mqtt_state += "}";

    Serial.printf("Switch state: %s\n", mqtt_state.c_str());
    client.publish("disc/switch/Regnerdings/state", mqtt_state.c_str(), false);

    setActivityLed();
}

// get the state of a switch, "ON" or "OFF"
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
    m_LastScreenUpdate = 0;
}

void setActivityLed()
{
    for (int i = 1; i <= NUMPORTS; i++)
    {
        if (getSwitch(i) == "ON")
        {
            digitalWrite(m_ActivityLed, HIGH);
            return;
        }
    }
    digitalWrite(m_ActivityLed, LOW);
}

// parse port from topic
int parsePort(String msg)
{
    return msg.substring(msg.lastIndexOf("dings/P") + 7, msg.lastIndexOf("/")).toInt();
}