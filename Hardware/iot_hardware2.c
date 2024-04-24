//Ryan Dharmadi

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "gpio.h"
#include "nvic.h"
#include "clock.h"
#include "uart0.h"
#include "tm4c123gh6pm.h"

#define MAX_CHARS 80
char strInput[MAX_CHARS+1];
char* token;
uint8_t count = 0;

#define openDoor PORTC,7
#define closeDoor PORTD,6
#define kickInt PORTB,2

uint8_t flag = 1;

void initHw()
{
    // Initialize system clock to 40 MHz
    initSystemClockTo40Mhz();
    _delay_cycles(3);

    SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R0 | SYSCTL_RCGCTIMER_R1;
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
    TIMER0_TAILR_R = 80e6;
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

//Kick detection cool-down
void cooldown()
{
    TIMER0_ICR_R = TIMER_ICR_TATOCINT;
    TIMER0_CTL_R &= ~TIMER_CTL_TAEN;
    flag = 1;
}

void pinClear()
{
    TIMER1_ICR_R = TIMER_ICR_TATOCINT;
    TIMER1_CTL_R &= ~TIMER_CTL_TAEN;
    if(getPinValue(openDoor))
    {
        setPinValue(openDoor, 0);
        putsUart0("Unlocked\n");        //Publish door was unlocked
    }
    if(getPinValue(closeDoor))
    {
        setPinValue(closeDoor, 0);
        putsUart0("Locked\n");          //Publish door was locked
    }
}

void kickIsr()
{
    clearPinInterrupt(kickInt);
    if(flag)
    {
        putsUart0("you got robbed\n");              //Publish that kick was detected
        TIMER0_CTL_R |= TIMER_CTL_TAEN;
        flag = 0;
    }
}

//Receive correct key from subscriber to unlock/lock door
void receiver(char * token)
{
    if (strcmp(token, "open") == 0)
    {
        setPinValue(openDoor, 1);
        TIMER1_CTL_R |= TIMER_CTL_TAEN;
    }
    if (strcmp(token, "close") == 0)
    {
        setPinValue(closeDoor, 1);
        TIMER1_CTL_R |= TIMER_CTL_TAEN;
    }
}

void processShell()
{
    bool end;
    char c;

    if (kbhitUart0())
    {
        c = getcUart0();

        end = (c == 13) || (count == MAX_CHARS);
        if (!end)
        {
            if ((c == 8 || c == 127) && count > 0)
                count--;
            if (c >= ' ' && c < 127)
                strInput[count++] = c;
        }
        else
        {
            strInput[count] = '\0';
            count = 0;
            token = strtok(strInput, " ");
            receiver(token);
        }
    }
}

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

// Max packet is calculated as:
// Ether frame header (18) + Max MTU (1500)

int main(void)
{
    // Init controller
    initHw();

    // Setup UART0
    initUart0();
    setUart0BaudRate(115200, 40e6);

    // Init ethernet interface (eth0)
    putsUart0("\nStarting eth0\n");

    while(true)
        processShell();
}

