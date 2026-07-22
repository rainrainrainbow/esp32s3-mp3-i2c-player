#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <AudioOutputI2S.h>
#include <AudioFileSourceFS.h>
#include <AudioGeneratorMP3.h>
#include <FFat.h>
#include <esp_partition.h>
#include "USB.h"
#include "USBMSC.h"
#include "board_config.h"

// ============================================================
// ILI9341 Minimal Driver - No external TFT library!
// ============================================================
#define TFT_CS   DISPLAY_CS_GPIO
#define TFT_DC   DISPLAY_DC_GPIO
#define TFT_CLK  DISPLAY_CLK_GPIO
#define TFT_MOSI DISPLAY_MOSI_GPIO
#define TFT_BL   DISPLAY_BACKLIGHT_PIN

static SPIClass *tft_spi = NULL;

void tft_cmd(uint8_t c) {
  digitalWrite(TFT_DC, LOW);
  digitalWrite(TFT_CS, LOW);
  tft_spi->transfer(c);
  digitalWrite(TFT_CS, HIGH);
}

void tft_data(uint8_t d) {
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_CS, LOW);
  tft_spi->transfer(d);
  digitalWrite(TFT_CS, HIGH);
}

void tft_init_custom() {
  tft_spi = new SPIClass(FSPI);
  tft_spi->begin(TFT_CLK, -1, TFT_MOSI, TFT_CS);
  tft_spi->setFrequency(40000000);
  tft_spi->setBitOrder(MSBFIRST);
  tft_spi->setDataMode(SPI_MODE0);

  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_BL, HIGH);

  tft_cmd(0x01); // soft reset
  delay(200);
  tft_cmd(0x11); // sleep out
  delay(150);
  tft_cmd(0x3A); // pixel format
  tft_data(0x55); // 16-bit
  tft_cmd(0x36); // MADCTL
  tft_data(0xE8); // BGR+MV+MX+MY
  tft_cmd(0x29); // display on
  delay(50);
}

void tft_fill(uint16_t color) {
  tft_cmd(0x2A);
  tft_data(0); tft_data(0); tft_data(0); tft_data(0xEF);
  tft_cmd(0x2B);
  tft_data(0); tft_data(0); tft_data(0x01); tft_data(0x3F);
  tft_cmd(0x2C);
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_CS, LOW);
  for (int i = 0; i < 320*240; i++) {
    tft_spi->transfer16(color);
  }
  digitalWrite(TFT_CS, HIGH);
}

// ============================================================
// I2C Slave
// ============================================================
#define I2C_SLAVE_ADDR 0x52
#define REG_TRACK_NUM  0x01
#define REG_PLAY_STATUS 0x02

#define STATUS_STOPPED  0
#define STATUS_PLAYING  1
#define STATUS_PAUSED   2
#define STATUS_CHANGING 3
#define STATUS_ERROR    4

static uint8_t current_track = 0;
static uint8_t target_track = 0;
static uint8_t play_status = STATUS_STOPPED;

AudioGeneratorMP3 *mp3 = NULL;
AudioFileSourceFS *audio_file = NULL;
AudioOutputI2S *audio_out = NULL;

USBMSC MSC;
static const esp_partition_t *storage_partition = NULL;
#define STORAGE_PARTITION_LABEL "storage"

void onRequest() { Wire.write(play_status); }

void onReceive(int len) {
  while (Wire.available() >= 2) {
    uint8_t reg = Wire.read();
    uint8_t val = Wire.read();
    if (reg == REG_TRACK_NUM && val >= 1 && val <= 255) {
      target_track = val;
    } else if (reg == REG_PLAY_STATUS && val == STATUS_STOPPED) {
      target_track = 0;
    }
  }
}

static int32_t msc_onRead(uint32_t lba, uint32_t off, void *buf, uint32_t sz) {
  if (!storage_partition) return 0;
  if (esp_partition_read(storage_partition, lba*512+off, buf, sz) == ESP_OK) return sz;
  return 0;
}
static int32_t msc_onWrite(uint32_t lba, uint32_t off, uint8_t *buf, uint32_t sz) {
  if (!storage_partition) return 0;
  if (esp_partition_write(storage_partition, lba*512+off, buf, sz) == ESP_OK) return sz;
  return 0;
}
static bool msc_onStartStop(uint8_t pc, bool start, bool eject) { return true; }

void stopPlayback() {
  if (mp3 && mp3->isRunning()) mp3->stop();
  delete mp3; mp3 = NULL;
  delete audio_file; audio_file = NULL;
  play_status = STATUS_STOPPED;
  digitalWrite(AUDIO_CODEC_PA_PIN, LOW);
  Serial.println("Stopped");
  tft_fill(0x0000);
}

bool startPlayback(uint8_t track) {
  if (track < 1 || track > 255) return false;
  stopPlayback();
  char path[32];
  snprintf(path, sizeof(path), "/music/%03d.mp3", track);
  if (!FFat.exists(path)) {
    Serial.printf("Not found: %s\n", path);
    play_status = STATUS_ERROR;
    return false;
  }
  audio_file = new AudioFileSourceFS(FFat, path);
  mp3 = new AudioGeneratorMP3();
  audio_out = new AudioOutputI2S();
  audio_out->SetPinout(AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT);
  audio_out->SetOutputModeMono(true);
  audio_out->SetGain(0.8);
  mp3->begin(audio_file, audio_out);
  current_track = track;
  play_status = STATUS_PLAYING;
  digitalWrite(AUDIO_CODEC_PA_PIN, HIGH);
  Serial.printf("Playing: Track %d\n", track);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S3 MP3 Player v5 ===");

  tft_init_custom();
  tft_fill(0x0000);
  Serial.println("TFT OK");

  storage_partition = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
    STORAGE_PARTITION_LABEL);
  if (storage_partition) {
    Serial.printf("Storage: %u MB\n", storage_partition->size/(1024*1024));
  }

  if (FFat.begin(false, STORAGE_PARTITION_LABEL, 5)) {
    Serial.println("FFat OK");
  } else {
    Serial.println("FFat FAIL");
  }

  Wire.onRequest(onRequest);
  Wire.onReceive(onReceive);
  Wire.begin((uint8_t)I2C_SLAVE_ADDR, I2C_SLAVE_SDA, I2C_SLAVE_SCL, 100000);
  Serial.println("I2C Slave 0x52");

  pinMode(AUDIO_CODEC_PA_PIN, OUTPUT);
  digitalWrite(AUDIO_CODEC_PA_PIN, LOW);

  if (storage_partition) {
    MSC.vendorID("ESP32");
    MSC.productID("S3-MP3");
    MSC.productRevision("1.0");
    MSC.onStartStop(msc_onStartStop);
    MSC.onRead(msc_onRead);
    MSC.onWrite(msc_onWrite);
    MSC.begin(storage_partition->size/512, 512);
    USB.begin();
    Serial.println("USB MSC OK");
  }

  Serial.println("Ready");
}

void loop() {
  if (target_track >= 1 && target_track <= 255) {
    uint8_t t = target_track;
    target_track = 0;
    if (t != current_track || play_status == STATUS_STOPPED) {
      play_status = STATUS_CHANGING;
      startPlayback(t);
    }
  }

  if (mp3 && mp3->isRunning()) {
    if (!mp3->loop()) {
      Serial.printf("Track %d done\n", current_track);
      stopPlayback();
    }
  }

  delay(1);
}
