#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ---------------- CONFIG ----------------
#define MODE_SIMULATION true       // true = simulation, false = DHT réel
#define DHTPIN 4
#define DHTTYPE DHT22

#define LED_C 16
#define LED_F 17
#define BUTTON 0

// Remplace par ton réseau WiFi
const char* WIFI_SSID = "Gregmrcr";
const char* WIFI_PASSWORD = "Greg2003";

// MQTT
#define MQTT_SERVER "captain.dev0.pandor.cloud"
#define MQTT_PORT 1884
#define DEVICE_ID "ESP32_01"
#define MQTT_TOPIC_TELEMETRY "classroom/" DEVICE_ID "/telemetry"
#define MQTT_TOPIC_STATUS    "classroom/" DEVICE_ID "/status"
#define MQTT_TOPIC_EVENTS    "classroom/" DEVICE_ID "/events"
#define MQTT_TOPIC_CMD       "classroom/" DEVICE_ID "/cmd"

#define PUBLISH_INTERVAL 5000  // ms

// ---------------- VARIABLES ----------------
DHT dht(DHTPIN, DHTTYPE);

WiFiClient espClient;
PubSubClient client(espClient);

bool modeCelsius = true;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

unsigned long lastPublish = 0;

// ---------------- PROTOTYPES ----------------
void connectWiFi();
void reconnectMQTT();
void handleButton();
void updateLEDs();
void publishStatus(const char* status);

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Station Météo ESP32 ===");

  pinMode(LED_C, OUTPUT);
  pinMode(LED_F, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);

  digitalWrite(LED_C, HIGH);
  digitalWrite(LED_F, LOW);

  if (!MODE_SIMULATION) dht.begin();

  connectWiFi();
  client.setServer(MQTT_SERVER, MQTT_PORT);

  // Publier un status initial
  publishStatus("online");
}

// ---------------- LOOP ----------------
void loop() {
  handleButton();

  if (!client.connected()) reconnectMQTT();
  client.loop();

  if (millis() - lastPublish > PUBLISH_INTERVAL) {
    lastPublish = millis();
    float temp = 0;
    float hum = 0;
    float battery = 3.9;   // valeur simulée, remplacer par mesure réelle si dispo
    unsigned long ts = millis(); // timestamp en ms depuis démarrage

    if (MODE_SIMULATION) {
      temp = random(200, 350) / 10.0; // 20.0 - 35.0 °C
      hum = random(40, 80);           // 40 - 80 %
    } else {
      temp = dht.readTemperature();
      hum = dht.readHumidity();
      if (isnan(temp) || isnan(hum)) {
        Serial.println("Erreur lecture DHT !");
        return;
      }
    }

    if (!modeCelsius) temp = temp * 9.0 / 5.0 + 32.0;

    String payload = "{\"temperature\":";
    payload += String(temp, 1);
    payload += ",\"humidity\":";
    payload += String(hum, 1);
    payload += ",\"battery\":";
    payload += String(battery, 2);
    payload += ",\"timestamp\":";
    payload += String(ts);
    payload += "}";

    client.publish(MQTT_TOPIC_TELEMETRY, payload.c_str());
    Serial.println("Publié MQTT telemetry: " + payload);
  }

  updateLEDs();
}

// ---------------- FONCTIONS ----------------
void connectWiFi() {
  Serial.print("Connexion WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int essais = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    essais++;
    if (essais > 20) {
      Serial.println("\nImpossible de se connecter au WiFi !");
      return;
    }
  }

  Serial.println("\nWiFi connecté !");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connexion MQTT...");
    if (client.connect(DEVICE_ID)) {
      Serial.println("connecté");
      // S'abonner aux commandes
      client.subscribe(MQTT_TOPIC_CMD);
    } else {
      Serial.print("échoué, rc=");
      Serial.print(client.state());
      Serial.println(" nouvelle tentative dans 2s");
      delay(2000);
    }
  }
}

void handleButton() {
  bool reading = digitalRead(BUTTON);
  if (reading != lastButtonState) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW) modeCelsius = !modeCelsius;
  }

  lastButtonState = reading;
}

void updateLEDs() {
  digitalWrite(LED_C, modeCelsius ? HIGH : LOW);
  digitalWrite(LED_F, modeCelsius ? LOW : HIGH);
}

void publishStatus(const char* status) {
  String payload = "{\"status\":\"";
  payload += status;
  payload += "\"}";
  client.publish(MQTT_TOPIC_STATUS, payload.c_str(), true); // retained
}
