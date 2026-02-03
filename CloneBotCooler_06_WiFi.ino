/*
  CloneBotCooler, based on:  ColdSnap Coolbot Clone
  Freeware from Mark Holmes 9/2019

This sketch is designed as a controller for a cold room or walk-in refrigerator.  
It's similar in concept to a CoolBot unit that tricks a standard 
window AC unit into getting colder than it normally will (mine shuts off at 60°F).  
It does this by turning on a small heater (in my case a 5w power resistor)
strapped to the main temp sensor on the AC unit.  By turning this heater on, 
it tricks the AC into thinking the room is warmer than it actually is, and 
will run the AC down to whatever temp you set.  There's an additional temp sensor 
on the fins of the AC unit to sense if they are icing up, and to defrost
them if necessary.  Supposedly these will chill down to 34° if desired.

I have an added component: a set of two small fans that vent outside air into 
my root cellar, and a small fan that vents air out.  If it's cold enough outside,
I can simply power these two fans instead of the more expensive to run AC unit; 
there's code in the sketch to make choices about when to run the fans vs. the AC.

There's also a potentiometer in my setup that allows you to set a min/max range
to whatever you want; for our application, I have it set to 34°-60°F; that way 
we can dial in our Target Temp manually on the device with no screen.

For installation instructions, see detailed CoolBot examples online.

Happy cooling!  And apologies if you don't have an ultra-wide monitor....
  
*/

// Set Debug state

const byte         Debug = 1;                       //since we don't have conditional compilation......
                                                    // set to "0" to skip Serial output code of diagnostics.

// Initialize OneWire and Dallas Temperature libraries for DS18B20 temp sensors

#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS D5                            // Serial temp sensors plugged into pin D5 on ESP-12E board
#define TEMPERATURE_PRECISION 10                   // Set resolution on temp sensors: 9 = 0.5°, 10 = 0.25°
OneWire oneWire(ONE_WIRE_BUS);                     // Setup a oneWire instance to communicate with any OneWire devices
DallasTemperature sensors(&oneWire);               // Pass our oneWire reference to Dallas Temperature.


// This setup section specifically for Wifi/ThinkSpeak

#include "ThingSpeak.h"
#include <ESP8266WiFi.h>
WiFiClient  client;
int number = 0;
char ssid[] = "XXXXXXXXXXX";                        // your network SSID (name) 
char pass[] = "XXXXXXXX";                           // your network password
unsigned long myChannelNumber = XXXXXX;             // Replace the 0 with your channel number
const char * myWriteAPIKey = "XXXXXXXXXXXXXXX";    // Paste your ThingSpeak Write API Key between the quotes 



// Define uniqute addresses for each of temp sensors & GPIO pins

uint8_t             ROOM_SENSOR[8] = { XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX };   // Interior Room Sensor
uint8_t             FINS_SENSOR[8] = { XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX };   // AC Fin Sensor
uint8_t             EXT_SENSOR[8]  = { XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX };   // Outside Air Temp Sensor
const int           HEATER         = D2;                                                   // Pin for SSR for HEATER; D2 on ESP-12E board
const int           LED            = D3;                                                   // Indicator LED; D3 on ESP-12E board
const int           EXT_FANS       = D1;                                                   // Pin for SSR for both Exterior Fans; D1 on ESP-12E board


// Temps and other constants for users to set according to their needs.

