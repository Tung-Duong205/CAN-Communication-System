#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h> 
#include <ArduinoJson.h>      
#include "driver/twai.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const char* ssid = "ESP32_CAN_DATA";
const char* password = "";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define SDA_PIN 13
#define SCL_PIN 14
#define TX_PIN 2
#define RX_PIN 15

float global_rpm = 0.0;
int global_temp = 0;
float global_fuel = 0.0;
uint8_t engine_frame[8] = {0};
uint8_t fuel_frame[8] = {0};

unsigned long lastLcdUpdate = 0;
unsigned long lastDummySend = 0;
unsigned long lastWsUpdate = 0;
bool engineDataValid = false;   
int64_t last_t120 = 0; 
int64_t last_t520 = 0; 

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><title>Modern Dashboard</title>
<style>
body{margin:0;background:radial-gradient(circle,#172033,#03050a);color:white;font-family:Arial;text-align:center;}
h2{margin:18px 0 5px;color:#9ee8ff;letter-spacing:3px;text-shadow:0 0 12px #00aaff;}
.dashboard{width:96vw;max-width:1100px;margin:auto;background:#050811;border-radius:30px;padding:20px;box-shadow:0 0 40px #0077ff88 inset;border:1px solid #1c3d5c;}
.grid{display:grid;grid-template-columns:1fr 1.5fr 1fr;gap:15px;align-items:center;}
.card{background:radial-gradient(circle,#101a2e,#05070c);border-radius:25px;padding:12px;box-shadow:0 0 20px #009dff33 inset;}
canvas{width:100%;height:auto;}
.value{font-size:38px;font-weight:bold;color:#fff;text-shadow:0 0 12px #00b7ff;}
.label{color:#8da1b8;font-size:14px;letter-spacing:2px;}
.warning{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:15px;}
.warn{padding:12px;border-radius:15px;background:#111;color:#555;border:1px solid #333;font-weight:bold;}
.on{color:white;background:#9b0010;box-shadow:0 0 25px red;animation:blink .5s infinite alternate;}
.low{color:white;background:#aa5d00;box-shadow:0 0 25px orange;animation:blink .7s infinite alternate;}
@keyframes blink{from{opacity:.35}to{opacity:1}}
.hex-container{display:grid;grid-template-columns:1fr 1fr;gap:15px;margin-top:20px;}
.hex-card{background:#000;padding:15px;border-radius:15px;border:1px solid #1c3d5c;box-shadow:0 0 15px rgba(0, 255, 204, 0.1) inset;}
.hex-value{font-family:'Courier New',monospace;font-size:1.3rem;color:#00ffcc;letter-spacing:2px;text-shadow:0 0 8px rgba(0, 255, 204, 0.5);}
.hex-label{display:block;font-size:0.7rem;color:#4b6a8a;margin-bottom:5px;text-transform:uppercase;}
@media(max-width:800px){.grid{grid-template-columns:1fr}.hex-container{grid-template-columns:1fr}}
</style></head><body>
<h2>CAN BUS MODERN MONITOR</h2>
<div class="dashboard">
  <div class="grid">
    <div class="card"><canvas id="tempGauge" width="280" height="220"></canvas><div class="value" id="tempText">0°C</div><div class="label">ENGINE TEMP</div></div>
    <div class="card"><canvas id="rpmGauge" width="420" height="320"></canvas><div class="value" id="rpmText">0</div><div class="label">RPM</div></div>
    <div class="card"><canvas id="fuelGauge" width="280" height="220"></canvas><div class="value" id="fuelText">0%</div><div class="label">FUEL</div></div>
  </div>
  <div class="warning"><div id="tempWarn" class="warn">HOT ENGINE</div><div id="fuelWarn" class="warn">LOW FUEL</div></div>
  <div class="hex-container">
    <div class="hex-card"><span class="hex-label">Engine (0x120)</span><div class="hex-value" id="h_eng">-- -- -- -- -- -- -- --</div></div>
    <div class="hex-card"><span class="hex-label">Fuel (0x520)</span><div class="hex-value" id="h_fuel">-- -- -- -- -- -- -- --</div></div>
  </div>
</div>
<script>
let target={rpm:0,temp:0,fuel:100,he:"--",hf:"--"};let shown={rpm:0,temp:0,fuel:100};
function drawGauge(id,v,min,max,title,redL){
  let cv=document.getElementById(id);let ctx=cv.getContext("2d");
  let w=cv.width,h=cv.height,cx=w/2,cy=h*0.68,r=Math.min(w,h)*0.42;
  ctx.clearRect(0,0,w,h);
  let st=Math.PI*0.75,en=Math.PI*2.25,rg=en-st;
  function ang(v){let t=(v-min)/(max-min);return st+Math.max(0,Math.min(1,t))*rg;}
  ctx.lineWidth=12;ctx.strokeStyle="#18304a";ctx.beginPath();ctx.arc(cx,cy,r,st,en);ctx.stroke();
  ctx.lineWidth=12;ctx.strokeStyle="#00aaff";ctx.shadowBlur=18;ctx.shadowColor="#00aaff";
  ctx.beginPath();ctx.arc(cx,cy,r,st,ang(v));ctx.stroke();ctx.shadowBlur=0;
  if(redL!==undefined){
    ctx.lineWidth=13;ctx.strokeStyle="#ff2538";ctx.beginPath();
    if(id=="fuelGauge")ctx.arc(cx,cy,r,st,ang(redL));else ctx.arc(cx,cy,r,ang(redL),en);
    ctx.stroke();
  }
  let ticks = (max == 800) ? 8 : 10;
  for(let i=0;i<=ticks;i++){
    let val=min+(max-min)*i/ticks,a=ang(val);ctx.strokeStyle="#dff7ff";ctx.lineWidth=2;
    ctx.beginPath();ctx.moveTo(cx+Math.cos(a)*r,cy+Math.sin(a)*r);
    ctx.lineTo(cx+Math.cos(a)*(r-18),cy+Math.sin(a)*(r-18));ctx.stroke();
    let xt=cx+Math.cos(a)*(r-40),yt=cy+Math.sin(a)*(r-40);ctx.fillStyle="#dff7ff";ctx.font="12px Arial";ctx.textAlign="center";
    let text=Math.round(val);if(max==800)text=Math.round(val/100);ctx.fillText(text,xt,yt);
  }
  let a=ang(v),needleColor="#ff9d1e";
  if(id=="rpmGauge"&&v>700)needleColor="#ff2538";
  if(id=="tempGauge"&&v>100)needleColor="#ff2538";
  if(id=="fuelGauge"&&v<15)needleColor="#ff2538";
  ctx.strokeStyle=needleColor;ctx.lineWidth=5;ctx.shadowBlur=14;ctx.shadowColor=needleColor;
  ctx.beginPath();ctx.moveTo(cx,cy);ctx.lineTo(cx+Math.cos(a)*(r-30),cy+Math.sin(a)*(r-30));ctx.stroke();ctx.shadowBlur=0;
  ctx.fillStyle="#111";ctx.beginPath();ctx.arc(cx,cy,12,0,Math.PI*2);ctx.fill();
  ctx.strokeStyle=needleColor;ctx.lineWidth=3;ctx.stroke();
  ctx.fillStyle="#8da1b8";ctx.font="bold 13px Arial";ctx.fillText(title,cx,cy+45);
}
function animate(){
  shown.rpm+=(target.rpm-shown.rpm)*0.12;shown.temp+=(target.temp-shown.temp)*0.1;shown.fuel+=(target.fuel-shown.fuel)*0.1;
  drawGauge("rpmGauge",shown.rpm,0,800,"RPM x100",700);
  drawGauge("tempGauge",shown.temp,0,120,"TEMP °C",100);
  drawGauge("fuelGauge",shown.fuel,0,100,"FUEL %",15);
  document.getElementById("rpmText").innerText=Math.round(shown.rpm);
  document.getElementById("tempText").innerText=Math.round(shown.temp)+"°C";
  document.getElementById("fuelText").innerText=Math.round(shown.fuel)+"%";
  document.getElementById("tempWarn").className=target.temp>100?"warn on":"warn";
  document.getElementById("fuelWarn").className=target.fuel<15?"warn low":"warn";
  document.getElementById("h_eng").innerHTML=target.he;document.getElementById("h_fuel").innerHTML=target.hf;
  requestAnimationFrame(animate);
}
function connectWS(){
  let socket=new WebSocket("ws://"+location.hostname+":81/");
  socket.onmessage=(e)=>{let d=JSON.parse(e.data);target.rpm=d.rpm;target.temp=d.temp;target.fuel=d.fuel;target.he=d.he;target.hf=d.hf;};
  socket.onclose=()=>setTimeout(connectWS,1000);
}
connectWS();animate();
</script></body></html>
)rawliteral";

void broadcastWS() {
  if (webSocket.connectedClients() > 0 && millis() - lastWsUpdate > 50) { 
    lastWsUpdate = millis();
    StaticJsonDocument<400> doc;
    doc["rpm"] = global_rpm;
    doc["temp"] = global_temp;
    doc["fuel"] = global_fuel;

    char engStr[35], fuelStr[35];
    snprintf(engStr, sizeof(engStr), "%02X %02X %02X %02X %02X %02X %02X %02X", engine_frame[0], engine_frame[1], engine_frame[2], engine_frame[3], engine_frame[4], engine_frame[5], engine_frame[6], engine_frame[7]);
    snprintf(fuelStr, sizeof(fuelStr), "%02X %02X %02X %02X %02X %02X %02X %02X", fuel_frame[0], fuel_frame[1], fuel_frame[2], fuel_frame[3], fuel_frame[4], fuel_frame[5], fuel_frame[6], fuel_frame[7]);
    doc["he"] = engStr;
    doc["hf"] = fuelStr;

    String json;
    serializeJson(doc, json);
    webSocket.broadcastTXT(json);
  }
}

void setupCAN() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();
  Serial.println("TWAI RECEIVER INSTALLED");
}

void checkBusStatus() {
  twai_status_info_t status;
  twai_get_status_info(&status);
  if (status.state == TWAI_STATE_BUS_OFF) {
    Serial.println("RECEIVER BUS-OFF! RECOVERING...");
    twai_stop();
    twai_driver_uninstall();
    setupCAN();
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init(); lcd.backlight();
  WiFi.softAP(ssid, password);
  server.on("/", [](){ server.send(200, "text/html", index_html); });
  server.begin();
  webSocket.begin();
  setupCAN();
}

void loop() {
  checkBusStatus(); 
  server.handleClient();
  webSocket.loop();
  
  twai_message_t msg;

  if (millis() - lastDummySend > 200) {
    lastDummySend = millis();
    twai_message_t dummy_msg = {0}; 
    dummy_msg.identifier = 0x777; 
    dummy_msg.data_length_code = 0;
    twai_transmit(&dummy_msg, 0); 
  }

  while (twai_receive(&msg, 0) == ESP_OK) {
    int64_t now_us = esp_timer_get_time(); 

    Serial.print("ID: 0x");
    Serial.print(msg.identifier, HEX);
    Serial.print(" | T: ");
    Serial.print(now_us);
    Serial.print(" us | DATA: ");

    for (int i = 0; i < msg.data_length_code; i++) {
      if (msg.data[i] < 0x10) Serial.print("0");
      Serial.print(msg.data[i], HEX);
      Serial.print(" ");
    }

    if (msg.identifier == 0x120) {
      last_t120 = now_us;
      memcpy(engine_frame, msg.data, 8);
      uint16_t raw = (msg.data[0] << 8) | msg.data[1];
      global_rpm = raw / 4.0;
      global_temp = msg.data[2] - 40;
      engineDataValid = true;   

      Serial.print(" -> [ENGINE] RPM: ");
      Serial.print(global_rpm, 1);
      Serial.print(" Temp: ");
      Serial.print(global_temp);
      Serial.print(" C");
    }

    if (msg.identifier == 0x520) {
      last_t520 = now_us;
      memcpy(fuel_frame, msg.data, 8);
      global_fuel = (msg.data[2] * 100.0) / 255.0;

      Serial.print(" -> [FUEL] Fuel: ");
      Serial.print(global_fuel, 1);
      Serial.print(" %");

      int64_t diff = last_t520 - last_t120;
      if (diff > 0 && diff < 1000) { 
        Serial.print(" | ARBITRATION DELAY: ");
        Serial.print(diff);
        Serial.print(" us");
      }
    }
    Serial.println();
    broadcastWS(); 
  }

  if (millis() - lastLcdUpdate > 500) {
    lastLcdUpdate = millis();
    lcd.setCursor(0, 0);
    lcd.print("RPM :      ");   
    lcd.setCursor(6, 0);
    lcd.print(global_rpm, 0); 

    lcd.setCursor(0, 1);
    lcd.print("Temp:");
    if (engineDataValid) { lcd.print(global_temp); } else { lcd.print("--"); }
    lcd.print((char)223); 
    lcd.print("C F:");
    lcd.print(global_fuel, 0); 
    lcd.print("% ");
  }
  
  vTaskDelay(pdMS_TO_TICKS(1)); 
}
