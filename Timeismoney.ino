
#include <SPI.h>
#include <Ethernet.h>
#include <DmxSimple.h>
//#include <avr/pgmspace.h>

byte mac[] = { 0x90, 0xA2, 0xDA, 0x0F, 0x57, 0x4D };
EthernetClient client;
const unsigned long requestInterval = 14400UL;        // delay between server time requests, in seconds
boolean syncBool = true;                 // if we have gotten at least one online time value, then we can track offline time
unsigned long lastSync = requestInterval;           // last time you connected to the server, in seconds
unsigned long secondsSinceStartup = 0UL;    // used in the offline calculation of exact minutes
boolean connectionState = false;          // state of the connection last time through the main loop
String responseString;
unsigned long lastPulseMillis = 0UL;

int DMXSignalPin = 7;
int DMXPowerRelayPin = 4;
const int DMXMaxChannels = 512;
int secondsTogglePin = 22;               // toggle pin between minutes to set pace for uno board animating seconds

int theHour = 10;
int theMinute = 24;
float floatingMinute = 39.00;
int hourHand = 0;
int minuteHand = 0;
byte upperMinuteHand;
byte lowerMinuteHand;
int lastHourHand = 0;
int lastMinuteHand = 0;
int offsetLastHourHand = 1;
int offsetHourHand = 1;
const int hourFixtures = 48;
const int minuteFixtures = 48;
//const int secondFixtures = 60;
int hourChannelsOffset = 0;
int minuteChannelsOffset = hourFixtures * 3;
//int secondChannelsOffset = minuteChannelsOffset + minuteFixtures * 3;
int sineArrayFrameCounter = 1;

const byte sineArray [9][36] = {
  {104, 109, 113, 116, 119, 122, 123, 125, 125, 125, 123, 122, 119, 116, 113, 109, 104, 100, 96, 91, 88, 84, 81, 78, 77, 75, 75, 75, 77, 78, 81, 84, 88, 91, 96, 100},
  {78, 80, 83, 87, 91, 95, 99, 103, 108, 112, 115, 119, 121, 123, 124, 125, 125, 124, 122, 120, 117, 113, 109, 105, 101, 97, 92, 88, 85, 81, 79, 77, 76, 75, 75, 76},
  {120, 123, 124, 125, 125, 124, 123, 120, 118, 114, 111, 106, 102, 98, 94, 89, 86, 82, 80, 77, 76, 75, 75, 76, 77, 80, 82, 86, 89, 94, 98, 102, 106, 111, 114, 118},
  {75, 75, 76, 77, 79, 81, 85, 88, 92, 97, 101, 105, 109, 113, 117, 120, 122, 124, 125, 125, 124, 123, 121, 119, 115, 112, 108, 103, 99, 95, 91, 87, 83, 80, 78, 76},
  {125, 123, 122, 119, 116, 113, 109, 104, 100, 96, 91, 88, 84, 81, 78, 77, 75, 75, 75, 77, 78, 81, 84, 88, 91, 96, 100, 104, 109, 113, 116, 119, 122, 123, 125, 125},
  {82, 79, 77, 76, 75, 75, 76, 78, 80, 83, 86, 90, 94, 98, 103, 107, 111, 115, 118, 121, 123, 124, 125, 125, 124, 122, 120, 117, 114, 110, 106, 102, 97, 93, 89, 85},
  {114, 111, 106, 102, 98, 94, 89, 86, 82, 80, 77, 76, 75, 75, 76, 77, 80, 82, 86, 89, 94, 98, 102, 106, 111, 114, 118, 120, 123, 124, 125, 125, 124, 123, 120, 118},
  {89, 93, 97, 102, 106, 110, 114, 117, 120, 122, 124, 125, 125, 124, 123, 121, 118, 115, 111, 107, 103, 98, 94, 90, 86, 83, 80, 78, 76, 75, 75, 76, 77, 79, 82, 85},
  {96, 91, 88, 84, 81, 78, 77, 75, 75, 75, 77, 78, 81, 84, 88, 91, 96, 100, 104, 109, 113, 116, 119, 122, 123, 125, 125, 125, 123, 122, 119, 116, 113, 109, 104, 100}
};

