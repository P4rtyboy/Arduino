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

//////////////////////////////////////////////////////////////////////////
// Evaluated limit of the system
// - With no idle, there is enough storage for  510 writing, * 10m = 5.1km or 2.55km up & down
// - Maximum 3.2km up/down because we write absolute in a short
// - Maximum duration of recording time is 4064 seconds = 1h07 (with no altitude variation)
// - Recording time is around 7ms, so maximum accuracy is 10m/7ms = speed of order of 1,4km in 1 second
//      - BMP Reading is 1-2 ms, Eeprom write 2-3ms x 2 bytes. Only way to make it faster, is to either write 1 byte, or buff in memory 
//        and write in eeprom when nothing to do.



#define SEPARATOR_MARKER              0xFFFF                  // Unique value that indicate the beggining of a record
#define MAX_DELAY_MARKER              0xFEFE                  // When we reach Max delay, we always write the new alt.
#define MAX_DELAY                     32000                   // 32 sec = 32000 ms
#define ALTITUDE_STEPS_FLOAT          10.0f                   // Each step is 10m change
#define LIFTOFF_THRESHOLD             2.0f                    // Start recording after 2m liftoff detected

// Header size is 4 bytes
// 2 bytes for absolute altitude  (S16)
// 2 Bytes for Temperature        (S16)
#define HEADER_SIZE                   4

#define DUMP_RESET_DELAY              5                         // Number of sec to wait for a reset command
#define BLINKING_SPEED                30                        // Every 60 sleeps, blink (30 loop on/ 30 loop off)

#define PHASE_DUMP                    0
#define PHASE_STANDBY                 1
#define PHASE_RECORDING               2
#define PHASE_MEMFULL                 3

Adafruit_BMP280   bmp;                                        // I2C
static int        gPhase                = PHASE_DUMP;
static unsigned   gEEPROMIndex          = 0;
static unsigned   gEEPROMSize           = 0;
static float      gStartingPressure     = 0.0f;
static float      gApex                 = 0.0f;

static short      gDisplayCallCount     = 0;

///////////////////////////////////////////////////////////////////////////////////////////
//// SETUP
///////////////////////////////////////////////////////////////////////////////////////////

void UpdateDisplay(const char* Title, bool Blinking);

void setup() 
{
  // Retreive EEPROM size
  gEEPROMSize = EEPROM.length();

  // Init serial port
  Serial.begin(115200);

  Serial.println(F("Altimeter V1.0 Started!"));  

  DisplaySetup();

  Bmp280Setup();

  // Init for recording
  gStartingPressure = bmp.readPressure(); 

  // Clear the buffer
  display.clearDisplay();
}

void Bmp280Setup() 
{
  if (!bmp.begin()) 
  {  
    Serial.println(F("Could not find a valid BMP280 sensor, check wiring!"));
    UpdateDisplay("BMP-ERR", false);
    while (1);
  } 
}

void DisplaySetup() 
{
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) 
  { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
}

///////////////////////////////////////////////////////////////////////////////////////////

float ReadRelativeAltitude()
{
    return bmp.readAltitude(gStartingPressure / 100.0f);
}

void WriteS16(short val)
{
    EEPROM.put(gEEPROMIndex, val);
    gEEPROMIndex += sizeof(val);  
}

void WriteU16(unsigned short val)
{
    EEPROM.put(gEEPROMIndex, val);
    gEEPROMIndex += sizeof(val);    
}

void DisplayStatus()
{
    static int StatusDisplayCount = 300;
    if(--StatusDisplayCount == 0)
    {       
        // Each bmp reading has been measured, and it seems they take around 1-2 ms each
        float Temp = bmp.readTemperature();
        float Pressure = bmp.readPressure();
        float Altitude = ReadRelativeAltitude();
        
        Serial.print(F("Temperature     = "));
        Serial.print(Temp);
        Serial.println(" C");
        
        Serial.print(F("Pressure        = "));
        Serial.print(Pressure);
        Serial.println(" Pa");
     
        Serial.print(F("Approx altitude = "));
        Serial.print(Altitude); // this should be adjusted to your local forcase
        Serial.println(" m");

        Serial.print(F("Memory Index    = "));
        Serial.println(gEEPROMIndex);
        
        Serial.println();    
        StatusDisplayCount = 500;    
    }
}

void UpdateDisplay(const char* Title, bool Blinking)
{  
    gDisplayCallCount = (gDisplayCallCount + 1) % (BLINKING_SPEED * 2);
  
    display.clearDisplay();
    display.setTextSize(2); // Draw 2X-scale text
    display.setTextColor(WHITE);
    if((gDisplayCallCount < BLINKING_SPEED) || !Blinking)
    {    
      display.setCursor(10, 0);
      display.println(Title);
    }
      
    char str[20];
    char ApexStr[10];
    dtostrf(gApex, 6, 1, ApexStr);
    sprintf(str, "%s m", ApexStr);

    display.setTextSize(2); // Draw 2X-scale text
    display.setTextColor(WHITE);
    display.setCursor(10, 16);   
    display.println(str);

    unsigned short MemUsageHeight = (gEEPROMIndex * SCREEN_HEIGHT) / gEEPROMSize;
    display.drawRect(SCREEN_WIDTH-10, 0, 10, SCREEN_HEIGHT, WHITE);        
    display.fillRect(SCREEN_WIDTH-10, SCREEN_HEIGHT - MemUsageHeight, 10, MemUsageHeight, WHITE);
    display.display();      // Show initial text
}

