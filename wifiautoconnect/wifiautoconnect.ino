/*------------------------------------------------------------------*/
/* Author: Root                                                     */
/* ESP8266 Root Arduino Code                                        */
/* autoconnect.ino                                                  */
/*                                                                  */
/* Performs autoconnect functionality, even when power is removed   */
/* and later restored. Remembers the previous wifi network, and     */
/* contains a hardware button to reset the module to receive new    */
/* wifi credentials at any time.                                    */
/*------------------------------------------------------------------*/


/*------------------------ Include Statements ----------------------*/
#include <WiFiManager.h>        //https://github.com/tzapu/WiFiManager
#include <Ticker.h>             // For LED Status blinking
/*------------------------------------------------------------------*/


Ticker blinkLED;
WiFiManager wifiManager;
WiFiClient client;


/*------------------------- Pin Declarations -----------------------*/
int pinZeroCrossVoltage = 2;
int pinTemp = 4;
int pinTempPower = 5;
int pinReset = 14;
int pinAnalogCurrent = A0;
/*------------------------------------------------------------------*/


/*------------------------ General Variables -----------------------*/
// Temperature variables
int tempPulses = 0;
int tempTime = 0;
double tempVal = 0;

// Timing variables
long timeMillis = 0;

// Reset ISR toggle
int RESET = 0;

// State of if the root is on or off
int rootState = 0;

//Contants
// 0.2 works pretty well for calibration if not adccounting for pf
double MAX_CURRENT_CALIBRATION = -0.12;
double MIN_CURRENT_CALIBRATION = -0.12;
double ZERO_POINT = 1.74;
double ROOT_TWO = 1.41421356237;
double ZERO_CROSS_THRESH = 0.01;
double TICKS_PER_MICROSECOND = 120;
int TEST_TEMP = 1;
int TEST_ENERGY = 0;

// Max and min voltages
int maxVoltage = 0;
int minVoltage = 0;
int count = 0;

// Averages for values
double avgCurrentDifference = 0;
double avgMaxCurrent = 0;
double avgMinCurrent = 0;
double avgMaxVoltage = 0;
double avgMinVoltage = 0;
double avgPower = 0;
int avgIndex = 0;

// Interrupt trigger boolean
long phaseTime = 0;
long phaseTimeFinal = 0;
double phaseShiftRad = 1;

// Misc
int wifiState;
const char* server = "https://rootserver-heroku.herokuapp.com";
/*------------------------------------------------------------------*/


/*-------------------------------- ISR -----------------------------*/
/* Called when measuring the energy                                 */
void zeroCrossVoltageISR()
{
    phaseTime = millis();

    // Interrupt has fired, millis is counting, determine the zero crossing point for the current
    int voltageVal = analogRead(pinAnalogCurrent)*3;
    while (voltageVal / 4095.0 * 3.3 - ZERO_POINT > ZERO_CROSS_THRESH || voltageVal / 4095.0 * 3.3 - ZERO_POINT < -1 * ZERO_CROSS_THRESH)
        voltageVal = analogRead(pinAnalogCurrent);

    // Detected Zero cross of current
    phaseTimeFinal = millis() - phaseTime;

    // Determine the phase shift from the time
    phaseShiftRad = (16.666 - (phaseTimeFinal / TICKS_PER_MICROSECOND)) / 16.666; // 16.666 = milliseconds per period on 60 hz signal

    if (avgMaxCurrent < avgMinCurrent)
        avgPower = 120 * phaseShiftRad * (avgMaxCurrent) / ROOT_TWO;
    else avgPower = 120 * phaseShiftRad * (avgMinCurrent) / ROOT_TWO;

    count++;
    detachInterrupt(pinZeroCrossVoltage);
}

/* Called when measuring the temperature pulses from digital temp   */
void tempISR()
{
    tempPulses++;
}

/* ISR called when the physical reset button is pressed             */
void resetISR()
{
    RESET = 1;
}
/*------------------------------------------------------------------*/


/*------------------------- Callback Functions ---------------------*/
/* Callback function for Ticker to toggle the built in LED          */
void tick()
{
    int state = digitalRead(BUILTIN_LED);
    digitalWrite(BUILTIN_LED, !state);
}

/* Callback funciton for the C++ code to use whenever it needs to
   set up the module as an access point.                          */
void configModeCallback (WiFiManager *myWiFiManager)
{
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    //if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());
    blinkLED.attach(0.2, tick);
}
/*------------------------------------------------------------------*/


/* Checks the wifi connection status, returns true if the module is
   connected and false otherwise.                                   */
boolean connectionStatus()
{
    return WiFi.status() == WL_CONNECTED;
}


/*------------------------ Arduino Core Functions ------------------*/
void setup()
{
    Serial.begin(115200);
    Serial.println("RESET is");
    Serial.println(RESET);

    // Set pinmodes
    pinMode(BUILTIN_LED, OUTPUT);
    pinMode(pinZeroCrossVoltage, INPUT);
    pinMode(pinTemp, INPUT);
    pinMode(pinTempPower, OUTPUT);
    pinMode(pinReset, INPUT);
    pinMode(pinAnalogCurrent, INPUT);

    blinkLED.attach(0.6, tick);
    digitalWrite(BUILTIN_LED, LOW);
    // Attach reset ISR to the button
    attachInterrupt(pinReset, resetISR, RISING);

    // Check the state of the module connected to wifi
    wifiState = wifiManager.autoConnect();

    /* wifiState possible values
        0: Disconnected from the internet and no previously saved values
        1: Connected to the internet
        -1: Disconnected from the internet but has previously saved
            values.                                                 */
    if (wifiState == 0)
    {
        Serial.println("autoconnect == 0 ");
        wifiManager.setAPCallback(configModeCallback);
        wifiManager.startConfigPortal("Root", NULL);
    }
    else if (wifiState == -1)
    {
        Serial.println("autoconnect == -1 ");
    }
    else if (wifiState == 1)
    {
        Serial.println("autoconnect == 1 ");
        Serial.println("connected to the internet)");
        blinkLED.detach();
        digitalWrite(BUILTIN_LED, LOW);
    }

    Serial.println("Setup ended");

}

