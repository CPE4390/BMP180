#include <xc.h>
#include <stdio.h>
#include "LCD.h"


#pragma config FOSC=HSPLL
#pragma config WDTEN=OFF
#pragma config XINST=OFF


/*
Connections:
        Master RD5 <-> SDA
        Master RD6 <-> SCL
        pullups already on board
 */

void InitPins(void);
void ConfigInterrupts(void);
void ConfigPeriph(void);

void ReadBMP180Calibration(void);
void ReadUT(void);
void ReadUP(void);
void Calculate(void);
long CalculateAltitude(long pressure);

void I2CWriteRegister(unsigned char reg, unsigned char byte);
void I2CReadData(unsigned char reg, char count);

#define _XTAL_FREQ 32000000L

char line1str[17];
char line2str[17];

union {
    struct {
        int AC1;
        int AC2;
        int AC3;
        unsigned int AC4;
        unsigned int AC5;
        unsigned int AC6;
        int B1;
        int B2;
        int MB;
        int MC;
        int MD;
    };
    unsigned char bytes[22];
} cal;

unsigned char buffer[22];
long UT;
long UP;
long temp;
long pressure;

char oss = 3;

void main(void) {
    long i;
    OSCTUNEbits.PLLEN = 1;
    LCDInit();
    LCDClear();
    InitPins();
    ConfigPeriph();
    ConfigInterrupts();
    ReadBMP180Calibration();
    while (1) {
        ReadUT();
        ReadUP();
        Calculate();
        sprintf(line1str, "%.1f C", temp / 10.0);
        LCDClearLine(0);
        LCDWriteLine(line1str, 0);
        sprintf(line2str, "%ld Pa", pressure);
        LCDClearLine(1);
        LCDWriteLine(line2str, 1);
        __delay_ms(1000);
    }
}

void ReadBMP180Calibration(void) {
    int i;
    I2CReadData(0xAA, 22);
    //swap bytes
    for (i = 0; i < 11; ++i) {
        cal.bytes[2 * i] = buffer[2 * i + 1];
        cal.bytes[2 * i + 1] = buffer[2 * i];
    }
}

void ReadUT(void) {
    I2CWriteRegister(0xf4, 0x2e);
    __delay_ms(5);
    I2CReadData(0xf6, 2);
    UT = buffer[0];
    UT <<= 8;
    UT += buffer[1];
}

void ReadUP(void) {
    I2CWriteRegister(0xf4, 0x34 + (oss << 6));
    __delay_ms(26);
    I2CReadData(0xf6, 3);
    UP = buffer[0];
    UP <<= 8;
    UP += buffer[1];
    UP <<= 8;
    UP += buffer[2];
    UP >>= (8 - oss);
}

void Calculate(void) {
    long X1, X2, X3, B3, B5, B6;
    unsigned long B4, B7;
    //Calculate temp
    X1 = (UT - cal.AC6) * cal.AC5 / 32768L;
    X2 = (long)cal.MC * 2048 / (X1 + cal.MD);
    B5 = X1 + X2;
    temp = (B5 + 8) / 16;

    //Calculate pressure
    B6 = B5 - 4000;
    X1 = (cal.B2 * (B6 * B6 / 4096)) / 2048;
    X2 = cal.AC2 * B6 / 2048;
    X3 = X1 + X2;
    B3 = (((cal.AC1 * 4 + X3) << oss) + 2) / 4;
    X1 = cal.AC3 * B6 / 8192;
    X2 = (cal.B1 * (B6 * B6 / 4096)) / 65536L;
    X3 = ((X1 + X2) + 2) / 4;
    B4 = cal.AC4 * (unsigned long)(X3 + 32768L) / 32768L;
    B7 = ((unsigned long) UP - B3) * (50000 >> oss);
    if (B7 < 0x80000000UL) {
        pressure = (B7 * 2) / B4;
    } else {
        pressure = (B7 / B4) * 2;
    }
    X1 = (pressure / 256) * (pressure / 256);
    X1 = (X1 * 3038) / 65536L;
    X2 = (-7357 * pressure) / 65536L;
    pressure = pressure + (X1 + X2 + 3791) / 16;
}

