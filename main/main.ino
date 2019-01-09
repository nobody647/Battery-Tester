//Purpose: Repeatedly charge and discharge a number of batteries, periodically measuring their voltages and sending that data over serial

#include "LowPower.h"

const int shift_register_clock_pin = 1; //clock for shift registers controlling transistors for charging
const int shift_register_data_pin = 2;  //pin where we put data that we want to shift out

const int mode_switch_button_pin = 3; //physical button for switching modes

const int spi_clock_pin = 4;
const int spi_in = 5;
const int spi_out = 6;

const int serial_baud = 9600; //baud rate to use for usb serial connection

const int number_of_batteries = 24;
const int number_of_ADCs = 3;
const int ADC_SS_pins[3] = {10, 11, 12}; //these are the pins used for slave selection. so in this example, we have 3 ADCs, first one with SS on pin 10, then pin 11, then pin 12

//Note that these "voltages" are not the actual voltages, but values returned by the ADC. Assuming a reference voltage of 5v, 4095 would be 5v, 0 would be 0v
const int upper_voltage_threshold = 3465; //any battery measured above this voltage will begin discharging. Around 4.2 volts
const int lower_voltage_threshold = 2460; //any battery below this voltage will begin charging. Around 3 volts

struct battery
{
    double voltage; //
    int mode;       //0 for nothing, 1 for charge, 2 for discharge
};

battery battery_list[number_of_batteries] = {};

void setup()
{
    pinMode(shift_register_clock_pin, OUTPUT);
    pinMode(shift_register_data_pin, OUTPUT);

    pinMode(mode_switch_button_pin, INPUT);

    pinMode(spi_clock_pin, OUTPUT);
    pinMode(spi_in, INPUT);
    pinMode(spi_out, OUTPUT);

    Serial.begin(serial_baud);

    //attachInterrupt(digitalPinToInterrupt(mode_switch_button_pin), changeMode, RISING);

    for (int i = 0; i < number_of_batteries; i++)
    {
        battery batt;
        batt.voltage = -1; //default value
        batt.mode = 1;     //default mode
        battery_list[i] = batt;
    }
}

void loop()
{

    handleBatteries(); //reads voltages for each battery, outputs them, and sets modes
    setModes();        //actually sends out the bits to the shift register chain based on the modes set in handleBatteries()

    LowPower.powerDown(SLEEP_4S, ADC_OFF, BOD_OFF); //Sleeps for 4 seconds. In effect, the main loop only runs every 4 seconds
}

void handleBatteries()
{ //Updates the voltage and mode for every battery. Gets run every loop
    for (int i = 0; i < number_of_batteries; i++)
    {
        battery batt = battery_list[i];

        int adcnum = i / 8;     //Which ADC the battery is connected to
        int adcchannel = i % 8; //Which channel of the ADC the battery is connected to

        batt.voltage = readADC(adcnum, adcchannel); //NOT ACTUAL VOLTAGE. value will be between 0 and 4095

        Serial.println("Batt " + String(i) + ": " + String(batt.voltage));

        if (batt.voltage > upper_voltage_threshold)
        {
            batt.mode = 2; //discharge
            Serial.println("Batt "+String(i)+" is now DISCHARGING");
        }
        if (batt.voltage < lower_voltage_threshold)
        {
            batt.mode = 1; //charge
            Serial.println("Batt "+String(i)+" is now CHARGING");
        }

        battery_list[i] = batt;
    }
}

void setModes()
{
    /*
    So essentially what this function does is shift out a sequence of bits depending on the modes of each battery.
    It works in reverse, from the last battery to the first. The way this is written right now assumes a few things
    about how the board is layed out. I'm assuming that each battery has two bits each on the shift register chain 
    (one for charge, one for discharge), and that those two bits will be right next to eachother. 
    
    So the shift register chain looks something like this:

    [battery 1]   [battery 2]   [battery 3]
    [b1]---[b2]---[b1]---[b2]---[b1]---[b2]

    Where each battery has a b1 and a b2, b1 controlling charging and b2 controlling discharging for that battery.
    I hope that made sense.

    Maybe the board works completely differently though. Idk. In that case, I'd be happy to re-write this whole function
    */
    for (int i = number_of_batteries - 1; i >= 0; i--)
    { //Loops thru the batteries BACKWARDS
        if (battery_list[i].mode == 0)
        {                //nothing
            shiftBit(0); //bit for the "charge" transistor
            shiftBit(0); //bit for the "discharge" transistor
        }
        else if (battery_list[i].mode == 1)
        {                //charge
            shiftBit(1); //bit for the "charge" transistor
            shiftBit(0); //bit for the "discharge" transistor
        }
        else if (battery_list[i].mode = 2)
        {                //discharge
            shiftBit(0); //bit for the "charge" transistor
            shiftBit(1); //bit for the "discharge" transistor
        }
    }
}

void shiftBit(int value)
{ //Shifts either a 1 or a zero out to the shift register(s)
    if (value != 1 && value != 0)
    {
        return;
    }
    digitalWrite(shift_register_data_pin, value);

    digitalWrite(shift_register_clock_pin, HIGH);
    digitalWrite(shift_register_clock_pin, LOW);
}

int readADC(int SS, int channel)
{ //copied from the internet and slighlty modified. some comments are mine, some are from the internet
    //yes, i know arduino has a generic SPI library, but i did it this way because i wanted to actually understand what is going on at a lower level

    if (channel > 7)
    {
        return 0; // we only have 8 channels
    }

    int adcvalue = 0;
    byte commandbits = B11000000; //command bits - start, mode, chn (3), dont care (3)

    //we convert the channel argument into a 3 bit binary number, shift it to the left, and combine it with the command bits
    commandbits |= (channel << 3);

    //so the ADC wants 5 bits telling it what to do. first two are ones, the next three determine the channel. the last 3 bits in the byte don't matter
    //for example, if we wanted channel 3, commandbits would look like 11 011 000. channel 7 commandbits would be 11 111 000.

    digitalWrite(SS, LOW); //select the ADC we want to read

    // setup bits to be written
    for (int i = 7; i >= 3; i--)
    {
        digitalWrite(spi_out, commandbits & 1 << i); //outputs the command bits from left to right
        //cycle clock
        digitalWrite(spi_clock_pin, HIGH);
        digitalWrite(spi_clock_pin, LOW);
    }

    digitalWrite(spi_clock_pin, HIGH); //ignores 2 null bits
    digitalWrite(spi_clock_pin, LOW);
    digitalWrite(spi_clock_pin, HIGH);
    digitalWrite(spi_clock_pin, LOW);

    //read bits from adc
    for (int i = 11; i >= 0; i--)
    {
        adcvalue += digitalRead(spi_in) << i;
        digitalWrite(spi_clock_pin, HIGH);
        digitalWrite(spi_clock_pin, LOW);
    }
    digitalWrite(SS, HIGH); //turn off device
    return adcvalue;        //this should be the 12 bit representation of the fraction of the reference voltage. so FFF (all ones) would be 100% of the reference voltage
}

// int *readAllADCs() //returns an array of all ADC readings
// {
//     int results[8 * number_of_ADCs]; //one int for each analog value, 8 channels * number of ADCs
//     int k = 0;                       //k will be the index at which data is inserted because C++ doesnt' let you add to the end of an array

//     for (int i = 0; i < number_of_ADCs; i++)
//     { //for each ADC
//         for (int j = 0; j < 8; j++)
//         {                                            //for each channel
//             results[k] = readADC(ADC_SS_pins[i], j); //adds to the end of results
//         }
//     }
//     return results;
// }
