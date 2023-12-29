/*s
  Teensy 4.1 data logger
  Rev 0
  6-JUN-2022


  Description:  This Teensy 4.1 based data logger collects input from the following pins:

  Name             Type      Size      Pin #  Pin Name   Direction     Description
  -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  COUNT_SDA        I2C       NA        18     SDA        BI            Exteranl 24-bit I2C counter I2C Data  (Part# S-35770)
  COUNT_SCL        I2C       NA        19     SCL        OUT           External 24-bit I2C counter I2C clock (Part# S-35770)
  flywheel_count   I2C       3-Bytes   NA     READ       NA            Count value from the S-35770 Counter read via I2C 
 	L_COUNT_REST     DIGITAL   NA        2      DOUT2      OUT           External 24-Bit I2C counter /RST (Part# S-35770)
  START_SIG        DIGITAL   1-Byte    0      DIN0       IN            Starter +Coil voltage signal (High = Starter Cranking)
  FLYWHEEL_DIR     DIGITAL   1-Byte    1      DIN1       IN            Flyshweel direction of spin indicator (High = CCW, Low = CW)
  START_POWER_K15  DIGITAL   1-Byte    3      DIN3       IN            Starter Main High Power input positive voltage (High = Start powered, Low = Starter not powered)  same as K15
  CALIBRATE        DIGITAL   NA        4      DOUT4      OUT           Calibrate analog input signals (High = calibrate, Low = normal signal inputs)
  START_CURRENT    ANALOG    10-Bit    14     AIN0       IN            Starter Main current sense (+/- 2000 Amps)
  FUEL_HTR_CURRENT ANALOG    10-Bit    15     AIN1       IN            Fuel Heater power source current (+/- 250 Amps)
  STATER_STRAIN    ANALOG    10-Bit    16     AIN2       IN            Starter Strain Sensor

  Algorithm (loop):
  1. After power on initialize the SD Card to check if it's there.
  2. Start a new file with incrementing number 00000001.txt (read what is the next value available)
  3. Wait until the START_SIG == High
  4. Record signal at a sample Rate of 500Hz (Sample time = 2ms)
     While the START_SIG is active High record directly to the SD CARD the following signals ever 2ms:
     {
       TIME_STAMP_MICROS,
       FLYWHEEL_COUNT,
       FLYWHEEL_DIR,
       START_PWR_K15,
       START_CURRENT,
       FUEL_HTR_CURRENT,
       STARTER_STRAIN,
      }
 
 The circuit:
 * SD card attached to SPI bus as follows:
 *  MOSI - pin 11, pin 7 on Teensy with audio board
 *  MISO - pin 12
 *  CLK - pin 13, pin 14 on Teensy with audio board
 *  CS - pin 4,  pin 10 on Teensy with audio board
 
 */

#define SAMPLE_RATE (2500)
#define SAMPLE_PERIOD_USEC (400UL)

#define START_SIG                           (0)
#define FLYWHEEL_DIR_CHANNEL                (1)
#define STARTER_PWR_K15_CHANNEL             (3) 
#define STARTER_CURRENT_CHANNEL             (0)
#define FUEL_HTR_CURRENT_CHANNEL            (1)
#define STARTER_STRAIN_CURRENT_CHANNEL      (2)
#define L_COUNTER_RESET_CHANNEL             (2)

#define EXTERNAL_MEM_SIZE                   (1024*1024*8)

#include <SD.h>
#include <SPI.h>
#include <Wire.h>

// On the Ethernet Shield, CS is pin 4. Note that even if it's not
// used as the CS pin, the hardware CS pin (10 on most Arduino boards,
// 53 on the Mega) must be left as an output or the SD library
// functions will not work.
// Teensy 3.5 & 3.6 & 4.1 on-board: BUILTIN_SDCARD

const int chipSelect = BUILTIN_SDCARD;
unsigned long last_time_usec = 0;

int char_count = 0;

EXTMEM char psram[EXTERNAL_MEM_SIZE];
int memptr = 0;

bool record_daq_data = false;

// Prototypes
unsigned long read_S35770_Counter (void);
void dump_text (void);
int StoreDataSDCard (void);

void setup()
{
  // Setup Pin's
  pinMode (START_SIG, INPUT);
  pinMode (FLYWHEEL_DIR_CHANNEL, INPUT);
  pinMode (STARTER_PWR_K15_CHANNEL, INPUT);
  pinMode (L_COUNTER_RESET_CHANNEL, OUTPUT);

  // Start the counter
  digitalWrite (L_COUNTER_RESET_CHANNEL, HIGH);
  
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect.
  }

  // Start Wire for reading counter chip
  Wire.begin();
  
  // Setup the first sample window time quanta
  last_time_usec = micros() + SAMPLE_PERIOD_USEC; // reset timer
}