void loop()
{
    // Perform all core functionality first, then check connection and
    // send data if appropriate

    /*------------------ Read digital temperature -----------------*/
    if (TEST_TEMP)
    {
        tempPulses = 0;
        digitalWrite(pinTempPower, HIGH);
        attachInterrupt(pinTemp, tempISR, RISING);
        timeMillis = millis();

        while (millis() - timeMillis < 100);

        digitalWrite(pinTempPower, LOW);
        detachInterrupt(pinTemp);
        tempVal = (tempPulses / 4095.0) * 256 - 50;
        tempVal = (9.0/5.0)*tempVal + 32 - 3.9;
    }
    /*-------------------------------------------------------------*/


    /*---------------------- Monitor the energy -------------------*/
    if (TEST_ENERGY)
    {
        avgIndex = 0;
        while (avgIndex < 10)
        {
            maxVoltage = 0;
            minVoltage = 1024;

            //Read the voltages for 20 ms, a little over 2 periods of a 60hz signal
            long startTime = millis();

            while (millis() - startTime < 20)
            {
                int currentVal = analogRead(pinAnalogCurrent);
                if (currentVal > maxVoltage)
                    maxVoltage = currentVal;
                if (currentVal < minVoltage)
                    minVoltage = currentVal;
            }

            avgMaxVoltage += maxVoltage;
            avgMinVoltage += minVoltage;
            avgIndex++;
        }

        avgMaxVoltage = ((avgMaxVoltage / 10) / 1024.0) * 3.3;
        avgMinVoltage = ((avgMinVoltage / 10) / 1024.0) * 3.3;

        Serial.print("AvgMaxVoltage and MinVoltage: ");
        Serial.println(String(avgMaxVoltage) + String(avgMinVoltage));

        avgMaxCurrent = (avgMaxVoltage - ZERO_POINT) / 0.0435 + MAX_CURRENT_CALIBRATION;
        avgMinCurrent = (ZERO_POINT - avgMinVoltage) / 0.0435 + MIN_CURRENT_CALIBRATION;
        avgCurrentDifference = avgMaxCurrent - avgMinCurrent;

        Serial.print("AvgMaxCurrent and AvgMinCurrent: ");
        Serial.println(String(avgMaxCurrent) + String(avgMinCurrent));

        if (avgMaxCurrent < 0 || avgMinCurrent < 0)
        {
            Serial.println("Zeroing out everything");
            avgMaxCurrent = 0;
            avgMinCurrent = 0;
            avgCurrentDifference = 0;
            avgPower = 0;
            count++;
            delay(1000);
        }
        else
        {
            // Calculate the phase shift between voltage and current
            // Set up the voltage interrupt
            attachInterrupt(pinZeroCrossVoltage, zeroCrossVoltageISR, RISING);

            // Wait for the interrupt to trigger
            //while(!interruptFired);
            delay(1000);
        }
    }
    /*-------------------------------------------------------------*/

    Serial.println("In loop");
    wifiState = wifiManager.autoConnect();
    Serial.println(wifiState);

    if (wifiState == -1)
    {
        blinkLED.attach(0.6, tick);
        if (RESET == 1)
        {
            WiFi.disconnect();
            RESET = 0;
            blinkLED.attach(0.2, tick);
            wifiManager.startConfigPortal("Root", NULL);
        }
    }
    else if (wifiState == 1)
    {
        if (RESET == 1)
        {
            WiFi.disconnect();
            RESET = 0;
            blinkLED.attach(0.2, tick);
            wifiManager.startConfigPortal("Root", NULL);
        }
        else
        {
            blinkLED.detach();
            digitalWrite(BUILTIN_LED, LOW);

            if (client.connect(server, 80))
            {
                String PostData = "rootid=";
                PostData += String(ESP.getChipId());
                PostData += "&state=";
                PostData += String(rootState);
                PostData += "&secret=weisslabs&energy=";
                PostData += String(avgPower);
                PostData += "&temp=";
                PostData += String(tempVal);
                Serial.print("PostData: ");
                Serial.println(PostData);

                timeMillis = millis();

                client.println("POST /mobile/root/ping HTTP/1.1");
                client.println("Host: rootserver-heroku.herokuapp.com");
                client.println("User-Agent: Arduino/1.0");
                client.println("Connection: close");
                client.println("Content-Type: application/x-www-form-urlencoded");
                client.print("Content-Length: ");
                client.println(PostData.length());
                client.print("\r\n");
                client.println(PostData);
                Serial.println("Client response status code: ");
                delay(1000);
                // Read all the lines of the reply from server and print them to Serial
                Serial.println("Response:");
                while (client.available())
                {
                    String line = client.readStringUntil('\r');
                    Serial.print(line);
                }
                Serial.println(String(millis() - timeMillis));
            }
            client.stop();
        }
    }
    else if (wifiState == 0)
    {
        WiFi.disconnect();
        RESET = 0;
        blinkLED.attach(0.2, tick);
        wifiManager.startConfigPortal("Root", NULL);
    }
    delay(1000);
}
