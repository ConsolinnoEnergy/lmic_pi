/*******************************************************************************
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 * Copyright (c) 2019 Leonid Verhovskij
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * This example sends a valid LoRaWAN packet with content from d0reader output file
 * transmission config params should be available in json format file
 *
 * Note: LoRaWAN per sub-band duty-cycle limitation is enforced (1% in g1, 
*  0.1% in g2). 
 *
 * Change DEVADDR to a unique address! 
 * See http://thethingsnetwork.org/wiki/AddressSpace
 *
 * Do not forget to define the radio type correctly in config.h, default is:
 *   #define CFG_sx1272_radio 1
 * for SX1272 and RFM92, but change to:
 *   #define CFG_sx1276_radio 1
 * for SX1276 and RFM95.
 *
 *******************************************************************************/

#include <stdio.h>
#include <time.h>
#include <wiringPi.h>
#include <lmic.h>
#include <hal.h>
#include <local_hal.h>
#include <jsoncpp/json/json.h>
#include <fstream>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <sys/time.h>

using namespace std;

//LoRaWAN config file
string filenameLorawanConfig = "/boot/d0logging/lorawan.conf";
string filenameLastReadingPath = "/boot/d0logging/lastreadingpath.conf";
string pathLastReading;

//MeterID
string meterId;

//SerialNumber
string meterSerial;

//Obis
string obisSelection;
string obisUnit;
string obisValue;

//Lora Data
string loraData;

std::stringstream convertStream;

// LoRaWAN Application identifier (AppEUI)
// Not used in this example
unsigned char APPEUI[8];

// LoRaWAN DevEUI, unique device ID (LSBF)
// Not used in this example
unsigned char DEVEUI[8];

// LoRaWAN NwkSKey, network session key
// Use this key for The Things Network
unsigned char DEVKEY[16];

// LoRaWAN AppSKey, application session key
// Use this key to get your data decrypted by The Things Network
unsigned char ARTKEY[16];

// LoRaWAN end-device address (DevAddr)
// See http://thethingsnetwork.org/wiki/AddressSpace
devaddr_t DEVADDR; // <-- Change this address for every node!

//////////////////////////////////////////////////
// APPLICATION CALLBACKS
//////////////////////////////////////////////////

struct HexCharStruct
{
  unsigned char c;
  HexCharStruct(unsigned char _c) : c(_c) {}
};

inline std::ostream &operator<<(std::ostream &o, const HexCharStruct &hs)
{
  return (o << std::hex << (int)hs.c);
}

inline HexCharStruct hex(unsigned char _c)
{
  return HexCharStruct(_c);
}

void stringToUnsignedChar(string input, unsigned char *output)
{
  int offset = 0, i = 0;
  unsigned char result[input.size() / 2];
  while (offset < input.length())
  {
    unsigned int buffer;

    convertStream << std::hex << input.substr(offset, 2);
    convertStream >> std::hex >> buffer;

    output[i] = static_cast<unsigned char>(buffer);

    offset += 2;
    i++;

    // empty the stringstream
    convertStream.str(std::string());
    convertStream.clear();
  }
}

void readLoraWanConfig()
{
  ifstream jsonLoraWanConfigRaw(filenameLorawanConfig);
  Json::Reader reader;
  Json::Value jsonLoraWanConfig;
  reader.parse(jsonLoraWanConfigRaw, jsonLoraWanConfig); // reader can also read strings
  string deviceEuiRaw = jsonLoraWanConfig["deviceEui"].asString();
  string applicationEuiRaw = jsonLoraWanConfig["applicationEui"].asString();
  string deviceAddressRaw = jsonLoraWanConfig["deviceAddress"].asString();
  string networkSessionKeyRaw = jsonLoraWanConfig["networkSessionKey"].asString();
  string appSessionKeyRaw = jsonLoraWanConfig["appSessionKey"].asString();
  obisSelection = jsonLoraWanConfig["obisSelection"].asString();

  stringToUnsignedChar(applicationEuiRaw, APPEUI);
  stringToUnsignedChar(deviceEuiRaw, DEVEUI);
  stringToUnsignedChar(networkSessionKeyRaw, DEVKEY);
  stringToUnsignedChar(appSessionKeyRaw, ARTKEY);

  unsigned int x;
  std::stringstream ss;
  ss << std::hex << deviceAddressRaw;
  ss >> x;

  DEVADDR = x;
}

void readD0LastReadoutPath()
{
  ifstream ifs(filenameLastReadingPath);

  getline(ifs, pathLastReading);
  pathLastReading.append("-1"); //Use temp file while d0reader writes current file
}

