#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Network Variables
const char* ssid = "bia";
const char* password = "87654321";
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;

// Physical sensor and LED
const int led_pin = 16;
const int potentiometer = A0;

// Refill Logic Variables
int volumeML = 0;
bool mqttRefillRequested = false;
bool autoRefillRequested = false;
unsigned long lastRefillTime = 0;
const unsigned long refillInterval = 300;
const int refillStep = 1;
unsigned long lastSensorRead = 0;
const unsigned long sensorReadInterval = 3000;
unsigned long refillCooldownTime = 0;
const unsigned long cooldownDuration = 10000;

WiFiClient espClient;
PubSubClient client(espClient);

// Connect to WiFi
void setup_wifi(){
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nIP Address: ");
  Serial.println(WiFi.localIP());
}

// MQTT Callback
void callback(char* topic, byte* payload, unsigned int length){
  String message;
  for(unsigned int i = 0; i < length; i++){
    message += (char)payload[i];
  }

  if(String(topic) == "IoT/ESP8266/Response"){
    if(message == "true"){
      if(!autoRefillRequested){
        mqttRefillRequested = true;
        digitalWrite(led_pin, LOW);
        Serial.println("MQTT refill started.");
      } else{
        Serial.println("MQTT refill ignored: Auto refill in progress.");
      }
    } else if(message == "false"){
      mqttRefillRequested = false;
      digitalWrite(led_pin, HIGH);
      Serial.println("MQTT refill stopped.");
    }
  }
}

// Ensure MQTT Connection
void reconnect(){
  while(!client.connected()){
    String clientId = "ESP8266Client-" + String(random(0xffff), HEX);
    if(client.connect(clientId.c_str())){
      Serial.println("MQTT connected");
      client.subscribe("IoT/ESP8266/Response");
    } else{
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Try again in 5 sec...");
      delay(5000);
    }
  }
}

// Read Volume Sensor and check for auto refill
void readVolumeSensor(){
  int rawValue = analogRead(potentiometer);
  volumeML = map(rawValue, 0, 1023, 0, 100);
  Serial.print("Sensor volume: ");
  Serial.print(volumeML);
  Serial.println(" ml");

  if(volumeML <= 25 && !mqttRefillRequested && !autoRefillRequested){
    autoRefillRequested = true;
    digitalWrite(led_pin, LOW);
    Serial.println("Auto refill triggered due to low volume.");
  }
  digitalWrite(led_pin, HIGH);
  publishVolume();
}

// Refill Logic
void gradualRefill(){
  if(millis() - lastRefillTime >= refillInterval){
    lastRefillTime = millis();

    if(volumeML < 100){
      volumeML += refillStep;
      if(volumeML > 100) volumeML = 100;

      Serial.print("Refilling... ");
      Serial.print(volumeML);
      Serial.println(" ml");

      publishVolume();
    } else{
      Serial.println("Refill complete.");

      if(mqttRefillRequested) mqttRefillRequested = false;
      if(autoRefillRequested) autoRefillRequested = false;

      digitalWrite(led_pin, HIGH);
      refillCooldownTime = millis();
    }
  }
}

// Publish volume to MQTT
void publishVolume(){
  String volumeStr = String(volumeML);
  client.publish("IoT/ESP8266/WashMachine", volumeStr.c_str());
}

// Setup function
void setup(){
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  pinMode(led_pin, OUTPUT);
}

// Main loop
void loop(){
  if(!client.connected()){
    reconnect();
  }
  client.loop();
  unsigned long now = millis();

  // Read sensor if not refilling and cooldown passed
  if((now - lastSensorRead >= sensorReadInterval) &&
      !mqttRefillRequested && !autoRefillRequested &&
      (now - refillCooldownTime >= cooldownDuration)){
    lastSensorRead = now;
    readVolumeSensor();
  }
  // Handle refill
  if(mqttRefillRequested || autoRefillRequested){
    gradualRefill();
  }
}