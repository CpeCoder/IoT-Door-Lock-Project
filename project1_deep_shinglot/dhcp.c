// DHCP Library
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
#include "dhcp.h"
#include "arp.h"
#include "timer.h"

#define DHCP_OP_CLIENT  1
#define DHCP_OP_SERVER  2
#define DHCP_HTYPE      1
#define DHCP_HLEN       6
#define DHCP_HOPS       0
#define DHCP_CLINT      68
#define DHCP_SERVER     67
#define MAGIC_COOKIE    0x63825363
#define CHADDR_SIZE     16
#define DHCP_DATA_SIZE  192
#define DHCP_TYPE       53

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPDECLINE  4
#define DHCPACK      5
#define DHCPNAK      6
#define DHCPRELEASE  7
#define DHCPINFORM   8

#define DHCP_DISABLED   0
#define DHCP_INIT       1
#define DHCP_SELECTING  2
#define DHCP_REQUESTING 3
#define DHCP_TESTING_IP 4
#define DHCP_BOUND      5
#define DHCP_RENEWING   6
#define DHCP_REBINDING  7
#define DHCP_INITREBOOT 8 // not used since ip not stored over reboot
#define DHCP_REBOOTING  9 // not used since ip not stored over reboot

#define MAX_MESSAGE_SIZE 20
#define MAX_DHCP_REQUEST_RETRIES 4
// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint32_t xid = 0;
uint32_t leaseSeconds = 0;
uint32_t leaseT1 = 0;
uint32_t leaseT2 = 0;
uint8_t dhcpRequestTries = 0;
int k = 0;
// use these variables if you want
bool discoverNeeded = false;
bool requestNeeded = false;
bool releaseNeeded = false;
bool arpNeeded = false;
bool ipConflictDetectionMode = false;

uint8_t dhcpOfferedIpAdd[4];
uint8_t dhcpServerIpAdd[4];

uint8_t dhcpState = DHCP_DISABLED;
bool    dhcpEnabled = true;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// State functions

void setDhcpState(uint8_t state)
{
    dhcpState = state;
}

uint8_t getDhcpState()
{
    return dhcpState;
}

// New address functions
// Manually requested at start-up
// Discover messages sent every 15 seconds

void callbackDhcpGetNewAddressTimer()
{
}

void requestDhcpNewAddress()
{
}

// Renew functions
void renewDhcp()
{
    if(dhcpEnabled && dhcpState == DHCP_BOUND)
    {
        requestNeeded = true;
    }
}

void callbackDhcpT1PeriodicTimer()
{
    requestNeeded = true;
}

void callbackDhcpT1HitTimer()
{
    requestNeeded = true;
    dhcpState = DHCP_RENEWING;
    startPeriodicTimer((_callback)callbackDhcpT1PeriodicTimer, 10);
}

// Rebind functions
void callbackDhcpT2PeriodicTimer()
{
    requestNeeded = true;
}

void callbackDhcpT2HitTimer()
{
    requestNeeded = true;
    dhcpState = DHCP_REBINDING;
    startPeriodicTimer((_callback)callbackDhcpT2PeriodicTimer, 10);
}

// End of lease timer
void callbackDhcpLeaseEndTimer()
{
    releaseNeeded = true;
    if(dhcpEnabled)
    {
        dhcpState = DHCP_INIT;
        discoverNeeded = true;
    }
    else
    {
        dhcpState = DHCP_DISABLED;
    }
}

// Release functions

void releaseDhcp()
{
    if(dhcpEnabled)
    {
        stopAllTimers();
        releaseNeeded = true;
        dhcpState = DHCP_DISABLED;
    }
}

// IP conflict detection

void callbackDhcpIpConflictWindow()
{
    dhcpState = DHCP_SELECTING;
    requestNeeded = true;
}

void requestDhcpIpConflictTest()
{
    setIpAddress(dhcpOfferedIpAdd);
    dhcpState = DHCP_BOUND;
}

