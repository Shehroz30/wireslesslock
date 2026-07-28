// wireslesslock controller (ESP32-S3-WROOM-1)
// Hosts a local web page over WiFi to open/close the solenoid lock and
// shows live accelerometer + GPS readings (page polls /data every second,
// no cloud/backend involved).
//
// Pin map, derived from wireslesslock.kicad_sch (label -> nearest symbol pin):
//   CTRL_P (Q1 solenoid gate driver)      -> GPIO40
//   LIS2DH12 SPI: CS=GPIO10 SCK=GPIO12 MOSI=GPIO11 MISO=GPIO13
//   SIM68M GPS, primary NMEA UART (net UART1_RX/TX): RX=GPIO17 TX=GPIO18, 9600 baud
//
// Requires the "TinyGPSPlus" library (Library Manager, by Mikal Hart) for
// NMEA parsing; everything else is bundled with the ESP32 Arduino core.

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <TinyGPSPlus.h>

// ---- fill in before flashing ----
const char *WIFI_SSID = "YOUR_WIFI";
const char *WIFI_PASS = "YOUR_PASSWORD";

// ---- pins ----
const int SOLENOID_PIN = 40; // CTRL_P
const int LIS_CS = 10, LIS_SCK = 12, LIS_MOSI = 11, LIS_MISO = 13;
const int GPS_RX = 17, GPS_TX = 18;

// ponytail: no PWM hold-current limiting in hardware, so a client that
// never calls /close would otherwise cook the coil; cap how long it can
// stay energized regardless of what the web page does.
const unsigned long MAX_OPEN_MS = 5000;

WebServer server(80);
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

bool solenoidOpen = false;
unsigned long openedAt = 0;
float accelX = 0, accelY = 0, accelZ = 0;

// ---------- LIS2DH12 accelerometer: minimal register-level SPI driver ----------
void lisWrite(uint8_t reg, uint8_t val) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(LIS_CS, LOW);
  SPI.transfer(reg & 0x3F); // write, no auto-increment
  SPI.transfer(val);
  digitalWrite(LIS_CS, HIGH);
  SPI.endTransaction();
}

void lisReadAccel() {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(LIS_CS, LOW);
  SPI.transfer(0x28 | 0xC0); // OUT_X_L, read + auto-increment
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = SPI.transfer(0x00);
  digitalWrite(LIS_CS, HIGH);
  SPI.endTransaction();

  int16_t x = (int16_t)((b[1] << 8) | b[0]) >> 4; // normal mode: 10 bits, left-justified
  int16_t y = (int16_t)((b[3] << 8) | b[2]) >> 4;
  int16_t z = (int16_t)((b[5] << 8) | b[4]) >> 4;
  const float SENS = 0.004f; // g/LSB, normal mode, +-2g range
  accelX = x * SENS;
  accelY = y * SENS;
  accelZ = z * SENS;
}

// ---------- solenoid ----------
void setSolenoid(bool open) {
  digitalWrite(SOLENOID_PIN, open ? HIGH : LOW);
  solenoidOpen = open;
  openedAt = open ? millis() : 0;
}

// ---------- web ----------
const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>wireslesslock</title>
<style>body{font-family:sans-serif;max-width:420px;margin:2rem auto;text-align:center}
button{font-size:1.2rem;padding:.75rem 1.5rem;margin:.5rem;border:0;border-radius:.5rem;color:#fff}
#open{background:#2e7d32}#close{background:#c62828}
table{margin:1rem auto;text-align:left}</style></head><body>
<h2>wireslesslock</h2>
<p>Status: <b id="status">-</b></p>
<button id="open" onclick="cmd('open')">Open</button>
<button id="close" onclick="cmd('close')">Close</button>
<table>
<tr><td>Accel (g)</td><td id="accel">-</td></tr>
<tr><td>GPS fix</td><td id="fix">-</td></tr>
<tr><td>Lat, Lon</td><td id="pos">-</td></tr>
</table>
<script>
function cmd(a){fetch('/'+a,{method:'POST'}).then(refresh);}
function refresh(){
  fetch('/data').then(r=>r.json()).then(d=>{
    document.getElementById('status').textContent = d.locked ? 'Locked' : 'OPEN';
    document.getElementById('accel').textContent = d.accel.x.toFixed(2)+', '+d.accel.y.toFixed(2)+', '+d.accel.z.toFixed(2);
    document.getElementById('fix').textContent = d.gps.fix ? ('yes ('+d.gps.sats+' sats)') : 'no';
    document.getElementById('pos').textContent = d.gps.fix ? d.gps.lat.toFixed(6)+', '+d.gps.lon.toFixed(6) : '-';
  });
}
setInterval(refresh, 1000);
refresh();
</script></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleData() {
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"locked\":%s,\"accel\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f},"
    "\"gps\":{\"fix\":%s,\"sats\":%d,\"lat\":%.6f,\"lon\":%.6f}}",
    solenoidOpen ? "false" : "true",
    accelX, accelY, accelZ,
    gps.location.isValid() ? "true" : "false",
    gps.satellites.isValid() ? gps.satellites.value() : 0,
    gps.location.isValid() ? gps.location.lat() : 0.0,
    gps.location.isValid() ? gps.location.lng() : 0.0);
  server.send(200, "application/json", buf);
}

void handleOpen()  { setSolenoid(true);  server.send(200, "text/plain", "ok"); }
void handleClose() { setSolenoid(false); server.send(200, "text/plain", "ok"); }

void setup() {
  Serial.begin(115200);

  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW); // locked by default

  pinMode(LIS_CS, OUTPUT);
  digitalWrite(LIS_CS, HIGH);
  SPI.begin(LIS_SCK, LIS_MISO, LIS_MOSI, LIS_CS);
  lisWrite(0x20, 0x27); // CTRL_REG1: 10Hz, X/Y/Z enabled, normal mode

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(200);
  Serial.print("wireslesslock web page: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/open", HTTP_POST, handleOpen);
  server.on("/close", HTTP_POST, handleClose);
  server.begin();
}

void loop() {
  server.handleClient();

  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  static unsigned long lastAccel = 0;
  if (millis() - lastAccel > 200) {
    lisReadAccel();
    lastAccel = millis();
  }

  if (solenoidOpen && millis() - openedAt > MAX_OPEN_MS) setSolenoid(false);
}
