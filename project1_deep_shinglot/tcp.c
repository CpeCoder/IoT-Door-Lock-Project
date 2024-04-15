// TCP Library
// Jason Losh

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

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

#define MAX_TCP_PORTS 4

uint32_t sequenceNumber;
uint32_t serverSeqNumber;
uint16_t tcpPorts[MAX_TCP_PORTS];
uint8_t tcpPortCount = 0;
uint8_t tcpState[MAX_TCP_PORTS];
socket sMQTT;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
void sendTcpMessage(etherHeader *ether, socket *s, uint16_t flags, uint8_t data[], uint16_t dataSize);
void sendTcpResponse(etherHeader *ether, socket* s, uint16_t flags);

//-----------------------------------------------------------------------------

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

// Determines whether packet is TCP packet
// Must be an IP packet
bool isTcp(etherHeader* ether)
{
    bool ok;
    ipHeader *ip = (ipHeader*)ether->data;
    ok = ip->protocol == PROTOCOL_TCP;
    return ok;
}

bool isTcpSyn(etherHeader *ether)
{
    bool ok;
    ipHeader *ip   = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)ip->data;
    uint16_t tcp_offset_msb = htons(tcp->offsetFields);
    ok = tcp_offset_msb & SYN;
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
        sequenceNumber = random32();
        serverSeqNumber = 0;
        tcpPorts[tcpPortCount] = 8080;
        tcpPortCount++;
        tcpState[0] = TCP_CLOSED;

        sMQTT.remoteIpAddress[0] = 192;
        sMQTT.remoteIpAddress[1] = 168;
        sMQTT.remoteIpAddress[2] = 1;
        sMQTT.remoteIpAddress[3] = 1;
/*        sMQTT->remoteHwAddress[0] = 0x6C;
        sMQTT->remoteHwAddress[1] = 0x02;
        sMQTT->remoteHwAddress[2] = 0xE0;
        sMQTT->remoteHwAddress[3] = 0x03;
        sMQTT->remoteHwAddress[4] = 0xAD;
        sMQTT->remoteHwAddress[5] = 0x20;*/
        sMQTT.remotePort = 8080;
        sMQTT.localPort = 6000;
        sMQTT.acknowledgementNumber = 0;
        sMQTT.sequenceNumber = 0;
    }

    uint8_t i;

    for(i = 0; i < tcpPortCount; i++)
    {
        switch(getTcpState(i))
        {
        case TCP_CLOSED:
            sendTcpResponse(ether, &sMQTT, SYN);
            setTcpState(i, TCP_SYN_RECEIVED);
            break;
        case TCP_SYN_RECEIVED:
            break;
        case TCP_SYN_SENT:
            sendTcpResponse(ether, &sMQTT, ACK);
            setTcpState(i, TCP_ESTABLISHED);
            break;
        case TCP_ESTABLISHED:
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

    for(i = 0; i < tcpPortCount; i++)
    {
        if(tcpSourcePort == tcpPorts[i])
        {
            break;
        }
    }
    if(isTcpSyn(ether) && isTcpAck(ether) && getTcpState(i)==TCP_SYN_RECEIVED)
    {
        setTcpState(i, TCP_SYN_SENT);
        serverSeqNumber = ntohl(tcp->sequenceNumber);
    }
/*    else if()
    {

    }*/

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
            tcpPorts[tcpPortCount] = 8080;
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
    tcp->sequenceNumber = htonl(sequenceNumber + 1);
    tcp->acknowledgementNumber = htonl(serverSeqNumber + 1);
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

// Send TCP message
void sendTcpMessage(etherHeader *ether, socket *s, uint16_t flags, uint8_t data[], uint16_t dataSize)
{
    uint8_t localIpAddress[4];
    uint8_t localHwAddress[6];
    uint8_t i, ipHeaderLength;
    uint8_t *options, *dataPointer;
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
    tcp->sequenceNumber = htonl(sequenceNumber + 1);
    tcp->acknowledgementNumber = htonl(serverSeqNumber + 1);
    tcp->offsetFields = htons((tcpLength/4)<<12 | flags);
    tcp->windowSize = htons(1460);
    tcp->urgentPointer = htons(0);

    tcpLength += dataSize;
    for(i = 0; i < dataSize; i++)
    {
        *dataPointer++ = data[i];
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
