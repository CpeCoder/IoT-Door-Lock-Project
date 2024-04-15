// MQTT Library
// Deep Shinglot

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: -
// Target uC:       -
// System Clock:    -

// Hardware configuration:
// -

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mqtt.h"
#include "timer.h"

#define MAX_MESSAGE_SIZE 1460
#define MAX_SUBSCRIPTION 5

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------
socket sMQTT;
uint8_t data[MAX_MESSAGE_SIZE];
uint16_t dataSize = 0;

/*uint16_t subPacketId[MAX_SUBSCRIPTION];
uint8_t  numSubs = 0;*/

bool sendMqttConnect      = false;
bool sendMqttDisconnect   = false;
bool sendMqttPublish      = false;
bool sendMqttSubscribe    = false;
bool sendMqttUnsubscribe  = false;
bool mqttPingNeeded       = false;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------
void sendMqttMessage(etherHeader *ether, socket *s, uint16_t flags, uint8_t data[], uint16_t dataSize);

void sendMqttPingReq()
{
    mqttPingNeeded = true;
}

void connectMqtt()
{
    sMQTT.remoteIpAddress[0] = 192;
    sMQTT.remoteIpAddress[1] = 168;
    sMQTT.remoteIpAddress[2] = 1;
    sMQTT.remoteIpAddress[3] = 1;
    sMQTT.remoteHwAddress[0] = 0xE0;
    sMQTT.remoteHwAddress[1] = 0xBE;
    sMQTT.remoteHwAddress[2] = 0x03;
    sMQTT.remoteHwAddress[3] = 0x77;
    sMQTT.remoteHwAddress[4] = 0x05;
    sMQTT.remoteHwAddress[5] = 0xF7;
    sMQTT.remotePort = 1883;
    sMQTT.localPort = 5517;

    sendMqttConnect  = true;
    dataSize = 0;

    // MQTT CONNECT packet
    data[dataSize++] = 0x10;        // 0.  Control header type 1 (4b), 4b (DUP, QoS, QoS, Retain)
    data[dataSize++] = 0x00;        // 1.  length of remaining packet - # of bytes after this
    data[dataSize++] = 0x00;        // 2.  Length MSB - 0
    data[dataSize++] = 0x04;        // 3.  Length LSB - 4
    data[dataSize++] = 0x4D;        // 4.  'M' ASCII
    data[dataSize++] = 0x51;        // 5.  'Q'
    data[dataSize++] = 0x54;        // 6.  'T'
    data[dataSize++] = 0x54;        // 7.  'T'
    data[dataSize++] = 0x04;        // 8.  protocol level, level 4 for v3.1.1
    data[dataSize++] = 0x02;        // 9.  flags (user, passw, retain, QoS, QoS, flag, clean, rev)
    data[dataSize++] = 0x00;        // 10. keep alive MSB - 0
    data[dataSize++] = 0x3C;        // 11. keep alibe LSB - 60 seconds
    data[dataSize++] = 0x00;        // 12. string lenfth MSB --------- client id
    data[dataSize++] = 0x07;        // 13. string length LSB
    data[dataSize++] = 0x79;        // 14. 'y'
    data[dataSize++] = 0x6F;        // 15. 'o'
    data[dataSize++] = 0x42;        // 16. 'B'
    data[dataSize++] = 0x49;        // 17. 'I'
    data[dataSize++] = 0x54;        // 18. 'T'
    data[dataSize++] = 0x43;        // 19. 'C'
    data[dataSize++] = 0x48;        // 20. 'H'      // data size 21

    data[1] = dataSize - 2;         // data[1] is length of packet (subtract 2 - header type and length)
    startPeriodicTimer((_callback)sendMqttPingReq, 40);
}

void disconnectMqtt()
{
    sendMqttDisconnect  = true;
    dataSize = 0;

    // MQTT DISCONNECT packet
    data[dataSize++] = 0xE0;        // Control header type 14 (4b), 4b (DUP, QoS, QoS, Retain)
    data[dataSize++] = 0x00;        // length of remaining packet

    stopTimer((_callback)sendMqttPingReq);
}