// Color definitions (RGB)
const byte baseColor [3] = {0, 64, 255};
const byte hourHandColor [3] = {200, 32, 0};
const byte minuteHandColor [3] = {255, 32, 0};

// new variables for am/pm and power saving mode
boolean pmStatus = false;
int pmStatusPin = 5;
const int powerSaverStartHour = 11;      //assumed to be PM
const int powerSaverStopHour = 5;       //assumed to be AM

void setup() {
  DmxSimple.usePin(DMXSignalPin);
  DmxSimple.maxChannel(DMXMaxChannels);
  pinMode(DMXPowerRelayPin, OUTPUT);
  digitalWrite(DMXPowerRelayPin, LOW);
  pinMode(secondsTogglePin, OUTPUT);
  digitalWrite(secondsTogglePin, LOW);
  Serial.begin(9600);  // start serial port:
  delay(1000);  // give the ethernet module time to boot up:
  Ethernet.begin(mac);  // start the Ethernet connection using a fixed IP address and DNS server:
  Serial.print("My IP address: ");  // print the Ethernet board/shield's IP address:
  Serial.println(Ethernet.localIP());
}

void loop() {
  // lastSync = 0;         //comment out to reinstate online time retrieval
  Ethernet.maintain();
  if (client.available()) {
    char c = client.read();
    //Serial.print(c);
    responseString += c;
  }
  // if there's no net connection, but there was last time, stop the client:
  if (!client.connected() && connectionState) {
    Serial.println("Disconnecting");
    client.stop();
    parseTimeString();
  }
  // update offline time every second
  if (syncBool && ((millis() / 1000UL - secondsSinceStartup) >= 1)) {
    updateOfflineTime();
    if (digitalRead(DMXPowerRelayPin) == LOW) {
      Serial.println("ACTIVATING DMX POWER RELAY");
      digitalWrite(DMXPowerRelayPin, HIGH);
    }
    updateMinuteHand();
    if (hourHand != lastHourHand) updateHourHand();
  }
  // if not connected, and postingInverval has passed, send server time request:
  if (!client.connected() && (millis() / 1000UL - lastSync > requestInterval)) {
    requestTimeString();
  }
  //if (millis() > lastPulseMillis + 120)
  pulseFixtures(30);
  // store the state of the connection for next time through the loop:
  connectionState = client.connected();
}

void requestTimeString() {
  responseString = "";
  Serial.println("Connecting... ");
  if (client.connect("www.ansenseale.com", 80)) {
    client.println("GET /frost-bank-clock/time_v2.cfm HTTP/1.1");
    client.println("Host: www.ansenseale.com");
    client.println("User-Agent: arduino-ethernet");
    client.println("Connection: close");
    client.println();
  }
  else {
    Serial.println("Connection failed");
    client.stop();
  }
}

void parseTimeString() {
  //Serial.print("Time at index of: ");
  //Serial.println(responseString.indexOf("\r\n\r\n"));
  int hourIndex = responseString.indexOf("\r\n\r\n") + 4;
  String hourString = responseString.substring(hourIndex, hourIndex + 2);
  int minuteIndex = hourIndex + 3;
  String minuteString = responseString.substring(minuteIndex, minuteIndex + 2);
  int pmStatusIndex = minuteIndex + 3;
  String pmStatusString = responseString.substring(pmStatusIndex, pmStatusIndex + 2);
  theHour = hourString.toInt();
  theMinute = minuteString.toInt();
  floatingMinute = float(theMinute);
  if (pmStatusString == "PM") {
    pmStatus = true;
  } else {
    pmStatus = false;
  }
  syncBool = true;
  lastSync = millis() / 1000UL;
  Serial.print("Server time: ");
  Serial.print(theHour);
  Serial.print(" hours and ");
  Serial.print(theMinute);
  Serial.print(" minutes (");
  Serial.print(millis() / 1000UL);
  Serial.print(") ");
  Serial.println(pmStatus);
}

