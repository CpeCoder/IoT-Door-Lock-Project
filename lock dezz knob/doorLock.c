//Ryan Dharmadi

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gpio.h"
#include "nvic.h"
#include "mqtt.h"
#include "tm4c123gh6pm.h"

#define openDoor PORTC,7
#define closeDoor PORTD,6
#define kickInt PORTB,2

char token[50];
uint8_t flag = 1, tokenSize;
const uint16_t password = 40925;
uint16_t interruptCounter = 0;
bool motorNeeded = false, doorStatus = false, doorTampered = false, keyRequest = false,
        isSendChallengeResponse = false;

int range, publicKey, privateKey;
char clientArr[5];

int gcd(int a, int b) {
    if (b == 0)
        return a;
    return gcd(b, a % b);
}

// Function to calculate modulo inverse of a number
int mod_inverse(int a, int m) {
    int m0 = m, t, q;
    int x0 = 0, x1 = 1;

    if (m == 1)
        return 0;

    // Apply extended Euclid Algorithm
    while (a > 1) {
        q = a / m;
        t = m;
        m = a % m, a = t;
        t = x0;
        x0 = x1 - q * x0;
        x1 = t;
    }

    // Make x1 positive
    if (x1 < 0)
        x1 += m0;

    return x1;
}

// Function to perform modular exponentiation
int mod_exp(int base, unsigned int exp, int mod) {
    int result = 1;
    base = base % mod;

    while (exp > 0) {
        if (exp % 2 == 1)
            result = (result * base) % mod;

        exp = exp / 2;
        base = (base * base) % mod;
    }
    return result;
}

// Function to generate keys
void generate_keys(int p, int q, int *n, int *e, int *d) {
    *n = p * q;
    int phi = (p - 1) * (q - 1);

    // Choose e such that e and phi(n) are coprime
    for (*e = 2; *e < phi; (*e)++) {
        if (gcd(*e, phi) == 1)
            break;
    }

    // Calculate d (private key)
    *d = mod_inverse(*e, phi);
}

// Function to encrypt plaintext
int encrypt(int plaintext, int e, int n) {
    return mod_exp(plaintext, e, n);
}

// Function to decrypt ciphertext
int decrypt(int ciphertext, int d, int n) {
    return mod_exp(ciphertext, d, n);
}

void processChallengeRequestSendPublicKey(uint8_t *data, uint16_t dataSize)
{
    int prime1, prime2, i; //prime number
    prime1 = 211; // First prime number
    prime2 = 197; // Second prime number
    generate_keys(prime1, prime2, &range, &publicKey, &privateKey);

    clientArr[0] = (char) (publicKey >> 8);
    clientArr[1] = (char) (publicKey & 0xFF);
    clientArr[2] = (char) (range >> 8);
    clientArr[3] = (char) (range & 0xFF);
    clientArr[4] = NULL;

    keyRequest = true;

    tokenSize = dataSize;
    for(i = 0; i < dataSize; i++)
    {
        token[i] = (char) data[i];
    }
    token[i] = NULL;
}

void processKey(uint8_t *data)
{
    publicKey = (uint16_t)((data[0] << 8) & 0xFF00);
    publicKey += (uint16_t)(data[1] & 0x00FF);
    range = (uint16_t)((data[2] << 8) & 0xFF00);
    range += (uint16_t)(data[3] & 0x00FF);
}

void sendChallengeResponse(char *data, uint16_t dataLength)
{
    int clientPassword = atoi(data);
    int encryptedData = encrypt(clientPassword, (int)publicKey, (int)range);

    snprintf(data, 6, "%d", encryptedData);
}

void processChallengeRespond(uint8_t *data, uint16_t dataLength)
{
    int clientInput = atoi((char *)data);
    int decryptedData = decrypt(clientInput, (int)privateKey, (int)range);

    if(decryptedData == password)
    {
        if(!strcmp(token, "open"))
        {
            motorNeeded = true;
            doorStatus = false;
        }
        else if(!strcmp(token, "close"))
        {
            motorNeeded = true;
            doorStatus = true;
        }
    }
}

