#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <printf.h>

#include "PL1167_nRF24.h"
#include "MiLightRadio.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

WiFiUDP udp1;
WiFiUDP udp2;
WiFiUDP udp3;
WiFiUDP udp4;

uint8_t udp1Id[] = {0x63, 0xD2};
uint8_t udp2Id[] = {0x63, 0xD3};
uint8_t udp3Id[] = {0x63, 0xD0};
uint8_t udp4Id[] = {0x63, 0xC2};

const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PWD";
char packetBuffer[255];

//GPIO12 - MISO - D6
//GPIO13 - MOSI - D7
//GPIO14 - CLK - D5

//#define CE_PIN 16 // - D0
//#define CSN_PIN 5 // - D1

#define CE_PIN D0 // - D0 
#define CSN_PIN D8 // - D1

RF24 radio(CE_PIN, CSN_PIN);
PL1167_nRF24 prf(radio);
MiLightRadio mlr(prf);

//Thanks to Henryk and Erantimus for providing details and checksum code.
//Calculate Checksum - Returns 2 bytes.
uint16_t calc_crc(uint8_t data[], uint8_t data_length = 0x08) {
  uint16_t state = 0;
  for (uint8_t i = 0; i < data_length; i++) {
    uint8 byte = data[i];
    for (int j = 0; j < 8; j++) {
      if ((byte ^ state) & 0x01) {
        state = (state >> 1) ^ 0x8408;
      } else {
        state = state >> 1;
      }
      byte = byte >> 1;
    }
  }
  return state;
}

uint8_t resendCounter = 0;
uint8_t lastOnGroup = 0;
long unsigned int lastMicros = 0;

void setup()
{
  Serial.begin(115200);
  delay(10);

  // We start by connecting to a WiFi network

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  udp1.begin(8899);
  udp2.begin(8898);
  udp3.begin(8897);
  udp4.begin(8896);

  printf_begin();
  delay(1000);
  Serial.println("# OpenMiLight Receiver/Transmitter starting");
  mlr.begin();
}


static int dupesPrinted = 0;
static bool receiving = false;
static bool escaped = false;
static uint8_t outgoingPacket[7];
static uint8_t outgoingPacketUDP[7];
static uint8_t outgoingPacketPos = 0;
static uint8_t nibble;
static enum {
  IDLE,
  HAVE_NIBBLE,
  COMPLETE,
} state;

static uint8_t reverse_bits(uint8_t data) {
  uint8_t result = 0;
  for (int i = 0; i < 8; i++) {
    result <<= 1;
    result |= data & 1;
    data >>= 1;
  }
  return result;
}

void udpLoop(WiFiUDP Udp, uint8_t id1, uint8_t id2) {
  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    // read the packet into packetBufffer
    int len = Udp.read(packetBuffer, 255);
    if (len > 0) packetBuffer[len] = 0;
    Serial.println();
    Serial.print("Contents: ");
    for (int j = 0; j < len; j++) {
      Serial.print(packetBuffer[j], HEX);
      Serial.print(" ");
    }
    Serial.println();

    outgoingPacketUDP[0] = 0xB0; // B0 - White | B8 - RGBW
    outgoingPacketUDP[1] = id1; // Remote ID
    outgoingPacketUDP[2] = id2; // Remote ID

    if (packetBuffer[0] == 0x40) {  // Color
      outgoingPacketUDP[3] = ((uint8_t)0xFF - packetBuffer[1]) + 0xC0;
      outgoingPacketUDP[4] = lastOnGroup; // Use last ON group
      outgoingPacketUDP[5] = 0x0F; // Button

    } else if (packetBuffer[0] == 0x4E) { // Brightness

      // 2 to 1B (2-27)
      // (0x90-0x00 and 0xF8-0xB0) increments of 8
      // 0x90-0x00 = 1 to 18
      // 0xB0-F8 = 19 to 27
      /*
       * x - 98
       * x - 90
       * x - 88
       * 2 - 80*
       * 3 - 78
       * 4 - 70
       * 5 - 68
       * 6 - 60
       * 7 - 58
       * 8 -50
       * 9 - 48
       * 10 - 40
       * 11 - 38
       * 12 - 30
       * 13 - 28
       * 14 - 20
       * 15 - 18
       * 16 - 10
       * 17 - 8
       * 18 - 0
       * 19 - F8
       * 20 - F0
       * 21 - E8
       * 22 - E0
       * 23 - D8
       * 24 - D0
       * 25 - C8
       * 26 - C0
       * 27 - B8*
       * xx - B0
       * xx - A8
       */

      if (packetBuffer[1] <= 18) {
        outgoingPacketUDP[4] = (18 - packetBuffer[1]) * 0x08;
      } else {
        outgoingPacketUDP[4] = 0xB8 + (27 - packetBuffer[1]) * 0x08;
      }
      outgoingPacketUDP[4] += lastOnGroup; // add group number
      outgoingPacketUDP[5] = 0x0E; // Button

    } else if ((packetBuffer[0] & 0xF0) == 0xC0) {
      outgoingPacketUDP[5] = packetBuffer[0] - 0xB2; // Full White

    } else if (packetBuffer[0] == 0x41) {   // Button RGBW COLOR LED ALL OFF
      outgoingPacketUDP[5] = 0x02;
    } else if (packetBuffer[0] == 0x42) {   // Button RGBW COLOR LED ALL ON
      outgoingPacketUDP[5] = 0x01;
      lastOnGroup = 0;
    } else if (packetBuffer[0] == 0x45) {   // Group 1 ON
      outgoingPacketUDP[5] = 0x03;
      lastOnGroup = 1;
    } else if (packetBuffer[0] == 0x46) {   // Group 1 OFF
      outgoingPacketUDP[5] = 0x04;
    } else if (packetBuffer[0] == 0x47) {   // Group 2 ON
      outgoingPacketUDP[5] = 0x05;
      lastOnGroup = 2;
    } else if (packetBuffer[0] == 0x48) {   // Group 2 OFF
      outgoingPacketUDP[5] = 0x06;
    } else if (packetBuffer[0] == 0x49) {   // Group 3 ON
      outgoingPacketUDP[5] = 0x07;
      lastOnGroup = 3;
    } else if (packetBuffer[0] == 0x4A) {   // Group 3 OFF
      outgoingPacketUDP[5] = 0x08;
    } else if (packetBuffer[0] == 0x4B) {   // Group 4 ON
      outgoingPacketUDP[5] = 0x09;
      lastOnGroup = 4;
    } else if (packetBuffer[0] == 0x4C) {   // Group 5 OFF
      outgoingPacketUDP[5] = 0x0A;

    } else {
      Serial.println("Wooops!");
      outgoingPacketUDP[5] = packetBuffer[0] - 0x42; // Button
    }
    outgoingPacketUDP[6]++; // Counter

    Serial.print("Write : ");
    for (int j = 0; j < sizeof(outgoingPacketUDP); j++) {
      Serial.print(outgoingPacketUDP[j], HEX);
      Serial.print(" ");
    }
    Serial.println();

    mlr.write(outgoingPacketUDP, sizeof(outgoingPacketUDP));
    resendCounter = 16;
    lastMicros = micros();
  }
  delay(0);

  if (resendCounter > 0) {
    if (micros() - 350 > lastMicros) {
      mlr.resend();
      resendCounter--;
      Serial.print(".");
      lastMicros = micros();
    }
  }
}

