
/* * Teensy 3.5 Custom PCB developed to automate ISCO 6700 Sampling with a digital output pin
 * Unit is controlled via SMS commands
 * Bottle number and time sampled is parsed from ISCO Serial Output
 * Data including bottle number, water level and fixed GPS coordinates are streamed to VIPER database using HTTP Post from a FONA 808 cellular module
 * Time, bottle number and water level are saved to SD card locally
 * Water level (presence) is detected by an optical sensor (Optomax Digital Liquid Level Sensor)
 * Auto Sampling Mode - Pulses pin every 5 minutes when water is present in the collection basin
 * Grab Sampling Mode - Pulses pin whenever a grab sample is initiated by SMS command - if water is in the collection basin


  Tim McArthur

*/
//094 Pod code Version 0.1 - Cleaned up working code and added watchdog to testing unit. 2/21/18 - Tim McArthur
// Version 1.0 - Cleaned up variable/function names and logic. 4/11/2018 - Tim McArthur
//Version 1.01 - Added static set of numbers to accept commands from
//             - Default starts in grab mode
//             - Grab sample ignores water level. Still depends on ISCO being enabled

//GPS and unit values iused in VIPER Post

char GPS[] = "40.508745 ,-74.358065 0"; //needs a (space 0) at the end, example ="35.068471,-89.944730 0"
int unit = 2;
char versionNum[] = "_094_Pod_ISCOv1.01";

//////////////////////////////////


//Watchdog
#include <Adafruit_SleepyDog.h>

//////////////////////////////////////////////

//Char arrays to hold time information

char monthChar[5];
char dateChar[5];
char hourChar[5];
char minuteChar[5];
char secondChar[5];

///////////////////////////////////////

//Char arrays to hold measurement values for posting to VIPER
char Temp1Char[10];
char Temp2Char[10];
char Temp3Char[10];
char levelChar[10];
char bVChar[10];

///////////////////////////////////////


//Static numbers to text when bottles are full
char* timsPhone = "+19199204318";
//Numbers to accept SMS commands from:
char* annesPhone1 = "+19198860812";
char* annesPhone2 = "+15179451531";
bool successTimText = false; //logic to control when to text
int readSMSSuccess = 0;
int deleteSMSSuccess = 0;
////////////////////////////////////////////////

//FONA HTTP

#include "Adafruit_FONA.h"
char senderNum[30];    //holds number of last number to text SIM
#define FONA_RST 4
char replybuffer[255]; //holds Fonas text reply
HardwareSerial *fonaSerial = &Serial5;
HardwareSerial *GPRSSerial = &Serial5;
#define FONA_PKEY 37 //Power Key pin to turn on/off Fona
#define FONA_PS 38 //Power Status PIN
#define FONA_RI_BYPASS 35 //Pull this pin high to bypass the ring indicator pin when resetting the Fona from the Teensy so the Fona doesn't lock up
bool fonaPower = true; //Whether or not Fona is currently powered on
int fonaStatus;
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);
int commandNum = -1; //integer value for which text command has been sent
uint8_t type;
unsigned long deleteTextsTimer = millis(); //after a set timeout, all(30) texts are deleted from SIM
bool _debug = true;
unsigned long lastPost = millis();
int minsToPost = 5;
/////////////////////////////////////This section is taken from the Ubidots library which was modified to work with VIPER. Could change it since the TOKEN is irrelevant. But if it ain't broke....
#include <Ubidots_FONA.h>  //Custom library specifically to post to VIPER with the FONA
#define TOKEN "A1E-ft1AUBxmYrb1T5Rm37dnAHU7pjyXEC"  // Replace it with your Ubidots token
#define APN "wholesale" // Assign the APN 
#define USER ""  // If your apn doesnt have username just put ""
#define PASS ""  // If your apn doesnt have password just put ""
Ubidots client(TOKEN);
///////////////////////////////////////////////end weird Ubidots section


////////////////////////////////////


//Voltage Readings
#define BattRail_VIN A1
float BattVoltage;
#define FiveRail_VIN A9
float FiveVoltage;
//#define ThreeRail_VIN A5
#define levelPin A20
//float ThreeVoltage;
///////////////////////

//SD
int writeNum; // integer to control whether or not the header data is written to SD file
char fileName[20];  //fileName built from date
char dateTime[30];  // dateTime to be written to SD card header
char dateTimeVIPER[40]; //dateTime for VIPER string
#include <SD.h>
const int chipSelect = BUILTIN_SDCARD; //For Teensy 3.5, SPI for SD card is separate
File myFile;
int minsToSave = 1;
int date;  //holds current date so new file can be written at midnight
unsigned long lastSave = millis();  // holds time since execution of last save to ensure data is saved every 60 seconds
///////////////SD