const int           DEADBAND = 5;                   // °F temp range 'deadband' to reduce AC compressor cycling
const int           EXT_DIFFERENCE = 20;            // Temp differential between ROOM and EXT temps; if cold enough outside, the system will use just fans, not AC
const int           FINS_FREEZING_TEMP = 32;        // °F Temp reference to detect fins icing
const int           AC_CUTOUT_TEMP = 60;            // °F Temp that AC naturally turns itself off (above this temp, the AC will behave properly w/o override)
const int           DIAL_RANGE_MIN = 34;            // Minimum temp of range for pot / dial in °F
const int           DIAL_RANGE_MAX = 64;            // Maximum temp of range for pot / dial in °F
const unsigned long DEFROST_TIME = 60000;           // Delay time to allow for fin defrost -- repeats as necessary
const unsigned long HEATER_COOL_TIME = 20000;       // Delay time to allow AC temp sensor heater to cool off, turning off AC
const unsigned long REFRESH_TIME = 15000;           // Delay between main loop; 15 sec min for ThingSpeak updates


// Main variables

int                 TARGET_TEMP;                    // Variable to hold TARGET_TEMP to set cooling; established by reading potentiometer (see subroutine for range)
int                 ROOM_TEMP;                      // Variable to hold current measured Room Temp
int                 FINS_TEMP;                      // Variable to hold current measured Fins Temp
int                 EXT_TEMP;                       // Variable to hold current measured Exterior Temp
byte                AC_STATE;                       // Flag to indicate if AC should be cooling; 0 is OFF, 1 in ON
byte                FAN_STATE;                      // Flag to indicate if EXT FANS should be cooling; 0 is OFF, 1 in ON  
byte                DEADBAND_STATE;                  // Flag to indicate if we need to factor in our deadband temp to avoid rapid AC cycling         


// Setup runs once at powerup or after RESET:

void setup() {
  pinMode(LED, OUTPUT);                             // Set mode of LED I/O pin
  pinMode(HEATER, OUTPUT);                          // Set mode of HEATER I/O pin
  pinMode(EXT_FANS, OUTPUT);                        // Set mode of EXT_FANS I/O pin
  AC_STATE = 0;                                     // Start with AC_STATE in the Off condition; 0 = OFF, 1 = ON
  FAN_STATE = 0;                                    // Start with FAN_STATE in the Off condition; 0 = OFF, 1 = ON
  DEADBAND_STATE = 0;                               // Start with DEADBAND_STATE in the Off condition; 0 = OFF, 1 = ON
  if (Debug) Serial.begin(115200);                  // Initialize serial communication at X bits per second if Debug is ON

  WiFi.mode(WIFI_STA);                              // Set Wifi Mode
  ThingSpeak.begin(client);                         // Somehing for ThingSpeak obviously??
}


// **************** Main loop routine runs forever: ****************************