void updateOfflineTime() {
  //floatingMinute += (((float)(millis() / 1000UL - secondsSinceStartup)) / (float)(60.0000)); // Calculates the difference between now and when the last measurement was taken
  floatingMinute += float(millis() / 1000UL - secondsSinceStartup) / 60.0000; // Calculates the difference between now and when the last measurement was taken
  secondsSinceStartup = millis() / 1000UL;
  // Takes care of changing on the hour or minute
  if (floatingMinute >= (1 + theMinute)) {
    theMinute += 1;
    flipSecondsPin();
  }
  if (theMinute >= 60) {
    theHour += 1;
    theMinute = 0;
    floatingMinute = 0.0000;
    if (theHour == 12) pmStatus = !pmStatus;
  }
  if (theHour > 12) theHour = 1;

  Serial.print("Offline time: ");
  Serial.print(theHour);
  Serial.print(" hours and ");
  Serial.print(theMinute);
  Serial.print(" minutes (");
  Serial.print(millis() / 1000UL);
  Serial.print(") ");
  Serial.print(floatingMinute);
  Serial.print(" ");
  Serial.println(pmStatus);

  int tempHourHand = 1 + ((theHour - 1) + floatingMinute / 60.0000) * hourFixtures / 12.0000;
  if (tempHourHand > hourFixtures) tempHourHand = 1;
  //Serial.println(1 + (theHour - 1) + floatingMinute / 60.0000);
  if (tempHourHand != hourHand) {
    Serial.print("UPDATE HOUR HAND: ");
    Serial.print(hourHand);
    Serial.print(" > ");
    Serial.println(tempHourHand);
    hourHand = tempHourHand;
  }
  //offset the hour hand to move 12 to the bottom from the top
  offsetLastHourHand = lastHourHand + 4;
  offsetHourHand = hourHand + 4;
  if (offsetLastHourHand > 48) offsetLastHourHand = offsetLastHourHand - 48;
  if (offsetHourHand > 48) offsetHourHand = offsetHourHand - 48;


  int tempMinuteHand = 1 + floatingMinute * minuteFixtures / 60.0000;
  if (tempMinuteHand > minuteFixtures) tempMinuteHand = 1;
  if (tempMinuteHand != minuteHand) {
    //    Serial.print("UPDATE MINUTE HAND: ");
    //    Serial.print(minuteHand);
    //    Serial.print(" > ");
    //    Serial.println(tempMinuteHand);
    minuteHand = tempMinuteHand;
  }
}

void updateMinuteHand () {
  float floatingMinuteHand = floatingMinute * minuteFixtures / 60;
  float remainder = floatingMinuteHand - int(floatingMinuteHand);
  float lowerMinuteHandScaler = 1 - remainder;
  float upperMinuteHandScaler = remainder;
  lowerMinuteHand = int(floatingMinuteHand);
  upperMinuteHand = lowerMinuteHand + 1;
  if (lowerMinuteHand < 1) lowerMinuteHand = minuteFixtures;
  if (upperMinuteHand > minuteFixtures) upperMinuteHand = 1;
  DmxSimple.write(upperMinuteHand * 3 - 1 + minuteChannelsOffset, minuteHandColor[0] * upperMinuteHandScaler + baseColor[0] * lowerMinuteHandScaler / 2);
  DmxSimple.write(upperMinuteHand * 3 - 2 + minuteChannelsOffset, minuteHandColor[1] * upperMinuteHandScaler + baseColor[1] * lowerMinuteHandScaler / 2);
  DmxSimple.write(upperMinuteHand * 3 - 0 + minuteChannelsOffset, minuteHandColor[2] * upperMinuteHandScaler + baseColor[2] * lowerMinuteHandScaler / 2);
  DmxSimple.write(lowerMinuteHand * 3 - 1 + minuteChannelsOffset, minuteHandColor[0] * lowerMinuteHandScaler + baseColor[0] * upperMinuteHandScaler / 2);
  DmxSimple.write(lowerMinuteHand * 3 - 2 + minuteChannelsOffset, minuteHandColor[1] * lowerMinuteHandScaler + baseColor[1] * upperMinuteHandScaler / 2);
  DmxSimple.write(lowerMinuteHand * 3 - 0 + minuteChannelsOffset, minuteHandColor[2] * lowerMinuteHandScaler + baseColor[2] * upperMinuteHandScaler / 2);
}

