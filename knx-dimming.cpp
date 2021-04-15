#include <knx.h>
#include <Arduino.h>

#ifdef ARDUINO_ARCH_ESP8266
#include <WiFiManager.h>
#endif

// #define verbose         //enable debug output
#define channelCount 3  //defining number of active channels
#define GOcount 5      //defining how much GO 1 Channel can have

// channel structure containing every information needed for a channel
struct Channel
{
    uint8_t DayValue = 0; // refference value for switch on Signal at day
    uint8_t nightValue = 0; // refference value for switch on Signal at night
    uint8_t activeValue = 0; // active value is used written to execute output
    uint8_t oldactiveValue = 0; // needed for edge reconition
    uint8_t switchOffStartValue = 0; // needed for dim to dark
    uint8_t switchOnEndValue = 0; // needed for dim to light
    bool statusOn = false; // containing information wether output is on or off
    long refTime; // needed for all dimming executions
    uint32_t switchOnDelay = 500; // should be filled by ets parameter
    uint32_t switchOffDelay = 500; // should be filled by ets parameter
    uint32_t timePerDimStepUp = 5000; // should be filled by ets parameter
    uint32_t timePerDimStepDown = 5000; // should be filled by ets parameter
    int8_t activeDimStep = 0; // dim step out of dim step list only if dimming is active
    uint8_t dimStartValue = 0; // dim start value for rel dimming
    uint8_t outPutPin; // output pin of channel

    // future ideas
    bool rgbSlaveChannel = false; // if true, Slave of Channel -1 or Channel -2 has to be set by parameter
    bool groupedChannel = false; // if true, grouped with Channel -1
    int32_t groupedChannelOnDelay = 0; // switch on delay to add
    int32_t groupedChannelOffDelay = 0; // switch off delay to add
}Channels[channelCount];

// asining Output Pins
uint8_t pinConnection[channelCount] = {D1, D2, D3};

// assining dim Step values for relative dimming
int dimStepList[16] = {0, -100, -50, -25, -12, -6, -3, -1, 0, 100, 50, 25, 12, 6, 3, 1};

void cbSwitch(GroupObject &go);
void cbDimRel(GroupObject &go);
void cbDimAbs(GroupObject &go);
void updatingOutput(int channel);
void updatingDayNightValue(int channel);
void relDimming(int channel);
void switchDimming(int channel);
void setup();
void loop();

/*
callback for switching
*/
void cbSwitch(GroupObject &go)
{
    uint8_t channel = (go.asap() - 1) / GOcount;

    Channels[channel].statusOn = go.value(); //copy status into statusOn
    Channels[channel].refTime = millis();    //copy millis into refDelay for Timers

    if ((Channels[channel].statusOn == true) && (Channels[channel].activeValue == 0))
    {

        if (Channels[channel].switchOnDelay == 0)
        {
            Channels[channel].switchOnDelay = 1;
        }
        // set Dimvalue to last used Dimvalue
        Channels[channel].switchOnEndValue = Channels[channel].DayValue;
    }
    else if (Channels[channel].statusOn == false)
    {
        if (Channels[channel].switchOffDelay == 0)
        {
            Channels[channel].switchOffDelay = 1;
        }
        // set SwitchOff start Value
        Channels[channel].switchOffStartValue = knx.getGroupObject(channel * GOcount + 5).value();
    }

// debug
#ifdef verbose
    print("interrupt Switch value: ");
    println(Channels[channel].statusOn);
    print("Channel: ");
    println(channel + 1);
#endif
}

/*
callback for dimming relativ
*/
void cbDimRel(GroupObject &go)
{
    uint8_t *dimStepIndex = go.valueRef();
    uint8_t channel = (go.asap() - 2) / GOcount;

    Channels[channel].dimStartValue = knx.getGroupObject(channel * GOcount + 5).value();
    Channels[channel].activeDimStep = dimStepList[*dimStepIndex];

    // start timer if no stop command
    if (Channels[channel].activeDimStep != 0)
    {
        Channels[channel].refTime = millis(); //copy millis into refDelay for Timers
    }

// debug
#ifdef verbose
    print("interrupt dimRel value: ");
    println(dimStepList[*dimStepIndex]);
    print("Channel: ");
    println(channel + 1);
#endif
}

/*
callback for dimming absolute
*/
void cbDimAbs(GroupObject &go)
{
    uint8_t dimValue = go.value();
    uint8_t channel = (go.asap() - 3) / GOcount;

    Channels[channel].activeValue = dimValue;

// debug
#ifdef verbose
    print("interrupt dimAbs value: ");
    println(dimValue);
    print("Channel: ");
    println(channel + 1);
#endif
}

/*
writing status of Channel[n].activeValue to outputs feedbacks etc.
only executed if activeValue changed
*/
void updatingOutput(int channel)
{
    // only executed, if ChannelA.activeValue changed
    // write Output Dimvalue
    analogWrite(Channels[channel].outPutPin, Channels[channel].activeValue * 1023 / 100.0);
    knx.getGroupObject(channel * GOcount + 5).value(Channels[channel].activeValue);

    // send out Status on/off if changed
    bool statusValue = knx.getGroupObject(channel * GOcount + 4).value();
    if ((Channels[channel].activeValue > 0) && (statusValue != true))
    {
        knx.getGroupObject(channel * GOcount + 4).value(true);
    }
    else if ((Channels[channel].activeValue == 0) && (statusValue != false))
    {
        knx.getGroupObject(channel * GOcount + 4).value(false);
    }
    
    Channels[channel].oldactiveValue = Channels[channel].activeValue;
}