//////////////////////////////////////////////////////
// Display older log & retreive EEPROM index to write
//////////////////////////////////////////////////////
void PhaseDump()
{     
    Serial.println(F(">>> DISPLAY RECORDED RUNS"));
    Serial.print(F("EEPROM Size = "));
    Serial.println(EEPROM.length());    
    
    while(gEEPROMIndex + HEADER_SIZE < gEEPROMSize)
    {
        UpdateDisplay("READING", false);      

        /*
        // TODO: TEMP
        {
            for(unsigned i = 0; i < 10; i++)
            {
                unsigned short  Tmp; 
                EEPROM.get(gEEPROMIndex + i, Tmp);
                Serial.println(Tmp);
            }
        }
        */

        // Read altitude. f its marker, exit (do not increment gEEPROMIndex yet coz we will overwrite it)
        short  AbsAlt; 
        EEPROM.get(gEEPROMIndex, AbsAlt);        
        if(AbsAlt == SEPARATOR_MARKER)
          break;
          
        short Temp;
        EEPROM.get(gEEPROMIndex + 2, Temp);        

        Serial.println("");
        Serial.print(F("Mem Position      : "));
        Serial.println(gEEPROMIndex);

        Serial.print(F("Starting altitude : "));
        Serial.print(((float)AbsAlt) / 10.0f);
        Serial.println(" m");        

        Serial.print(F("Temperature       : "));
        Serial.print(((float)Temp) / 10.0f);
        Serial.println(" C");

        gEEPROMIndex += 4;

        gApex = 0.0f;
        float         RelativeAltitude = 0.0f;
        unsigned long TimeMs = 0;
        
        while(gEEPROMIndex < gEEPROMSize)
        {
            UpdateDisplay("READING", false);   
                   
            unsigned short  ValU16;
            EEPROM.get(gEEPROMIndex, ValU16);
            gEEPROMIndex += 2;
            
            // We are reaching an end of record marker - move to next
            if(ValU16 == SEPARATOR_MARKER)
                break;

            if(ValU16 != MAX_DELAY_MARKER)
            {
                TimeMs += ValU16 & 0x7FFF;
                if(ValU16 & 0x8000) 
                  RelativeAltitude -= ALTITUDE_STEPS_FLOAT;
                else
                  RelativeAltitude += ALTITUDE_STEPS_FLOAT;
            }
            else
            {
                // When we have a MAX_DELAY_MARKER, the next value is the relative value
                // And the time is always increased by MAX_DELAY
                short  ValS16;
                EEPROM.get(gEEPROMIndex, ValS16);
                gEEPROMIndex += 2;   
                RelativeAltitude = ((float)ValS16) / 10.0f;    
                TimeMs += MAX_DELAY;
            }

            // Keep track of last block APEX
            if(RelativeAltitude > gApex)
              gApex = RelativeAltitude;

            // Display on serial port
            Serial.print(F("Time = "));
            Serial.print(TimeMs);
            Serial.print(F(" ms : Relative Altitude = "));
            Serial.print(RelativeAltitude);
            Serial.println(" m");
            
       } // End while(gEEPROMIndex < gEEPROMSize)        
    } // End while(gEEPROMIndex + 4 < gEEPROMSize)
    
    Serial.println(F(">>> END OF RECORDING"));
    Serial.println();

    // Wait for reset command        
    for(unsigned i =0; i < DUMP_RESET_DELAY; i++)
    {
        unsigned CountDown = DUMP_RESET_DELAY - i;
        if(CountDown == 5)          
          UpdateDisplay("RESET? 5", false);
        else if(CountDown == 4)          
          UpdateDisplay("RESET? 4", false);
        else if(CountDown == 3)          
          UpdateDisplay("RESET? 3", false);
        else if(CountDown == 2)          
          UpdateDisplay("RESET? 2", false);
        else if(CountDown == 1)          
          UpdateDisplay("RESET? 1", false);
        else
          UpdateDisplay("RESET?", false);
          
        Serial.print(F("Type R now to reset memory! "));
        Serial.print(CountDown);
        Serial.println(F("s..."));
        delay(1000); // Pause for 1 seconds
        if(Serial.available() > 0)
        {
          char InputChar = Serial.read();
          if(InputChar == 'R')
          {
              Serial.println(F("RESET REQUESTED - ERASING ALL MEMORY"));
              for(i = 0; i < gEEPROMSize; i += 2)
              {
                  gEEPROMIndex = gEEPROMSize - 2 - i;
                  UpdateDisplay("ERASING", true);

                  // Fill the whole memory with SEPARATOR_MARKER, but avoid writting it for nothing (EEPROM have a lifespan)
                  unsigned short Val;
                  EEPROM.get(gEEPROMIndex, Val);
                  if(Val != SEPARATOR_MARKER)                 
                    EEPROM.put(gEEPROMIndex, (unsigned short)SEPARATOR_MARKER);
              }
              break;
          }
          else if(InputChar == 'D')
          {
              // Memory Dump!
              for(i = 0; i < gEEPROMSize; i += 2)
              {
                  UpdateDisplay("DUMPING", true);
                  unsigned short Val;
                  EEPROM.get(i, Val);
                  Serial.print(i, HEX);
                  Serial.print(F(":"));
                  Serial.println(Val, HEX);
              }              
          }
        }
    }

    Serial.println(F("ALTIMETER READY TO RECORD!"));
    gPhase = ((gEEPROMIndex + HEADER_SIZE) < gEEPROMSize) ? PHASE_STANDBY : PHASE_MEMFULL;
}