void loop() {

connectWifi();                                                                   // Subroutine to connect or re-connect to WiFi
  
// ------------- If it's cold enough outside, turn off AC heater, maybe delay, turn on EXT fans -----------------------------

  ReadSensors();                                                                 // Runs sub-routine to read sensors and store current temps @ °F
  sendtoThingSpeak();                                                            // Transmit data to ThingSpeak


  if ((EXT_TEMP <= (ROOM_TEMP - EXT_DIFFERENCE)) && (ROOM_TEMP > TARGET_TEMP)) { // If EXT temp is (or is still) cold enough, run fans instead of AC
    
      if (AC_STATE) {                                                            // If AC was ON, turn off HEATER and delay fans coming on
        AC_STATE = 0;                                                            // Set AC_STATE to 0, no AC needed
        digitalWrite(HEATER, LOW);                                               // Turn heater off, allow AC sensor to cool & turn off AC naturally
        if (Debug) { Serial.println("HEATER cooling delay"); DebugVerbose(); }     // Run verbose debug output subroutine
        delay(HEATER_COOL_TIME);                                                 // Preset delay to allow heater to cool, AC to turn off
        }  

        FAN_STATE = 1;                                                           // Set FAN_STATE to 1, indicating to other processes that fan takes priority 
        digitalWrite(EXT_FANS, HIGH);                                            // Turn on EXT Fans
        if (Debug) { Serial.println("Turned EXT FANS on"); DebugVerbose(); }         // Run verbose debug output subroutine
  }
     
   else {                                                                        // If EXT cooling conditions are no longer present, turn off fans
    FAN_STATE = 0;                                                               // Set FAN_STATE to 0
    digitalWrite(EXT_FANS, LOW);                                                 // Turn off (or leave off) EXT Fans
    if (Debug) { Serial.println("Conditions don't warrant Fans"); DebugVerbose(); }  // Run verbose debug output subroutine
   }


// ------------This section checks and sets the AC functions -- but only if the FAN isn't running--------------------------

 ReadSensors();                                                                                     // Runs sub-routine to read sensors and store current temps @ °F

  if ((!FAN_STATE) && (ROOM_TEMP <= (AC_CUTOUT_TEMP + 5))) {                                        // Only enter this AC loop if a) fans are off, b) AC needs to be overriden
                                                                                                    // --either because it's not needed (fan is on) or the AC doesn't need to be overriden to cool
                                                                                                    
         if ((DEADBAND_STATE) && (ROOM_TEMP >= (TARGET_TEMP + DEADBAND))) {                         // Proceed if AC DEADBAND is on and we've warmed through the DEADBAND temp                                         
          AC_STATE = 1;                                                                             
          digitalWrite(HEATER, HIGH);                                                               // Turn on HEATER/AC
          DEADBAND_STATE = 0;                                                                       // Reset Deadband state until we reach target temp again
          if (Debug) { Serial.println("DEADBAND wait done: Turned HEATER on"); DebugVerbose(); }        // Run verbose debug output subroutine 
         }
                                                                                                                 
         else if ((!DEADBAND_STATE) && (ROOM_TEMP > TARGET_TEMP)) {                                 // Proceed if we're NOT in a DEADBAND state and the room needs AC cooling                              
          AC_STATE = 1;                                                                             
          digitalWrite(HEATER, HIGH);                                                               // Turn on HEATER/AC
          if (Debug) { Serial.println("Normal AC test passed: Turned HEATER on"); DebugVerbose(); }     // Run verbose debug output subroutine 
         }
         
         else {                                                                                     // We reach this point if the AC has reached the TARGET TEMP.  Woohoo!
          AC_STATE = 0;
          DEADBAND_STATE = 1;                                                                       // Indicate that we need to allow the DEADBAND to influence at what temp the AC turns back on
          digitalWrite(HEATER, LOW);                                                                // Turn off heater to allow AC to turn off naturally
          if (Debug) { Serial.println("AC Reached Target Temp: Turned HEATER off"); DebugVerbose(); }   // Run verbose debug output subroutine
         }
    
     
       if ((!AC_STATE) && ((ROOM_TEMP >= (TARGET_TEMP + DEADBAND)) && (FINS_TEMP > FINS_FREEZING_TEMP))) {  // If AC is OFF, check to see if it should be ON (also considering DEADBAND temp)
          AC_STATE = 1;                                                                               // If TRUE sets AC_STATE to ON
          digitalWrite(HEATER, HIGH);                                                                 // Turn on HEATER to force AC ON
          DEADBAND_STATE = 0;                                                                         // Resets DEADBAND state back to 0 (because we've passed through it)
          if (Debug) { Serial.println("Passed 'Need AC test': AC State = ON"); DebugVerbose(); }                     // Run verbose debug output subroutine
          }
  }  

// ------------This section checks to see if the fins are freezing up (regardless of AC or FAN states) and delays until defrosted --------------------------

       if (FINS_TEMP <= FINS_FREEZING_TEMP) {                                                       // If fins are freezing, turn off HEATER/AC, start defrost delay
           AC_STATE = 0;                                                                            // Turn off HEATER/AC and set AC state to 0
           digitalWrite(HEATER, LOW);                                                               
           FAN_STATE = 0;                                                                           // Turn off FANS and set FAN_STATE to 0 -- it's very conservative to ensure fans are off during defrost
           digitalWrite(EXT_FANS, LOW);    
           if (Debug) { Serial.println("Entering Defrost Cycle"); DebugVerbose(); }                 // Run verbose debug output subroutine                                                                   
           do {                                                                                     // This section holds AC OFF for DEFROST_TIME and repeat until FINS reach FINS FREEZE TEMP + 5°F
               if (Debug) { Serial.println("Extending Defrost Cycle 1 second"); DebugVerbose(); }   // Run verbose debug output subroutine
               sendtoThingSpeak();                                                                  // Keep updating ThingSpeak while in Defrost delay
               delay(DEFROST_TIME);                                                                 // Delay time to repeat until temp rises to FINS temp + DEADBAND
               }
           while (FINS_TEMP <= (FINS_FREEZING_TEMP + DEADBAND));  
          }

delay (REFRESH_TIME);    // Wait to repeat full cycle

  
} // ******************* End main loop *****************************



