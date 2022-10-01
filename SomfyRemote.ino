/* This sketch allows you to emulate a Somfy RTS or Simu HZ remote.

   This is a fork of the original sketch written by Nickduino (https://github.com/Nickduino)

   If you want to learn more about the Somfy RTS protocol, check out https://pushstack.wordpress.com/somfy-rts-protocol/

   The rolling code will be stored in EEPROM, so that you can power the D1 Mini.

   Easiest way to make it work for you:
    - Choose a remote number
    - Choose a starting point for the rolling code. Any unsigned int works, 1 is a good start
    - Upload the sketch
    - Long-press the program button of YOUR ACTUAL REMOTE_BASE_ID until your blind goes up and down slightly
    - send 'p' to the serial terminal or via 'MQTT'
   To make a group command, just repeat the last two steps with another blind (one by one)

   Then:
    - u will make it to go up
    - s make it stop
    - d will make it to go down
    - p sets the program mode
    - you can also send a HEX number directly for any weird command you (0x9 for the sun and wind detector for instance)
*/

#include <cstdint>
#include <algorithm>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <AsyncElegantOTA.h>
#include "config.hpp"
#include "print.hpp"

enum class Action : uint8_t
{
  WAIT = 0U,
  STOP = 1U << 0U,
  UP   = 1U << 1U,
  DOWN = 1U << 2U,
  PROG = 1U << 3U,
};

struct Command
{
  Action action = Action::WAIT;
  int8_t channel = -1;
};

constexpr unsigned int FRAME_LENGTH = 7U;
constexpr unsigned int SYMBOL_TIME_US = 640U;

AsyncWebServer server{80};
WiFiClient espClient;
PubSubClient client{espClient};

Command cmd;

static void reconnect(void);
static Action get_action_by_button(void);
static void build_frame(uint8_t (&frame)[FRAME_LENGTH], uint8_t channel, char button);
static void send_command(const uint8_t (&frame)[FRAME_LENGTH], bool sync);
static void callback(char *topic, uint8_t *payload, unsigned int length);

void setup()
{
  Serial.begin(115200);

  pinMode(TX_GPIO, OUTPUT);
  digitalWrite(TX_GPIO, LOW);

  pinMode(UP_BTN, INPUT_PULLUP);
  pinMode(STOP_BTN, INPUT_PULLUP);
  pinMode(DOWN_BTN, INPUT_PULLUP);
  pinMode(CHANNEL_INC_BTN, INPUT_PULLUP);

  println("Starting Somfy");
  println("Connecting to ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    print('.');
  }

  println();
  println("WiFi connected: ", WiFi.localIP());

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(&callback);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", "Hi! I am a Wemos D1 mini.");
  });

  AsyncElegantOTA.begin(&server);
  server.begin();

  uint16_t codes[AMOUNT_CHANNELS];
  EEPROM.begin(sizeof(codes) > 4U ? sizeof(codes) : 4U);
  EEPROM.get(0, codes);

  for (unsigned int channel = 0U; channel < AMOUNT_CHANNELS; ++channel)
  {
    if (codes[channel] < ROLLING_CODE_BEGIN)
      codes[channel] = EEPROM.put(static_cast<int>(channel * 2U), ROLLING_CODE_BEGIN);
  }

  println();

  for (unsigned int i = 0U; i < AMOUNT_CHANNELS; ++i)
    println("Remote Id (Channel ", i, "):    0x", AsHex{REMOTE_BASE_ID + i});

  for (unsigned int i = 0U; i < AMOUNT_CHANNELS; ++i)
    println("Rolling Code (Channel ", i, "): ", codes[i]);
}

void loop()
{
  static uint8_t frame[FRAME_LENGTH];
  static uint8_t channel = 0U;

  AsyncElegantOTA.loop();

  if (!client.connected())
    reconnect();

  client.loop();

  Action action = cmd.action;
  char button;
  const char *action_str;

  cmd.action = Action::WAIT;

  if (action == Action::WAIT)
    action = get_action_by_button();

  switch (action)
  {
  case Action::UP:
    button = 'u';
    action_str = "up";
    break;
  case Action::DOWN:
    button = 'd';
    action_str = "down";
    break;
  case Action::PROG:
    button = 'p';
    action_str = "prog";
    break;
  case Action::STOP:
    button = 's';
    action_str = "stop";
    break;
  default:
    return;
  }

  println(action_str);
  build_frame(frame, (cmd.channel < 0) ? cmd.channel : channel, button);
  client.publish(FEEDBACK_TOPIC, action_str);
  delay(50);
  client.subscribe(FEEDBACK_TOPIC);

  println();
  send_command(frame, true);

  for (unsigned int i = 0U; i < 2U; ++i)
    send_command(frame, false);
}

static void reconnect(void)
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    print("Attempting MQTT connection...");
    if (client.connect(CLIENT_ID, MQTT_USER, MQTT_PASSWORD))
    {
      println("connected.");
      client.subscribe(MAIN_TOPIC);
      client.subscribe(FEEDBACK_TOPIC);
    }
    else
    {
      println("failed, rc = ", client.state(), ", try again in 3 seconds");
      delay(3000);
    }
  }
}