//ISCO
#include <Time.h>
#define IscoSamplePin 16
char iscoData[500];
int cdIndex = 0;
bool grabSampleMode = true;
int grabSampleInterval = 5; //Minute interval to auto grab sample as long as water level is high enough
char iscoTime[15]; //holds last bottle sample time from ISCO memory
#define iscoSerial Serial4
bool timerOn = false;//logic for texting after sample is complete
unsigned long textTimer = millis();
unsigned long sampleTimer = millis();
bool ISCORail = true;
int levelReading;
unsigned long iscoTimeout = millis();
/////////////////ISCO


///////////////////////////RELAYS
bool RQ30 = true;
#define HPRelay 27
#define RQ30Relay 26

//////////////////////////RELAYS


void setup() {
  Serial.begin(9600);
  int countdownMS = Watchdog.enable(300000);
  Serial.print("Enabled the watchdog with max countdown of ");
  Serial.print(countdownMS, DEC);
  Serial.println(" milliseconds!");
  Serial.println();
  delay(2000);
  setSyncProvider(getTeensy3Time);
  Serial.print("Initializing SD card...");  //startup SD card

  if (!SD.begin(chipSelect)) {
    Serial.println("initialization failed!");
  }
  Serial.println("initialization done.");

  Serial.print("Code --- ");Serial.println(versionNum);

  pinMode(HPRelay, OUTPUT); //Set up HydraProbe relay
  pinMode(FONA_PKEY, OUTPUT); //Power Key INPUT
  pinMode(FONA_RI_BYPASS, OUTPUT); //FONA RI BYPASS signal - RI Bypass keeps the RI line high while the Fona is resetting, so the Teensy doesn't reset
  pinMode(FONA_PS, INPUT); //Power Status INPUT
  //pinMode(levelPin, INPUT);
  delay(50);
  digitalWrite(FONA_RI_BYPASS, HIGH); //RI Bypass on
  pinMode(BattRail_VIN, INPUT);
  digitalWrite(HPRelay, HIGH); //turn on HydraProbe 12V Rail
  Watchdog.reset();
  Serial.print("Fona is...");
  Serial.print(getFonaStatus());
  if (getFonaStatus() > 500)
  {
    Serial.println(" On!");
    digitalWrite(FONA_RI_BYPASS, LOW); //If fona is already on, we can disable RI bypass
  } else
  {
    Serial.println(" off.");
  }
  if (getFonaStatus() < 500)
  {
    digitalWrite(FONA_RI_BYPASS, HIGH); //If fona is not on, make sure RI bypass is high
    Serial.println("Turning on Fona");
    digitalWrite(FONA_PKEY, HIGH);
    delay(2000);
    digitalWrite(FONA_PKEY, LOW); //turn on Fona
    delay(5000);
    digitalWrite(FONA_PKEY, HIGH); //turn on Fona
    delay(2000);
    digitalWrite(FONA_RI_BYPASS, LOW); //...and disable RI bypass
    Serial.println("Done");

  }
  Watchdog.reset();

  GPRSSerial->begin(19200);  //Startup HTTP post client
  if (! client.init(*GPRSSerial)) {
    Serial.println(F("Couldn't find FONA"));
    //while (1);
  }

  //fonaSerial->begin(19200); //Startup SMS and Cell client
  if (! fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't find FONA"));
    //while (1);
  }
  type = fona.type();
  Serial.println(F("FONA is OK"));
  Serial.print(F("Found "));  //Get Fona type from module and display as status to user
  switch (type) {
    case FONA800L:
      Serial.println(F("FONA 800L")); break;
    case FONA800H:
      Serial.println(F("FONA 800H")); break;
    case FONA808_V1:
      Serial.println(F("FONA 808 (v1)")); break;
    case FONA808_V2:
      Serial.println(F("FONA 808 (v2)")); break;
    case FONA3G_A:
      Serial.println(F("FONA 3G (American)")); break;
    case FONA3G_E:
      Serial.println(F("FONA 3G (European)")); break;
    default:
      Serial.println(F("???")); break;

      client.setApn(APN, USER, PASS);
  }
  pinMode(IscoSamplePin, OUTPUT);  // Setup sample pin output
  digitalWrite(IscoSamplePin, LOW);
  Serial.begin(9600);
  iscoSerial.begin(9600); //Setup comms with ISCO
  Watchdog.reset();
}