bool isDhcpIpConflictDetectionMode()
{
    return ipConflictDetectionMode;
}

// Lease functions

uint32_t getDhcpLeaseSeconds()
{
    return leaseSeconds;
}

// Determines whether packet is DHCP
// Must be a UDP packet
bool isDhcpResponse(etherHeader* ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    bool ok;
    ok = (htons(udp->destPort) == DHCP_CLINT);
    if(ok)
    {
        dhcpFrame *dhcp = (dhcpFrame*)(getUdpData(ether));
        uint8_t *options = dhcp->options;
        while(*options != 0xFF)
        {
            if(*options++ == 53)
            {
                return ok;
            }
        }
    }
    ok = false;
    return ok;
}

// Send DHCP message
void sendDhcpMessage(etherHeader *ether, uint8_t type)
{
    uint16_t udpLength;
    uint8_t localIpAddress[4];
    uint8_t localHwAddress[6];
    uint8_t i;
    uint32_t sum;
    uint16_t tmp16;
    uint8_t *options;
    char message[50];

    // Ether frame
    getEtherMacAddress(localHwAddress);
    getIpAddress(localIpAddress);

    // IP header
    ipHeader* ip = (ipHeader*)ether->data;
    ip->rev = 0x4;
    ip->size = 0x5;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = PROTOCOL_UDP;
    ip->headerChecksum = 0;

    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = 0xFF;
        ether->sourceAddress[i] = localHwAddress[i];
    }
    ether->frameType = htons(TYPE_IP);

    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        if(dhcpState == DHCP_RENEWING || dhcpState == DHCP_BOUND)
        {
            ip->destIp[i] = dhcpServerIpAdd[i];
        }
        else
        {
            ip->destIp[i] = 0xFF;
        }
        ip->sourceIp[i] = localIpAddress[i];
    }

    uint8_t ipHeaderLength = ip->size * 4;
    // UDP header
        //  UDP is encapsulated in IP for IP networks, so start UDP after IP
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + (ip->size * 4));
    dhcpFrame* dhcp = (dhcpFrame*)((uint8_t*)udp + sizeof(udpHeader));
    udp->sourcePort = htons(DHCP_CLINT);        // source port 68 for dhcp discover
    udp->destPort = htons(DHCP_SERVER);         // destination port 67 dhcp discover

    dhcp->op = DHCP_OP_CLIENT;
    dhcp->htype = DHCP_HTYPE;
    dhcp->hlen = DHCP_HLEN;
    dhcp->hops = DHCP_HOPS;
    dhcp->xid = htonl(xid);
    dhcp->secs = htons(0);
    dhcp->flags = htons(0);

    options = dhcp->options;    // allows arthmetic init

    for(i = 0; i < CHADDR_SIZE; i++)
    {
        if(i < 6)           // size of my MAC address, left field init to 0
        {
            dhcp->chaddr[i] = localHwAddress[i];
        }
        else
        {
            dhcp->chaddr[i] = 0;
        }
    }
    for(i = 0; i < DHCP_DATA_SIZE; i++)
    {
        dhcp->data[i] = 0;
    }
    dhcp->magicCookie = htonl(MAGIC_COOKIE);

    switch(type)
    {
        case DHCPDISCOVER:          //-----------------------------------
            // could imply mmcpy to init to 0
            for(i = 0; i < IP_ADD_LENGTH; i++)
            {
                dhcp->ciaddr[i] = 0;
                dhcp->yiaddr[i] = 0;
                dhcp->siaddr[i] = 0;
                dhcp->giaddr[i] = 0;
            }

            *options++ = DHCP_TYPE;    // Defines DHCP Message Type, option 53
            *options++ = 1;     // length
            *options++ = DHCPDISCOVER;
            *options++ = 50;    // IP requested
            *options++ = 4;
            *options++ = 192;
            *options++ = 168;
            *options++ = 1;
            *options++ = 123;
            *options++ = 55;    // Parameter Request
            *options++ = 4;
            *options++ = 1;
            *options++ = 3;
            *options++ = 15;
            *options++ = 6;
            *options++ = 0xFF;  // 255, end of options
            break;

        case DHCPREQUEST:           //-----------------------------------
            // could imply mmcpy to init to 0
            for(i = 0; i < IP_ADD_LENGTH; i++)
            {
                dhcp->ciaddr[i] = dhcpOfferedIpAdd[i];
                dhcp->yiaddr[i] = 0;
                dhcp->siaddr[i] = 0;
                dhcp->giaddr[i] = 0;
            }

            *options++ = DHCP_TYPE;    // Defines DHCP Message Type, option 53
            *options++ = 1;     // length
            *options++ = DHCPREQUEST;

            if(dhcpState != DHCP_RENEWING && dhcpState != DHCP_BOUND)
            {
                *options++ = 50;    // IP requested
                *options++ = 4;
                *options++ = dhcpOfferedIpAdd[0];
                *options++ = dhcpOfferedIpAdd[1];
                *options++ = dhcpOfferedIpAdd[2];
                *options++ = dhcpOfferedIpAdd[3];
            }
            if(dhcpState != DHCP_REBINDING && dhcpState != DHCP_RENEWING && dhcpState != DHCP_BOUND)
            {
                *options++ = 54;    // Srever Identifier
                *options++ = 4;
                *options++ = dhcpServerIpAdd[0];
                *options++ = dhcpServerIpAdd[1];
                *options++ = dhcpServerIpAdd[2];
                *options++ = dhcpServerIpAdd[3];
            }
            *options++ = 0xFF;  // 255, end of options
            break;

        case DHCPDECLINE:
            strncpy(message,"IP not available",MAX_MESSAGE_SIZE);
            //could imply mmcpy to init to 0
            for(i = 0; i < IP_ADD_LENGTH; i++)
            {
                dhcp->ciaddr[i] = 0;
                dhcp->yiaddr[i] = 0;
                dhcp->siaddr[i] = 0;
                dhcp->giaddr[i] = 0;
            }

            *options++ = DHCP_TYPE;    // Defines DHCP Message Type, option 53
            *options++ = 1;     // length
            *options++ = DHCPDECLINE;
            *options++ = 50;    // IP requested
            *options++ = 4;
            *options++ = dhcpOfferedIpAdd[0];
            *options++ = dhcpOfferedIpAdd[1];
            *options++ = dhcpOfferedIpAdd[2];
            *options++ = dhcpOfferedIpAdd[3];
            *options++ = 54;    // Srever Identifier
            *options++ = 4;
            *options++ = dhcpServerIpAdd[0];
            *options++ = dhcpServerIpAdd[1];
            *options++ = dhcpServerIpAdd[2];
            *options++ = dhcpServerIpAdd[3];
            *options++ = 56;    // Message
            *options++ = MAX_MESSAGE_SIZE;
            strncpy((char*)options, message,MAX_MESSAGE_SIZE);
            options += MAX_MESSAGE_SIZE;
            *options++ = 0xFF;  // 255, end of options
            break;

        case DHCPRELEASE:
            strncpy(message,"IP released",MAX_MESSAGE_SIZE);
            for(i = 0; i < IP_ADD_LENGTH; i++)
            {
                dhcp->ciaddr[i] = dhcpOfferedIpAdd[i];
                dhcp->yiaddr[i] = 0;
                dhcp->siaddr[i] = 0;
                dhcp->giaddr[i] = 0;
            }

            *options++ = DHCP_TYPE;    // Defines DHCP Message Type, option 53
            *options++ = 1;     // length
            *options++ = DHCPRELEASE;
            *options++ = 54;    // Srever Identifier
            *options++ = 4;
            *options++ = dhcpServerIpAdd[0];
            *options++ = dhcpServerIpAdd[1];
            *options++ = dhcpServerIpAdd[2];
            *options++ = dhcpServerIpAdd[3];
            *options++ = 56;    // Message
            *options++ = MAX_MESSAGE_SIZE;
            strncpy((char*)options, message, MAX_MESSAGE_SIZE);
            options += MAX_MESSAGE_SIZE;
            *options++ = 0xFF;  // 255, end of options
            break;
    }
    udpLength = sizeof(udpHeader) +  sizeof(dhcpFrame) + (options - dhcp->options);//14
    ip->length = htons(ipHeaderLength + udpLength);
    // 32-bit sum over ip header
    calcIpChecksum(ip);
    // set udp length
    udp->length = htons(udpLength);

    // 32-bit sum over pseudo-header
    sum = 0;
    sumIpWords(ip->sourceIp, 8, &sum);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    sumIpWords(&udp->length, 2, &sum);
    // add udp header
    udp->check = 0;
    sumIpWords(udp, udpLength, &sum);
    udp->check = getIpChecksum(sum);

    putEtherPacket(ether, sizeof(etherHeader) + ipHeaderLength + udpLength);
}

