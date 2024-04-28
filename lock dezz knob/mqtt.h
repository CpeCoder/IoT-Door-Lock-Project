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

#ifndef MQTT_H_
#define MQTT_H_

#include <stdint.h>
#include <stdbool.h>
#include "tcp.h"

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void processMqttPublish(etherHeader *ether, uint16_t payload);
bool isPublishMessage(etherHeader *ether, uint16_t payload);
void sendPendingMqtt(etherHeader *ether);
void sendMqttMessage(etherHeader *ether, socket *s, uint16_t flags, uint8_t data[], uint16_t dataSize);
void connectMqtt();
void disconnectMqtt();
void publishMqtt(char strTopic[], char strData[]);
void subscribeMqtt(char strTopic[]);
void unsubscribeMqtt(char strTopic[]);

#endif