void loop() {
  if (ISCORail)
  {
    Serial.print("Started waiting for ISCO...Millis = "); Serial.println(millis()); //If ISCO is enabled, reset the watchdog while waiting for data on ISCO Serial
    do {
      Watchdog.reset();
    } while (iscoSerial.available() == 0 && millis() - iscoTimeout < 40000);
    iscoTimeout = millis(); //reset timer
    Serial.print("Finished waiting for ISCO...Millis = "); Serial.println(millis());
    do {  //read in data from ISCO Serial
      readIscoSerial();
    } while (iscoSerial.available() > 0);
  } else
  {
    delay(5000); //If ISCORail is disabled , wait 5 seconds every loop
  }
  checkTexts(); //check for SMS commands
  Watchdog.reset();
  //checkHydraProbes();
  checkUserInput(); //check Serial for input commands
  getBV(); //get voltage rail readings
  if (millis() - lastPost > minsToPost * 60000) //Post data every (minsToPost) minutes
  {
    postData();
    lastPost = millis(); //reset timer
    clearIscoSerial(); //clear isco and fona serials
    flushFonaSerial();
  } else
  {
    Serial.print(((minsToPost * 60000) - (millis() - lastPost)) / 1000); Serial.println(" Seconds left b4 Post");
  }

  if (millis() - lastSave > minsToSave * 60000) //Save data every (minsToSave) minutes
  {
    Serial.println("----------");
    Serial.println("SAVING");
    saveData();
    lastSave = millis();
  }
  if (grabSampleMode) //Display sample mode on Serial Monitor
  {
    Serial.println("------------------Currently in GRAB Sample Mode--------------------");
  } else
  {
    Serial.println("------------------Currently in AUTO Sample Mode--------------------");
  }

  if (getWaterLevel()) //If there is water in the pan......
  {
    Serial.println(" Water detected in Pan");
    if (!grabSampleMode) // ....AND.... we are in auto sample mode.....
    {
      if (millis() - sampleTimer > 60000 * grabSampleInterval) //.....AND....it's time to sample. Then sample.
      {
        Serial.println("Timer Finished!");
        sampleTimer = millis();
        if (ISCORail)
        {
          toggleSample();
        }
      } else
      {
        Serial.print("Sample timer has "); Serial.print(((60000 * grabSampleInterval) - (millis() - sampleTimer)) / 1000); Serial.println("Seconds remaining");
      }
    } else
    {
      //Serial.println("------------------Currently in grab Sample Mode--------------------");
    }
  } else
  {
    Serial.println(" No Water detected in Pan");
  }

  Serial.print("Finished reading Serial from ISCO. Received "); Serial.print(cdIndex); Serial.println(" characters");
  iscoData[cdIndex] = '\0';
  cdIndex = 0;

  Serial.print("Fona onCount = "); Serial.println(getFonaStatus());

  if (getFonaStatus() < 500) //If Fona turns off for some reason,turn it on
  {
    toggleFona();
  }

  if (millis() - textTimer > 100000 && timerOn) //Send last number to text a status on sampling after 100 second delay from sample command
  {
    sendSampleReply(senderNum);
    timerOn = false;
  }



  for (int x = 0; x < 64 ; ++x)
  {
    char a = Serial.read();
    char b = fonaSerial->read();
  }

  clearIscoSerial();
}