void publishMqtt(char strTopic[], char strData[])
{
    sendMqttPublish = true;
    dataSize = 0;

    uint16_t topicLength = (uint16_t)strlen(strTopic);
    uint16_t dataLength = (uint16_t)strlen(strData);
    uint16_t remainingLength = topicLength + dataLength + 2; // 2 bytes for topic length
    uint16_t i;

    // MQTT PUBLISH packet
    data[dataSize++] = 0x30; // header type 3, 4b (DUP, QoS, QoS, Retain)

    // Encode Remaining Length
    do {
        uint8_t encodedByte = remainingLength % 128;
        remainingLength /= 128;
        if (remainingLength > 0) {
            encodedByte |= 0x80;
        }
        data[dataSize++] = encodedByte;
    } while(remainingLength > 0);

    data[dataSize++] = (uint8_t)((topicLength >> 8) & 0x00FF);
    data[dataSize++] = (uint8_t)(topicLength & 0x00FF);

    for(i = 0; i < topicLength; i++)
    {
        data[dataSize++] = (uint8_t)strTopic[i]; // topic name
    }
    for(i = 0; i < dataLength; i++)
    {
        data[dataSize++] = (uint8_t)strData[i]; // data
    }
}

void subscribeMqtt(char strTopic[])
{
    sendMqttSubscribe = true;
    dataSize = 0;
    uint16_t topicLength = (uint16_t)strlen(strTopic);
    uint16_t remainingLength = topicLength + 2 + 2 + 1; // 2 bytes for topic length and 2B Packet Identifier MSB & LSB
                                                            // 1B for QoS
    uint16_t i;
    uint16_t packetIdentifier = (uint16_t)(random32() & 0x0000FFFF);

    /*subPacketId[numSubs] = packetIdentifier;
    numSubs++;*/

    // MQTT SUBSCRIBE packet
    data[dataSize++] = 0x82;        // fixed header type (8), 4b (DUP, QoS, QoS, Retain)

    // Encode Remaining Length
    do {
        uint8_t encodedByte = remainingLength % 128;
        remainingLength /= 128;
        if (remainingLength > 0) {
            encodedByte |= 0x80;
        }
        data[dataSize++] = encodedByte;
    } while(remainingLength > 0);

    data[dataSize++] = (uint8_t)((packetIdentifier >> 8) & 0x00FF);
    data[dataSize++] = (uint8_t)(packetIdentifier & 0x00FF);

    data[dataSize++] = (uint8_t)((topicLength >> 8) & 0x00FF);
    data[dataSize++] = (uint8_t)(topicLength & 0x00FF);

    for(i = 0; i < topicLength; i++)
    {
        data[dataSize++] = (uint8_t)strTopic[i]; // topic name
    }

    // QoS 1 - if pub topic at QoS 0 then client gets QoS 0, pub 2 -> client 1
    // QoS 0 - nothing guaranteed
    data[dataSize++] = 0x00;        // requested QoS - 6b reserved 2 bits QoS
}

void unsubscribeMqtt(char strTopic[])
{
    sendMqttUnsubscribe = true;
    dataSize = 0;
    uint16_t topicLength = (uint16_t)strlen(strTopic);
    uint16_t remainingLength = topicLength + 2 + 2; // 2 bytes for topic length and 2B Packet Identifier MSB & LSB
    uint16_t i;
    uint16_t packetIdentifier = (uint16_t)(random32() & 0x0000FFFF);

    /*subPacketId[numSubs] = packetIdentifier;
    numSubs++;*/

    // MQTT SUBSCRIBE packet
    data[dataSize++] = 0xA2;        // fixed header type (10), 4b (DUP, QoS, QoS, Retain)

    // Encode Remaining Length
    do {
        uint8_t encodedByte = remainingLength % 128;
        remainingLength /= 128;
        if (remainingLength > 0) {
            encodedByte |= 0x80;
        }
        data[dataSize++] = encodedByte;
    } while(remainingLength > 0);

    data[dataSize++] = (uint8_t)((packetIdentifier >> 8) & 0x00FF);
    data[dataSize++] = (uint8_t)(packetIdentifier & 0x00FF);

    data[dataSize++] = (uint8_t)((topicLength >> 8) & 0x00FF);
    data[dataSize++] = (uint8_t)(topicLength & 0x00FF);

    for(i = 0; i < topicLength; i++)
    {
        data[dataSize++] = (uint8_t)strTopic[i]; // topic name
    }
}

