// TCP Library
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
#include <string.h>
#include "arp.h"
#include "tcp.h"
#include "timer.h"
#include "mqtt.h"

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

#define MAX_TCP_PORTS 4

socket sMQTT;
uint32_t clientSequence;
uint32_t serverSequence;
uint16_t payloadSize;
uint16_t tcpPorts[MAX_TCP_PORTS];
uint8_t tcpState[MAX_TCP_PORTS];
uint8_t tcpPortCount = 0;

bool ackInProcess = false;
bool pshInProcess = false;
// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
void sendTcpResponse(etherHeader *ether, socket* s, uint16_t flags);

//-----------------------------------------------------------------------------

uint32_t getSeqNumber()
{
    return clientSequence;
}

uint32_t getAckNumber()
{
    return serverSequence + payloadSize;
}

// Set TCP state
void setTcpState(uint8_t instance, uint8_t state)
{
    tcpState[instance] = state;
}

// Get TCP state
uint8_t getTcpState(uint8_t instance)
{
    return tcpState[instance];
}

void tcpTimeWait()
{
    uint8_t i;
    for(i = 0; i < tcpPortCount; i++)
    {
        if(getTcpState(i) == TCP_TIME_WAIT)
        {
            setTcpState(i, TCP_CLOSED);
        }
    }
}

void setCloseState()
{
    uint8_t i;
    for(i = 0; i < tcpPortCount; i++)
    {
        if(getTcpState(i) == TCP_SYN_RECEIVED)
        {
            setTcpState(i, TCP_CLOSED);
        }
    }
}

// Determines whether packet is TCP packet
// Must be an IP packet
bool isTcp(etherHeader* ether)
{
    bool ok;
    ipHeader *ip = (ipHeader*)ether->data;
    ok = ip->protocol == PROTOCOL_TCP;
    return ok;
}

bool isTcpFlag(etherHeader *ether, uint16_t flag)
{
    bool ok;
    ipHeader *ip   = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)ip->data;
    uint16_t tcp_offset_msb = htons(tcp->offsetFields);
    ok = tcp_offset_msb & flag;
    return ok;
}

bool isTcpAck(etherHeader *ether)
{
    bool ok;
    ipHeader *ip   = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)ip->data;
    uint16_t tcp_offset_msb = htons(tcp->offsetFields);
    ok = tcp_offset_msb & ACK;
    return ok;
}

void sendTcpPendingMessages(etherHeader *ether)
{
    if(tcpPortCount == 0)
    {
        tcpPorts[tcpPortCount] = 1883;
        tcpPortCount++;
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
        sMQTT.acknowledgementNumber = 0;
        sMQTT.sequenceNumber = 0;
    }

    uint8_t i;

    for(i = 0; i < tcpPortCount; i++)
    {
        switch(getTcpState(i))
        {
        case TCP_CLOSED:
            clientSequence = random32();
            sendTcpResponse(ether, &sMQTT, SYN);
            setTcpState(i, TCP_SYN_RECEIVED);
            startOneshotTimer((_callback)setCloseState, 10);
            break;
        case TCP_SYN_RECEIVED:
            break;
        case TCP_SYN_SENT:
            sendTcpResponse(ether, &sMQTT, ACK);
            setTcpState(i, TCP_ESTABLISHED);
            break;
        case TCP_ESTABLISHED:
            sendPendingMqtt(ether);
            if(ackInProcess)
            {
                sendTcpResponse(ether, &sMQTT, ACK);
                ackInProcess = false;
            }
            else if(pshInProcess)
            {
                pshInProcess = false;   // not needed since using MQTT
            }
            break;
        case TCP_FIN_WAIT_2:
            if(ackInProcess)
            {
                stopAllTimers();
                sendTcpResponse(ether, &sMQTT, ACK|FIN);
                ackInProcess = false;
                setTcpState(i, TCP_TIME_WAIT);
                startOneshotTimer((_callback)tcpTimeWait, 30);
            }
            break;
        case TCP_TIME_WAIT:
            break;
        }
    }
}