void checkTexts() //reads SMS(s) off the Fona buffer to check for commands
{

  int8_t numSMS = readSMSNum();
  Serial.print("readSMSNum()="); Serial.println(numSMS);
  if (numSMS > 0)
  {
    Serial.println("Received text Message!");
    //Serial.print("You have ");Serial.print(numSMS);Serial.println(" text messages");
    for (int x = 0 ; x < numSMS ; ++x)   //for the number of messages...
    {
      int tries = 0;
      do {                                                         //read the next SMS in (readSMS also processes the commandNum)
        readSMSSuccess = readSMS(x);                              
        ++x;
        Serial.print("Success ="); Serial.println(readSMSSuccess);
      } while (readSMSSuccess == 0  && tries < 30);  


      deleteSMSSuccess = deleteSMS(x - 1); //after processing, delete the meesage from the buffer

      if (millis() - deleteTextsTimer > 300000) //if its been 5 minutes, delete every text on the buffer, to make sure it stays clear
      {
        for (int y = 0 ; y < 30 ; ++y)
        {
          deleteSMS(y);
        }
        deleteTextsTimer = millis();
      }

    }
    char Message[200];
    //commandNum parsed from SMS will control how the unit responds to each text
    if (commandNum == 1)  //Send voltage rail values to senderNum
    {
      commandNum = -1;
      char message[100];
      int y = sprintf(message, "%s%f%s%f", "Battery Voltage = ", BattVoltage, "\n5V Rail Voltage= ", FiveVoltage);
      sendSMS(senderNum, message);
      Serial.println("Sent voltages to Sender");
    }
    if (commandNum == 2) //Sample, and start timer to send sample status back to user
    {
      commandNum = -1;
        if (ISCORail) //...and the ISCO is enabled..
        {
          toggleSample(); //Sample
        } else
        {
          sendSMS(senderNum, "ISCO is disabled");
        }
      
    }
     if (commandNum == 8) //Turn ISCO on or off
    {
      Serial.println("Toggled ISCO Rail");
      if (ISCORail)
      {
        ISCORail = false;
        sendSMS(senderNum, "Turned off ISCO");
      } else
      {
        ISCORail = true;
        sendSMS(senderNum, "Turned on ISCO");
      }

      commandNum = -1;
    }
    if (commandNum == 9) //CHanges the sample mode
    {
      Serial.println("Toggled Sample Mode");
      if (grabSampleMode)
      {
        grabSampleMode = false;
        sendSMS(senderNum, "Changed to AUTO Sample Mode");
      } else
      {
        grabSampleMode = true;
        sendSMS(senderNum, "Changed to GRAB Sample Mode");
      }
      commandNum = -1;
    }
    if (commandNum == 0) //Default message to send back if a command is not received.
    {
      sprintf(Message, "%s", "Did not understand the command.\n 1= Rail Voltages \n 2=Sample ISCO");
      //sprintf(Message, "%s", "\n 5=Hydra3 \n 6=Cheap Sensor \n 7=Turn");
      /*if (HPRail)
        {
        sprintf(Message, "%s%s", Message, " off ");
        } else
        {
        sprintf(Message, "%s%s", Message, " on ");
        }

        sprintf(Message, "%s%s", Message, "Hydra Probe \n 8= Turn");
      */
      sprintf(Message, "%s%s", Message, "\n 8= Turn");
      if (ISCORail)
      {
        sprintf(Message, "%s%s", Message, " off ");
      } else
      {
        sprintf(Message, "%s%s", Message, " on ");
      }
      sprintf(Message, "%s%s", Message, "ISCO \n 9= Change to");
      if (grabSampleMode)
      {
        sprintf(Message, "%s%s", Message, " AUTO ");
      } else
      {
        sprintf(Message, "%s%s", Message, " GRAB ");
      }
      sprintf(Message, "%s%s", Message, "Sample Mode");

      sendSMS(senderNum, Message);
      commandNum = -1;
    }
    else
    {

    }
  } else
  {
    Serial.println("No New SMS");
    commandNum = -1;
  }
  commandNum = -1;
}




///////////////////////////////***********************FONA FUNCTIONS

/*** SMS ***/
int readSMSNum() {  //read in the number of text messages on the buffer and send back
  // read the number of SMS's!
  int smsnum;
  char response[50];
  flushFonaSerial();
  //Serial.println("Writing AT Command to Serial 1");
  fonaSerial->print("AT+CPMS?");
  fonaSerial->write(13);
  delay(50);
  while (fonaSerial->available())
  {
    delay(50);
    for (int x = 0; x < fonaSerial->available() ; ++x)
    {
      char a = fonaSerial->read();
      response[x] = a;
    }
  }
  Serial.print("Response from Fona -"); Serial.println(response);
  Serial.print("11th char is "); Serial.println(response[16]);
  smsnum = response[14] - '0';
  //Serial.print("to an int ");Serial.println(atoi(response[10]);
  Serial.print("You have "); Serial.print(smsnum); Serial.println( "messages");
  sprintf(response, "%s", "");
  return smsnum;


}

