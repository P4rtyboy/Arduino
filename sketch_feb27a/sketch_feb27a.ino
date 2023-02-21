#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <EEPROM.h>
 
Adafruit_BMP280 bmp; // I2C

/////////////////////////////
// Header
#define RECORD_DELAY            200         // 200 ms inbetween each recording
#define RECORD_HISTORY_SIZE     16          // 10 x 200 ms = 2 seconds
#define RECORD_HISTORY_MASK     0x0000000F  // 0-15 index
#define LIFTOFF_THRESHOLD       2.0f        // Consider liftoff when you reach 2m

static int        Phase                             = 0;
static float      StartingPressure                  = 0.0f;
static unsigned   HistoryIndex                      = 0;
static short      AltitudeHistory[RECORD_HISTORY_SIZE];    
static unsigned   EEPROMIndex                       = 0;
static short      RecordNo                          = 0;

void setup() 
{
  Serial.begin(9600);
  Serial.println(F("BMP280 test"));
  
  if (!bmp.begin()) {  
    Serial.println(F("Could not find a valid BMP280 sensor, check wiring!"));
    while (1);
  }

  // Init for recording
  StartingPressure = bmp.readPressure();
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



// Display older log
void Phase0()
{  
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
  }
  
  Serial.println(F(">>> END OF RECORDING"));
  Serial.println();    
  Phase = 1;
}

// Wait for liftoff
void Phase1()
{    
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
  
    float CurrentAltitude = ReadAltitude();
    WriteAltitude(CurrentAltitude);
  
    delay(RECORD_DELAY);
}

void loop() 
{
    if(Phase == 0)
      Phase0();           // Print out previous EEPROM history
    else if(Phase == 1)
      Phase1();           // Wait for lift-off    
    else
      Phase2();           // Lift-off! Record in EEPROM
}
