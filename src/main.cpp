#include <Arduino.h>
#include <Wire.h>
#include <AudioOutputI2S.h>
#include <AudioFileSourceFS.h>
#include <AudioGeneratorMP3.h>
#include <FFat.h>
#include <esp_partition.h>
#include "USB.h"
#include "USBMSC.h"
#include "board_config.h"

#define I2C_SLAVE_ADDR    0x52
#define REG_TRACK_NUM     0x01
#define REG_PLAY_STATUS   0x02

#define STATUS_STOPPED     0
#define STATUS_PLAYING     1
#define STATUS_PAUSED      2
#define STATUS_CHANGING    3
#define STATUS_ERROR       4

static uint8_t current_track = 0;
static uint8_t target_track = 0;
static uint8_t play_status = STATUS_STOPPED;

AudioGeneratorMP3 *mp3 = NULL;
AudioFileSourceFS *audio_file = NULL;
AudioOutputI2S *audio_out = NULL;

USBMSC MSC;
static const esp_partition_t *storage_partition = NULL;
#define STORAGE_PARTITION_LABEL "storage"

void onRequest() {
  Wire.write(play_status);
}

void onReceive(int len) {
  while (Wire.available() >= 2) {
    uint8_t reg = Wire.read();
    uint8_t value = Wire.read();
    switch (reg) {
      case REG_TRACK_NUM:
        if (value >= 1 && value <= 255) { target_track = value; }
        break;
      case REG_PLAY_STATUS:
        if (value == STATUS_STOPPED) target_track = 0;
        break;
    }
  }
}

static int32_t msc_onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (!storage_partition) return 0;
  esp_err_t err = esp_partition_read(storage_partition, lba * 512 + offset, buffer, bufsize);
  return (err == ESP_OK) ? bufsize : 0;
}

static int32_t msc_onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  if (!storage_partition) return 0;
  esp_err_t err = esp_partition_write(storage_partition, lba * 512 + offset, buffer, bufsize);
  return (err == ESP_OK) ? bufsize : 0;
}

static bool msc_onStartStop(uint8_t pc, bool start, bool eject) {
  return true;
}

void stopPlayback() {
  if (mp3 && mp3->isRunning()) mp3->stop();
  delete mp3; mp3 = NULL;
  delete audio_file; audio_file = NULL;
  play_status = STATUS_STOPPED;
  digitalWrite(AUDIO_CODEC_PA_PIN, LOW);
}

bool startPlayback(uint8_t track) {
  if (track < 1 || track > 255) return false;
  stopPlayback();
  char path[32];
  snprintf(path, sizeof(path), "/music/%03d.mp3", track);
  if (!FFat.exists(path)) { play_status = STATUS_ERROR; return false; }
  audio_file = new AudioFileSourceFS(FFat, path);
  mp3 = new AudioGeneratorMP3();
  audio_out = new AudioOutputI2S();
  if (!audio_file || !mp3 || !audio_out) { play_status = STATUS_ERROR; return false; }
  audio_out->SetPinout(AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT);
  audio_out->SetOutputModeMono(true);
  audio_out->SetGain(0.8);
  mp3->begin(audio_file, audio_out);
  current_track = track;
  play_status = STATUS_PLAYING;
  digitalWrite(AUDIO_CODEC_PA_PIN, HIGH);
  return true;
}

void setup() {
  disableCore0WDT();
  disableCore1WDT();
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S3 MP3 Player (NO TFT) ===");

  if (psramFound()) {
    Serial.printf("PSRAM: %u MB\n", ESP.getPsramSize() / (1024 * 1024));
  } else {
    Serial.println("No PSRAM");
  }

  storage_partition = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
    STORAGE_PARTITION_LABEL);
  if (storage_partition) {
    Serial.printf("Storage: %u MB\n", storage_partition->size / (1024 * 1024));
  } else {
    Serial.println("ERROR: No storage partition!");
  }

  if (FFat.begin(false, STORAGE_PARTITION_LABEL, 5)) {
    Serial.println("FFat mounted");
    fs::File root = FFat.open("/");
    if (root) {
      fs::File f = root.openNextFile();
      while (f) {
        Serial.printf("  %s %s\n", f.isDirectory() ? "DIR" : "FILE", f.name());
        f = root.openNextFile();
      }
      root.close();
    }
  } else {
    Serial.println("FFat mount failed");
  }

  Wire.onRequest(onRequest);
  Wire.onReceive(onReceive);
  Wire.begin((uint8_t)I2C_SLAVE_ADDR, I2C_SLAVE_SDA, I2C_SLAVE_SCL, 100000);
  Serial.printf("I2C Slave: 0x%02X\n", I2C_SLAVE_ADDR);

  pinMode(AUDIO_CODEC_PA_PIN, OUTPUT);
  digitalWrite(AUDIO_CODEC_PA_PIN, LOW);

  if (storage_partition) {
    uint32_t block_count = storage_partition->size / 512;
    MSC.vendorID("ESP32");
    MSC.productID("S3-MP3-Player");
    MSC.productRevision("1.0");
    MSC.onStartStop(msc_onStartStop);
    MSC.onRead(msc_onRead);
    MSC.onWrite(msc_onWrite);
    MSC.begin(block_count, 512);
    USB.begin();
    Serial.println("USB MSC started");
  }

  Serial.println("Setup complete, waiting for I2C commands...");
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
      stopPlayback();
    }
  }

  delay(1);
}