int readSMS(int8_t number) { //read the SMS into the replybuffer and parse the command number
  boolean respond = false;
  // read an SMS
  //Serial.print(F("Read #"));
  Serial.print(F("\n\rReading SMS #")); Serial.println(number);

  // Retrieve SMS sender address/phone number.
  if (! fona.getSMSSender(number, replybuffer, 250)) {
    Serial.println("Failed!");
    return 0;
  }
  Serial.print(F("FROM: ")); Serial.println(replybuffer);

  int y = sprintf(senderNum, "%s", replybuffer);
  Serial.print("Sender is ");
  Serial.println(senderNum);

   if (strcmp(senderNum, timsPhone) == 0)
  {
    respond = true;
  }
   if (strcmp(senderNum, annesPhone1) == 0)
  {
    respond = true;
  }
   if (strcmp(senderNum, annesPhone2) == 0)
  {
    respond = true;
    
  }
  if (respond)
  {
    Serial.println("Accepted number");
  // Retrieve SMS value.
  uint16_t smslen;
  if (! fona.readSMS(number, replybuffer, 250, &smslen)) { // pass in buffer and max len!
    Serial.println("Failed!");
  }
  Serial.print(F("***** SMS #")); Serial.print(number);
  Serial.print(" ("); Serial.print(smslen); Serial.println(F(") bytes *****"));
  Serial.println(replybuffer);
  commandNum = atoi(replybuffer);
  Serial.println(F("*****"));
  return 1;
}else
{
   Serial.println("NOT Accepted number");
}
}

int deleteSMS(int8_t number) { //delete the SMS number
  // delete an SMS
  //Serial.print(F("Delete #"));
  Serial.print(F("\n\rDeleting SMS #")); Serial.println(number);
  if (fona.deleteSMS(number)) {
    Serial.println(F("OK!"));
    return 1;
  } else {
    Serial.println(F("Couldn't delete"));
    return 0;
  }

}

bool sendSMS(char* sendto, char*message) {
  // send an SMS!
  Serial.print(F("Send to #"));
  Serial.println(sendto);
  Serial.print(F("Message (140 char): "));
  Serial.println(message);
  if (!fona.sendSMS(sendto, message)) {
    Serial.println(F("Failed"));
    return false;
  } else {
    Serial.println(F("Sent!"));
    return true;
  }


}

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout) {
  uint16_t buffidx = 0;
  boolean timeoutvalid = true;
  if (timeout == 0) timeoutvalid = false;

  while (true) {
    if (buffidx > maxbuff) {
      //Serial.println(F("SPACE"));
      break;
    }

    while (Serial.available()) {
      char c =  Serial.read();

      //Serial.print(c, HEX); Serial.print("#"); Serial.println(c);

      if (c == '\r') continue;
      if (c == 0xA) {
        if (buffidx == 0)   // the first 0x0A is ignored
          continue;

        timeout = 0;         // the second 0x0A is the end of the line
        timeoutvalid = true;
        break;
      }
      buff[buffidx] = c;
      buffidx++;
    }

    if (timeoutvalid && timeout == 0) {
      //Serial.println(F("TIMEOUT"));
      break;
    }
    delay(1);
  }
  buff[buffidx] = 0;  // null term
  return buffidx;
}

bool sendFona(char* message)
{
  char a;
  flushFonaSerial();
  fonaSerial->println(message);
  while (fonaSerial->available())
    a = fonaSerial->read();
  if (a == '0' || a == false)
  {
    return false;
  }
  if (a == '1' || a == true)
  {
    return true;
  }

}

void flushFonaSerial() {
  while (fonaSerial->available())
    fonaSerial->read();
}

///////////////////////////////////////**********************FONA FUNCTIONS

void checkUserInput()
{
  if (Serial.available())
  {
    char a = Serial.read();
    if (a == 'S')
    {
      toggleSample();
    }
    if (a == 'T')
    {
      toggleFona();
    }
    if (a == 'Q')
    {
      sendQuery();
    }
    if (a == 'G')
    {
      delay(200);
      if (getBottleNumber() == 0)
      {
        Serial.println("No communication with ISCO. Could not find data");
      } else
      {
        Serial.print("Sampled -- Bottle ");
        Serial.print(getBottleNumber());
        if (getBottleNumber() == 24)
        {
          sendSMS(senderNum, "All bottles are full. ISCO Sampler must be reset and bottles replaced");
        }
        unsigned long sampled = getSampledTime();
        //Serial.print("time_t=");Serial.println(sampled);
        Serial.print(" @ ");
        Serial.print(month(sampled)); Serial.print("/"); Serial.print(day(sampled)); Serial.print("/"); Serial.print(year(sampled)); Serial.print("  "); Serial.print(hour(sampled)); Serial.print(":"); Serial.print(minute(sampled)); Serial.print(":"); Serial.println(second(sampled));
      }
    }


  }

}