void loop()
{

  udpLoop(udp1, udp1Id[0], udp1Id[1]);
  udpLoop(udp2, udp2Id[0], udp2Id[1]);
  udpLoop(udp3, udp3Id[0], udp3Id[1]);
  udpLoop(udp4, udp4Id[0], udp4Id[1]);

  if (receiving) {
    if (mlr.available()) {
      printf("\n");
      //Serial.println();
      uint8_t packet[7];
      size_t packet_length = sizeof(packet);
      mlr.read(packet, packet_length);

      for (int i = 0; i < packet_length; i++) {
        //Serial.print(packet[i], HEX);
        //Serial.print(" ");
        printf("%02X ", packet[i]);
      }
    }

    int dupesReceived = mlr.dupesReceived();
    for (; dupesPrinted < dupesReceived; dupesPrinted++) {
      printf(".");
      Serial.print(".");
    }
  }

  while (Serial.available()) {
    yield();
    char inChar = (char)Serial.read();
    uint8_t val = 0;
    bool have_val = true;

    if (inChar >= '0' && inChar <= '9') {
      val = inChar - '0';
    } else if (inChar >= 'a' && inChar <= 'f') {
      val = inChar - 'a' + 10;
    } else if (inChar >= 'A' && inChar <= 'F') {
      val = inChar - 'A' + 10;
    } else {
      have_val = false;
    }

    if (!escaped) {
      if (have_val) {
        switch (state) {
          case IDLE:
            nibble = val;
            state = HAVE_NIBBLE;
            break;
          case HAVE_NIBBLE:
            if (outgoingPacketPos < sizeof(outgoingPacket)) {
              outgoingPacket[outgoingPacketPos++] = (nibble << 4) | (val);
            } else {
              Serial.println("# Error: outgoing packet buffer full/packet too long");
            }
            if (outgoingPacketPos >= sizeof(outgoingPacket)) {
              state = COMPLETE;
            } else {
              state = IDLE;
            }
            break;
          case COMPLETE:
            Serial.println("# Error: outgoing packet complete. Press enter to send.");
            break;
        }
      } else {
        switch (inChar) {
          case ' ':
          case '\n':
          case '\r':
          case '.':
            if (state == COMPLETE) {
              Serial.print("Write : ");
              for (int j = 0; j < sizeof(outgoingPacket); j++) {
                Serial.print(outgoingPacket[j], HEX);
                Serial.print(" ");
              }
              Serial.print(" - Size : ");
              Serial.print(sizeof(outgoingPacket), DEC);
              Serial.println();
              mlr.write(outgoingPacket, sizeof(outgoingPacket));
              Serial.println("Write End");
            }
            if (inChar != ' ') {
              outgoingPacketPos = 0;
              state = IDLE;
            }
            if (inChar == '.') {
              mlr.resend();
              delay(1);
            }
            break;
          case 'x':
            Serial.println("# Escaped to extended commands: r - Toggle receiver; Press enter to return to normal mode.");
            escaped = true;
            break;
        }
      }
    } else {
      switch (inChar) {
        case '\n':
        case '\r':
          outgoingPacketPos = 0;
          state = IDLE;
          escaped = false;
          break;
        case 'r':
          receiving = !receiving;
          if (receiving) {
            Serial.println("# Now receiving");
          } else {
            Serial.println("# Now not receiving");
          }
          break;
      }
    }
  }
}
