/*********
  Complete project details at https://randomnerdtutorials.com
  
  This is an example for our Monochrome OLEDs based on SSD1306 drivers. Pick one up today in the adafruit shop! ------> http://www.adafruit.com/category/63_98
  This example is for a 128x32 pixel display using I2C to communicate 3 pins are required to interface (two I2C and one reset).
  Adafruit invests time and resources providing this open source code, please support Adafruit and open-source hardware by purchasing products from Adafruit!
  Written by Limor Fried/Ladyada for Adafruit Industries, with contributions from the open source community. BSD license, check license.txt for more information All text above, and the splash screen below must be included in any redistribution. 
*********/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define NUMFLAKES     10 // Number of snowflakes in the animation example

#define LOGO_HEIGHT   16
#define LOGO_WIDTH    16
static const unsigned char PROGMEM logo_bmp[] =
{ B00000000, B11000000,
  B00000001, B11000000,
  B00000001, B11000000,
  B00000011, B11100000,
  B11110011, B11100000,
  B11111110, B11111000,
  B01111110, B11111111,
  B00110011, B10011111,
  B00011111, B11111100,
  B00001101, B01110000,
  B00011011, B10100000,
  B00111111, B11100000,
  B00111111, B11110000,
  B01111100, B11110000,
  B01110000, B01110000,
  B00000000, B00110000 };

/////////////////////////////
// Header
#define RECORD_DELAY            200         // 200 ms inbetween each recording
#define RECORD_HISTORY_SIZE     16          // 10 x 200 ms = 2 seconds
#define RECORD_HISTORY_MASK     0x0000000F  // 0-15 index
#define LIFTOFF_THRESHOLD       1.0f        // Consider liftoff when you reach 1m

Adafruit_BMP280   bmp; // I2C
static int        Phase                             = 0;
static float      StartingPressure                  = 0.0f;
static float      Apex                              = 0.0f;
static unsigned   HistoryIndex                      = 0;
static short      AltitudeHistory[RECORD_HISTORY_SIZE];    
static unsigned   EEPROMIndex                       = 0;
static short      RecordNo                          = 0;
static short      DisplayCallCount                  = 0;
/////

void setup() {
  Serial.begin(115200);

  Serial.println(F("Started!"));

  Bmp280Setup();

  DisplaySetup();

  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();
}

void Bmp280Setup() 
{
  if (!bmp.begin()) {  
    Serial.println(F("Could not find a valid BMP280 sensor, check wiring!"));
    while (1);
  } 

  // Init for recording
  StartingPressure = bmp.readPressure();
}

void DisplaySetup() 
{
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
}

float ReadAltitude()
{
    return bmp.readAltitude(StartingPressure / 100.0f);
}

void WriteAltitude(float Altitude)
{
  short CompressedAltitude = (short)(Altitude * 10.0f);
  AltitudeHistory[HistoryIndex] = CompressedAltitude;
  HistoryIndex = (HistoryIndex + 1) & RECORD_HISTORY_MASK;
}

void DisplayStatus()
{
    Serial.print(F("Temperature = "));
    Serial.print(bmp.readTemperature());
    Serial.println(" *C");
    
    Serial.print(F("Pressure = "));
    Serial.print(bmp.readPressure());
    Serial.println(" Pa");
 
    Serial.print(F("Approx altitude = "));
    Serial.print(ReadAltitude()); // this should be adjusted to your local forcase
    Serial.println(" m");
    
    Serial.println();    
}


void DisplayApex(const char* Title, bool Blinking)
{
    DisplayCallCount = (DisplayCallCount + 1) % 10;
  
    display.clearDisplay();
    display.setTextSize(2); // Draw 2X-scale text
    display.setTextColor(WHITE);
    if((DisplayCallCount < 5) || !Blinking)
    {    
      display.setCursor(10, 0);
      display.println(Title);
    }
      
    char str[20];
    char ApexStr[10];
    dtostrf(Apex, 6, 1, ApexStr);
    sprintf(str, "Alt%s", ApexStr);

    display.setTextSize(2); // Draw 2X-scale text
    display.setTextColor(WHITE);
    display.setCursor(10, 16);   
    display.println(str);

    display.display();      // Show initial text
}