void toggleSample()
{
  int botNum;
  char Message[50];
  if (getBottleNumber() == 24)
  {
    if (!successTimText)
    {
      successTimText = sendSMS(timsPhone, "All bottles are full. ISCO Sampler must be reset and bottles replaced. Will not sample");
    }
   }
  {
    Serial.println("Start sampler");
    textTimer = millis();
    timerOn = true;

    {
      botNum = getBottleNumber() + 1;
      if (botNum > 24)
      {
        botNum = 1;
      }
      if (grabSampleMode)
      {
        if (botNum == 2)
        {
          successTimText = false;
        }
        sprintf(Message, "%s%i", "Received Sample Command. Now Sampling Bottle # ", botNum);
        sendSMS(senderNum, Message);
      }
      Sample();
    }
  }
}

void Sample()
{
  Serial.println("Sending signal to ISCO");
  digitalWrite(IscoSamplePin, HIGH);
  delay(3000);
  digitalWrite(IscoSamplePin, LOW);

}

void sendQuery() //Function to establish comms with ISCO if needed
{
  Serial.println("Sending query to ISCO");
  iscoSerial.write('?');
  iscoSerial.write(13);
  delay(2000);
  if (!iscoSerial.available())
  {
    iscoSerial.write('?');
    iscoSerial.write(13);
    delay(2000);
    Serial.println("Trying another baud...");
  } else
  {
    readIscoSerial();
  }
  if (!iscoSerial.available())
  {
    iscoSerial.write('?');
    iscoSerial.write(13);
    delay(10);
    Serial.println("3rd baud attempt...");
  } else
  {
    readIscoSerial();
  }
  delay(2000);
  if (!iscoSerial.available())
  {
    Serial.println("No data available...giving up");
  }

}

void clearIscoSerial()
{
  for (int x = 0; x < 64 ; ++x)
  {
    char a = iscoSerial.read();
  }
}


void readIscoSerial()
{
  unsigned long timer = millis();
  do
  {
    if (iscoSerial.available())
    {
      char a = iscoSerial.read();
      if (cdIndex < sizeof(iscoData))
      {
        iscoData[cdIndex] = a;
      }
      ++cdIndex;
    }


  } while (millis() - timer < 1000);
}
int getBottleNumber() //parse the bottle Number from ISCO input
{
  int commaNumber1 = 0;
  int bottleNumber = 0;
  char bottleNumChar[10];
  for (int x = 0; x < 210 ; ++x)
  {
    if (iscoData[x] == ',')
    {
      ++commaNumber1;
      //Serial.print(commaNumber);
    }
    if (commaNumber1 == 10)
    {
      int y = sprintf(bottleNumChar, "%c%c%c", iscoData[x + 2], iscoData[x + 3], iscoData[x + 4]);
      bottleNumber = atoi(bottleNumChar);
      Serial.print("Next four characters are"); Serial.print(iscoData[x + 1]); Serial.print(iscoData[x + 2]); Serial.print(iscoData[x + 3]); Serial.println(iscoData[x + 4]);
      Serial.print("Bottle Number is ");
      //Serial.print(bottleNumber);
      //return bottleNumber;
      return bottleNumber;
    }

  }
  return 0;
}


unsigned long getSampledTime() //get time time the last bottle was sampled
{
  int commaNumber2 = 0;
  float sampledTime;
  for (int x = 0; x < 210 ; ++x)
  {
    if (iscoData[x] == ',')
    {
      ++commaNumber2;
      //Serial.print(commaNumber);
    }
    if (commaNumber2 == 11)
    {
      int startTimeIndex = x + 1;

      for (int y = 0; y < 11; ++y)
      {
        iscoTime[y] = iscoData[startTimeIndex];
        ++startTimeIndex;
      }
      Serial.println(iscoTime);
      double sampledTime = atof(iscoTime);
      time_t unixTime;
      Serial.print("Sampled time "); Serial.println(sampledTime, 6);
      unixTime = (sampledTime - 25569) * 86400;
      Serial.print("unix time "); Serial.println(unixTime);
      return unixTime;

      //Serial.print("Next two characters are");Serial.print(iscoData[x+1]);Serial.println(iscoData[x+2]);
      //Serial.print("Bottle Number is ");Serial.println(bottleNumber);

    }

  }
  return 0;
}