// Wait for liftoff
void PhaseStandBy()
{    
    UpdateDisplay("STAND-BY", true);
      
    // TODO     : Temp
    //DisplayStatus();
    //

    // Reset apex to current altitude
    gApex = ReadRelativeAltitude();
    if(gApex < 0.0f)
      gApex = 0.0f;

    // TEMP    
    bool bypass = false;
    /*
    if(Serial.available() > 0)
      if(Serial.read() == 'G')
       bypass = true;
    */
    
    if((gApex >= LIFTOFF_THRESHOLD) || (bypass))
    {
      Serial.println(F("LIFT-OFF!"));
      
      float Temp = bmp.readTemperature();
      // Write header in EEPROM
      WriteS16((short)(gApex * 10.0f));
      WriteS16((short)(Temp * 10.0f));

      // Go to record phase
      gPhase = PHASE_RECORDING;
      UpdateDisplay("LIFT-OFF", false);
    }     
}

// Continous recording after lift-off
void PhaseRecording()
{   
    static float gLastRecordedAltitude = 0.0f;
    static unsigned long gLastRecordTime = millis();

    // TODO     : Temp
    //DisplayStatus();
    //    
    
    // Check for MemFull
    if(gEEPROMIndex >= gEEPROMSize)
      gPhase = PHASE_MEMFULL;
      
    // Read Altitude!   
    float Altitude = ReadRelativeAltitude();

    /// TODO DEBUG ONLY
    /*
    static float gFakeAltitude = 0.0f;    
    if(Serial.available() > 0)
    {
      char InputChar = Serial.read();
      if(InputChar == '+')
       gFakeAltitude += 10.0f;
      else if(InputChar == '-')
       gFakeAltitude -= 10.0f;       
    }
    Altitude += gFakeAltitude;    
    */
    ////
       

    // Keep track of highest point
    if(Altitude > gApex)
      gApex = Altitude;

    // Write if necessary
    float DeltaAltitude = Altitude - gLastRecordedAltitude;
    unsigned long DeltaT = millis() - gLastRecordTime;
    if(DeltaAltitude >= ALTITUDE_STEPS_FLOAT)
    {
        float Value = ((float)DeltaT * ALTITUDE_STEPS_FLOAT) / DeltaAltitude;
        unsigned short ValueU16 = (unsigned short) Value;
        if(Value < 0.0f)
            ValueU16 = 0x8000 | (unsigned short) -Value;
            
        // Write value in eeprom
        WriteU16(ValueU16);        

        gLastRecordTime += ValueU16;
        gLastRecordedAltitude += ALTITUDE_STEPS_FLOAT;
    }
    else if(DeltaAltitude <= -ALTITUDE_STEPS_FLOAT)
    {
        float Value = ((float)DeltaT * ALTITUDE_STEPS_FLOAT) / -DeltaAltitude;
        unsigned short ValueU16 = 0x8000 | (unsigned short) Value;
            
        // Write value in eeprom
        WriteU16(ValueU16);

        gLastRecordTime += (unsigned long) Value;
        gLastRecordedAltitude -= ALTITUDE_STEPS_FLOAT;
    }    
    else if(DeltaT >= MAX_DELAY)
    {        
        // Just write current altitude as a U16      
        WriteU16(MAX_DELAY_MARKER);
        WriteS16((short)(Altitude * 10.0f));

        gLastRecordTime += MAX_DELAY;
        gLastRecordedAltitude = Altitude;        
    }
    else
    {
        // Update Display only when we have nothing to record, in order to not slow down quick recoding!
        UpdateDisplay("GO!!!!", false);
    }
}

// Memory full!
void PhaseMemFull()
{
    UpdateDisplay("MEM-FULL", false);
    while(1);
}

void loop() 
{
    if(gPhase == PHASE_DUMP)
      PhaseDump();                    // Print out previous EEPROM history
    else if(gPhase == PHASE_STANDBY)
      PhaseStandBy();                 // Wait for lift-off    
    else if(gPhase == PHASE_RECORDING)
      PhaseRecording();               // Lift-off! Record in EEPROM
    else 
      PhaseMemFull();                 // Memory filled - just wait
}