// ************************************
// Use I2C to read the S-35770 chip
// Ref: https://www.pjrc.com/teensy/td_libs_Wire.html
// Ref: https://www.mouser.com/datasheet/2/360/S35770_I_E-1628617.pdf
// I2C address: 011 0010 = 0x32
// ************************************

unsigned long read_S35770_Counter (void) {
  Wire.requestFrom(0x32, 3); // Read 24-bit S-33770
  
  unsigned long ext_counter  = Wire.read() << 16;
                ext_counter |= Wire.read() << 8;
                ext_counter |= Wire.read();
  
  return (ext_counter);
}

// ************************************
// Dump Text from psram to serial port
// ************************************
void dump_text (void) {
  for (int i=0;i<(char_count * 100);i++) {
    Serial.print(psram[i]);
  }
}

// ************************************
// StoreDataSDCard from psram
// Find the next available incremental number file and transfer the psram data from 0 to memptr to the file
// ************************************
int StoreDataSDCard (void) {
  int n = 0;  
  
  // make it long enough to hold your longest file name, plus a null terminator
  char filename[16];

  Serial.print("Initializing SD card...");
  Serial.print("Logging to SD Card Sample period[uS]: ");
  Serial.print(String (SAMPLE_PERIOD_USEC));
  Serial.print("\n");

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    while (1) {
      Serial.println("no-card detected"); // No SD card, so don't do anything more - stay stuck here
    }
  }
  Serial.println("card initialized.");

  // Create the file name
  snprintf(filename, sizeof(filename), "LOG_%04d.txt", n); // includes a four-digit sequence number in the file name
  while(SD.exists(filename)) {
    n++;
    snprintf(filename, sizeof(filename), "LOG_%04d.txt", n);
  }
  File dataFile = SD.open(filename,FILE_READ);
  //Serial.println(n);
  Serial.println(filename);
  dataFile.close();

  //Now write the data out to the SD Card stored in the psram
  dataFile = SD.open(filename, FILE_WRITE);
  dataFile.println("time_usec,flywheel_count,flywheel_direction,starter_pwr_k15,starter_current,fuel_htr_current,starter_housing_strain");
  //Dump started data
  for (int i=0;i<=memptr;i++) {
    dataFile.print(psram[i]);
  }
  dataFile.close();
  return (0);
}


// ***************************************************************
// Log the data to SD card in a loop at the requested sample rate
// ****************************************************************
void loop()
{
  unsigned long time_usec = micros();

  // Detect START signal to record
  if ((record_daq_data == false) && (digitalRead(START_SIG) == HIGH)) {
    record_daq_data = true;
    memptr = 0; // Reset the psram memory pointer
  }
  else {
    if (digitalRead(START_SIG) == LOW) {
      record_daq_data = false;
      StoreDataSDCard();
      Serial.print("Data Aquired!\n");
    }
  }
  
  if (record_daq_data == true && (time_usec >= (last_time_usec + SAMPLE_PERIOD_USEC))){
     last_time_usec = time_usec;
     
     // Read the data
     unsigned long  fly_wheel_count   = read_S35770_Counter();  
     unsigned int   fly_wheel_dir     = digitalRead(FLYWHEEL_DIR_CHANNEL);
     unsigned int   starter_pwr_k15   = digitalRead(STARTER_PWR_K15_CHANNEL);
     unsigned int   starter_current   = analogRead(STARTER_CURRENT_CHANNEL);
     unsigned int   fuel_htr_current  = analogRead(FUEL_HTR_CURRENT_CHANNEL);
     unsigned int   starter_strain    = analogRead(STARTER_STRAIN_CURRENT_CHANNEL);

     // micros() is 32bit with max value: 4,294,967,295 (2^32-1) or 10-digits
     // S-35770 is 24bit with max value: 16,777,215 (2^24-1) or 8-digits
     // fly_wheel direction is 1-digit
     // starter_pwr_k15 is 1-digit
     // starter_curret is 4-digits
     // fuel_htr_curretn is 4-digits
     // strain is 4-digits
     // Total is: 1234567890,12345678,1,1,1234,1234,1234<cr><\0> give 39-digits
     // At a rate of 39-digits*2500 samples/sec = 97,500 bytes/sec
     // 8MByte (1024*1024*8)/97,500 =~ 86s of psram record time
     
     // Assemble the output into comma seperated string
     char_count = sprintf(&psram[memptr], "%.10lu,%.8lu,%u,%u,%.4u,%.4u,%.4u\n", time_usec, fly_wheel_count, fly_wheel_dir, starter_pwr_k15, starter_current, fuel_htr_current, starter_strain);
     memptr = memptr + char_count;
     if ((memptr-1) > EXTERNAL_MEM_SIZE) {
       memptr = 0; // wrap around an keep recording to psram.
     }
     
  } // end if
}