void sendDoorMessage(etherHeader *ether, socket *s)
{
    if(doorTampered)
    {
        char str[] = "you ROBBED!";
        publishMqtt("uta/lock/kickin", str);
        doorTampered = false;
    }
    if(motorNeeded)
    {
        if(!doorStatus)
        {
            setPinValue(openDoor, 1);
            motorNeeded = false;
        }
        else
        {
            setPinValue(closeDoor, 1);
            motorNeeded = false;
        }
        TIMER1_CTL_R |= TIMER_CTL_TAEN;
    }
    if(keyRequest)
    {
        publishMqtt("uta/lock/get_public_key" , clientArr);
        keyRequest = false;
    }
    if(isSendChallengeResponse)
    {
        publishMqtt("uta/lock/challenge_response" , clientArr);
        isSendChallengeResponse = false;
    }
}

void initDoor()
{
    SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R0 | SYSCTL_RCGCTIMER_R1;
    _delay_cycles(3);

    enablePort(PORTB);
    enablePort(PORTD);
    enablePort(PORTC);
    selectPinPushPullOutput(openDoor);
    selectPinPushPullOutput(closeDoor);
    selectPinDigitalInput(kickInt);

    enablePinInterrupt(kickInt);
    selectPinInterruptRisingEdge(kickInt);
    enableNvicInterrupt(17);

    //Timer for kick detection cool-down of 2 seconds
    TIMER0_CTL_R &= ~TIMER_CTL_TAEN;
    TIMER0_CFG_R = TIMER_CFG_32_BIT_TIMER;
    TIMER0_TAMR_R = TIMER_TAMR_TAMR_1_SHOT | TIMER_TAMR_TACDIR;
    TIMER0_TAILR_R = 200e6;
    TIMER0_IMR_R = TIMER_IMR_TATOIM;
    enableNvicInterrupt(INT_TIMER0A);

    //Turn off GPIO on H-bridge and publish message locked/unlocked
    TIMER1_CTL_R &= ~TIMER_CTL_TAEN;
    TIMER1_CFG_R = TIMER_CFG_32_BIT_TIMER;
    TIMER1_TAMR_R = TIMER_TAMR_TAMR_1_SHOT | TIMER_TAMR_TACDIR;
    TIMER1_TAILR_R = 200e6;
    TIMER1_IMR_R = TIMER_IMR_TATOIM;
    enableNvicInterrupt(INT_TIMER1A);
}

// Kick debounce cool-down
void lockTimeout()
{
    TIMER0_ICR_R = TIMER_ICR_TATOCINT;
    TIMER0_CTL_R &= ~TIMER_CTL_TAEN;
    flag = 1;
    interruptCounter = 0;
}

// 5 second timer for stating door state
void pinClear()
{
    TIMER1_ICR_R = TIMER_ICR_TATOCINT;
    TIMER1_CTL_R &= ~TIMER_CTL_TAEN;
    if(getPinValue(openDoor))
    {
        setPinValue(openDoor, 0);
        //putsUart0("Unlocked\n");        //Publish door was unlocked
    }
    if(getPinValue(closeDoor))
    {
        setPinValue(closeDoor, 0);
        //putsUart0("Locked\n");          //Publish door was locked
    }
}

// on the high vibration isr is occured to send alert
void kickIsr()
{
    if(doorStatus)
    {
        interruptCounter++;
        if(flag)
        {
            TIMER0_CTL_R |= TIMER_CTL_TAEN;
            flag = 0;
        }
        if(interruptCounter == 150)
        {
            doorTampered = true;
      //      putsUart0("you got robbed\n");      //Publish that kick was detected
        }
    }
    clearPinInterrupt(kickInt);
}
