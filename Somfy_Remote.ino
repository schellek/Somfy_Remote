/* This sketch allows you to emulate a Somfy RTS or Simu HZ remote.
   
   This is a fork of the original sketch written by Nickduino (https://github.com/Nickduino)
    
   If you want to learn more about the Somfy RTS protocol, check out https://pushstack.wordpress.com/somfy-rts-protocol/
   
   The rolling code will be stored in EEPROM, so that you can power the D1 Mini.
   
   Easiest way to make it work for you:
    - Choose a remote number
    - Choose a starting point for the rolling code. Any unsigned int works, 1 is a good start
    - Upload the sketch
    - Long-press the program button of YOUR ACTUAL REMOTE until your blind goes up and down slightly
    - send 'p' to the serial terminal oder via 'MQTT'
   To make a group command, just repeat the last two steps with another blind (one by one)
  
   Then:
    - u will make it to go up
    - s make it stop
    - d will make it to go down
    - p sets the program mode
    - you can also send a HEX number directly for any weird command you (0x9 for the sun and wind detector for instance)
*/

#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <AsyncElegantOTA.h>

WiFiClient espClient;
PubSubClient client(espClient);

String header;
const char* ssid = "My-Wifi-Name";   // <-- Enter your Wifi-SSID
const char* password = "My-Wifi-Password";  // <-- Enter your Wifi-Password

const char* mqtt_server = "192.168.178.xxx";  // <-- Enter the IP of your MQTT-Server
const unsigned int mqtt_port = 1883; 
const char* mqtt_user =   "admin"; 
const char* mqtt_pass =   "My-MQTT-Password"; // <-- Enter the Password of your MQTT-Server
String clientId = "Awning";
 
#define SYMBOL 640
#define UP 0x2
#define STOP 0x1
#define DOWN 0x4
#define PROG 0x8
#define EEPROM_ADDRESS 0
#define REMOTE 0x121309    //<-- Change it to a unique no., if you have more than one Remote (0x121305, 0x121306, 0x121307, ...)

AsyncWebServer server(80);

char demand = 'w'; // w = waiting

const int transmitPin = 5;

unsigned long rollingCode = 1;

byte frame[7];
byte checksum;



void BuildFrame(byte *frame, byte button) {
  unsigned int code;
  EEPROM.get(EEPROM_ADDRESS, code);
  frame[0] = 0xA7; // Encryption key. Doesn't matter much
  frame[1] = button << 4;  // Which button did  you press? The 4 LSB will be the checksum
  frame[2] = code >> 8;    // Rolling code (big endian)
  frame[3] = code;         // Rolling code
  frame[4] = REMOTE >> 16; // Remote address
  frame[5] = REMOTE >>  8; // Remote address
  frame[6] = REMOTE;       // Remote address

  Serial.print("Frame         : ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] >> 4 == 0) { //  Displays leading zero in case the most significant
      Serial.print("0");     // nibble is a 0.
    }
    Serial.print(frame[i],HEX); Serial.print(" ");
  }
  
// Checksum calculation: a XOR of all the nibbles
  checksum = 0;
  for(byte i = 0; i < 7; i++) {
    checksum = checksum ^ frame[i] ^ (frame[i] >> 4);
  }
  checksum &= 0b1111; // We keep the last 4 bits only


//Checksum integration
  frame[1] |= checksum; //  If a XOR of all the nibbles is equal to 0, the blinds will
                        // consider the checksum ok.

  Serial.println(""); Serial.print("With checksum : ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] >> 4 == 0) {
      Serial.print("0");
    }
    Serial.print(frame[i],HEX); Serial.print(" ");
  }

  
// Obfuscation: a XOR of all the bytes
  for(byte i = 1; i < 7; i++) {
    frame[i] ^= frame[i-1];
  }

  Serial.println(""); Serial.print("Obfuscated    : ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] >> 4 == 0) {
      Serial.print("0");
    }
    Serial.print(frame[i],HEX); Serial.print(" ");
  }
  Serial.println("");
  Serial.print("Rolling Code  : "); Serial.println(code);
  EEPROM.put(EEPROM_ADDRESS, code + 1); //  We store the value of the rolling code in the
                                        // EEPROM. It should take up to 2 adresses but the
                                        // Arduino function takes care of it.
 EEPROM.commit();                                       
}



