#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <TFT_eSPI.h>
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

TFT_eSPI tft = TFT_eSPI();

static uint8_t current_image = 0;
static unsigned long last_image_change = 0;
static const int IMAGE_INTERVAL_MS = 5000;

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
        if (value >= 1 && value <= 255) {
          target_track = value;
          Serial.printf("I2C: Set track to %d\n", value);
        }
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

void showTrackInfo(uint8_t track, uint8_t img_num) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Now Playing", 60, 30);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  char buf[32];
  snprintf(buf, sizeof(buf), "Track %03d", track);
  tft.drawString(buf, 40, 70);
  tft.drawRect(20, 130, 280, 20, TFT_BLUE);
  tft.fillRect(22, 132, 276, 16, TFT_NAVY);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(1);
  snprintf(buf, sizeof(buf), "Image: %03d", img_num);
  tft.drawString(buf, 10, 180);
}

void stopPlayback() {
  if (mp3 && mp3->isRunning()) mp3->stop();
  delete mp3; mp3 = NULL;
  delete audio_file; audio_file = NULL;
  play_status = STATUS_STOPPED;
  digitalWrite(AUDIO_CODEC_PA_PIN, LOW);
  Serial.println("Playback stopped");
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2);
  tft.drawString("Stopped", 80, 120);
}

bool startPlayback(uint8_t track) {
  if (track < 1 || track > 255) return false;
  stopPlayback();
  char path[32];
  snprintf(path, sizeof(path), "/music/%03d.mp3", track);
  if (!FFat.exists(path)) {
    Serial.printf("MP3 not found: %s\n", path);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED); tft.setTextSize(2);
    tft.drawString("File Not Found", 40, 100);
    tft.setTextSize(1); tft.drawString(path, 40, 140);
    play_status = STATUS_ERROR;
    return false;
  }
  audio_file = new AudioFileSourceFS(FFat, path);
  mp3 = new AudioGeneratorMP3();
  audio_out = new AudioOutputI2S();
  if (!audio_file || !mp3 || !audio_out) {
    Serial.println("ERROR: Failed to allocate audio objects");
    play_status = STATUS_ERROR;
    return false;
  }
  audio_out->SetPinout(AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT);
  audio_out->SetOutputModeMono(true);
  audio_out->SetGain(0.8);
  mp3->begin(audio_file, audio_out);
  current_track = track;
  current_image = 0;
  last_image_change = 0;
  play_status = STATUS_PLAYING;
  digitalWrite(AUDIO_CODEC_PA_PIN, HIGH);
  showTrackInfo(track, 0);
  Serial.printf("Started playback: Track %d\n", track);
  return true;
}

void setup() {
  // Kill all watchdogs immediately
  disableCore0WDT();
  disableCore1WDT();

  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-S3 MP3 Player ===");

  // Check PSRAM status - don't crash if missing
  if (psramFound()) {
    Serial.printf("PSRAM found: %u MB\n", ESP.getPsramSize() / (1024 * 1024));
  } else {
    Serial.println("WARNING: No PSRAM, continuing with internal RAM");
  }

  // Init TFT with safe defaults
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Booting...", 60, 100);
  Serial.println("TFT initialized");

  // Find storage partition
  storage_partition = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
    STORAGE_PARTITION_LABEL);

  if (storage_partition) {
    Serial.printf("Storage partition: 0x%X, %u MB\n",
      storage_partition->address, storage_partition->size / (1024 * 1024));
    tft.drawString("Partition OK", 60, 130);
  } else {
    Serial.println("ERROR: Storage partition not found!");
    tft.setTextColor(TFT_RED);
    tft.drawString("Partition FAIL", 40, 130);
  }

  // Mount FAT - try with auto format on first use
  if (FFat.begin(false, STORAGE_PARTITION_LABEL, 5)) {
    Serial.println("FFat mounted");
    tft.setTextColor(TFT_GREEN);
    tft.drawString("FFat OK", 60, 160);

    // List root
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
    Serial.println("WARNING: FFat mount failed, try formatting...");
    tft.setTextColor(TFT_RED);
    tft.drawString("FFat FAIL", 60, 160);
    // Don't crash - let user format via USB MSC
  }

  // I2C Slave
  Wire.onRequest(onRequest);
  Wire.onReceive(onReceive);
  Wire.begin((uint8_t)I2C_SLAVE_ADDR, I2C_SLAVE_SDA, I2C_SLAVE_SCL, 100000);
  Serial.printf("I2C Slave: 0x%02X\n", I2C_SLAVE_ADDR);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString("I2C 0x52", 60, 190);

  // PA pin
  pinMode(AUDIO_CODEC_PA_PIN, OUTPUT);
  digitalWrite(AUDIO_CODEC_PA_PIN, LOW);

  // USB MSC
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

  delay(1000);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString("Ready", 100, 120);
  tft.drawString("I2C Slave 0x52", 40, 160);
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
      Serial.printf("Track %d finished\n", current_track);
      stopPlayback();
    }
  }

  if (play_status == STATUS_PLAYING && mp3 && mp3->isRunning()) {
    unsigned long now = millis();
    if (now - last_image_change >= IMAGE_INTERVAL_MS) {
      current_image++;
      showTrackInfo(current_track, current_image);
      last_image_change = now;
    }
  }

  delay(1);
}