void sendSampleReply(char*from)
{
  Serial.println("In sample reply");
  Serial.print("Sampled -- Bottle ");
  Serial.print(getBottleNumber());
  unsigned long sampled = getSampledTime();
  //Serial.print("time_t=");Serial.println(sampled);
  Serial.print(" @ ");
  Serial.print(month(sampled)); Serial.print("/"); Serial.print(day(sampled)); Serial.print("/"); Serial.print(year(sampled)); Serial.print("  "); Serial.print(hour(sampled)); Serial.print(":"); Serial.print(minute(sampled)); Serial.print(":"); Serial.println(second(sampled));
  char message[100];
  int y = sprintf(message, "%s%i%s%i%s%i%s%i%s%i%s%i%s%i", "Sampled bottle ", getBottleNumber(), " @ ", month(sampled), "/", day(sampled), "/", year(sampled), "   ", hour(sampled), ":", minute(sampled), ":", second(sampled));
  Serial.print("Sending to "); Serial.println(from);
  sendSMS(from, message);

}


void getBV()
{

  float bvVoltSplit = analogRead(BattRail_VIN) * (3.3 / 1023);
  float hpVoltSplit = analogRead(FiveRail_VIN) * (3.3 / 1023);
  //float ThreeVoltSplit = analogRead(ThreeRail_VIN) * (3.3 / 1023);
  Serial.print("Raw Batt voltage = "); Serial.println(bvVoltSplit);
  Serial.print("Raw Hydra voltage = "); Serial.println(hpVoltSplit);
  //Serial.print("Raw 33 voltage = "); Serial.println(ThreeVoltSplit);
  //ThreeVoltage = ThreeVoltSplit;
  BattVoltage = bvVoltSplit * (1000 + 220) / 220; //Resistor Values R1 = 220 , R2 = 1000
  FiveVoltage = hpVoltSplit * (220 + 220) / 220; //Resistor Values R1 = 220 , R2 = 1000
  Serial.print("Batt voltage = "); Serial.println(BattVoltage);
  Serial.print("5V voltage = "); Serial.println(FiveVoltage);
  //Serial.print("33 voltage = "); Serial.println(ThreeVoltage);
}

void toggleRQ30()
{
  if (RQ30)
  {
    RQ30 = false;
    digitalWrite(RQ30Relay, LOW);
  } else
  {
    RQ30 = true;
    digitalWrite(RQ30Relay, HIGH);
  }
}


void postData() //post Data to VIPER
{
  if (! client.init(*GPRSSerial)) {
    Serial.println(F("Couldn't find FONA"));
    Serial.println("Cannot post to VIPER, GPRS on Fona not working");
    Serial.println("Cannot post to VIPER, GPRS on Fona not working");
    Serial.println("Cannot post to VIPER, GPRS on Fona not working");
    Serial.println("Cannot post to VIPER, GPRS on Fona not working");
    Serial.println("Cannot post to VIPER, GPRS on Fona not working");
  } else
  {
    int intWL;
    if (getWaterLevel()) //Transpose the water level to 50 (wet) or 0 (dry) for viewing on VIPER
    {
      intWL = 50;
    } else
    {
      intWL = 0;
    }
    //client.addFloat("moisture1", moisture * 100, "%");
    //client.addFloat("moisture2", moisture2 * 100, "%");
    //client.addFloat("moisture3", moisture3 * 100, "%");
    //client.addFloat("temp1", temp, "degC");
    //client.addFloat("temp2", temp2, "degC");
    //client.addFloat("temp3", temp3, "degC");
    client.addInt("Bottle Number", getBottleNumber(), "/24");
    client.addFloat("Battery Voltage", BattVoltage, "V");
    client.addInt("Level Reading", intWL, " 0/50");
    //client.add(VARIABLE_LABEL_2, humidity);
    ///client.add(VARIABLE_LABEL_3, pressure);
    client.sendAll(unit, GPS);
    client.clearData();
    Serial.println("Reached end of postData");
     }
}

int getFonaStatus()
{

  int onCount = 0;
  int z = 0;

  for (z; z < 1000; ++z)
  {
    if (analogRead(FONA_PS) > 500)
    {
      ++onCount;
    }
    delay(1);
  }
  return onCount;
}

void toggleFona() {
  digitalWrite(FONA_RI_BYPASS, HIGH);
  if (fonaPower)
  {
    digitalWrite(FONA_PKEY, LOW); //turn off Fona
    delay(5000);
    digitalWrite(FONA_PKEY, HIGH); //turn off Fona
    fonaPower = false;
    Serial.println("Turned off Fona");
  } else
  {
    digitalWrite(FONA_PKEY, LOW); //turn on Fona
    delay(5000);
    digitalWrite(FONA_PKEY, HIGH); //turn on Fona
    fonaPower = true;
    Serial.println("Turned on Fona");
  }

}