// Assumed isDhcp/Udp isDhcpResponse is checked off
    // Points to requested option
uint8_t* getDhcpOption(etherHeader *ether, uint8_t getOption, uint8_t *length)
{
    dhcpFrame *dhcp = (dhcpFrame *)(getUdpData(ether));
    uint8_t *options = dhcp->options;

    while(*options != 0xFF)
    {
        if(*options++ == getOption)
        {
            *length = *options++;
            return options;
        }
        else
        {
            *length = *options++;
            options += *length;
        }
    }
    return NULL;
}

// Determines whether packet is DHCP offer response to DHCP discover
// Must be a UDP packet
bool isDhcpOffer(etherHeader *ether)
{
    dhcpFrame *dhcp = (dhcpFrame*)(getUdpData(ether));
    bool ok;
    uint8_t localHwAddress[6];
    getEtherMacAddress(localHwAddress);
    uint8_t length, i;
    ok = ((*getDhcpOption(ether, 53, &length)) == (uint8_t)DHCPOFFER);
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ok &= (localHwAddress[i] == ether->destAddress[i]);
    }
    ok &= ((dhcp->op) == DHCP_OP_SERVER);
    return ok;
}

// Determines whether packet is DHCP ACK response to DHCP request
// Must be a UDP packet
bool isDhcpAck(etherHeader *ether)
{
    dhcpFrame *dhcp = (dhcpFrame*)(getUdpData(ether));
    bool ok;
    uint8_t localHwAddress[6];
    uint8_t length, i;
    ok = ((*getDhcpOption(ether, 53, &length)) == (uint8_t)DHCPACK);
    ok &= ((dhcp->op) == DHCP_OP_SERVER);
    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ok &= (localHwAddress[i] == ether->destAddress[i]);
    }
    return ok;
}