// ------------------------- Subroutines below ---------------------------------

void ReadSensors(void){  // Read current sensors and store values in Temp variables in °F

   sensors.requestTemperatures();                   // Reads all temp sensors using Dallas Temp Library
   ROOM_TEMP = sensors.getTempF(ROOM_SENSOR);       // Read and store ROOM temp
   FINS_TEMP = sensors.getTempF(FINS_SENSOR);       // Read and store FINS temp
   EXT_TEMP = sensors.getTempF(EXT_SENSOR);         // Read and store EXT temp

   // Next comand sets TARGET_TEMP by reading 0-1024 pot value and converting to MIN-MAX range defined for pot @°F
   // Formula: NewValue = (((OldValue - OldMin) * (NewMax - NewMin)) / (OldMax - OldMin)) + NewMin
   
   TARGET_TEMP = (((analogRead(A0) - 0) * (DIAL_RANGE_MAX - DIAL_RANGE_MIN)) / (1024 - 0)) + DIAL_RANGE_MIN;
}


void DebugVerbose() {                                // Subroutine prints debug data from sensors and various stored states

    ReadSensors();
    Serial.print("Fins Temp    = "); Serial.println(int(FINS_TEMP));
    Serial.print("Room Temp    = "); Serial.println(int(ROOM_TEMP));
    Serial.print("Outside Temp = "); Serial.println(int(EXT_TEMP));
    Serial.print("Target Temp  = "); Serial.println(int(TARGET_TEMP));
    Serial.print("Target+Dead  = "); Serial.println(int(TARGET_TEMP + DEADBAND));
    Serial.print("AC_STATE     = "); Serial.println(byte(AC_STATE));
    Serial.print("FAN_STATE    = "); Serial.println(byte(FAN_STATE));
    Serial.print("Heater SSR   = "); Serial.println(digitalRead(D2));
    Serial.print("Fans SSR     = "); Serial.println(digitalRead(D1));
    Serial.println("———————————————————————————");    
}



void connectWifi() {
  if(WiFi.status() != WL_CONNECTED){
    if (Debug) Serial.print("Attempting to connect to SSID: ");
    if (Debug) Serial.println(ssid);
    while(WiFi.status() != WL_CONNECTED){
      WiFi.begin(ssid, pass);
      if (Debug) Serial.print(".");
      delay(5000);     
    } 
    if (Debug) Serial.println("\nConnected.");
  }
} //end connect




void sendtoThingSpeak() {
  ThingSpeak.setField(1, TARGET_TEMP);                               // set the fields with the values
  ThingSpeak.setField(2, ROOM_TEMP);
  ThingSpeak.setField(3, EXT_TEMP);
  ThingSpeak.setField(4, FINS_TEMP);
  ThingSpeak.setField(5, AC_STATE);
  ThingSpeak.setField(6, FAN_STATE);

  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);    // write to the ThingSpeak channel
  if(x == 200){
    if (Debug) Serial.println("Channel update successful.");
  }
  else{
    if (Debug) Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
}