static Action get_action_by_button(void)
{
  static uint8_t states[3] = {0};

  (void)states[0];


  return Action::WAIT;
}

static void build_frame(uint8_t (&frame)[FRAME_LENGTH], uint8_t channel, char button)
{
  auto print_frame = [](uint8_t (&frame)[FRAME_LENGTH]) -> void
  {
    for (uint8_t b : frame)
    {
      if (b < 0x10)
        print('0');
      println(AsHex{b});
    }
  };

  uint16_t code;
  int codeAddr = static_cast<int>(channel * sizeof(uint16_t));
  uint32_t remoteId = REMOTE_BASE_ID + channel;

  EEPROM.get(codeAddr, code);
  frame[0] = static_cast<uint8_t>(0xA7);            // Encryption key. Doesn't matter much
  frame[1] = static_cast<uint8_t>(button << 4U);    // Which button was pressed? The 4 LSB will be the checksum
  frame[2] = static_cast<uint8_t>(code >> 8U);      // Rolling code (big endian)
  frame[3] = static_cast<uint8_t>(code);            // Rolling code
  frame[4] = static_cast<uint8_t>(remoteId >> 16U); // Remote address
  frame[5] = static_cast<uint8_t>(remoteId >> 8U);  // Remote address
  frame[6] = static_cast<uint8_t>(remoteId);        // Remote address

  print("Frame         : ");
  print_frame(frame);

  // Checksum calculation: a XOR of all the nibbles
  uint8_t checksum = 0U;

  for (uint8_t &b : frame)
    checksum ^= b ^ (b >> 4U);

  // Checksum integration: If a XOR of all the nibbles is equal to 0, the blinds will consider the checksum ok.
  frame[1] |= checksum & 0x0FU;

  print("With checksum : ");
  print_frame(frame);

  // Obfuscation: a XOR of all the uint8_ts
  for (unsigned int i = 1U; i < FRAME_LENGTH; i++)
    frame[i] ^= frame[i - 1];

  print("Obfuscated    : ");
  print_frame(frame);

  println("Rolling Code  : ", code);

  // We store the value of the rolling code in the EEPROM. It should take up to 2 addresses but the Arduino function
  // takes care of it.
  EEPROM.put(codeAddr, code + 1);
  EEPROM.commit();
}

static void send_command(const uint8_t (&frame)[FRAME_LENGTH], bool sync)
{
  auto pulse = [](bool state, unsigned int t_fst, unsigned int t_sec) -> void
  {
    digitalWrite(TX_GPIO, state ? HIGH : LOW);
    delayMicroseconds(t_fst);
    digitalWrite(TX_GPIO, state ? LOW : HIGH);
    delayMicroseconds(t_sec);
  };

  // Only with the first frame.
  if (sync)
    pulse(true, 9415U, 89565U); // Wake-up pulse & Silence

  // Hardware sync: two sync for the first frame, seven for the following ones.
  for (unsigned int i = 0U; i < sync; i++)
    pulse(true, SYMBOL_TIME_US * 4U, SYMBOL_TIME_US * 4U);

  // Software sync
  pulse(true, 4550, SYMBOL_TIME_US);

  // Data: bits are sent one by one, starting with the MSB.
  unsigned int i;
  for (const uint8_t b : frame)
  {
    for (i = 0U; i < 8U; ++i)
    {
      const bool state = (((b >> (7U - i)) & 0x01U) == 0U);
      pulse(state, SYMBOL_TIME_US, SYMBOL_TIME_US);
    }
  }

  digitalWrite(TX_GPIO, LOW);
  delayMicroseconds(30415); // Inter-frame silence
}

static void callback(char *topic, uint8_t *payload, unsigned int length)
{
  char cmd_str[3];

  if ((strcmp(topic, MAIN_TOPIC) != 0) || (length >= sizeof(cmd_str)))
    return;

  *std::copy_n(reinterpret_cast<char *>(payload), length, cmd_str) = '\0';
  println("Received Command: ", cmd_str);

  switch (cmd_str[0])
  {
  case 'u':
    cmd.action = Action::UP;
    break;
  case 'd':
    cmd.action = Action::DOWN;
    break;
  case 's':
    cmd.action = Action::STOP;
    break;
  case 'p':
    cmd.action = Action::PROG;
    break;
  default:
    cmd.action = Action::WAIT;
    break;
  }

  int8_t channel = cmd_str[1] - '0';
  if (cmd.action == Action::WAIT)
  {
    /* Do nothing */;
  }
  else if ((channel > -1) && (channel < static_cast<int>(AMOUNT_CHANNELS)))
  {
    cmd.channel = channel;
  }
  else
  {
    if (cmd_str[1] != '\0')
      cmd.action = Action::WAIT;

    cmd.channel = -1;
  }
}
