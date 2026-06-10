#include "driver/twai.h"

#define TX_PIN 2
#define RX_PIN 15
#define POT_PIN 4

uint8_t txCounter = 0;

void installTWAI() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();
  Serial.println("TWAI SENDER INSTALLED");
}

// Hàm kiểm tra trạng thái Bus và Reset nếu cần
void checkBusStatus() {
  twai_status_info_t status;
  twai_get_status_info(&status);
  if (status.state == TWAI_STATE_BUS_OFF) {
    twai_stop();
    twai_driver_uninstall();
    installTWAI();
  }
}

void printHexFrame(twai_message_t &msg, uint32_t duration, float fuelPercent) {
  Serial.print("ID: ");
  Serial.print(msg.identifier, HEX);
  Serial.print("  DLC: ");
  Serial.print(msg.data_length_code);
  Serial.print(" DATA: ");
  for (int i = 0; i < msg.data_length_code; i++) {
    if (msg.data[i] < 0x10) Serial.print("0");
    Serial.print(msg.data[i], HEX);
    Serial.print(" ");
  }
  Serial.print(" -> Fuel: "); Serial.print(fuelPercent, 1);
  Serial.print("% | TX(us): "); Serial.println(duration);
}

float readFuelPercent() {
  int adc = analogRead(POT_PIN);
  return (float)constrain(adc, 0, 4095) * 100.0 / 4095.0;
}

void setup() {
  Serial.begin(115200);
  pinMode(POT_PIN, INPUT);
  installTWAI();
}

void loop() {
  checkBusStatus(); // KIỂM TRA LỖI TRƯỚC MỖI VÒNG LẶP

  static unsigned long lastTime = 0;

  if (millis() - lastTime >= 300) {
    lastTime = millis();

    float fuelP = readFuelPercent();
    uint8_t fuel_raw = (uint8_t)((fuelP * 255.0) / 100.0);

    twai_message_t msg = {};
    msg.identifier = 0x520;
    msg.data_length_code = 8;
    msg.data[2] = fuel_raw;
    msg.data[3] = 0x01;
    msg.data[4] = txCounter; 

    uint32_t t_start = esp_timer_get_time();
    esp_err_t res = twai_transmit(&msg, pdMS_TO_TICKS(10));
    uint32_t t_end = esp_timer_get_time();

    if (res == ESP_OK) {
      printHexFrame(msg, (uint32_t)(t_end - t_start), fuelP);
      txCounter++; 
    }
  }
}