/*
updates last used Value for day and night
--- in work ---
*/
void updatingDayNightValue(int channel)
{
    bool day = true;

    if (day == true)
    {
        Channels[channel].DayValue = Channels[channel].activeValue;
    }
    else
    {
        Channels[channel].nightValue = Channels[channel].activeValue;
    }
}

/*
calculating dimming value out of relDimming and time
*/
void relDimming(int channel)
{
    // starting new dimStep by writing refTime to millis if last Step is over and Button still pressed.
    if ((millis() - Channels[channel].refTime) < 0)
    {
        Channels[channel].refTime = millis();
    }
    // dim up
    if (((Channels[channel].activeDimStep > 0) && (Channels[channel].activeValue < 100)) && ((Channels[channel].dimStartValue + Channels[channel].activeDimStep) > Channels[channel].activeValue))
    {
        // calculate dimstep
        int dimstep = (millis() - Channels[channel].refTime) * 1.0 / Channels[channel].timePerDimStepUp * Channels[channel].activeDimStep;
        // calculate dim Value
        Channels[channel].activeValue = dimstep + Channels[channel].dimStartValue;
    }
    // dim down
    if (((Channels[channel].activeDimStep < 0) && (Channels[channel].activeValue > 0)) && ((Channels[channel].dimStartValue + Channels[channel].activeDimStep) < Channels[channel].activeValue))
    {
        // calculate dimstep
        int dimstep = (millis() - Channels[channel].refTime) * 1.0 / Channels[channel].timePerDimStepUp * Channels[channel].activeDimStep;
        // calculate dim Value
        Channels[channel].activeValue = dimstep + Channels[channel].dimStartValue;
    }
    updatingDayNightValue(channel);
}

/*
calculating "dim to light" and "dim to dark"
softstart of lamps
*/
void switchDimming(int channel)
{
    // switch on dim to light
    if (Channels[channel].switchOnEndValue > 0)
    {
        // calculate acutal dimstep
        Channels[channel].activeValue = (millis() - Channels[channel].refTime) * 1.0 / Channels[channel].switchOnDelay * Channels[channel].DayValue;
        // if time is over stop set to end value and stop diming
        if ((millis() - Channels[channel].refTime) >= Channels[channel].switchOnDelay)
        {
            Channels[channel].activeValue = Channels[channel].switchOnEndValue;
            Channels[channel].switchOnEndValue = 0;
        }
    }
    // switch of dimm to dark
    else if (Channels[channel].switchOffStartValue > 0)
    {
        // calculate acutal dimstep
        Channels[channel].activeValue = (Channels[channel].switchOnDelay - (millis() - Channels[channel].refTime)) * 1.0 / Channels[channel].switchOnDelay * Channels[channel].switchOffStartValue;
        // if time is over stop set to end value and stop diming
        if ((millis() - Channels[channel].refTime) >= Channels[channel].switchOffDelay)
        {
            Channels[channel].activeValue = 0;
            Channels[channel].switchOffStartValue = 0;
        }
    }
}

/*
arduino setup loop
*/
void setup()
{
    Serial.begin(115200);
    ArduinoPlatform::SerialDebug = &Serial;

    randomSeed(millis());

#ifdef ARDUINO_ARCH_ESP8266
    WiFiManager wifiManager;
    wifiManager.autoConnect("knx-demo");
#endif

    // read adress table, association table, groupobject table and parameters from eeprom
    knx.readMemory();

    // print values of parameters if device is already configured
    if (knx.configured())
    {
        // register callback for reset GO
        // goReset.callback(resetCallback);
        // goReset.dataPointType(DPT_Trigger);
        // goCurrent.dataPointType(DPT_Value_Temp);
        // goMin.dataPointType(DPT_Value_Temp);
        // goMax.dataPointType(DPT_Value_Temp);

        // ChannelA.outPutPin = BUILTIN_LED;
        // ChannelA.startGO = 1;

        for (int i = 0; i < channelCount; i++)
        {
            // initialize datapoint types for knx
            knx.getGroupObject(i * GOcount + 1).dataPointType(DPT_Switch);
            knx.getGroupObject(i * GOcount + 2).dataPointType(DPT_Control_Dimming);
            knx.getGroupObject(i * GOcount + 3).dataPointType(DPT_Scaling);
            knx.getGroupObject(i * GOcount + 4).dataPointType(DPT_Switch);
            knx.getGroupObject(i * GOcount + 5).dataPointType(DPT_Scaling);

            // connect callbacks
            knx.getGroupObject(i * GOcount + 1).callback(*cbSwitch);
            knx.getGroupObject(i * GOcount + 2).callback(*cbDimRel);
            knx.getGroupObject(i * GOcount + 3).callback(*cbDimAbs);

            // connect output pin
            Channels[i].outPutPin = pinConnection[i];

            print("init Channel");
            println(i + 1);
        }
    }

    // pin or GPIO the programming led is connected to. Default is LED_BUILTIN
    // knx.ledPin(LED_BUILTIN);
    // is the led active on HIGH or low? Default is LOW
    // knx.ledPinActiveOn(HIGH);
    // pin or GPIO programming button is connected to. Default is 0
    knx.buttonPin(D8);

    // start the framework.
    knx.start();

    print("init done");
}

/*
main loop
*/
void loop()
{
    // don't delay here to much. Otherwise you might lose packages or mess up the timing with ETS
    knx.loop();

    // only run the application code if the device was configured with ETS
    if (!knx.configured())
        return;

    for (int i = 0; i < channelCount; i++)
    {
        // switch on/off dimming
        switchDimming(i);

        // rel dimming
        if (Channels[i].activeDimStep != 0)
        {
            relDimming(i);
        }

        // Updating Output if changed

        if (Channels[i].activeValue != Channels[i].oldactiveValue)
        {
            updatingOutput(i);
        }
    }
}
