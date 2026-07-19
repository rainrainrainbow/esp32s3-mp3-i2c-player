# ESP32-S3 MP3 Player with I2C Slave Control

## Hardware
- ESP32-S3-N16R8 (16MB Flash, 8MB OPI PSRAM)
- ES8311 Audio Codec
- 240x320 TFT Display (SPI)
- No SD Card (uses Flash FAT partition)

## Features
- I2C Slave (addr 0x52) receives commands from other MCU
- Register 0x01: Track number (1-255) to play
- Register 0x02: Play status (0=stopped, 1=playing, 2=paused, 4=error)
- MP3 playback via ES8311 from Flash FAT filesystem
- JPEG image slideshow on TFT (images in /img/XXX/ folder)
- USB Mass Storage mode for copying files to Flash FAT partition

## File Structure on FAT Partition
```
/music/001.mp3  (track 1 MP3)
/img/001/001.jpg (track 1 display images - slideshow)
```

## I2C Protocol
- Read from 0x52: returns current play status byte
- Write to 0x52: write register address + value
  - 0x01 + track_num: play track (1-255)
  - 0x02 + 0x00: stop playback

## Flashing
```
esptool.py --chip esp32s3 --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode qio --flash_freq 80m --flash_size 16MB   0x0 bootloader.bin   0x10000 firmware.bin   0x9000 partitions.bin
```

Then connect USB to copy MP3/JPG files to the FAT partition.
