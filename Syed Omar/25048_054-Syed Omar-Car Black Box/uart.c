#include <xc.h>
#include "uart.h"

void init_uart(unsigned long baud)
{
    SPEN = 1;     // Enable serial port
    CREN = 1;     // Enable continuous receive
    BRGH = 1;     // High speed baud rate

    SPBRG = (unsigned char)((FOSC / (16UL * baud)) - 1);

    RCIE = 1;     // Enable RX interrupt
}

unsigned char getchar(void)
{
    while (!RCIF)   // Wait until data is received
        ;
    return RCREG;   // Reading clears RCIF
}

void putchar(unsigned char data)
{
    while (!TXIF)   // Wait until TXREG is empty
        ;
    TXREG = data;   // Writing clears TXIF
}

void puts(const char *s)
{
    while (*s)
    {
        putchar((unsigned char)*s++);
    }
}