#include <WiFi.h>
#include <WiFiManager.h> 
#include <Preferences.h>
#include <PubSubClient.h> // Include MQTT Library
#include <DHT.h>
#include <Ticker.h>
#include <BluetoothSerial.h>



#define DHTTYPE DHT11
#define DHTPIN 5
// --- Objects ---
Preferences pref;
WiFiManager wm;
WiFiClient espClient;
PubSubClient client(espClient); // Initialize MQTT Client
DHT dht(DHTPIN, DHTTYPE);
Ticker dhtTicker;
BluetoothSerial BT;

bool SwitchState1 = LOW;
bool SwitchState2 = LOW;
bool SwitchState3 = LOW;
bool SwitchState4 = LOW;
// --- Variables ---
char userID[50] = "default_user"; 
const char* mqtt_server = "broker.emqx.io"; // Free Public Broker
const int mqtt_port = 1883; // TCP Port

// --- Topics (Buffers to hold dynamic topic names) ---
char subTopic[100]; // iot/USERID/to_device
char pubTopic[100]; // iot/USERID/from_device
char dhtTopicT[100];
char dhtTopicH[100];
char HighTempTopic[100];

// --- Timers (Non-Blocking Delays) ---
unsigned long lastReconnectAttempt = 0;

// --- Pins ---
const int RESET_PIN = 4;
const int LED_PIN = 2;
const int relay_1 = 18;
const int relay_2 = 19;
const int relay_3 = 21;
const int relay_4 = 22;
const int switch_1 = 13;
const int switch_2 = 14;
const int switch_3 = 26;
const int switch_4 = 27;


// --- Function Declarations ---
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
void updateTopics();

void setup() {
  Serial.begin(115200);
  dht.begin();
  BT.begin("Ble esp"); 
  pinMode(DHTPIN, INPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(relay_1, OUTPUT);
  pinMode(relay_2, OUTPUT);
  pinMode(relay_3, OUTPUT);
  pinMode(relay_4, OUTPUT);
  pinMode(switch_1, INPUT_PULLUP);
  pinMode(switch_2, INPUT_PULLUP);
  pinMode(switch_3, INPUT_PULLUP);
  pinMode(switch_4, INPUT_PULLUP);


  //run Dht data
  dhtTicker.attach(5.0,DHTdata);
  
  // 1. Load User ID from Storage
  pref.begin("data", false); 
  String saved_id = pref.getString("uid", ""); 
  if(saved_id.length() > 0){
    saved_id.toCharArray(userID, sizeof(userID));
  }
  pref.end();

  // 2. Provisioning (WiFiManager)
  WiFiManagerParameter custom_userid("userid", "Enter Firebase User ID", userID, 50);
  wm.addParameter(&custom_userid);


  // Keep connection timeout short so we don't freeze forever if router is down
  wm.setConnectTimeout(120);
  digitalWrite(2,HIGH);


  if (!wm.autoConnect("ESP32_SmatHome")) {
    manual();
    //Bluetoothsetup();
    Serial.println("Failed to connect. Restarting...");
    ESP.restart();
  }

  // 3. Save User ID if changed
  const char* input_uid = custom_userid.getValue();
  if(strlen(input_uid) > 0) {
      pref.begin("data", false); 
      pref.putString("uid", input_uid); 
      strcpy(userID, input_uid);
      pref.end();
  }

  // 4. Generate Dynamic Topics based on User ID
  updateTopics();

  // 5. Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback); // Define function to call when message arrives
  
  Serial.println("Setup Complete. Entering Loop...");
}

void loop() {
  // --- 1. Factory Reset Logic ---
  if(digitalRead(RESET_PIN) == LOW) {
    Serial.println("Factory Reset...");
    wm.resetSettings();
    pref.begin("data", false);
    pref.clear();
    pref.end();

    for(int i = 0 ; i<10 ; i++){
      digitalWrite(LED_PIN, HIGH);
      delay(1000);
      digitalWrite(LED_PIN, LOW);
      delay(1000);
    }
    ESP.restart();
  }

  // --- 2. MQTT Management (Non-Blocking) ---
  if (!client.connected()) {
    manual();
    Bluetoothsetup(); 
    long now = millis();
    // Try to reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      reconnect();
    }
  } else {
    // Client connected, let it handle messages
    client.loop();
  }

  // --- 3. Other Logic (Relays, Sensors) goes here ---
  // Note: Because we use 'if (now - last...)' above, the code flows freely here!
  manual(); 

  if(WiFi.status() != WL_CONNECTED){
    Bluetoothsetup();
    
    /*READ THIS*/
    //Wifi manager auto AP mode(if wifi router disconnected) is disable. because this condition work when wifi is disconnected
  }
    
}

    //control with swtch