void getLastReading()
{
  //Read lastreading
  cout << "get last reading" << endl;
  cout << "path: " << pathLastReading << endl;
  ifstream jsonraw(pathLastReading);

  Json::Reader reader;
  Json::Value obj;
  reader.parse(jsonraw, obj); // reader can also read strings
  const Json::Value dataMessage = obj["data message"];
  meterId = dataMessage["meter ID"].asString();
  const Json::Value &dataBlocks = dataMessage["data block"];

  cout << meterId << endl;
  string currentAddress;

  for (int i = 0; i < dataBlocks.size(); i++)
  {
    currentAddress = dataBlocks[i]["address"].asString();
    //Lookup meterSerial
    if (currentAddress == "0.0.0")
    {
      meterSerial = dataBlocks[i]["value"].asString();
    }
    else if (currentAddress == obisSelection)
    {
      obisValue = dataBlocks[i]["value"].asString();
      obisUnit = dataBlocks[i]["unit"].asString();
    }
  }
}

// provide application router ID (8 bytes, LSBF)
void os_getArtEui(u1_t *buf)
{
  memcpy(buf, APPEUI, 8);
}

// provide device ID (8 bytes, LSBF)
void os_getDevEui(u1_t *buf)
{
  memcpy(buf, DEVEUI, 8);
}

// provide device key (16 bytes)
void os_getDevKey(u1_t *buf)
{
  memcpy(buf, DEVKEY, 16);
}

u4_t cntr=0;
u1_t mydata[100];
static osjob_t sendjob;

// Pin mapping
lmic_pinmap pins = {
    .nss = 6,
    .rxtx = UNUSED_PIN, // Not connected on RFM92/RFM95
    .rst = 0,           // Needed on RFM92/RFM95
    .dio = {7, 4, 5}};

void onEvent(ev_t ev)
{
  //debug_event(ev);

  switch (ev)
  {
  // scheduled data sent (optionally data received)
  // note: this includes the receive window!
  case EV_TXCOMPLETE:
    // use this event to keep track of actual transmissions
    fprintf(stdout, "Event EV_TXCOMPLETE, time: %d\n", millis() / 1000);
    if (LMIC.dataLen)
    { // data received in rx slot after tx
      //debug_buf(LMIC.frame+LMIC.dataBeg, LMIC.dataLen);
      fprintf(stdout, "Data Received!\n");
    }
    break;
  default:
    break;
  }
}

static void do_send(osjob_t *j)
{
  time_t t = time(NULL);
  fprintf(stdout, "[%x] (%ld) %s\n", hal_ticks(), t, ctime(&t));
  // Show TX channel (channel numbers are local to LMIC)
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & (1 << 7))
  {
    fprintf(stdout, "OP_TXRXPEND, not sending");
  }
  else
  {
    // Prepare upstream data transmission at the next possible time.
    loraData = "";

    loraData.append(meterSerial);
    loraData.append(",");
    loraData.append(obisSelection);
    loraData.append(",");
    loraData.append(obisUnit);
    loraData.append(",");
    loraData.append(obisValue);

    char buf[100];
    loraData.copy(buf, loraData.size() + 1);

    int i = 0;
    while (buf[i])
    {
      mydata[i] = buf[i];
      i++;
    }
    mydata[i] = '\0';
    LMIC_setTxData2(1, mydata, strlen(buf), 0);
  }
  // Schedule a timed job to run at the given timestamp (absolute system time)
  os_setTimedCallback(j, os_getTime() + sec2osticks(20), do_send);
}

void setup()
{
  // LMIC init
  wiringPiSetup();

  readLoraWanConfig();
  readD0LastReadoutPath();

  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();
  // Set static session parameters. Instead of dynamically establishing a session
  // by joining the network, precomputed session parameters are be provided.
  LMIC_setSession(0x1, DEVADDR, (u1_t *)DEVKEY, (u1_t *)ARTKEY);
  // Disable data rate adaptation
  LMIC_setAdrMode(0);
  // Disable link check validation
  LMIC_setLinkCheckMode(0);
  // Disable beacon tracking
  LMIC_disableTracking();
  // Stop listening for downstream data (periodical reception)
  LMIC_stopPingable();
  // Set data rate and transmit power (note: txpow seems to be ignored by the library)
  LMIC_setDrTxpow(DR_SF7, 14);
  //

}

int main()
{
  setup();

  getLastReading();

  do_send(&sendjob);

  return 0;
}