void updateHourHand () {
  for (int i = 0; i <= 100; i++) {
    if (hourHand != lastHourHand && lastHourHand > 0) {
      DmxSimple.write(offsetLastHourHand * 3 - 1, hourHandColor[0] * (100 - i) / 100 + baseColor[0] * i / 100 + baseColor[0] * i / 100); //red
      DmxSimple.write(offsetLastHourHand * 3 - 2, hourHandColor[1] * (100 - i) / 100 + baseColor[1] * i / 100 + baseColor[1] * i / 100); //green
      DmxSimple.write(offsetLastHourHand * 3 - 0, hourHandColor[2] * (100 - i) / 100 + baseColor[2] * i / 100 + baseColor[2] * i / 100); //blue
    }
    if (hourHand != lastHourHand) {
      DmxSimple.write(offsetHourHand * 3 - 1, hourHandColor[0] * i / 100 + baseColor[0] * (100 - i) / 100); //red
      DmxSimple.write(offsetHourHand * 3 - 2, hourHandColor[1] * i / 100 + baseColor[1] * (100 - i) / 100); //green
      DmxSimple.write(offsetHourHand * 3 - 0, hourHandColor[2] * i / 100 + baseColor[2] * (100 - i) / 100); //blue
    }
    delay(20);
    pulseFixtures(10);
  }
  lastHourHand = hourHand;
  offsetLastHourHand = offsetHourHand;
}

void pulseFixtures (int myDelay) {
  for (byte i = 1; i <= hourFixtures; i++) {
    if (i != offsetHourHand && i != offsetLastHourHand) {
      if ((!pmStatus && theHour < powerSaverStartHour) or (pmStatus && theHour > powerSaverStopHour)) {
        DmxSimple.write(i * 3 - 1, baseColor[0] * sineArray[i % 9][sineArrayFrameCounter - 1] / 255);
        DmxSimple.write(i * 3 - 2, baseColor[1] * sineArray[i % 9][sineArrayFrameCounter - 1] / 255);
        DmxSimple.write(i * 3 - 0, baseColor[2] * sineArray[i % 9][sineArrayFrameCounter - 1] / 255);
      } else {
        DmxSimple.write(i * 3 - 1, 0);
        DmxSimple.write(i * 3 - 2, 0);
        DmxSimple.write(i * 3 - 0, 0);
      }
    }
  }
  for (byte i = 1; i <= minuteFixtures; i++) {
    if (i != upperMinuteHand && i != lowerMinuteHand) { //minuteFixturesState[i - 1] == false
      if ((!pmStatus && theHour < powerSaverStartHour) or (pmStatus && theHour > powerSaverStopHour)) {
        DmxSimple.write(i * 3 - 1 + minuteChannelsOffset, baseColor[0] * sineArray[i % 9][sineArrayFrameCounter - 1] / 255);
        DmxSimple.write(i * 3 - 2 + minuteChannelsOffset, baseColor[1] * sineArray[i % 9][sineArrayFrameCounter - 1] / 255);
        DmxSimple.write(i * 3 - 0 + minuteChannelsOffset, baseColor[2] * sineArray[i % 9][sineArrayFrameCounter - 1] / 255);
      } else {
        DmxSimple.write(i * 3 - 1 + minuteChannelsOffset, 0);
        DmxSimple.write(i * 3 - 2 + minuteChannelsOffset, 0);
        DmxSimple.write(i * 3 - 0 + minuteChannelsOffset, 0);
      }
    }
  }
  updateSineArrayFrameCounter();
  delay(myDelay);
  lastPulseMillis = millis();
}

void updateSineArrayFrameCounter() {
  sineArrayFrameCounter++;
  if (sineArrayFrameCounter > 36) sineArrayFrameCounter = 1;
}

void flipSecondsPin () {
  if (digitalRead(secondsTogglePin) == HIGH) {
    digitalWrite(secondsTogglePin, LOW);
  } else {
    digitalWrite(secondsTogglePin, HIGH);
  }
}
