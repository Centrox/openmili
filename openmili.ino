#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <printf.h>

#include "PL1167_nRF24.h"
#include "MiLightRadio.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
WiFiUDP Udp;

const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PWD";
char packetBuffer[255];

//GPIO12 - MISO - D6
//GPIO13 - MOSI - D7
//GPIO14 - CLK - D5

#define CE_PIN 16 // - D0 
#define CSN_PIN 5 // - D1

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

  Udp.begin(8899);

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

void loop()
{

  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    /*
    Serial.print("Received packet of size ");
    Serial.println(packetSize);
    Serial.print("From ");
    IPAddress remoteIp = Udp.remoteIP();
    Serial.print(remoteIp);
    Serial.print(", port ");
    Serial.println(Udp.remotePort());
    */
    // read the packet into packetBufffer
    int len = Udp.read(packetBuffer, 255);
    if (len > 0) packetBuffer[len] = 0;
    Serial.print("Contents: ");
    for (int j = 0; j < len; j++) {
      Serial.print(packetBuffer[j], HEX);
      Serial.print(" ");
    }
    Serial.println();

    outgoingPacketUDP[0] = 0xB0; // B0 - White | B8 - RGBW
    outgoingPacketUDP[1] = 0x16; // Remote ID
    outgoingPacketUDP[2] = 0x7D; // Remote ID
    if (packetBuffer[0] == 0x40) {
      outgoingPacketUDP[3] = abs((packetBuffer[1])-0xC8); // Color
      outgoingPacketUDP[5] = 0x0F; // Button
    } else if (packetBuffer[0] == 0x4E) {
      outgoingPacketUDP[4] = 0x191-(packetBuffer[1]*0x08); // Brightness
      outgoingPacketUDP[5] = 0x0E; // Button
    } else if ((packetBuffer[0] & 0xF0) == 0xC0) {
      outgoingPacketUDP[5] = packetBuffer[0] - 0xB2; // Full White
    } else {
      outgoingPacketUDP[5] = packetBuffer[0] - 0x42; // Button
    }
    outgoingPacketUDP[6]++; // Counter
/*
    uint16_t value = calc_crc(outgoingPacketUDP);;
    outgoingPacketUDP[7] = value & 0xff;
    outgoingPacketUDP[8] = value >> 8;
*/
    Serial.print("Write : ");
    for (int j = 0; j < sizeof(outgoingPacketUDP); j++) {
      Serial.print(outgoingPacketUDP[j], HEX);
      Serial.print(" ");
    }
    Serial.println();
    mlr.write(outgoingPacketUDP, sizeof(outgoingPacketUDP));
    for (int k = 0; k < 4 ; k++) {
      for (int j = 0; j < 10 ; j++) {
        mlr.resend();
      }
      outgoingPacketUDP[6]++; // Counter
      /*
      uint16_t value = calc_crc(outgoingPacketUDP);;
      outgoingPacketUDP[7] = value & 0xff;
      outgoingPacketUDP[8] = value >> 8;
      */
    }
  }
  delay(0);

  if (receiving) {
    if (mlr.available()) {
      printf("\n");
      Serial.println();
      uint8_t packet[7];
      size_t packet_length = sizeof(packet);
      mlr.read(packet, packet_length);

      for (int i = 0; i < packet_length; i++) {
        Serial.print(packet[i], HEX);
        Serial.print(" ");
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