// Handle a DHCP ACK
void handleDhcpAck(etherHeader *ether)
{
    uint8_t length, i;
    uint8_t *lease  = getDhcpOption(ether, 51, &length);
    if(lease != NULL)
    {
        leaseSeconds = 0;
        for(i = 0; i < length; i++)
        {
            leaseSeconds <<= 8;
            leaseSeconds += lease[i];
        }
        leaseT1 = leaseSeconds * 0.5;
        leaseT2 = leaseSeconds * 0.875;
    }

    startOneshotTimer((_callback)callbackDhcpT1HitTimer, leaseT1);
    startOneshotTimer((_callback)callbackDhcpT2HitTimer, leaseT2);
    startOneshotTimer((_callback)callbackDhcpLeaseEndTimer, leaseSeconds);

    uint8_t *subnetMask  = getDhcpOption(ether, 1, &length);
    if(subnetMask != NULL)
    {
        setIpSubnetMask(subnetMask);
    }
    uint8_t *gatewayAdd = getDhcpOption(ether, 3, &length);
    if(gatewayAdd != NULL)
    {
        setIpGatewayAddress(gatewayAdd);
    }
    uint8_t *dnsAdd = getDhcpOption(ether, 6, &length);
    if(dnsAdd != NULL)
    {
        setIpDnsAddress(dnsAdd);
    }
}

