idf_component_register(SRCS "petti_sensors.c" "petti_audio.c" "petti_battery.c" "petti_nand.c"
    INCLUDE_DIRS "include" REQUIRES esp_driver_i2c esp_driver_spi esp_driver_i2s esp_timer
    esp_codec_dev espressif2022__bmi270 tangyumei3535__bmm150_sensorapi bq27220 spi_nand_flash)
