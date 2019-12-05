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
 * This example sends a valid LoRaWAN packet with payload "Hello,
 * world!", using frequency and encryption settings matching those of
 * the The Things Network.
 *
 * This uses OTAA (Over-the-air activation), where where a DevEUI and
 * application key is configured, which are used in an over-the-air
 * activation procedure where a DevAddr and session keys are
 * assigned/generated for use with all further communication.
 *
 * Note: LoRaWAN per sub-band duty-cycle limitation is enforced (1% in
 * g1, 0.1% in g2), but not the TTN fair usage policy (which is probably
 * violated by this sketch when left running for longer)!

 * To use this sketch, first register your application and device with
 * the things network, to set or generate an AppEUI, DevEUI and AppKey.
 * Multiple devices can use the same AppEUI, but each device has its own
 * DevEUI and AppKey.
 *
 * Do not forget to define the radio type correctly in config.h.
 *
 *******************************************************************************/

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include <lmic.h>
#include <hal/hal.h>

#include <jsoncpp/json/json.h>
#include <fstream>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <iterator>
#include <algorithm>
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

// LoRaWAN NwkSKey, network session key
// This is the default Semtech key, which is used by the early prototype TTN
// network.
u1_t NWKSKEY[16];

// LoRaWAN AppSKey, application session key
// This is the default Semtech key, which is used by the early prototype TTN
// network.
u1_t APPSKEY[16];

// LoRaWAN end-device address (DevAddr)
u4_t DEVADDR; // <-- Change this address for every node!

uint8_t mydata[50];
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty)
// cycle limitations).
const unsigned TX_INTERVAL = 120;

//Flag for Ctrl-C
volatile sig_atomic_t force_exit = 0;

// LoRasPi board
// see https://github.com/hallard/LoRasPI
#define RF_LED_PIN RPI_V2_GPIO_P1_16 // Led on GPIO23 so P1 connector pin #16
#define RF_CS_PIN RPI_V2_GPIO_P1_24  // Slave Select on CE0 so P1 connector pin #24
#define RF_IRQ_PIN RPI_V2_GPIO_P1_22 // IRQ on GPIO25 so P1 connector pin #22
#define RF_RST_PIN RPI_V2_GPIO_P1_15 // RST on GPIO22 so P1 connector pin #15

// Raspberri PI Lora Gateway for multiple modules
// see https://github.com/hallard/RPI-Lora-Gateway
// Module 1 on board RFM95 868 MHz (example)
//#define RF_LED_PIN RPI_V2_GPIO_P1_07 // Led on GPIO4 so P1 connector pin #7
//#define RF_CS_PIN  RPI_V2_GPIO_P1_24 // Slave Select on CE0 so P1 connector pin #24
//#define RF_IRQ_PIN RPI_V2_GPIO_P1_22 // IRQ on GPIO25 so P1 connector pin #22
//#define RF_RST_PIN RPI_V2_GPIO_P1_29 // Reset on GPIO5 so P1 connector pin #29

// Dragino Raspberry PI hat (no onboard led)
// see https://github.com/dragino/Lora
//#define RF_CS_PIN  RPI_V2_GPIO_P1_22 // Slave Select on GPIO25 so P1 connector pin #22
//#define RF_IRQ_PIN RPI_V2_GPIO_P1_07 // IRQ on GPIO4 so P1 connector pin #7
//#define RF_RST_PIN RPI_V2_GPIO_P1_11 // Reset on GPIO17 so P1 connector pin #11

// Pin mapping
const lmic_pinmap lmic_pins = {
    .nss = RF_CS_PIN,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = RF_RST_PIN,
    .dio = {LMIC_UNUSED_PIN, LMIC_UNUSED_PIN, LMIC_UNUSED_PIN},
};

#ifndef RF_LED_PIN
#define RF_LED_PIN NOT_A_PIN
#endif

struct HexCharStruct
{
    unsigned char c;
    HexCharStruct(unsigned char _c) : c(_c) {}
};

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

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
    string networkSessionKey = jsonLoraWanConfig["networkSessionKey"].asString();
    string appSessionKey = jsonLoraWanConfig["appSessionKey"].asString();
    string deviceAddress = jsonLoraWanConfig["deviceAddress"].asString();

    obisSelection = jsonLoraWanConfig["obisSelection"].asString();

    stringToUnsignedChar(networkSessionKey, NWKSKEY);
    stringToUnsignedChar(appSessionKey, APPSKEY);

    unsigned int x;
    std::stringstream ss;
    ss << std::hex << deviceAddress;
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

    cout << "obisSelection: " << obisSelection << endl;

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

    // Prepare upstream data transmission at the next possible time.
    loraData = "";

    loraData.append(meterSerial);
    loraData.append(",");
    loraData.append(obisSelection);
    loraData.append(",");
    loraData.append(obisUnit);
    loraData.append(",");
    loraData.append(obisValue);

    cout << "loraData: " << loraData << endl;

    char buf[50];
    loraData.copy(buf, loraData.size() + 1);

    int i = 0;
    while (buf[i])
    {
        
        mydata[i] = buf[i];
        i++;
    }
    mydata[i] = '\0';
}

