//#define USE_EXTERNAL_MIC A8  // D2 on pygbadge
#define USE_EDGEBADGE_PDMMIC

#include <SPI.h>
#include "Adafruit_NeoPixel.h"

#define LED_OUT       LED_BUILTIN
#define BUTTON        12
#define NEOPIXEL_PIN  8
#define AUDIO_OUT     A0

#if defined(USE_EXTERNAL_MIC)
  #include "Adafruit_ZeroTimer.h"
  #define AUDIO_IN         USE_EXTERNAL_MIC
  #define DAC_TIMER        5
  void TC5_Handler(){
    Adafruit_ZeroTimer::timerHandler(DAC_TIMER);
  }
  Adafruit_ZeroTimer zt = Adafruit_ZeroTimer(DAC_TIMER);
#endif
#if defined(USE_EDGEBADGE_PDMMIC)
  #include <Adafruit_ZeroPDMSPI.h>
  #define PDM_SPI            SPI2    // PDM mic SPI peripheral
  #define TIMER_CALLBACK     SERCOM3_0_Handler
  Adafruit_ZeroPDMSPI pdmspi(&PDM_SPI);
#endif

#define SAMPLERATE_HZ 16000
#define BUFFER_SIZE   SAMPLERATE_HZ
volatile uint16_t audio_idx = 0;
uint16_t audio_max = 0;
int16_t audio_buffer[BUFFER_SIZE];

Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

volatile bool val;
volatile bool isPlaying, isRecording;
bool button_state;

void TIMER_CALLBACK()
{
  int32_t sample;

#if defined(USE_EDGEBADGE_PDMMIC)
  uint16_t read_pdm;
  if (!pdmspi.decimateFilterWord(&read_pdm)) {
    return; // not ready for data yet!
  }
  sample = read_pdm;
#endif

  if (isRecording) {
    digitalWrite(LED_OUT, val);
    val = !val;
#if defined(USE_EXTERNAL_MIC)
    sample = analogRead(AUDIO_IN);
    sample -= 2047; // 12 bit audio unsigned  0-4095 to signed -2048-~2047
    sample *= 16;   // convert 12 bit to 16 bit
#endif
#if defined(USE_EDGEBADGE_PDMMIC)
    sample -= 32676;
    sample *= 2;
#endif
    audio_buffer[audio_idx] = sample;
    audio_idx++;
    if (audio_idx == BUFFER_SIZE) {
      isRecording = false;
    }
  }
  if (isPlaying) {
    digitalWrite(LED_OUT, val);
    val = !val;
    int16_t sample = audio_buffer[audio_idx];
    sample /= 16;    // convert 16 bit to 12 bit
    sample += 2047;  // turn into signed 0-4095
    analogWrite(AUDIO_OUT, sample);
    audio_idx++;
    if (audio_idx == audio_max) {
      audio_idx = 0;
    }
  }
}

void setup() {
  pinMode(LED_OUT, OUTPUT);   // Onboard LED can be used for precise
  digitalWrite(LED_OUT, LOW); // benchmarking with an oscilloscope
  pixel.begin();
  pixel.clear();
  pixel.show();
  delay(10);
  pixel.show();

#if defined(ADAFRUIT_PYBADGE_M4_EXPRESS)
  // enable speaker
  pinMode(51, OUTPUT);
  digitalWrite(51, HIGH);
#endif
  isPlaying = isRecording = false;
  
  Serial.begin(115200);
  while(!Serial) delay(10);       // Wait for Serial monitor before continuing

  initTimer();
  analogWriteResolution(12);
  analogReadResolution(12);
  analogWrite(AUDIO_OUT, 512);
  
  Serial.println("Record & Play audio test");
  pinMode(BUTTON, INPUT_PULLUP);
  Serial.println("Waiting for button press to record...");
  // wait till its pressed
  while (digitalRead(BUTTON)) { delay(10); }

  // make pixel red
  pixel.setPixelColor(0, pixel.Color(20, 0, 0));
  pixel.show();
  // reset audio to beginning of buffer
  audio_idx = 0;
  
  // and begin!
  isRecording = true;
  Serial.print("Recording...");
  
  // while its pressed...
  while (!digitalRead(BUTTON) && isRecording) {
    delay(10);
  }
  isRecording = false;  // definitely done!
  pixel.setPixelColor(0, pixel.Color(0, 0, 0));
  pixel.show();

  audio_max = audio_idx;
  Serial.printf("Done! Recorded %d samples\n", audio_max);

  Serial.println("/**********************************************/");
  Serial.printf("const int16_t g_recaudio_sample_data[%d] = {\n", audio_max);
  for (int i=0; i<audio_max; i+=16) {
    for (int x=0; x<16; x++) {
      Serial.printf("%d,\t", audio_buffer[i+x]);
    }
    Serial.println();
  }
  Serial.println("};");

  Serial.println("/**********************************************/");

  Serial.println("Waiting for button press to play...");
  button_state = true;
  audio_idx = 0;
  // make sure we're not pressed now
  while (!digitalRead(BUTTON)) {
    delay(10);
  }
}

void loop() {
  if (button_state == digitalRead(BUTTON)) {
    delay(10);
    return; // no change!
  }

  // something different...
  if (!digitalRead(BUTTON)) {
    // button is pressed
    pixel.setPixelColor(0, pixel.Color(0, 20, 0));
    pixel.show();
    audio_idx = 0;
    isPlaying = true;
    Serial.println("Playing");
  } else {
    // released
    isPlaying = false;
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
    Serial.println("Stopped");
  }
  button_state = digitalRead(BUTTON);
}

void initTimer(void) {
#if defined(USE_EXTERNAL_MIC)
  zt.configure(TC_CLOCK_PRESCALER_DIV1, // prescaler
                TC_COUNTER_SIZE_16BIT,   // bit width of timer/counter
                TC_WAVE_GENERATION_MATCH_PWM // frequency or PWM mode
                );

  zt.setCompare(0, (24000000/SAMPLERATE_HZ)*2);
  zt.setCallback(true, TC_CALLBACK_CC_CHANNEL0, TIMER_CALLBACK);  // this one sets pin low
  zt.enable(true);
#endif
#if defined(USE_EDGEBADGE_PDMMIC)
  pdmspi.begin(SAMPLERATE_HZ);
  Serial.print("Final PDM frequency: "); Serial.println(pdmspi.sampleRate);
#endif
}