void manual(){
    if(digitalRead(switch_1)== LOW && SwitchState1 == LOW){
    SwitchState1 = HIGH;
    Serial.println("switch 1 is on");
    relay1_on();
  }
    else if(digitalRead(switch_1) == HIGH && SwitchState1 == HIGH){
    SwitchState1 = LOW;
    Serial.println("switch 1 is off");
    relay1_off();
  }
  else if(digitalRead(switch_2)==LOW && SwitchState2 == LOW){
    SwitchState2 = HIGH;
    relay2_on();
    Serial.println("r2 on");
  }
  else if(digitalRead(switch_2) == HIGH && SwitchState2 == HIGH){
    SwitchState2 = LOW;
    relay2_off(); 
    Serial.println("r2 off");
  }
  else if(digitalRead(switch_3) == LOW && SwitchState3 == LOW){
    SwitchState3 = HIGH;
    relay3_on();
    Serial.println("r3 on");
  }
  else if(digitalRead(switch_3) == HIGH && SwitchState3 == HIGH){
    SwitchState3 = LOW;
    relay3_off();
    Serial.println("r3 off");
  }
  else if(digitalRead(switch_4) == LOW && SwitchState4 == LOW){
    SwitchState4 = HIGH;
    relay4_on();
    Serial.println("r4 on");
  }
  else if(digitalRead(switch_4) == HIGH && SwitchState4 == HIGH){
    SwitchState4 = LOW;
    relay4_off();
    Serial.println("r4 off");
  }
}

// --- Custom Functions ---

// 1. Update Topic Strings
void updateTopics() {
  // Create topics: "iot/<userID>/to_device"
  snprintf(subTopic, 100, "iot/%s/to_device", userID);
  snprintf(pubTopic, 100, "iot/%s/from_device", userID);
  snprintf(dhtTopicT, 100, "iot/%s/from_device/t", userID);
  snprintf(dhtTopicH, 100 , "iot/%s/from_device/h" , userID);
  snprintf(HighTempTopic, 100, "iot/%s/from_device/ht", userID);
  
  Serial.print("Subscribing to: "); Serial.println(subTopic);
}

// 2. The "Callback" - What happens when a message arrives?
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  // Convert payload to String for easy comparison
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // --- CONTROL LOGIC ---
  if (message == "R1_ON") {
    relay1_on();
  } 
  else if (message == "R1_OFF") {
    relay1_off();
  }
  else if(message == "R2_ON"){
    relay2_on();
  }
  else if(message == "R2_OFF"){
    relay2_off();
  }
  else if(message == "R3_ON"){
    relay3_on();
  }
  else if(message == "R3_OFF"){
    relay3_off();
  }
  else if(message == "R4_ON"){
    relay4_on();
  }
  else if(message == "R4_OFF"){
    relay4_off();
  }
  else{
    Serial.println("waiting for command");
  }



}

//relays control

void relay1_on(){
    digitalWrite(relay_1, HIGH);
    client.publish(pubTopic, "R1_ON"); // Feedback
    Serial.println("Relay 1 is on");
}

void relay1_off(){
    digitalWrite(relay_1, LOW);
    client.publish(pubTopic, "R1_OFF"); // Feedback
    Serial.println("Relay 1 is off");
}

void relay2_on(){
    digitalWrite(relay_2, HIGH);
    client.publish(pubTopic,"R2_ON"); 
    Serial.println("Realy 2 is on l");
}

void relay2_off(){
    digitalWrite(relay_2, LOW);
    client.publish(pubTopic,"R2_OFF");
    Serial.println("Relay 2 is off");  
}

void relay3_on(){
    digitalWrite(relay_3, HIGH);
    client.publish(pubTopic,"R3_ON");
    Serial.println("Relay 3 is on");  
}

void relay3_off(){
    digitalWrite(relay_3, LOW);
    client.publish(pubTopic,"R3_OFF");
    Serial.println("Relay 3 is off");  
}

void relay4_on(){
    digitalWrite(relay_4, HIGH);
    client.publish(pubTopic,"R4_ON");
    Serial.println("Relay 4 is on");  
}

void relay4_off(){
    digitalWrite(relay_4, LOW);
    client.publish(pubTopic,"R4_OFF");
    Serial.println("Relay 4 is off");  
}

//Dht sensor
void DHTdata(){
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  client.publish(dhtTopicT,String(t).c_str());
  client.publish(dhtTopicH,String(h).c_str());

  if(t == 50.00){
    relay1_off();
    relay2_off();
    relay3_off();
    relay4_off();
    
    while(t >= 50.00){
      client.publish(HighTempTopic,"Alert Fire Alarm");
      delay(10000);
      
    }
  }

  if(t == 39.00){
    client.publish(HighTempTopic,"Temp is more that 39");
  }

}

// 3. Reconnect to MQTT
void reconnect() {
  Serial.print("Attempting MQTT connection...");
  
  // Create a random client ID so the broker doesn't kick us off
  String clientId = "ESP32Client-";
  clientId += String(random(0xffff), HEX);

  if (client.connect(clientId.c_str())) {
    Serial.println("connected");
    // Once connected, publish an announcement...
    client.publish(pubTopic, "Device Online");
    // ... and resubscribe to our topic
    client.subscribe(subTopic);
    digitalWrite(LED_PIN,LOW);
  } else {
    manual();
    Bluetoothsetup();
    digitalWrite(LED_PIN,HIGH);
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    //ESP.restart();
  }
}

void Bluetoothsetup(){
  if(BT.available()){

    char btdata = BT.read();

    if(btdata=='1'){
      relay1_on();
    }
    else if(btdata == '2'){
      relay1_off();
    }
    else if(btdata == '3'){
      relay2_on();
    }
    else if(btdata == '4'){
      relay2_off();
    }
    else if(btdata == '5'){
      relay3_on();
    }
    else if(btdata == '6'){
      relay3_off();
    }
    else if(btdata == '7'){
      relay4_on();
    }
    else if(btdata == '8'){
      relay4_off();
    }
  }
}