// Message requests

void callbackDhcpDiscoverNeeded()
{
    dhcpState = DHCP_INIT;
    discoverNeeded = true;
}

void callbackDhcpRequestNeeded()
{
    if(dhcpRequestTries < MAX_DHCP_REQUEST_RETRIES)
    {
        dhcpState = DHCP_SELECTING;
        requestNeeded = true;
        dhcpRequestTries++;
    }
    else
    {
        dhcpState = DHCP_INIT;
        discoverNeeded = true;
        dhcpRequestTries = 0;
    }
}

void sendDhcpPendingMessages(etherHeader *ether)
{
    if(releaseNeeded)
    {
        uint8_t i;
        sendDhcpMessage(ether, DHCPRELEASE);

        dhcpOfferedIpAdd[0] = 192;
        dhcpOfferedIpAdd[1] = 168;
        dhcpOfferedIpAdd[2] = 1;
        dhcpOfferedIpAdd[3] = 123;
        for(i = 0; i < IP_ADD_LENGTH; i++)
        {
            dhcpServerIpAdd[i] = 0;
        }
        setIpAddress(dhcpOfferedIpAdd);         // {0,0,0,0}
        setIpSubnetMask(dhcpServerIpAdd);
        setIpGatewayAddress(dhcpServerIpAdd);
        setIpDnsAddress(dhcpServerIpAdd);

        if(dhcpState != DHCP_INIT)
        {
            dhcpEnabled = false;
        }
        releaseNeeded = false;
    }
    switch(dhcpState)
    {
    case DHCP_DISABLED:         // do nothing
        break;
    case DHCP_INIT:             // send discover
        if(discoverNeeded)
        {
            startPeriodicTimer((_callback)callbackDhcpDiscoverNeeded, 15);
            sendDhcpMessage(ether, DHCPDISCOVER);
            dhcpState = DHCP_SELECTING;
            discoverNeeded = false;
        }
        break;
    case DHCP_SELECTING:        // send request | stay here and process first one
        if(requestNeeded)
        {
            stopTimer((_callback)callbackDhcpDiscoverNeeded);
            startOneshotTimer((_callback)callbackDhcpRequestNeeded, 15);
            sendDhcpMessage(ether, DHCPREQUEST);
            dhcpState = DHCP_REQUESTING;
            requestNeeded = false;
        }
        break;
    case DHCP_REQUESTING:       // record lease & set T1 & T2 |
        if(arpNeeded)
        {
            stopTimer((_callback)callbackDhcpRequestNeeded);
            uint8_t localIpAddress[IP_ADD_LENGTH];
            getIpAddress(localIpAddress);
            sendArpRequest(ether, localIpAddress, dhcpOfferedIpAdd);
            dhcpState = DHCP_TESTING_IP;
            arpNeeded = false;
        }
        break;
    case DHCP_TESTING_IP:       // send ARP request wait a bit send ARP respond
        if(isDhcpIpConflictDetectionMode())
        {
            stopTimer((_callback)requestDhcpIpConflictTest);
            startOneshotTimer((_callback)callbackDhcpIpConflictWindow, 15);
            stopTimer((_callback)callbackDhcpLeaseEndTimer);
            stopTimer((_callback)callbackDhcpT1HitTimer);
            stopTimer((_callback)callbackDhcpT2HitTimer);
            sendDhcpMessage(ether, DHCPDECLINE);
            ipConflictDetectionMode = false;
        }
        break;
    case DHCP_BOUND:            // T1 expires send request
        if(requestNeeded)
        {
            sendDhcpMessage(ether, DHCPREQUEST);
            requestNeeded = false;
            dhcpState = DHCP_BOUND;
        }
        if(arpNeeded)
        {
            dhcpState = DHCP_BOUND;
            arpNeeded = false;
        }
        break;
    case DHCP_RENEWING:         // ACK get lease go to BOUND | T2 expires broadcast request | NACK go to INIT
        if(requestNeeded)
        {
            sendDhcpMessage(ether, DHCPREQUEST);
            requestNeeded = false;
        }
        else if(arpNeeded)
        {
            dhcpState = DHCP_BOUND;
            arpNeeded = false;
        }
        break;
    case DHCP_REBINDING:        // Ack get lease go to BOUND | NACK go to INIT
        if(requestNeeded)
        {
            sendDhcpMessage(ether, DHCPREQUEST);
            requestNeeded = false;
        }
        else if(arpNeeded)
        {
            dhcpState = DHCP_BOUND;
            arpNeeded = false;
        }
        break;
    }
}