void processTcpResponse(etherHeader *ether)
{
    ipHeader *ip   = (ipHeader *) ether->data;
    tcpHeader *tcp = (tcpHeader *) ip->data;
    uint8_t i;
    uint16_t tcpSourcePort = htons(tcp->sourcePort);

    if(htons(tcp->destPort) == sMQTT.localPort)
    {
        for(i = 0; i < tcpPortCount; i++)
        {
            if(tcpSourcePort == tcpPorts[i])
            {
                break;
            }
        }
        if(isTcpFlag(ether, SYN) && isTcpAck(ether) && getTcpState(i)==TCP_SYN_RECEIVED)
        {
            stopTimer((_callback)setCloseState);
            setTcpState(i, TCP_SYN_SENT);
            clientSequence = ntohl(tcp->acknowledgementNumber);
            serverSequence = ntohl(tcp->sequenceNumber) + 1;        // in initial state packet is count as len 1
        }
        else if(isTcpAck(ether) &&  isTcpFlag(ether, PSH) && getTcpState(i)==TCP_ESTABLISHED)
        {
            clientSequence = ntohl(tcp->acknowledgementNumber);
            serverSequence = ntohl(tcp->sequenceNumber);
            ackInProcess = true;
        }
        else if(isTcpAck(ether) && isTcpFlag(ether, FIN))
        {
            clientSequence = ntohl(tcp->acknowledgementNumber);
            serverSequence = ntohl(tcp->sequenceNumber) + 1;        // because payload is zero add 1
            setTcpState(i, TCP_FIN_WAIT_2);
            ackInProcess = true;
        }
        else if(isTcpAck(ether) && getTcpState(i)==TCP_ESTABLISHED)
        {
            clientSequence = ntohl(tcp->acknowledgementNumber);
            serverSequence = ntohl(tcp->sequenceNumber);
        }
        // tcp->offsetFields flags(8b) dataoffset(4b) reserved(4b), this is reserved no ntohs
            // ans : (dataoffset >> 4) * 4 = dataoffset >> 2
        payloadSize = ntohs(ip->length) - (ip->size * 4) - ((tcp->offsetFields & 0x00F0) >> 2);

/*        if(payloadSize == 0)
        {
            serverSequence++;               // len 0 is considered payload = 1
        }*/
    }
}

// Must be an ARP response
void processTcpArpResponse(etherHeader *ether)
{
    bool ok = true;
    arpPacket *arp = (arpPacket *)ether->data;
    uint8_t i;
    uint8_t localIpAddress[4];

    getIpAddress(localIpAddress);

    if(tcpPortCount < MAX_TCP_PORTS)
    {
        for(i = 0; i < IP_ADD_LENGTH; i++)
        {
            ok &= arp->destIp[i] == localIpAddress[i];
        }
        if(ok)
        {
            getSocketInfoFromArpResponse(ether, &sMQTT);
            setIpMqttBrokerAddress(sMQTT.remoteIpAddress);
            tcpPorts[tcpPortCount] = 1883;
            tcpPortCount++;
        }
    }
}

void setTcpPortList(uint16_t ports[], uint8_t count)
{
    uint8_t i;
    if(count > MAX_TCP_PORTS)
    {
        count = MAX_TCP_PORTS;
        tcpPortCount = count;
    }
    else
    {
        tcpPortCount = count;
    }
    for(i = 0; i < count; i++)
    {
        tcpPorts[i] = ports[i];
    }
}

bool isTcpPortOpen(etherHeader *ether)
{
    ipHeader *ip   = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)ip->data;
    uint16_t tcpSourcePort = htons(tcp->sourcePort);
    uint8_t i;

    for(i = 0; i < MAX_TCP_PORTS; i++)
    {
        if(tcpPorts[i] == tcpSourcePort)
        {
            return true;
        }
    }
    return false;
}

void sendTcpResponse(etherHeader *ether, socket* s, uint16_t flags)
{
    uint8_t localIpAddress[4];
    uint8_t localHwAddress[6];
    uint8_t i, ipHeaderLength;
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

    // TCP header
    tcpLength = sizeof(tcpHeader);   // no options/data in 3-way
    tcp->sourcePort = htons(s->localPort);
    tcp->destPort = htons(s->remotePort);
    tcp->sequenceNumber = htonl(clientSequence);
    tcp->acknowledgementNumber = htonl(serverSequence + payloadSize);
    tcp->offsetFields = htons((tcpLength/4)<<12 | flags);
    tcp->windowSize = htons(1460);
    tcp->urgentPointer = htons(0);

    ip->length = htons(ipHeaderLength + tcpLength);
    calcIpChecksum(ip);                  // 32-bit sum over ip header
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

/*
// Send TCP message
void sendTcpMessage(etherHeader *ether, socket *s, uint16_t flags, uint8_t data[], uint16_t dataSize)
{
    uint8_t localIpAddress[4];
    uint8_t localHwAddress[6];
    uint8_t *options, *dataPointer;
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
    options = tcp->data;

    *options++ = 2;
    *options++ = 4;
    *options++ = 0x05;      // MSS = 1460
    *options++ = 0xB4;
    *options++ = 1;         // No-Operation
    *options++ = 1;         // padding so data begins at new word
    *options++ = 1;
    *options++ = 0;         // End of Option List

    dataPointer = options;

    // TCP header
    tcpLength = sizeof(tcpHeader) + (options - tcp->data);   // typical: 20 + options (no data)
    tcp->sourcePort = htons(s->localPort);
    tcp->destPort = htons(s->remotePort);
    tcp->sequenceNumber = htonl(clientSequence);
    tcp->acknowledgementNumber = htonl(serverSequence + payloadSize);
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
*/
