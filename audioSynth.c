/**
 *  ECE 4760 FA22 Final Project
 *  Tommy Chen (tc575), Rhia Malhotra (rm722), Raphael Fortuna (raf269)
 *  Main .c file for audio synthesizer
 */

// DDS Libraries
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/spi.h"

// Protothread Library
#include "pt_cornell_rp2040_v1.h"

// TFT Libraries
#include <stdlib.h> //C stdlib
#include "hardware/gpio.h" //The hardware GPIO library
#include "pico/time.h" //The pico time library
#include "hardware/irq.h" //The hardware interrupt library
#include "hardware/pwm.h" //The hardware PWM library
#include "hardware/pio.h" //The hardware PIO library
#include "TFTMaster.h" //The TFT Master library

// Capacitive Touch Libraries
#include "hardware/i2c.h"
#include "mpr121.h"

// Input Libraries
#include "hardware/adc.h"

// Fixed-Point Macros
typedef signed int fix15;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0))
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a)    abs(a)
#define int2fix15(a)   ((fix15)(a << 15))
#define fix2int15(a)   ((int)(a >> 15))
#define char2fix15(a)  (fix15)(((fix15)(a)) << 15)
#define divfix(a,b)    (fix15)( (((signed long long)(a)) << 15) / (b))

fix15 ZeroFix15 = float2fix15(0);
const fix15 fix15One = int2fix15(1);
fix5 fourFix15 = int2fix15(4);


/////////////////////////////
/////// START OF PINS ///////
/////////////////////////////

#define PIN_rotaryA 3 //pin 5
#define PIN_rotaryB 2 // pin 4

#define potentiometer 26 // pin 31

#define PIN_toggleGroup 10 // pin 14
#define PIN_oscillGroup 11 // pin 15
#define PIN_filterGroup 12 // pin 16
#define PIN_LFOGroup 13 // pin 17

// I2C Definitions
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15

//SPI Configurations (GPIO port number, NOT pin number on Pi Pico)
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define LDAC     8
#define SPI_PORT spi0

// capacitive touch MPR121 Definitions
#define MPR121_ADDR 0x5A
#define MPR121_I2C_FREQ 100000

// set up the SPI channels
void initalizeSPI(){
    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000) ;

    // Format (channel, data bits per transfer, polarity, phase, order);
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;
}