void processDhcpResponse(etherHeader *ether)
{
    dhcpFrame *dhcp = (dhcpFrame*)(getUdpData(ether));
    uint8_t i;
    uint8_t length;

    if (isDhcpOffer(ether))
    {
        uint8_t *serverIP = getDhcpOption(ether, 54, &length);
        for(i = 0; i < length; i++)
        {
            dhcpOfferedIpAdd[i] = dhcp->yiaddr[i];
            dhcpServerIpAdd[i]  = serverIP[i];
        }
        discoverNeeded = false;
        requestNeeded = true;
    }
    else if(isDhcpAck(ether))
    {
        arpNeeded = true;
        if(dhcpState == DHCP_RENEWING)
        {
            stopTimer((_callback)callbackDhcpT1PeriodicTimer);
        }
        else if(dhcpState == DHCP_REBINDING)
        {
            stopTimer((_callback)callbackDhcpT2PeriodicTimer);
        }

        handleDhcpAck(ether);
        if(dhcpState == DHCP_REQUESTING)
        {
            startOneshotTimer((_callback)requestDhcpIpConflictTest, 15);
        }
        else
        {
            setIpAddress(dhcpOfferedIpAdd);
            dhcpState = DHCP_BOUND;
        }
    }
}

void processDhcpArpResponse(etherHeader *ether)
{
    bool ok = true;
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i;
    uint8_t localHwAddress[HW_ADD_LENGTH];
    uint8_t localIpAddress[IP_ADD_LENGTH];

    getIpAddress(localIpAddress);
    getEtherMacAddress(localHwAddress);

    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ok &= (arp->destAddress[i] == localHwAddress[i]);
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        ok &= arp->sourceIp[i] == localIpAddress[i];
    }
    if(ok)
    {
        ipConflictDetectionMode = true;
        stopTimer((_callback)requestDhcpIpConflictTest);
    }
}

// DHCP control functions

void enableDhcp()
{
    if(dhcpState == DHCP_DISABLED)
    {
        discoverNeeded = false;
        requestNeeded = false;
        releaseNeeded = false;
        arpNeeded = false;
        ipConflictDetectionMode = false;

        startPeriodicTimer((_callback)callbackDhcpDiscoverNeeded, 15);
        dhcpState = DHCP_INIT;
        discoverNeeded = true;
    }
    if(xid == 0)
    {
        xid = random32();
    }
    dhcpEnabled = true;
}

void disableDhcp()
{
    if(dhcpState != DHCP_INIT || dhcpState != DHCP_SELECTING)
    {
        releaseNeeded = true;
        dhcpEnabled = false;
    }
    dhcpState = DHCP_DISABLED;
}

bool isDhcpEnabled()
{
    return dhcpEnabled;
}
