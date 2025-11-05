#include <WiFi.h>
#include <PubSubClient.h>
#include <Ultrasonic.h>

const char* WIFI_SSID   = "iPhone de Alex";
const char* WIFI_PASS   = "Adrianito1";

const char* MQTT_BROKER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883;
const char* CLIENT_ID   = "ESP32_Ultrasonic_Client";

const int PIN_TRIG = 2;
const int PIN_ECHO = 15;
const int PIN_LED  = 4;

const char* TOPIC_LED      = "ucb/test/topic/led";
const char* TOPIC_DISTANCE = "ucb/test/topic/distance";

WiFiClient      net;
PubSubClient    mqttClient(net);


class LedController {
  int pin;
public:
  explicit LedController(int pinLed): pin(pinLed) {}
  void begin() { pinMode(pin, OUTPUT); off(); }
  void on()    { digitalWrite(pin, HIGH); }
  void off()   { digitalWrite(pin, LOW);  }
};

class UltrasonicSensor {
  Ultrasonic sensor;
public:
  UltrasonicSensor(int trig, int echo): sensor(trig, echo) {}
  long readCM() { return sensor.read(); } 
};

class WiFiConnector {
  const char* ssid;
  const char* pass;
public:
  WiFiConnector(const char* s, const char* p): ssid(s), pass(p) {}

  void connect() {
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    Serial.println();
    Serial.print("Connected to WiFi. IP address: ");
    Serial.println(WiFi.localIP());
  }
};

class MqttManager {
  PubSubClient& client;
public:
  explicit MqttManager(PubSubClient& c): client(c) {}

  void begin(const char* host, int port) {
    client.setServer(host, port);
  }

  void setCallback(MQTT_CALLBACK_SIGNATURE) {
    client.setCallback(callback);
  }

  void ensureConnected() {
    while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      if (client.connect(CLIENT_ID)) {
        Serial.println("connected");
        client.subscribe(TOPIC_LED);
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" trying again in 5 seconds");
        delay(1000);
      }
    }
  }

  void loop() { client.loop(); }

  void publish(const char* topic, const char* payload) {
    client.publish(topic, payload);
  }

  bool connected() const { return client.connected(); }
};

class App {
  LedController     led;
  UltrasonicSensor  us;
  WiFiConnector     wifi;
  MqttManager       mqtt;

  static App* instance;

  static void onMqttMessageStatic(char* topic, byte* payload, unsigned int length) {
    if (instance) instance->onMqttMessage(topic, payload, length);
  }

  void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
      message += (char)payload[i];
    }
    if (String(topic) == TOPIC_LED) {
      if (message == "ON")  led.on();
      if (message == "OFF") led.off();
    }
  }

public:
  App()
  : led(PIN_LED)
  , us(PIN_TRIG, PIN_ECHO)
  , wifi(WIFI_SSID, WIFI_PASS)
  , mqtt(mqttClient)
  {}

  void begin() {
    Serial.begin(115200);
    led.begin();

    wifi.connect();

    mqtt.begin(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(onMqttMessageStatic);
    instance = this;
  }

  void loop() {
    if (!mqtt.connected()) {
      mqtt.ensureConnected();
    }
    mqtt.loop();

    long distance = us.readCM();
    String msg = "Distance: " + String(distance) + " cm";

    Serial.print("Publishing message: ");
    Serial.println(msg);

    mqtt.publish(TOPIC_DISTANCE, msg.c_str());

    delay(1000);
  }
};

App* App::instance = nullptr;
App app;

void setup() { app.begin(); }
void loop()  { app.loop();  }