void do_send(osjob_t *j)
{
    char strTime[16];
    getSystemTime(strTime, sizeof(strTime));
    printf("%s: ", strTime);

    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND)
    {
        printf("OP_TXRXPEND, not sending\n");
    }
    else
    {
        getLastReading();
        digitalWrite(RF_LED_PIN, HIGH);
        // Prepare upstream data transmission at the next possible time.
        LMIC_setTxData2(1, mydata, sizeof(mydata) - 1, 0);
        printf("Packet queued\n");
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void onEvent(ev_t ev)
{
    char strTime[16];
    getSystemTime(strTime, sizeof(strTime));
    printf("%s: ", strTime);

    switch (ev)
    {
    case EV_SCAN_TIMEOUT:
        printf("EV_SCAN_TIMEOUT\n");
        break;
    case EV_BEACON_FOUND:
        printf("EV_BEACON_FOUND\n");
        break;
    case EV_BEACON_MISSED:
        printf("EV_BEACON_MISSED\n");
        break;
    case EV_BEACON_TRACKED:
        printf("EV_BEACON_TRACKED\n");
        break;
    case EV_JOINING:
        printf("EV_JOINING\n");
        break;
    case EV_JOINED:
        printf("EV_JOINED\n");
        digitalWrite(RF_LED_PIN, LOW);
        // Disable link check validation (automatically enabled
        // during join, but not supported by TTN at this time).
        LMIC_setLinkCheckMode(0);
        break;
    case EV_RFU1:
        printf("EV_RFU1\n");
        break;
    case EV_JOIN_FAILED:
        printf("EV_JOIN_FAILED\n");
        break;
    case EV_REJOIN_FAILED:
        printf("EV_REJOIN_FAILED\n");
        break;
    case EV_TXCOMPLETE:
        printf("EV_TXCOMPLETE (includes waiting for RX windows)\n");
        if (LMIC.txrxFlags & TXRX_ACK)
            printf("%s Received ack\n", strTime);
        if (LMIC.dataLen)
        {
            printf("%s Received %d bytes of payload\n", strTime, LMIC.dataLen);
        }
        digitalWrite(RF_LED_PIN, LOW);
        // Schedule next transmission
        os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
        break;
    case EV_LOST_TSYNC:
        printf("EV_LOST_TSYNC\n");
        break;
    case EV_RESET:
        printf("EV_RESET\n");
        break;
    case EV_RXCOMPLETE:
        // data received in ping slot
        printf("EV_RXCOMPLETE\n");
        break;
    case EV_LINK_DEAD:
        printf("EV_LINK_DEAD\n");
        break;
    case EV_LINK_ALIVE:
        printf("EV_LINK_ALIVE\n");
        break;
    default:
        printf("Unknown event\n");
        break;
    }
}

/* ======================================================================
Function: sig_handler
Purpose : Intercept CTRL-C keyboard to close application
Input   : signal received
Output  : -
Comments: -
====================================================================== */
void sig_handler(int sig)
{
    printf("\nBreak received, exiting!\n");
    force_exit = true;
}

/* ======================================================================
Function: main
Purpose : not sure ;)
Input   : command line parameters
Output  : -
Comments: -
====================================================================== */
int main(void)
{
    readLoraWanConfig();
    readD0LastReadoutPath();

    // caught CTRL-C to do clean-up
    signal(SIGINT, sig_handler);

    printf("%s Starting\n", __BASEFILE__);

    // Init GPIO bcm
    if (!bcm2835_init())
    {
        fprintf(stderr, "bcm2835_init() Failed\n\n");
        return 1;
    }

    // Show board config
    printConfig(RF_LED_PIN);
    printKeys();

    // Light off on board LED
    pinMode(RF_LED_PIN, OUTPUT);
    digitalWrite(RF_LED_PIN, HIGH);

    // LMIC init
    os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();

    LMIC_setSession (0x1, DEVADDR, NWKSKEY, APPSKEY);

    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);      // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MILLI);      // g2-band

    // Disable link check validation
    LMIC_setLinkCheckMode(0);

    // TTN uses SF9 for its RX2 window.
    LMIC.dn2Dr = DR_SF9;

    // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
    LMIC_setDrTxpow(DR_SF7,14);

    // Start job (sending automatically starts OTAA too)
    do_send(&sendjob);

    while (!force_exit)
    {
        os_runloop_once();

        // We're on a multitasking OS let some time for others
        // Without this one CPU is 99% and with this one just 3%
        // On a Raspberry PI 3
        usleep(1000);
    }

    // We're here because we need to exit, do it clean

    // Light off on board LED
    digitalWrite(RF_LED_PIN, LOW);

    // module CS line High
    digitalWrite(lmic_pins.nss, HIGH);
    printf("\n%s, done my job!\n", __BASEFILE__);
    bcm2835_close();
    return 0;
}
