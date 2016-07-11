#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

//for LED status
#include <Ticker.h>
Ticker ticker;
Ticker checker;
WiFiManager wifiManager;
int val = 0;
int inputPin = 14;
int retval;
void tick()
{
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

void userInterrupt(){
    val = 1;
}

boolean connectionStatus(){
  return WiFi.status() == WL_CONNECTED;
}
void myLoop(){
  
}
//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("val is");
  Serial.println(val);
  //set led pin as output
  pinMode(BUILTIN_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);
  attachInterrupt(inputPin, userInterrupt, RISING); // implies that the button was pressed
  retval = wifiManager.autoConnect();

  if(retval == 0){
    Serial.println("autoconnect == 0 ");
    wifiManager.setAPCallback(configModeCallback); //set up the ticking
    wifiManager.startConfigPortal("Root",NULL); // set up as access point
  }
  else if(retval == -1){
    Serial.println("autoconnect == -1 ");
    //previous values and didn't connect so need to check until reset
  }
  else if(retval == 1){
    Serial.println("autoconnect == 1 ");
    Serial.println("connected...yeey :)");
    ticker.detach();
    //keep LED on
    digitalWrite(BUILTIN_LED, LOW);
  }
  
  Serial.println("i am at end of setup");
  //checker.attach(5,myLoop);
  
}

void loop() {
   Serial.println("in loop");
   Serial.println(retval);
   
  if(retval == -1){
    ticker.attach(0.6, tick);
    if(val == 1){
     WiFi.disconnect();
     val = 0;
     ticker.attach(0.2, tick);
     wifiManager.startConfigPortal("Root",NULL);
    }
  }
  else if(retval == 1){
    ticker.detach();
    digitalWrite(BUILTIN_LED, LOW);
  }
  else if(retval == 0){
     WiFi.disconnect();
     val = 0;
     ticker.attach(0.2, tick);
     wifiManager.startConfigPortal("Root",NULL);
  }
  retval = wifiManager.autoConnect();
}