void I2CWriteRegister(unsigned char reg, unsigned char byte) {
    char data;
    SSP2CON2bits.SEN = 1; //Start condition
    while (SSP2CON2bits.SEN == 1); //Wait for start to finish
    data = SSP2BUF; //Read SSPxBUF to make sure BF is clear
    SSP2BUF = 0xEE; //address with R/W clear for write
    while (SSP2STATbits.BF || SSP2STATbits.R_W); // wait until write cycle is complete
    SSP2BUF = reg; //Send register
    while (SSP2STATbits.BF || SSP2STATbits.R_W); // wait until write cycle is complete
    SSP2BUF = byte; //Send byte
    while (SSP2STATbits.BF || SSP2STATbits.R_W); // wait until write cycle is complete
    SSP2CON2bits.PEN = 1; //Stop condition
    while (SSP2CON2bits.PEN == 1); //Wait for stop to finish
}

void I2CReadData(unsigned char reg, char count) {
    char i;
    SSP2CON2bits.SEN = 1; //Start condition
    while (SSP2CON2bits.SEN == 1); //Wait for start to finish
    i = SSP2BUF; //Read SSPxBUF to make sure BF is clear
    SSP2BUF = 0xEE; //address with R/W clear for write
    while (SSP2STATbits.BF || SSP2STATbits.R_W); // wait until write cycle is complete
    SSP2BUF = reg; //Send register
    while (SSP2STATbits.BF || SSP2STATbits.R_W); // wait until write cycle is complete
    SSP2CON2bits.RSEN = 1; //Restart condition
    while (SSP2CON2bits.RSEN == 1); //Wait for restart to finish
    SSP2BUF = 0xEF; //address with R/W set for read
    while (SSP2STATbits.BF || SSP2STATbits.R_W); // wait until write cycle is complete
    for (i = 0; i < count; ++i) {
        SSP2CON2bits.RCEN = 1; // enable master for 1 byte reception
        while (!SSP2STATbits.BF); // wait until byte received
        buffer[i] = SSP2BUF;
        if (i == count - 1) {
            SSP2CON2bits.ACKDT = 1;
        } else {
            SSP2CON2bits.ACKDT = 0;
        }
        SSP2CON2bits.ACKEN = 1; //Send ACK/NACK
        while (SSP2CON2bits.ACKEN != 0);
    }
    SSP2CON2bits.PEN = 1; //Stop condition
    while (SSP2CON2bits.PEN == 1); //Wait for stop to finish
}

void InitPins(void) {
    LATD = 0; //LED's are outputs
    TRISD = 0; //Turn off all LED's


    //Set TRIS bits for any required peripherals here.
    TRISB = 0b00000001; //Button0 is input;
    INTCON2bits.RBPU = 0; //enable weak pullups on port B

    TRISD = 0b01100000; //MMSP2 uses RD5 as SDA, RD6 as SCL, both set as inputs

}

void ConfigInterrupts(void) {

    RCONbits.IPEN = 0; //no priorities.  This is the default.

    //Configure your interrupts here

    //set up INT0 to interrupt on falling edge
    INTCON2bits.INTEDG0 = 0; //interrupt on falling edge
    INTCONbits.INT0IE = 1; //Enable the interrupt
    //note that we don't need to set the priority because we disabled priorities (and INT0 is ALWAYS high priority when priorities are enabled.)
    INTCONbits.INT0IF = 0; //Always clear the flag before enabling interrupts

    INTCONbits.GIE = 1; //Turn on interrupts
}

void ConfigPeriph(void) {

    //Configure peripherals here

    SSP2ADD = 0x63; //100kHz
    SSP2CON1bits.SSPM = 0b1000; //I2C Master mode
    SSP2CON1bits.SSPEN = 1; //Enable MSSP
}

void __interrupt(high_priority) HighIsr(void) {
    unsigned char rx = -1;
    //Check the source of the interrupt
    if (INTCONbits.INT0IF == 1) {
        //source is INT0

        INTCONbits.INT0IF = 0; //must clear the flag to avoid recursive interrupts
    }
}