void SendCommand(byte *frame, byte sync) {
  if(sync == 2) { // Only with the first frame.
  //Wake-up pulse & Silence
    digitalWrite(transmitPin, HIGH);
    delayMicroseconds(9415);
    digitalWrite(transmitPin, LOW);
    delayMicroseconds(89565);
  }

// Hardware sync: two sync for the first frame, seven for the following ones.
  for (int i = 0; i < sync; i++) {
    digitalWrite(transmitPin, HIGH);
    delayMicroseconds(4*SYMBOL);
    digitalWrite(transmitPin, LOW);
    delayMicroseconds(4*SYMBOL);
  }

// Software sync
  digitalWrite(transmitPin, HIGH);
  delayMicroseconds(4550);
  digitalWrite(transmitPin, LOW);
  delayMicroseconds(SYMBOL);
  
  
//Data: bits are sent one by one, starting with the MSB.
  for(byte i = 0; i < 56; i++) {
    if(((frame[i/8] >> (7 - (i%8))) & 1) == 1) {
      digitalWrite(transmitPin, LOW);
      delayMicroseconds(SYMBOL);
      digitalWrite(transmitPin, HIGH);
      delayMicroseconds(SYMBOL);
    }
    else {
      digitalWrite(transmitPin, HIGH);
      delayMicroseconds(SYMBOL);
      digitalWrite(transmitPin, LOW);
      delayMicroseconds(SYMBOL);
    }
  }
  
  digitalWrite(transmitPin, LOW);
  delayMicroseconds(30415); // Inter-frame silence
}




void callback(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic,"Garden/Awning") == 0) {
      char demand_str[length + 1];
      strncpy (demand_str, (char*)payload, length);
      demand_str[length] = '\0';
      Serial.println(demand_str);
      
      // u = up, d = down, s = stop, p = program, w = wait
      
      if (strcmp(demand_str,"u") == 0) {
        demand = 'u';
      } else if (strcmp(demand_str,"d") == 0) {
        demand = 'd';
      } else if (strcmp(demand_str,"s") == 0) {
        demand = 's';
      } else if (strcmp(demand_str,"p") == 0) {
        demand = 'p';
      } else {
        demand = 'w';  
      }
      Serial.println(demand_str);
      Serial.println(demand);
    }
}



void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(clientId.c_str(),mqtt_user,mqtt_pass)) {
      Serial.println("connected. ");
      client.subscribe("Garden/Awning");
      client.subscribe("Garden/Awning/Feedback");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 3 seconds");
      delay(3000);
    }
  }
}






void setup() {
  Serial.begin(115200);
  Serial.println(" ");
  Serial.println("Starting Somfy");
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  Serial.println(WiFi.localIP());

  pinMode(transmitPin, OUTPUT); // Pin D1 on the Wemos D1 mini
  digitalWrite(transmitPin, LOW);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "Hi! I am a Wemos D1 mini.");
  });

  AsyncElegantOTA.begin(&server);    // Start ElegantOTA
  server.begin();


  EEPROM.begin(4);
  EEPROM.get(EEPROM_ADDRESS, rollingCode);

  Serial.println(" ");
  Serial.print("Simulated remote number : "); 
  Serial.println(REMOTE, HEX);
  Serial.print("Current rolling code    : "); 
  Serial.println(rollingCode);
}


void loop() {
  AsyncElegantOTA.loop();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (demand == 'u' || demand == 'd' || demand == 's' || demand == 'p') {

//    char serie = (char)Serial.read();
    char serie = (char)demand;
    if(serie == 'u') {
      demand = 'w';
      Serial.println("up"); // Somfy is a French company, after all.
      BuildFrame(frame, UP);

      client.publish("Garden/Awning/Feedback", "up");
      delay(50);
      client.subscribe("Garden/Awning/Feedback");
      Serial.println("moving up");
    }
    else if(serie == 'd') {
      demand = 'w';
      Serial.println("down");
      BuildFrame(frame, DOWN);
      client.publish("Garden/Awning/Feedback", "down");
      delay(50);
      client.subscribe("Garden/Awning/Feedback");
      Serial.println("moving down");
    }
    else if(serie == 'p') {
      demand = 'w';
      Serial.println("prog");
      BuildFrame(frame, PROG);
      client.publish("Garden/Awning/Feedback", "prog");
      delay(50);
      client.subscribe("Garden/Awning/Feedback");
      Serial.println("prog mode");
    }
    else if(serie == 's') {
      demand = 'w';
      Serial.println("stop");
      BuildFrame(frame, STOP);
      client.publish("Garden/Awning/Feedback", "stop");
      delay(50);
      client.subscribe("Garden/Awning/Feedback");
      Serial.println("stop");
    }
    
    demand = 'w';

    Serial.println("");
    SendCommand(frame, 2);
    for(int i = 0; i<2; i++) {
      SendCommand(frame, 7);
    }
  }
}

