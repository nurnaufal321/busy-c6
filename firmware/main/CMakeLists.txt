idf_component_register(
    SRCS "main.c" "lcd.c" "anim.c" "touch.c" "imu.c" "clock.c" "link.c" "font_data.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd hal esp_partition nvs_flash esp_timer
)
# tools/pack_status.py writes this; `idf.py flash` puts it in the status partition.
esptool_py_flash_to_partition(flash "status" "${PROJECT_DIR}/pack/status.pack")