void sendPendingMqtt(etherHeader *ether)
{
    if(sendMqttConnect)
    {
        sendMqttConnect = false;
        sendMqttMessage(ether, &sMQTT, ACK|PSH, data, dataSize);
    }
    else if(sendMqttDisconnect)
    {
        sendMqttDisconnect = false;
        sendMqttMessage(ether, &sMQTT, ACK|PSH, data, dataSize);
    }
    else if(sendMqttPublish)
    {
        sendMqttPublish = false;
        sendMqttMessage(ether, &sMQTT, ACK|PSH, data, dataSize);
    }
    else if(sendMqttSubscribe)
    {
        sendMqttSubscribe = false;
        sendMqttMessage(ether, &sMQTT, ACK|PSH, data, dataSize);
    }
    else if(sendMqttUnsubscribe)
    {
        sendMqttUnsubscribe = false;
        sendMqttMessage(ether, &sMQTT, ACK|PSH, data, dataSize);
    }
    else if(mqttPingNeeded)
    {
        mqttPingNeeded = false;
        dataSize = 0;

        data[dataSize++] = 0xC0;
        data[dataSize++] = 0x00;
        sendMqttMessage(ether, &sMQTT, ACK|PSH, data, dataSize);
    }
}

void sendMqttMessage(etherHeader *ether, socket *s, uint16_t flags, uint8_t data[], uint16_t dataSize)
{
    uint8_t localIpAddress[4];
    uint8_t localHwAddress[6];
    uint8_t *dataPointer;       //, *options;
    uint16_t i, ipHeaderLength;
    uint16_t tmp16;
    uint16_t tcpLength;
    uint32_t sum;

    // Ether frame
    getEtherMacAddress(localHwAddress);
    getIpAddress(localIpAddress);

    // IP header
    ipHeader *ip   = (ipHeader*)ether->data;
    ip->rev = 0x4;
    ip->size = 0x5;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = PROTOCOL_TCP;
    ip->headerChecksum = 0;

    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = s->remoteHwAddress[i];
        ether->sourceAddress[i] = localHwAddress[i];
    }
    ether->frameType = htons(TYPE_IP);

    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        ip->destIp[i] = s->remoteIpAddress[i];
        ip->sourceIp[i] = localIpAddress[i];
    }
    ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)ip->data;
//    options = tcp->data;
/*
    *options++ = 2;
    *options++ = 4;
    *options++ = 0x05;      // MSS = 1460
    *options++ = 0xB4;
    *options++ = 1;         // No-Operation
    *options++ = 1;         // padding so data begins at new word
    *options++ = 1;
    *options++ = 0;         // End of Option List
*/
    dataPointer = tcp->data;

    // TCP header
    tcpLength = sizeof(tcpHeader); //+ (options - tcp->data);   // typical: 20 + options (no data)
    tcp->sourcePort = htons(s->localPort);
    tcp->destPort = htons(s->remotePort);
    tcp->sequenceNumber = htonl(getSeqNumber());            // clientSequence
    tcp->acknowledgementNumber = htonl(getAckNumber());     // serverSequence + payloadSize
    tcp->offsetFields = htons((tcpLength/4)<<12 | flags);
    tcp->windowSize = htons(1460);
    tcp->urgentPointer = htons(0);

    tcpLength += dataSize;
    // try without for loop
    for(i = 0; i < dataSize; i++)
    {
        dataPointer[i] = data[i];
    }
    ip->length = htons(ipHeaderLength + tcpLength);

    // 32-bit sum over ip header
    calcIpChecksum(ip);
    sum = 0;
    sumIpWords(ip->sourceIp, 8, &sum);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    uint16_t tcpLength_lsb = htons(tcpLength);
    sumIpWords(&tcpLength_lsb, 2, &sum);
    // add tcp header
    tcp->checksum = 0;
    sumIpWords(tcp, tcpLength, &sum);
    tcp->checksum  = getIpChecksum(sum);

    putEtherPacket(ether, sizeof(etherHeader) + ipHeaderLength + tcpLength);
}
