#include "driver/twai.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define TX_PIN 2
#define RX_PIN 15
#define ENCODER_PIN 13
#define LM75_ADDR 0x48 
#define I2C_SDA 14
#define I2C_SCL 27

LiquidCrystal_I2C lcd(0x27, 16, 2);

volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseCount = 0;
uint8_t txCounter = 0; 

void IRAM_ATTR countPulse() {
  pulseCount++;
  lastPulseTime = millis();   
}

int readTemperatureC() {
  static int lastGoodTemp = 30;
  Wire.beginTransmission(LM75_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return lastGoodTemp; 
  if (Wire.requestFrom(LM75_ADDR, 2) == 2) {
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    int16_t raw = (msb << 8) | lsb;
    float tempC = (raw >> 5) * 0.125;
    lastGoodTemp = (int)(tempC + 0.5); 
    return lastGoodTemp;
  }
  return lastGoodTemp;
}

void installTWAI() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();
  Serial.println("TWAI SENDER INSTALLED");
}

void checkBusStatus() {
  twai_status_info_t status;
  twai_get_status_info(&status);
  if (status.state == TWAI_STATE_BUS_OFF) {
    twai_stop();
    twai_driver_uninstall();
    installTWAI();
  }
}

void printHexFrame(twai_message_t &msg, uint32_t duration) {
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
  uint16_t rpm_raw = (msg.data[0] << 8) | msg.data[1];
  float rpm = rpm_raw / 4.0;
  int tempC = msg.data[2] - 40;
  Serial.print(" -> RPM: "); Serial.print(rpm, 2);
  Serial.print(" Temp: "); Serial.print(tempC);
  Serial.print(" | TX(us): "); Serial.println(duration);
}

void lcdPrintHex(twai_message_t &msg) {
  lcd.setCursor(0, 1);
  for (int i = 0; i < 8; i++) {
    if (msg.data[i] < 0x10) lcd.print("0");
    lcd.print(msg.data[i], HEX);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init(); lcd.backlight();
  lcd.clear(); lcd.print("DATA ENGINE:");
  pinMode(ENCODER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN), countPulse, RISING);
  installTWAI();
}

void loop() {
  checkBusStatus(); 

  static unsigned long lastTime = 0;
  static unsigned long lastCount = 0;

  if (millis() - lastTime >= 300) {
    unsigned long now = millis();
    unsigned long currentCount = pulseCount;
    unsigned long pulsesInInterval = currentCount - lastCount;
    lastCount = currentCount;
    lastTime = now;

    float rpm = (now - lastPulseTime > 300) ? 0 : (float)(pulsesInInterval * 10);
    uint16_t rpm_raw = (uint16_t)(rpm * 4);
    int tempC = readTemperatureC();
    uint8_t temp_raw = (uint8_t)(tempC + 40);

    twai_message_t msg = {0};
    msg.identifier = 0x120;
    msg.data_length_code = 8;
    msg.data[0] = rpm_raw >> 8;
    msg.data[1] = rpm_raw & 0xFF;
    msg.data[2] = temp_raw;
    msg.data[3] = 0x01;
    msg.data[4] = txCounter; 

    uint32_t t_start = esp_timer_get_time();
    esp_err_t res = twai_transmit(&msg, pdMS_TO_TICKS(10)); 
    uint32_t t_end = esp_timer_get_time();

    if (res == ESP_OK) {
      printHexFrame(msg, t_end - t_start);
      lcdPrintHex(msg);
      txCounter++; 
    }
  }
}
