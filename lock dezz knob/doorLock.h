#ifndef DOORLOCK_H_
#define DOORLOCK_H_


//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------
void initDoor();
void processChallengeRequestSendPublicKey(uint8_t *data, uint16_t dataSize);
void processChallengeRespond(uint8_t *data, uint16_t dataLength);
void processKey(uint8_t *data);
void sendChallengeResponse(char *data, uint16_t dataLength);
void sendLockStatus();
void sendDoorMessage(etherHeader *ether, socket *s);

#endif