void printDigits(int digits) {
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}


void digitalClockDisplay() {
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year());
  Serial.println();
}

void setfileName()
{
  int y = sprintf(fileName, "%s%s%i%s", monthChar, dateChar, year(), ".txt");
  Serial.print("Filename is: "); Serial.println(fileName);
}


void saveData() {

  if (writeNum == 0)
  {
    buildDateTime();
    setfileName();
  }


  dtostrf(BattVoltage, 3, 2, bVChar);

  sprintf(levelChar, "%i", levelReading);

  myFile = SD.open(fileName, FILE_WRITE);
  // open the file for write at end like the Native SD library
  if (!myFile) {
    Serial.println("opening sd file for write failed");
  } else
  {

    Serial.println("File opened");

    // if the file opened okay, write to it:
    Serial.print("Writing to "); Serial.println(fileName);
    Serial.print("writeNum = "); Serial.println(writeNum);
    if (writeNum == 0)
    {

      writeNum++;
      Serial.println("Writing header");   //Write headers
      Serial.println("");

      myFile.print("## BOSS Unit ");
      myFile.print(unit);
      myFile.println(" ## ");
      myFile.println("");
      //int localHours = hour(local);
      //int utcHours = hour(utc);

      myFile.println("TIMESTAMP(UTC),Ambient Temperature(deg C),temp1 (degc),temp2 (degc),temp3 (degc),levelReading (/1024),Battery Voltage (V)");
    }
    delay(10);
    buildDateTime();
    myFile.print(dateTime);
    myFile.print(",");
    myFile.print(Temp1Char);
    myFile.print(",");
    myFile.print(Temp2Char);
    myFile.print(",");
    myFile.print(Temp3Char);
    myFile.print(",");
    myFile.print(levelChar);
    myFile.print(",");
    myFile.print(bVChar);
    myFile.println(" ");
    // close the file:
    myFile.close();
    Serial.println("done.");
  }


}


void buildDateTime()
{


  if (month() < 10)
  {
    int y = sprintf(monthChar, "%c%i", '0', month());
  } else
  {
    int y = sprintf(monthChar, "%i", month());
  }

  if (day() < 10)
  {
    int y = sprintf(dateChar, "%c%i", '0', day());
  } else
  {
    int y = sprintf(dateChar, "%i", day());
  }

  if (hour() < 10)
  {
    int y = sprintf(hourChar, "%c%i", '0', hour());
  } else
  {
    int y = sprintf(hourChar, "%i", hour());
  }

  if (minute() < 10)
  {
    int y = sprintf(minuteChar, "%c%i", '0', minute());
  } else
  {
    int y = sprintf(minuteChar, "%i", minute());
  }

  if (second() < 10)
  {
    int y = sprintf(secondChar, "%c%i", '0', second());
  } else
  {
    int y = sprintf(secondChar, "%i", second());
  }


  //int y = sprintf(dateTimeVIPER, "%i%s%s%s%s%s%s%s%s%s%s%s%i%s", year(localNow), "-", localMonthChar, "-", localDateChar, "T", localHourChar, ":", minuteChar, ":", secondChar, "-0",tzOffset,":00");
  //Serial.println("In build");
  //Serial.print("DTV -- "); Serial.println(dateTimeVIPER);
  sprintf(dateTime, "%i%s%s%s%s%s%s%s%s%s%s", year(), "-", monthChar, "-", dateChar, "T", hourChar, ":", minuteChar, ":", secondChar);
}

time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

/*  code to process time sync messages from the serial port   */
#define TIME_HEADER  "T"   // Header tag for serial time sync message

unsigned long processSyncMessage() {
  unsigned long pctime = 0L;
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013

  if (Serial.find(TIME_HEADER)) {
    pctime = Serial.parseInt();
    return pctime;
    if ( pctime < DEFAULT_TIME) { // check the value is a valid time (greater than Jan 1 2013)
      pctime = 0L; // return 0 to indicate that the time is not valid
    }
  }
  return pctime;
}

bool getWaterLevel()
{
  int reading = 0;
  int over = 0;
  int under = 0;
  for (int x = 0 ; x < 50 ; ++x)
  {
    reading = analogRead(levelPin);
    Serial.print("Reading --- ");
    Serial.println(reading);
    if (reading > 100)
    {
      ++over;
    } else
    {
      ++under;
    }
  }
  if (over + 20 > under) //bias towards failing dry
  {
    return false;
  } else
  {
    return true;
  }

}