// Display older log
void Phase0()
{  
    display.clearDisplay();
    display.setTextSize(2); // Draw 2X-scale text
    display.setTextColor(WHITE);
    display.setCursor(10, 0);
    display.println(F("DUMPING..."));
    display.display();      // Show initial text

  
    Serial.println(F(">>> LAST RECORDED RUN"));

    Serial.print(F("EEPROM Size = "));
    Serial.println(EEPROM.length());
  
    short RecordNo2 = 0;
    EEPROM.get(0, RecordNo);
    EEPROM.get(2, RecordNo2);
  
    Serial.print(F("Record Number "));
    Serial.print(RecordNo);  
    Serial.print(F(" / "));
    Serial.println(RecordNo2);  
  
    if(RecordNo == RecordNo2)
      RecordNo++;
     else
      RecordNo = 0;
  
    float Temperature;
    EEPROM.get(4, Temperature);
    Serial.print(F("Temperature = "));
    Serial.print(Temperature);
    Serial.println(" *C");
  
    float Pressure;
    EEPROM.get(8, Pressure);
    Serial.print(F("Pressure = "));
    Serial.print(Pressure);
    Serial.println(" Pa");
  
    unsigned ReadIndex = 12;
    Apex = 0.0f;
    while(ReadIndex < EEPROM.length())
    {
        float Altitude;
        short AltitudeS16;
        //EEPROM.get(ReadIndex, Altitude);
        //ReadIndex += 4;
        EEPROM.get(ReadIndex, AltitudeS16);
        ReadIndex += 2;  
        Altitude = ((float)AltitudeS16) / 10.0f;        
        Serial.println(Altitude); 

        // Keep highest point
        if(Altitude > Apex)
          Apex = Altitude;
    }
    
    Serial.println(F(">>> END OF RECORDING"));
    Serial.println();    
    Phase = 1;
}

// Wait for liftoff
void Phase1()
{    
    DisplayApex("STAND-BY", true);
      
    // Temp
    static int StatusDisplayCount = 10;
    if(--StatusDisplayCount == 0)
    {
      DisplayStatus();
      StatusDisplayCount = 10;
    }
    //

    float CurrentAltitude = ReadAltitude();
    WriteAltitude(CurrentAltitude);

    if(CurrentAltitude >= LIFTOFF_THRESHOLD)
    {
      Serial.println(F("LIFT-OFF!"));
      
      // Write header in EEPROM
      EEPROM.put(0, RecordNo);
      EEPROM.put(2, RecordNo);
      float Temp = bmp.readTemperature();
      float Pressure = bmp.readPressure();
      EEPROM.put(4, Temp);
      EEPROM.put(8, Pressure);
      EEPROMIndex = 12;   

      // Go to record phase
      Apex = 0.0f;
      Phase = 2;
    }
      
    delay(RECORD_DELAY);
}

// Continous recording after lift-off
void Phase2()
{ 
    // Store altitude into EEPROM
    EEPROM.put(EEPROMIndex, AltitudeHistory[HistoryIndex]);
    EEPROMIndex += 2;
    if(EEPROMIndex >= EEPROM.length())
      Phase = 3;

    char str[20];
    sprintf(str, "GO! (%u%%)", (EEPROMIndex * 10)/ (EEPROM.length() / 10));  
    DisplayApex(str, false);
  
    float CurrentAltitude = ReadAltitude();
    WriteAltitude(CurrentAltitude);

    // Keep track of highest point
    if(CurrentAltitude > Apex)
      Apex = CurrentAltitude;
  
    delay(RECORD_DELAY);
}

// Memory full!
void Phase3()
{
    DisplayApex("MEM-FULL", false);
}

void loop() 
{
    if(Phase == 0)
      Phase0();           // Print out previous EEPROM history
    else if(Phase == 1)
      Phase1();           // Wait for lift-off    
    else if(Phase == 2)
      Phase2();           // Lift-off! Record in EEPROM
    else 
      Phase3();
}