// setup the I2C
void setupI2C(){
    i2c_init(I2C_PORT, MPR121_I2C_FREQ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
}

///////////////////////////
/////// END OF PINS ///////
///////////////////////////

#define buttonArraySize 4 // IF YOU CHANGE THE NUMBER OF BUTTONS CHANGE THIS!!

// for easily acessing pins for each core
int buttons[buttonArraySize] = {
    PIN_toggleGroup, 
    PIN_oscillGroup, 
    PIN_filterGroup, 
    PIN_LFOGroup
    }; 

// indexes for button pins above:
#define toggleGroup 0
#define oscillGroup 1 
#define filterGroup 2
#define LFOGroup 3

//////////////////////////////////////////////
/////// START OF GPIO AND BUTTON SETUP ///////
//////////////////////////////////////////////

// get the button value - true is pressed, false is not pressed
bool getButtonStatus(int buttonValue){
    if (gpio_get(buttonValue)){
        //printf("1");
    }
    return !gpio_get(buttonValue);
}

// set up a button
void setUpButton(int buttonValue){
    gpio_init(buttonValue);
    gpio_set_dir(buttonValue, GPIO_IN);
    gpio_pull_up(buttonValue);
}

// set up all the gpio pins hardware
void setUpHardwareGPIO(){
    for (int i = 0; i < buttonArraySize; i++){
        setUpButton(buttons[i]);
    }
    //printf("Hardware setup");
}

////////////////////////////////////////////
/////// END OF GPIO AND BUTTON SETUP ///////
////////////////////////////////////////////

///////////////////////////////////////
/////// START OF ROTARY ENCODER ///////
///////////////////////////////////////

volatile int rotaryCounterData = 1; // updated by interrupt that is getting information from rotary encoder
volatile int tinyCounter = 0; // updated by interrupt that is getting information from rotary encoder

#define tinyCountGranularity 3 // how many clicks is 1 change
#define rotaryCounterWrap 50 // wrap around for rotary counter

// the general function each interrupt calls here
// has a wrap around to avoid unnecessarily high values
void updateRotaryCounter(){

    // if the B pin is high or low
    bool bStatus = gpio_get(PIN_rotaryB);
    
    if (bStatus){
        // left wise - counterclockwise
        tinyCounter += 1;

        // counterclockwise direciton
        if (tinyCounter > tinyCountGranularity){
            tinyCounter = 0; // reset tiny counter
            rotaryCounterData += 1;
            if (rotaryCounterData > rotaryCounterWrap){
                rotaryCounterData = 0; // reset rotary counter
            }
        }
    }

    else{
        // right wise - clockwise
        tinyCounter -= 1;

        // clockwise direciton
        if (tinyCounter < 0){
            tinyCounter = tinyCountGranularity; // reset tiny counter
            rotaryCounterData -= 1;
            if (rotaryCounterData < 0){
                rotaryCounterData = rotaryCounterWrap; // reset rotary counter
            }
        }
    }
}

// setup the rotary pin
void setUpRotaryPin(int pinValue){
    gpio_init(pinValue);
    gpio_set_dir(pinValue, GPIO_IN);
}

// set up a rotary encoder
void setUpRotaryEncoder(){
    // need falling edge of A - 4.1.10.4.43

    // first set up the pins for A, B
    setUpRotaryPin(PIN_rotaryB);
    
    // now set up the GPIO interrupt
    gpio_set_irq_enabled_with_callback(PIN_rotaryA, 
    GPIO_IRQ_EDGE_FALL, true, &updateRotaryCounter);
}

/////////////////////////////////////
/////// END OF ROTARY ENCODER ///////
/////////////////////////////////////

// get the current group that is being modified by the controller, -1 if no change
int getCurrentGroup(){
    if (getButtonStatus(buttons[oscillGroup])){
        //printf("o");
        return oscillGroup;
    }

    else if (getButtonStatus(buttons[filterGroup])){
        //printf("f");
        return filterGroup;
    }
    
    else if (getButtonStatus(buttons[LFOGroup])){
        //printf("l");
    return LFOGroup;
    }

    return -1; // no change
}

// check if toggle button was pressed - true yes, false no
bool getToggleButton(){
    
    if (getButtonStatus(buttons[toggleGroup])){
        //printf("button pressed");
        return true;
    }

    return false; // no change
}

//////////////////////////////////////
/////// START OF POTENTIOMETER ///////
//////////////////////////////////////

float oldPotentiometerValue = 0;
float newPotentiometerValue = 0;

// value change between thye two potentiometer values
float potentiometerDifference = .05;

// returns the value of the potentiometer from 0 to 1 range
fix15 getPotentimeterValue(){
    
    // complete with input
    // Select analog mux input (0...3 are GPIO 26, 27, 28, 29; 4 is temp sensor)
    // adc_select_input(0) ; // so potentimeter is 26
    // adc_select_input(1) ; // so potentimeter is 27

    // this is 1/2^12 since doing it non-decimal way ended up with 0
    float result = adc_read()*0.000244140625;

    // update with new potentiometer value
    newPotentiometerValue = result;

    // needs to return fix15
    return float2fix15(result);
}

// initalize the ADC
void initalizeADC(){

    // Init GPIO for analogue use: hi-Z, no pulls, disable digital input buffer.
    adc_gpio_init(potentiometer);

    // Initialize the ADC harware
    // (resets it, enables the clock, spins until the hardware is ready)
    adc_init() ;

    // select 0, pin 26
    adc_select_input(0);
}

//////////////////////////////////////
/////// END OF POTENTIOMETER /////////
//////////////////////////////////////

// to make it easier to do colors
#define WHITE ILI9340_WHITE
#define BLACK ILI9340_BLACK

// to make it easier to do width, height
#define TFTWidth ILI9340_TFTWIDTH
#define TFTHeight ILI9340_TFTHEIGHT

int lineYShift = 20;
int defaultX = 20;
int textToNumberValueShift = TFTWidth/2; // shift to where numbers are
#define titleShift 30

// needs the extra for the larger oscillator label
int defaultY = titleShift+10;

///////////////////////////////////////////
/////// START OF AUDIO PARAMETERS /////////
///////////////////////////////////////////

// scale a input to a variable's range
fix15 scaleToVariable(fix15 range, fix15 value){
    return multfix15(value, range);
}

// the number of variables for the oscillator
#define oscillatorVarSize 7

// labels for the oscillator variables
char oscillatorVariables[oscillatorVarSize][15] = {
    "On/Off: ",
    "Freq: ",
    "Amp: ",
    "Shape",
    "Attack: ",
    "Decay: ",
    "Release: "
    };

#define OnOff 0
#define Frequency 1
#define Amplitude 2
#define Shape 3
#define Attack 4
#define Decay 5
#define Release 6 

// different waves corresponding to 1, 2, 3, 4
#define squareWave int2fix15(1)
#define sinWave int2fix15(2)
#define triangleWave int2fix15(3)
#define sawtoothWave int2fix15(4)

// boundaries for shapes
fix15 ShapeSelectionBoundaries[3] = {float2fix15(0.25), float2fix15(0.5), float2fix15(0.75)};

// is just a range value, assumes bottom range is 0
fix15 oscillatorValueRanges[oscillatorVarSize] = {
    0, // on off, unused
    float2fix15(1000), // frequency
    float2fix15(1), // amplitude
    0, // shape, unused
    float2fix15(200), //attack
    float2fix15(200), //decay
    float2fix15(300) // release
    };

// values that store the data for the oscillator
fix15 oscillatorValues[oscillatorVarSize] = {int2fix15(1), 0, float2fix15(.5), int2fix15(1), float2fix15(50.0), float2fix15(50.0), float2fix15(100.0)};

// getter for oscillator values
fix15 getOscill(int varName){
    return oscillatorValues[varName];
}

// setter for oscillator values
void setOscill(int varName, fix15 value){

    // no scaling for on/off
    if (varName == OnOff){
        oscillatorValues[varName] = value;
    }

    else if (varName == Shape){

        if (value < ShapeSelectionBoundaries[0]){
            oscillatorValues[varName] = squareWave;
        }
        else if (ShapeSelectionBoundaries[2] < value){
            oscillatorValues[varName] = sawtoothWave;
        }
        else if (value < ShapeSelectionBoundaries[1]){
            oscillatorValues[varName] = sinWave;
        }
        else{
            oscillatorValues[varName] = triangleWave;
        }
    }

    else{
        // scale the input
        value = scaleToVariable(oscillatorValueRanges[varName], value);

        oscillatorValues[varName] = value;
    }
}
    

/////////////////////////////////////////////////////

// how many filter variables there are
#define filterVarSize 4

char filterVariables[filterVarSize][15] = {
    "On/Off: ",
    "Cutoff 1",
    "Cutoff 2",
    "Type: "
    };

// on off already defined
#define cutoff1 1
#define cutoff2 2
#define Type 3

fix15 filterValues[filterVarSize] = {0, float2fix15(100), float2fix15(500), int2fix15(1)};

// getter for filter values
fix15 getFilter(int varName){
    return filterValues[varName];
}

// is just a range value, assumes bottom range is 0
fix15 filterValueRanges[oscillatorVarSize] = {
    0, // on off, unused
    float2fix15(2000), // cutoff 1
    float2fix15(2000), // cutoff 2
    0 // type, unused
    };

// different filtering types corresponding to 1, 2, 3
#define lowPass int2fix15(1)
#define highPass int2fix15(2)
#define bandPass int2fix15(3) 

// boundaries for filter types
fix15 filterPassSelectionBoundaries[2] = {float2fix15(0.333), float2fix15(0.666)};

// setter for filter values
void setFilter(int varName, fix15 value){
    if (varName == Type){

        // three choices

        if (value < filterPassSelectionBoundaries[0]){
            filterValues[varName] = lowPass;
        }
        else if (filterPassSelectionBoundaries[1] < value){
            filterValues[varName] = bandPass;
        }
        else{
            filterValues[varName] = highPass;
        }
    }
    // no scaling for on/off
    else if (varName == OnOff){
        filterValues[varName] = value;
    }
    else{
        // scale cutoff
        value = scaleToVariable(filterValueRanges[varName], value);
        filterValues[varName] = value;
    }
}

/////////////////////////////////////////////////////

#define LFOVarSize 3

char LFOVariables[LFOVarSize][15] = {
    "On/Off",
    "Freq: ",
    "Shape: "
    };

// on off, freq, amp, shape already done
#define LFOShape 2

// ranges for LFO values
fix15 LFOValueRanges[LFOVarSize] = {
    0, // on off, unused
    float2fix15(1000), // frequency 
    0 // shapes, unused
    };

fix15 LFOValues[LFOVarSize] = {0, float2fix15(15), int2fix15(1)};

// getter for LFO values
fix15 getLFO(int varName){
    return LFOValues[varName];
}

// setter for LFO values
void setLFO(int varName, fix15 value){

    // four options
    
    if (varName == LFOShape){

        if (value < ShapeSelectionBoundaries[0]){
            LFOValues[varName] = squareWave;
        }
        else if (ShapeSelectionBoundaries[2] < value){
            LFOValues[varName] = sawtoothWave;
        }
        else if (value < ShapeSelectionBoundaries[1]){
            LFOValues[varName] = sinWave;
        }
        else{
            LFOValues[varName] = triangleWave;
        }
    }

    else{
        // no scaling for on/off
        if (varName == OnOff){
            LFOValues[varName] = value;
        }
        
        else{
            // scale the input
            value = scaleToVariable(LFOValueRanges[varName], value);

            LFOValues[varName] = value;
        }
    }
}

// simple get - look at all settings instead of one at a time
fix15 getSettingGroupValue(int settingGroupSelected, int variableSelected){
    if (settingGroupSelected == oscillGroup){
        return getOscill(variableSelected);
    }
    else if (settingGroupSelected == filterGroup){
        return getFilter(variableSelected);
    }
    else{
        return getLFO(variableSelected);
    }
}


// simple set - look at all settings instead of one at a time
void setSettingGroupValue(int settingGroupSelected, int variableSelected, fix15 value){
    if (settingGroupSelected == oscillGroup){
        setOscill(variableSelected, value);
    }
    else if (settingGroupSelected == filterGroup){
        setFilter(variableSelected, value);
    }
    else{
        setLFO(variableSelected, value);
    }
}

/////////////////////////////////////////////////////

// total number of variables
int numberOfVariables = oscillatorVarSize + filterVarSize + LFOVarSize;

// keep track of what setting group is being modified - only changed by hardware input
int currentSettingGroup = 1;

// keep track of previous setting group - only changed by vga thread
int oldSettingGroup = 1;

// keep track of what variable is being changed by the rotary encoder
int currentVariableSelection = 1;

// the previously selected bariable
int oldCurrentSelection = 0;

// what the size of the current setting group is for the rotary encoder
int currentSettingGroupSize = oscillatorVarSize-1;

// text arrays
static char oscillatorVariablesVGA[oscillatorVarSize][40];
static char filterVariablesVGA[filterVarSize][40];
static char LFOVariablesVGA[LFOVarSize][40];

// Filter type
volatile int filterType = 3; // Low-Pass = 0, High-Pass = 1, Band-Pass = 2

// Default single cutoff
volatile float cutoffFrequency1 = 500; // current cutoff frequency 1
volatile int bin1; // cutoff bin 1 used in FFT

// Used for Band-Pass filter
volatile float cutoffFrequency2 = 1000; // current cutoff frequency 2
volatile int bin2; // cutoff bin 2 used in FFT

/////////////////////////////////////////
/////// END OF AUDIO PARAMETERS /////////
/////////////////////////////////////////


///////////////////////////////////////////
/////// START OF CAPACITIVE TOUCH /////////
///////////////////////////////////////////

// Touch and release thresholds.
#define MPR121_TOUCH_THRESHOLD 20
#define MPR121_RELEASE_THRESHOLD 30

// Touch Sensor Variables
struct mpr121_sensor mpr121;
const uint8_t electrodes[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
bool touched[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
bool touched1[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint16_t baseline[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint16_t filtered[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int noiseCounter[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int touchedNote = -1;

// Note Frequency Table (C4 -> B4)
float noteTable[12] = {261.6256, 277.1826, 293.6648, 
                        311.1270, 329.6276, 349.2282,
                        369.9944, 391.9954, 415.3047,
                        440.0000, 466.1638, 493.8833};

// check if the capactivie touch is working
void check_touched(uint8_t electrode){
    mpr121_is_touched(electrode, &touched[electrode], &mpr121);
    mpr121_baseline_value(electrode, &baseline[electrode], &mpr121);
    mpr121_filtered_data(electrode, &filtered[electrode], &mpr121);

    if (touched[electrode] == 1){
        noiseCounter[electrode] = 1;
    }
    else if (noiseCounter[electrode] > 5){
        noiseCounter[electrode] = 0;
    }
    else if (noiseCounter[electrode] >= 1){
        noiseCounter[electrode] += 1;
    }
}

// initalize the touch sensor
void initalizeTouchSensor(){

    // Touch LED Setup
    const uint LED_PIN = PICO_DEFAULT_LED_PIN;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // setup breakout board for touch
    mpr121_init(I2C_PORT, MPR121_ADDR, &mpr121);
    
    mpr121_set_thresholds(MPR121_TOUCH_THRESHOLD,
                          MPR121_RELEASE_THRESHOLD, &mpr121);

    // Enable all touch sensors
    mpr121_enable_electrodes(12, &mpr121);

    int z = 0;

    // loop used to make sure touch sensor is initalized properly
    while(z < 200) {

        for (int i = 0; i < 12; i++) { // separate each sensor into own thread? or just only run one sound at a time
            check_touched(electrodes[i]);
        }
        z++;
    }

    printf("done with capactive touch setup\n");
}

/////////////////////////////////////////
/////// END OF CAPACITIVE TOUCH /////////
/////////////////////////////////////////


//////////////////////////////
/////// START OF DDS /////////
//////////////////////////////

// Direct Digital Synthesis (DDS)
#define two32 4294967296.0  // 2^32 (constant)
#define Fs    20000            // Sample Rate was 40000

// Phase accumulator and phase increment. Output frequency set via noteFrequency
volatile unsigned int phase_accum_main_0;                  
volatile unsigned int phase_incr_main_0 = 0;

////////////////////////////
/////// END OF DDS /////////
////////////////////////////


/////////////////////////////////////
/////// START OF WAVETABLES /////////
/////////////////////////////////////

// Shape Wavetables - populated in main()
#define lookup_table_size 256
#define amplitude_limit 2047.0

// different wave tables for each type of waveform
float amp_sin = amplitude_limit;
fix15 sin_table[lookup_table_size];
float amp_sqr = amplitude_limit;
fix15 square_table[lookup_table_size];
float amp_tri = amplitude_limit;
fix15 triangle_table[lookup_table_size];
float amp_saw = amplitude_limit;
fix15 sawtooth_table[lookup_table_size];

// Wavetable used for manipulations
fix15 currentWave[lookup_table_size]; 

int LFO_Shape = 1;

fix15 LFO_sin_freq_table[lookup_table_size]; // LFO applied to frequencies
fix15 LFO_square_freq_table[lookup_table_size]; // LFO applied to frequencies
fix15 LFO_triangle_freq_table[lookup_table_size]; // LFO applied to frequencies
fix15 LFO_sawtooth_freq_table[lookup_table_size]; // LFO applied to frequencies

float LFO_sin = amplitude_limit; // amplitude of the LFOs
float LFO_sq = 0; // amplitude of the LFOs
float LFO_tri = 0; // amplitude of the LFOs
float LFO_saw = 0; // amplitude of the LFOs
float LFO_f = 15; // frequency of the LFOs

// Build the sine, square, triangle, and sawtooth lookup tables	
// scaled to produce values between 0 and 4096 (for 12-bit DAC)	
void buildWaveTables(){	
    float mag_tri = -amp_tri;	
    float inc_tri = 2.0*amp_tri/(lookup_table_size/2);
    float LFO_inc_tri = 1.0/(lookup_table_size/2);
    float mag_saw = 0;	
    float inc_saw = amp_saw/(lookup_table_size/2 - 5);
    float dec_saw = 2.0*amp_saw/10;	
    float LFO_inc_saw = 1.0/(lookup_table_size - 10);
    float LFO_dec_saw = 1.0/10;

    // initalize regular and LFO wavetables
    for (int ii = 0; ii < lookup_table_size; ii++) {

        sin_table[ii] = float2fix15(amp_sin*sin((float)ii*6.283/(float)lookup_table_size));
        LFO_sin_freq_table[ii] = float2fix15((sin((float)ii*6.283/(float)lookup_table_size) + 1.0));



        square_table[ii] = float2fix15(amp_sqr);
        LFO_square_freq_table[ii] = float2fix15(LFO_sq);
        if((ii + 1) % (lookup_table_size/2) == 0) {	
            amp_sqr *= -1;
            LFO_sq = 1.0;
        }


        LFO_triangle_freq_table[ii] = float2fix15(LFO_tri);
        triangle_table[ii] = float2fix15(mag_tri);	
        if(mag_tri >= amp_tri) { 	
            inc_tri *= -1;	
            mag_tri = amp_tri;

            LFO_inc_tri *= -1;
            LFO_tri = 1;
        }	
        else if (mag_tri <= -amp_tri) {	
            inc_tri *= -1;	
            mag_tri = -amp_tri;

            LFO_inc_tri *= -1;
            LFO_tri = 0;
        }	
        mag_tri += inc_tri;
        LFO_tri += LFO_inc_tri;

        if (LFO_tri > 1.0){
            LFO_tri = 1.0;
        }
        else if (LFO_tri < 0.0){
            LFO_tri = 0.0;
        }

        sawtooth_table[ii] = float2fix15(mag_saw);	
        if (ii < lookup_table_size/2 - 5){	
            mag_saw += inc_saw;
        }	
        else if (ii < lookup_table_size/2 + 5){	
            mag_saw -= dec_saw;
        }	
        else {
            mag_saw += inc_saw;
        }

        LFO_sawtooth_freq_table[ii] = float2fix15(LFO_saw);
        if (LFO_saw > 1.0){
            LFO_saw = 1.0;
        }
        else if (LFO_saw < 0.0){
            LFO_saw = 0.0;
        }
        if (ii < lookup_table_size - 10){
            LFO_saw += LFO_inc_saw;
        }
        else {	
            LFO_saw -= LFO_dec_saw;
        }
    }	
}

// populate the current wave table
void populateWave(fix15 waveTable[lookup_table_size]){
    for (int i = 0; i < NUM_SAMPLES; i++) {
    currentWave[i] = waveTable[i];
    fr[i] = currentWave[i];
    fi[i] = 0;
    }
}

///////////////////////////////////
/////// END OF WAVETABLES /////////
///////////////////////////////////


//////////////////////////////
/////// START OF LFO /////////
//////////////////////////////

// LFO frequency setting
volatile unsigned int phase_accum_LFO = 0;
volatile unsigned int phase_incr_LFO = 0;

////////////////////////////
/////// END OF LFO /////////
////////////////////////////



//////////////////////////////
/////// START OF TFT /////////
//////////////////////////////

// add the text labels to the TFT
void setupTFTText(int setting){

    int baseX = defaultX;

    // base value
    int baseY = defaultY;

    // first wipe the correct side
    tft_fillRect(0, 0, TFTWidth, TFTHeight, BLACK);
    
    tft_setTextColor(WHITE) ;
    
    tft_setTextSize(3) ;

    // shift down just for name of core
    tft_setCursor(baseX, baseY-titleShift) ;

    tft_writeString("Synthesizer");
    
    tft_setTextSize(2) ;   

    if (setting == oscillGroup){
        // oscillator variables
        tft_setCursor(baseX, baseY) ;
        tft_writeString("Oscilliscope");
        baseY += lineYShift;

        for (int i = 0; i < oscillatorVarSize; i++){
            tft_setCursor(baseX, baseY) ;
            tft_writeString(oscillatorVariables[i]) ;
            baseY += lineYShift; // go to next line
        }
    }

    if (setting == filterGroup){
        // filter variables
        tft_setCursor(baseX, baseY) ;
        tft_writeString("Filter");
        baseY += lineYShift;

        for (int i = 0; i < filterVarSize; i++){
            tft_setCursor(baseX, baseY) ;
            tft_writeString(filterVariables[i]) ;
            baseY += lineYShift; // go to next line
        }
    }

    if (setting == LFOGroup){
        // LFO variables
        tft_setCursor(baseX, baseY) ;
        tft_writeString("LFO");
        baseY += lineYShift;

        for (int i = 0; i < LFOVarSize; i++){
            tft_setCursor(baseX, baseY) ;
            tft_writeString(LFOVariables[i]) ;
            baseY += lineYShift; // go to next line
        }
    }
}
// if the group setting has changed
bool groupSettingUpdated = false;

bool numbersUpdated = true;

// old setting selector
int oldSettingSelector = 0;

// update which setting is currently being selected
void updateSettingSelector(){

    if (oldSettingSelector != currentVariableSelection || groupSettingUpdated){
        oldSettingSelector = currentVariableSelection;

        //printf("selector1\n");
        // base value
        int baseX = defaultX;
        
        int baseY = defaultY;

        // first wipe the correct side
        tft_fillRect(0, 0, baseX, TFTHeight, BLACK);
        
        tft_setTextColor(WHITE) ;
        
        // now draw a little circle
        int xCircle = baseX - defaultX/2;

        // + 1 i for the name of the setting is for the title
        //+.5 for being in the center of the word
        int yCircle = baseY + (currentVariableSelection+1.5)*lineYShift;

        // now draw the selector circle
        // can also use tft_fillCircle if it looks better
        tft_drawCircle(xCircle, yCircle, defaultX/4, WHITE);
    }
}

// check if the setting group has changed
int checkSettingGroup(){
    //printf("setting1\n");
    if (oldSettingGroup != currentSettingGroup){
        oldSettingGroup = currentSettingGroup;
        //printf("setting2\n");

        // wipe text and numbers and redraw the text
        setupTFTText(oldSettingGroup);
    }
}

// update the numbers for the settings
void updateNumbers(){
    //printf("number1\n");

    int settingGroupSelected = oldSettingGroup;

    // move to other core location if needed and go to the number locations
    int baseX = defaultX + textToNumberValueShift;

    // base value
    int baseY = defaultY+lineYShift;

    tft_setTextColor(WHITE) ;
    tft_setTextSize(2) ;
    tft_setCursor(baseX, baseY) ;

    int y_range = numberOfVariables*(1+lineYShift);

    // erase old numbers
    tft_fillRect(baseX, baseY, TFTWidth-defaultX + textToNumberValueShift, y_range, BLACK);

    // draw new numbers

    if (settingGroupSelected == oscillGroup){
        // oscillator variables
        //printf("number2\n");
        for (int i = 0; i < oscillatorVarSize; i++){
            sprintf(oscillatorVariablesVGA[i], "%f", fix2float15(getOscill(i))) ;
            tft_setCursor(baseX, baseY) ;
            tft_writeString(oscillatorVariablesVGA[i]) ;
            baseY += lineYShift; // go to next line
        }
    }

    else if(settingGroupSelected == filterGroup){
        // filter variables
        for (int i = 0; i < filterVarSize; i++){
            //printf("number3\n");
            sprintf(filterVariablesVGA[i], "%f", fix2float15(getFilter(i))) ;
            tft_setCursor(baseX, baseY) ;
            tft_writeString(filterVariablesVGA[i]) ;
            baseY += lineYShift; // go to next line
        }
    }

    else if(settingGroupSelected == LFOGroup){
        // LFO variables
        for (int i = 0; i < LFOVarSize; i++){
            //printf("number4\n");
            sprintf(LFOVariablesVGA[i], "%f", fix2float15(getLFO(i))) ;
            tft_setCursor(baseX, baseY) ;
            tft_writeString(LFOVariablesVGA[i]) ;
            baseY += lineYShift; // go to next line
        }
    }
}

// initalize the TFT pannel
void initalizeTFT(){
    tft_init_hw(); //Initialize the hardware for the TFT
    tft_begin(); //Initialize the TFT
    tft_setRotation(3); //Set TFT rotation
    tft_fillScreen(BLACK); //Fill the entire screen with black colour
}

// update the TFT screen in the interrupt
bool updateTFT(struct repeating_timer *t){

    // update setting group if needed
    checkSettingGroup();

    // update the selector
    updateSettingSelector();

    if (numbersUpdated){
        // update the numbers
        updateNumbers();

        numbersUpdated = false;
    }
    return true;
}

////////////////////////////
/////// END OF TFT /////////
////////////////////////////


///////////////////////////////////
/////// START OF HARDWARE /////////
///////////////////////////////////

// update the oscilliator values using the hardware
void getNewHardwareValues(){

    // second if the button was pressed to turn setting group on/off
    if (getToggleButton()){

        // need to toggle the current setting group on/off
        fix15 currentValue = getSettingGroupValue(currentSettingGroup, OnOff);
        if (currentValue == 0){
            setSettingGroupValue(currentSettingGroup, OnOff, fix15One);
        }
        else{
            setSettingGroupValue(currentSettingGroup, OnOff, 0);
        }

        numbersUpdated = true;
    }

    // third check which state the buttons are in
    int buttonState = getCurrentGroup();

    if (buttonState != -1){
        // button was pressed
        if (buttonState != currentSettingGroup){
            // setting group has changed

            groupSettingUpdated = true; // there is a new group
            currentSettingGroup = buttonState;
            numbersUpdated = true; // number will change
            currentVariableSelection = 0;
            rotaryCounterData = 0; // this has the potential to break things WARNING!!!!

            // update group size, -1 since not including the on/off
            if (buttonState == oscillGroup){
                currentSettingGroupSize = oscillatorVarSize - 1;
            }
            else if (buttonState == filterGroup){
                currentSettingGroupSize = filterVarSize - 1;
            }
            else{
                currentSettingGroupSize = LFOVarSize - 1;
            }
        }
    }

    // now get the current setting selected from the rotary encoder, +1 to avoid the on/off setting
    currentVariableSelection = (rotaryCounterData%currentSettingGroupSize)+1;

    // see if can change values
    if (currentVariableSelection != oldCurrentSelection || groupSettingUpdated){
        getPotentimeterValue();

        bool significantChange = false;
        
        if ((oldPotentiometerValue - newPotentiometerValue) < 0){
            // negative, cannot use abs since is for ints
            // -.1 vs -.05 
            if ((oldPotentiometerValue - newPotentiometerValue) < -potentiometerDifference){
                // value has significantly changed
                significantChange = true;
            }
        }
        // .1 vs .05
        else if (oldPotentiometerValue - newPotentiometerValue > potentiometerDifference){
                // value has significantly changed
                significantChange = true;
            }

        if (significantChange){

            // numbers have changed
            numbersUpdated = true;

            // change the actual setting group value
            setSettingGroupValue(currentSettingGroup, currentVariableSelection, getPotentimeterValue());

            // update potentiometer value
            oldPotentiometerValue = newPotentiometerValue;

            // update group setting value
            groupSettingUpdated = false;

            // update variable selection value
            oldCurrentSelection = currentVariableSelection;
        }
    }
    else{
        // currently changing a value
        numbersUpdated = true;

        // change the actual setting group value
        setSettingGroupValue(currentSettingGroup, currentVariableSelection, getPotentimeterValue());

        oldPotentiometerValue = newPotentiometerValue;
    }
}

// update the hardware values in a interrupt
bool updateHardware(struct repeating_timer *t){

    getNewHardwareValues();
    return true;
}

/////////////////////////////////
/////// END OF HARDWARE /////////
/////////////////////////////////


///////////////////////////////
/////// START OF ADSR /////////
///////////////////////////////

// ADSR Variables
volatile fix15 max_amplitude = int2fix15(1);    // maximum amplitude
volatile fix15 attack_inc;                       // rate at which sound ramps up
volatile fix15 decay_inc;                        // rate at which sound ramps down
fix15 current_amplitude_0 = int2fix15(0);        // current amplitude (modified in ISR)

// ADSR Timing (units of interrupts)
volatile int attack = 50;
volatile int decay = 10;
volatile int release = 1;
volatile int ADSRcount = 0;
volatile int releaseFlag = 0;

// ADSR function
void ADSR(int attack, int decay, int release) {

    fix15 attack_inc = float2fix15(1.25 / attack);
    fix15 decay_dec = float2fix15(0.25 / decay);
    fix15 release_dec = float2fix15(1.0 / release);

    // iterate through twelve notes to see which one was touched
    for (int j = 0; j < 12; j++){

        mpr121_is_touched(j, &touched[j], &mpr121);

        if (touched[j]){
            phase_incr_main_0 = (noteTable[j]*two32)/Fs;

            // write tft
            if (oscillatorValues[Frequency] != float2fix15(noteTable[j])){
                oscillatorValues[Frequency] = float2fix15(noteTable[j]);
                updateNumbers(); // this makes it a tad faster when updating the value in the tft
            }
            touchedNote = j;
            break;
        }
    }

    // modulate the attack and decay based on touch
    if (touched[touchedNote]){
        if (ADSRcount < attack){
            current_amplitude_0 += attack_inc;
            ADSRcount += 1;
        }
        else if (ADSRcount < attack + decay){
            current_amplitude_0 -= decay_dec;
            ADSRcount += 1;
        }
        noiseCounter[touchedNote] = 1;
    }  

    else if (noiseCounter[touchedNote] > 8){

        current_amplitude_0 -= release_dec;

        // avoid negatives
        if (current_amplitude_0 < ZeroFix15){
            current_amplitude_0 = ZeroFix15;
        }
        noiseCounter[touchedNote] = 0;
    }

    else if (noiseCounter[touchedNote] >= 1){
        if (ADSRcount < attack){
            current_amplitude_0 += attack_inc;
            ADSRcount += 1;
        }

        else if (ADSRcount < attack + decay){
            current_amplitude_0 -= decay_dec;
            ADSRcount += 1;
        }
        
        noiseCounter[touchedNote] += 1;
    }

    else if(noiseCounter[touchedNote] == 0){
        // printf("%d\n", fix2float15(release_dec));
        current_amplitude_0 -= release_dec;//float2fix15(0.01);

        if (current_amplitude_0 < ZeroFix15){
            current_amplitude_0 = ZeroFix15;
        }
        
        ADSRcount = 0;
    }
}

/////////////////////////////
/////// END OF ADSR /////////
/////////////////////////////


// DAC Output Values
int DAC_output_0;

// SPI Data
uint16_t DAC_data_0; // output value

// DAC Parameters
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//////////////////////////////
/////// START OF FFT /////////
//////////////////////////////

// Fast Fourier Transform (FFT)
#define NUM_SAMPLES 256     // Number of samples per FFT
#define NUM_SAMPLES_M_1 255 // Number of samples per FFT, minus 1
#define SHIFT_AMOUNT 8      // Length of short (16 bits) minus log2 number of samples (8)
#define LOG2_NUM_SAMPLES 8  // Log2 number of samples
#define ADCCLK 48000000.0   // ADC clock rate (unmutable)

// Alpha Max + Beta Min Variables
fix15 zero_point_4 = float2fix15(0.4); // 0.4 in fixed point

// FFT Calculation Arrays (real and imaginary)
fix15 fr[NUM_SAMPLES];
fix15 fi[NUM_SAMPLES];
fix15 fin[NUM_SAMPLES];

// FFT Sine Table
fix15 fft_sinewave[NUM_SAMPLES];

// In-Place FFT Function
// Adapted from: https://vanhunteradams.com/FFT/FFT.html
void FFTfix(fix15 fr[], fix15 fi[]) {
    
    unsigned short m;   // one of the indices being swapped
    unsigned short mr;  // the other index being swapped (r for reversed)
    fix15 tr, ti;       // for temporary storage while swapping, and during iteration
    
    int i, j;           // indices being combined in Danielson-Lanczos part of the algorithm
    int L;              // length of the FFT's being combined
    int k;              // used for looking up trig values from sine table

    int istep;          // length of the FFT which results from combining two FFT's
    
    fix15 wr, wi;       // trigonometric values from lookup table
    fix15 qr, qi;       // temporary variables used during DL part of the algorithm
    
    // Bit Reversal
    // Adapted from: https://graphics.stanford.edu/~seander/bithacks.html#BitReverseObvious

    for (m=1; m<NUM_SAMPLES_M_1; m++) {

        mr = ((m >> 1) & 0x5555) | ((m & 0x5555) << 1); // swap odd and even bits
        
        mr = ((mr >> 2) & 0x3333) | ((mr & 0x3333) << 2); // swap consecutive pairs
        
        mr = ((mr >> 4) & 0x0F0F) | ((mr & 0x0F0F) << 4); // swap nibbles
        
        mr = ((mr >> 8) & 0x00FF) | ((mr & 0x00FF) << 8); // swap bytes
        
        mr >>= SHIFT_AMOUNT; // shift mr down
        
        if (mr<=m) continue; // don't swap that which has already been swapped

        // swap the bit-reversed indices
        tr = fr[m];
        fr[m] = fr[mr];
        fr[mr] = tr;
        ti = fi[m];
        fi[m] = fi[mr];
        fi[mr] = ti;
    }

    // Danielson-Lanczos 
    // Adapted from: Tom Roberts 11/8/89 and Malcolm Slaney 12/15/94 malcolm@interval.com

    L = 1; // Length of the FFT's being combined (starts at 1)
    k = LOG2_NUM_SAMPLES - 1; // Log2 of number of samples, minus 1

    // While length of the FFT's being combined is less than number of gathered samples
    while (L < NUM_SAMPLES) {
        
        istep = L<<1; // Determine the length of the resulting FFT

        // For each element in the FFTs that are being combined
        for (m=0; m<L; ++m) { 

            // Lookup the trig values for that element
            j = m << k;                        // index of the sine table
            wr =  fft_sinewave[j + NUM_SAMPLES/4]; // cos(2pi m/N)
            wi = -fft_sinewave[j];                 // sin(2pi m/N) // INVERSE get rid of - sign
            wr >>= 1;                          // divide by two
            wi >>= 1;                          // divide by two

            // i gets the index of one of the FFT elements being combined
            for (i=m; i<NUM_SAMPLES; i+=istep) {

                j = i + L; // j gets the index of the FFT element being combined with i

                // compute the trig terms (bottom half of the above matrix)
                tr = multfix15(wr, fr[j]) - multfix15(wi, fi[j]);
                ti = multfix15(wr, fi[j]) + multfix15(wi, fr[j]);

                // divide ith index elements by two (top half of above matrix)
                qr = fr[i]>>1; 
                qi = fi[i]>>1; 

                // compute the new values at each index
                fr[j] = qr - tr;
                fi[j] = qi - ti;
                fr[i] = qr + tr;
                fi[i] = qi + ti;
            }    
        }
        --k;
        L = istep;
    }
}

// In-Place Inverse FFT Function
// Slightly modified from above FFT function
// Adapted from: https://vanhunteradams.com/FFT/FFT.html
void iFFTfix(fix15 fr[], fix15 fi[]) {
    
    unsigned short m;   // one of the indices being swapped
    unsigned short mr;  // the other index being swapped (r for reversed)
    fix15 tr, ti;       // for temporary storage while swapping, and during iteration
    
    int i, j;           // indices being combined in Danielson-Lanczos part of the algorithm
    int L;              // length of the FFT's being combined
    int k;              // used for looking up trig values from sine table

    int istep;          // length of the FFT which results from combining two FFT's
    
    fix15 wr, wi;       // trigonometric values from lookup table
    fix15 qr, qi;       // temporary variables used during DL part of the algorithm
    
    // Bit Reversal
    // Adapted from: https://graphics.stanford.edu/~seander/bithacks.html#BitReverseObvious

    for (m=1; m<NUM_SAMPLES_M_1; m++) {

        mr = ((m >> 1) & 0x5555) | ((m & 0x5555) << 1); // swap odd and even bits
        
        mr = ((mr >> 2) & 0x3333) | ((mr & 0x3333) << 2); // swap consecutive pairs
        
        mr = ((mr >> 4) & 0x0F0F) | ((mr & 0x0F0F) << 4); // swap nibbles
        
        mr = ((mr >> 8) & 0x00FF) | ((mr & 0x00FF) << 8); // swap bytes
        
        mr >>= SHIFT_AMOUNT; // shift mr down
        
        if (mr<=m) continue; // don't swap that which has already been swapped

        // swap the bit-reversed indices
        tr = fr[m];
        fr[m] = fr[mr];
        fr[mr] = tr;
        ti = fi[m];
        fi[m] = fi[mr];
        fi[mr] = ti;
    }

    // Danielson-Lanczos 
    // Adapted from: Tom Roberts 11/8/89 and Malcolm Slaney 12/15/94 malcolm@interval.com

    L = 1; // Length of the FFT's being combined (starts at 1)
    k = LOG2_NUM_SAMPLES - 1; // Log2 of number of samples, minus 1

    // While length of the FFT's being combined is less than number of gathered samples
    while (L < NUM_SAMPLES) {
        
        istep = L<<1; // Determine the length of the resulting FFT

        // For each element in the FFTs that are being combined
        for (m=0; m<L; ++m) { 

            // Lookup the trig values for that element
            j = m << k;                        // index of the sine table
            wr =  fft_sinewave[j + NUM_SAMPLES/4]; // cos(2pi m/N)
            wi = fft_sinewave[j];                  // sin(2pi m/N) INVERSE: - sign removed
            wr >>= 1;                          // divide by two
            wi >>= 1;                          // divide by two

            // i gets the index of one of the FFT elements being combined
            for (i=m; i<NUM_SAMPLES; i+=istep) {

                j = i + L; // j gets the index of the FFT element being combined with i

                // compute the trig terms (bottom half of the above matrix)
                tr = multfix15(wr, fr[j]) - multfix15(wi, fi[j]);
                ti = multfix15(wr, fi[j]) + multfix15(wi, fr[j]);

                qr = fr[i]; // INVERSE: no divide
                qi = fi[i]; // INVERSE: no divide

                // compute the new values at each index
                fr[j] = qr - tr;
                fi[j] = qi - ti;
                fr[i] = qr + tr;
                fi[i] = qi + ti;
            }    
        }
        --k;
        L = istep;
    }
}

// run a low pass filter on the fft
void lowPassFilter(){
    float multiplier = 1.0; // Adjusts slope of the filter

    for (int x = 0; x < lookup_table_size/2; x++){

        if (x > bin1) {
            multiplier -= 0.5; // Decrease multiplier

            if (multiplier <= 0.0) { // Floor multiplier at 0
                multiplier = 0.0;
            }

            // Scale frequencies
            fr[x] = multfix15(fr[x], float2fix15(multiplier));
            fi[x] = multfix15(fi[x], float2fix15(multiplier));
        }

        // Assign the reflected frequencies
        fr[lookup_table_size - 1 - x] = fr[x];
        fi[lookup_table_size - 1 - x] = fi[x];
    }
}

// run a high pass filter on the fft
void highPassFilter(){
    float multiplier = 0.0; // Slope of filter

    for (int x = 0; x < lookup_table_size/2; x++){

        if(x < bin1 && x >= bin1 - 2) { // Ramp-up of the slope (cutoff - (multiplier / increment))
            multiplier += 0.5; // Increase multiplier

            if (multiplier >= 1.0) { // Ceiling multiplier at 1
                multiplier = 1.0;
            }

            // Scale frequencies
            fr[x] = multfix15(fr[x], float2fix15(multiplier));
            fi[x] = multfix15(fi[x], float2fix15(multiplier));
        }

        else if(x < bin1) { // Filter out the rest
            multiplier = 0;
            fr[x] = multfix15(fr[x], float2fix15(multiplier));
            fi[x] = multfix15(fi[x], float2fix15(multiplier));
        }

        // Assign the reflected frequencies
        fr[lookup_table_size - 1 - x] = fr[x];
        fi[lookup_table_size - 1 - x] = fi[x];
    }
}

// run a band pass filter on the fft
void bandPassFilter(){

    float multiplier = 0.0; // Slope of filter

    for (int x = 0; x < lookup_table_size/2; x++){

        if (x < bin1 - 2) { // filter out frequencies > 10 Hz below lower cutoff
            multiplier = 0;
            fr[x] = multfix15(fr[x], float2fix15(multiplier));
            fi[x] = multfix15(fi[x], float2fix15(multiplier));
        }

        else if (x < bin1 && x >= bin1 - 2) { // + slope if frequency < 10 Hz below lower cutoff
            multiplier += 0.5; // Increase multiplier

            if (multiplier > 1.0) { // Ceiling multiplier at 1
                multiplier = 1.0;
            }

            // Scale frequencies
            fr[x] = multfix15(fr[x], float2fix15(multiplier));
            fi[x] = multfix15(fi[x], float2fix15(multiplier));

            
        }

        else if (x > bin2 && x <= bin2 + 2) { // - slope if frequency < 10 Hz above upper cutoff
            multiplier -= 0.5; // Increase multiplier

            if (multiplier < 0.0) { // Floor multiplier at 0
                multiplier = 0.0;
            }

            // Scale frequencies
            fr[x] = multfix15(fr[x], float2fix15(multiplier));
            fi[x] = multfix15(fi[x], float2fix15(multiplier));

            
        }

        else if (x > bin2 + 2) { // filter out frequencies > 10 Hz above upper cutoff
            multiplier = 0;
            fr[x] = multfix15(fr[x], float2fix15(multiplier));
            fi[x] = multfix15(fi[x], float2fix15(multiplier));
        }

        // Assign the reflected frequencies
        fr[lookup_table_size - 1 - x] = fr[x];
        fi[lookup_table_size - 1 - x] = fi[x];
    }
}

void scaleWaveTable(){

    for (int x = 0; x < lookup_table_size; x++) {

        fr[x] = multfix15(fourFix15,fr[x]); // Scale by a multiple of 4

        // Check and prevent overflow errors
        if (fix2float15(fr[x]) > amplitude_limit) {
            fr[x] = float2fix15(amplitude_limit);
        }

        else if (fix2float15(fr[x]) < -amplitude_limit) {
            fr[x] = float2fix15(-amplitude_limit);
        }
    }
}

// In-Place Wavetable Filter
void FFTfilter(int type) {

    // Copy wavetable into fr[], setting only the real values
    for (int i=0; i < NUM_SAMPLES; i++) {
        fr[i] = currentWave[i];
        fi[i] = ZeroFix15;
    }

    // Compute the FFT
    FFTfix(fr, fi);

    // Filter the frequency domain output of the FFT
    if (type == 0){ // Low-Pass
        lowPassFilter();
    }

    else if (type == 1) { // High-Pass
        highPassFilter();
    }

    else if (type == 2) { // Band-Pass
        bandPassFilter();
    }

    // Perform inverse FFT on the filtered frequency array
    iFFTfix(fr, fi); // turns fr back into wavetable

    // Scale the output of the inverse 
    scaleWaveTable();
}

// populates the sinewave table for the FFT	
void generateSineWave(){	
    for (int jj = 0; jj < NUM_SAMPLES; jj++) {	
        fft_sinewave[jj] = float2fix15(sin(6.283 * ((float) jj) / (float)NUM_SAMPLES));	
    }	
}

////////////////////////////
/////// END OF FFT /////////
////////////////////////////

bool repeating_timer_callback_core_1(struct repeating_timer *t) {

    LFO_f = fix2float15(getLFO(Frequency));
    phase_incr_LFO = ((LFO_f*two32)/Fs);

    phase_accum_LFO += phase_incr_LFO;

    // multiply cutoff frequencies by LFO in time
    if (getLFO(OnOff) != ZeroFix15) {

        LFO_Shape = fix2int15(getLFO(LFOShape));

        if (getFilter(OnOff) != ZeroFix15){
            filterType = fix2int15(getFilter(Type)) - 1;

            if (LFO_Shape == 1) {
                cutoffFrequency1 = fix2float15(getFilter(cutoff1)) * fix2float15(LFO_sin_freq_table[phase_accum_LFO>>24]);
                cutoffFrequency2 = fix2float15(getFilter(cutoff2)) * fix2float15(LFO_sin_freq_table[phase_accum_LFO>>24]);
            }
            else if (LFO_Shape == 2) {
                cutoffFrequency1 = fix2float15(getFilter(cutoff1)) * fix2float15(LFO_square_freq_table[phase_accum_LFO>>24]);
                cutoffFrequency2 = fix2float15(getFilter(cutoff2)) * fix2float15(LFO_square_freq_table[phase_accum_LFO>>24]);
            }
            else if (LFO_Shape == 3) {
                cutoffFrequency1 = fix2float15(getFilter(cutoff1)) * fix2float15(LFO_triangle_freq_table[phase_accum_LFO>>24]);
                cutoffFrequency2 = fix2float15(getFilter(cutoff2)) * fix2float15(LFO_triangle_freq_table[phase_accum_LFO>>24]);
            }
            else {
                cutoffFrequency1 = fix2float15(getFilter(cutoff1)) * fix2float15(LFO_sawtooth_freq_table[phase_accum_LFO>>24]);
                cutoffFrequency2 = fix2float15(getFilter(cutoff2)) * fix2float15(LFO_sawtooth_freq_table[phase_accum_LFO>>24]);
            }
        
            // recalculate bin numbers
            bin1 = (int) ceil(cutoffFrequency1 * NUM_SAMPLES / Fs);
            bin2 = (int) ceil(cutoffFrequency2 * NUM_SAMPLES / Fs);
            FFTfilter(filterType);
        }

        for (int i = 0; i < NUM_SAMPLES; i++) {
            fin[i] = fr[i];
        }

    }
    else if (getFilter(OnOff) != ZeroFix15) {

        cutoffFrequency1 = fix2float15(getFilter(cutoff1));
        cutoffFrequency2 = fix2float15(getFilter(cutoff2));

        // recalculate bin numbers
        bin1 = (int) ceil(cutoffFrequency1 * NUM_SAMPLES / Fs);
        bin2 = (int) ceil(cutoffFrequency2 * NUM_SAMPLES / Fs);

        filterType = fix2int15(getFilter(Type)) - 1;

        FFTfilter(filterType);

        for (int i = 0; i < NUM_SAMPLES; i++) {
            fin[i] = fr[i];
        }
    }
    else {
        int waveShape = fix2int15(getOscill(Shape));

        if (waveShape == 1) {
            for (int i = 0; i < NUM_SAMPLES; i++) {
                fin[i] = sin_table[i];
            }
        }
        else if (waveShape == 2) {
            for (int i = 0; i < NUM_SAMPLES; i++) {
                fin[i] = triangle_table[i];
            }
        }
        else if (waveShape == 3) {
            for (int i = 0; i < NUM_SAMPLES; i++) {
                fin[i] = square_table[i];
            }
        }
        else {
            for (int i = 0; i < NUM_SAMPLES; i++) {
                fin[i] = sawtooth_table[i];
            }
        }
    }
    
    // check if ociallator is on
    if (getOscill(OnOff) != ZeroFix15){
        ADSR(fix2int15(getOscill(Attack)), fix2int15(getOscill(Decay)), fix2int15(getOscill(Release)));
    }
    else {
        current_amplitude_0 = ZeroFix15;
    }

    // cap the max and min amplitude
    if (current_amplitude_0 >= max_amplitude) {
        current_amplitude_0 = max_amplitude;
    }
    if (current_amplitude_0 < ZeroFix15) {
        current_amplitude_0 = ZeroFix15;
    }

    return true;
}

// This timer ISR is called on core 0
bool repeating_timer_callback_core_0(struct repeating_timer *t) {

    // DDS phase and sine table lookup
    phase_accum_main_0 += phase_incr_main_0;
        DAC_output_0 = fix2int15(multfix15(multfix15(current_amplitude_0,getOscill(Amplitude)), fin[phase_accum_main_0>>24])) + 2048;  

    // Mask with DAC control bits
    DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff))  ;

    // SPI write (no spinlock b/c of SPI buffer)
    spi_write16_blocking(SPI_PORT, &DAC_data_0, 1) ;

    return true;
}

// This is the core 1 entry point. Essentially main() for core 1
void core1_entry() {

    // create an alarm pool on core 1
    alarm_pool_t *core1pool ;
    core1pool = alarm_pool_create(2, 16) ;

    // Create a repeating timer that calls repeating_timer_callback.
    struct repeating_timer timer_core_1;
    
    struct repeating_timer tft_timer_core_1;

    struct repeating_timer hardware_timer_core_1;

    // Negative delay so means we will call repeating_timer_callback, and call it
    // again 25us (40kHz) later regardless of how long the callback took to execute
    // 40 khz -> 10 hz

    // Filter Interrupt Timer
    alarm_pool_add_repeating_timer_us(core1pool, -50, 
        repeating_timer_callback_core_1, NULL, &timer_core_1);

    // tft interrupt
    alarm_pool_add_repeating_timer_us(core1pool, -500, 
        updateTFT, NULL, &tft_timer_core_1);

    // hardware polling interrupt
    alarm_pool_add_repeating_timer_us(core1pool, -100, 
        updateHardware, NULL, &hardware_timer_core_1);

}


// Core 0 entry point
int main() {

    // Initialize stdio/uart (printf won't work unless you do this!)
    stdio_init_all();

    // SPI initalization
    initalizeSPI();

    // TFT initialization
    initalizeTFT();

    // ADC initialize for potentiometer
    initalizeADC();

    // set up the hardware for the buttons and rotary encoder
    setUpHardwareGPIO();
    setUpRotaryEncoder();

    // initalize the TFT text
    setupTFTText(oldSettingGroup);

    // setup the I2C
    setupI2C();

    // Touch Sensor Initialization
    initalizeTouchSensor();

    // set up increments for calculating bow envelope
    attack_inc = divfix(max_amplitude, int2fix15(attack)) ;
    decay_inc =  divfix(max_amplitude, int2fix15(decay)) ;

    // initialization for LFO and 
    phase_incr_main_0 = (noteTable[3]*two32)/Fs;
    phase_incr_LFO = ((LFO_f*two32)/Fs);

    // Build the sine, square, triangle, and sawtooth lookup tables
    buildWaveTables();

    generateSineWave();

    // populate the default wave
    populateWave(square_table);

    // //filterType = 0;
    FFTfilter(filterType);

    // Launch core 1
    multicore_launch_core1(core1_entry);

    // Create a repeating timer that calls 
    // repeating_timer_callback (defaults core 0)
    struct repeating_timer timer_core_0;

    // Negative delay so means we will call repeating_timer_callback, and call it
    // again 25us (40kHz) later regardless of how long the callback took to execute
    add_repeating_timer_us(-50, 
        repeating_timer_callback_core_0, NULL, &timer_core_0);

}
