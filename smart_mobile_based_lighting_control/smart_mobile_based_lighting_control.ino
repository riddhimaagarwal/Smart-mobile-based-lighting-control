// ESP32 LED controller - 4 LEDs, web UI, sequence, schedule, brightness
// Pins: 32, 33, 25, 26
// Uses WebServer, WiFi, NTP (for schedule at HH:MM). Change wifi_ssid / wifi_pass.
// Default timezone is IST (+5:30) in timezoneOffsetSeconds. Change if needed.

#include <WiFi.h>
#include <WebServer.h>
#include "time.h"

// --------- USER CONFIG ----------
const char* wifi_ssid = "Riddhima";
const char* wifi_pass = "hello123";
// timezone offset in seconds (IST = +5.5h = 19800). Change for your zone.
const long timezoneOffsetSeconds = 19800L;
// NTP servers
const char* ntpServer = "pool.ntp.org";
// --------------------------------

WebServer server(80);

const int ledPins[4] = {32, 33, 25, 26};
// Software PWM Constants
const int pwmPeriodUs = 500; // Total period for one PWM cycle (500us = 2000Hz)
unsigned long lastPwmMillis = 0;

// State
bool ledsOn[4] = {false, false, false, false};
bool allOnState = false;
bool sequenceRunning = false;
unsigned long sequenceInterval = 500; // ms between steps
int seqIndex = 0;
bool seqDirectionForward = true; // forward (on) then backward (off)
unsigned long lastSeqMillis = 0;

int globalBrightness = 200; // 0-255

// Scheduling
bool scheduleActive = false;
unsigned long scheduleMillis = 0; // absolute millis when to turn on
bool scheduleTurnOn = true; // we only schedule turn ON here
String scheduleMode = ""; // "delay" or "at"
String scheduledAtStr = "";

// ---------- Helpers ----------
void applyBrightnessToPin(int i, int brightness) {
  // This function is now mostly a placeholder, the actual PWM is handled in loop()
}

void setLedRaw(int i, bool on) {
  ledsOn[i] = on;
  // Pin state will be physically set in the softwarePwmLoop()
}

void setAll(bool on) {
  allOnState = on;
  for (int i=0;i<4;i++) setLedRaw(i, on);
}

// MODIFIED: Stops sequence before toggling all.
void toggleAll() {
  if (sequenceRunning) {
    sequenceRunning = false; // Stop the sequence if it's running
  }
  setAll(!allOnState);
}

String getTimeStr() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

void softwarePwmLoop() {
  for (int i=0; i<4; i++) {
    if (ledsOn[i]) {
      int onTimeUs = map(globalBrightness, 0, 255, 0, pwmPeriodUs);
      
      digitalWrite(ledPins[i], HIGH);
      delayMicroseconds(onTimeUs);
      digitalWrite(ledPins[i], LOW);
      delayMicroseconds(pwmPeriodUs - onTimeUs);
    } else {
      digitalWrite(ledPins[i], LOW);
    }
  }
}

// ---------- HTTP handlers ----------
void handleRoot() {
  // serve a simple webpage (inline HTML + JS)
  String page = R"rawliteral(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 4-LED Control</title>
<style>
body{font-family:Arial;text-align:center;background:#1e1e1e;color:#cfcfcf;padding:16px}
button{font-size:18px;padding:12px 20px;margin:8px;border-radius:6px}
#status{margin-top:12px}
.slider{width:80%}
.box{background:#2b2b2b;padding:12px;border-radius:8px;display:inline-block;margin:8px}
input[type="time"]{font-size:16px;padding:6px}
</style>
</head>
<body>
<h2>ESP32 4-LED Control</h2>
<div class="box">
<button id="btnAll">Toggle All</button> 
<button id="btnSeq">Start Sequence</button> 
</div>
<div class="box">
<label>Schedule delay (sec): <input id="delaySec" type="number" min="1" max="86400" value="10"></label><br>
<button id="btnSetDelay">Set Delay</button>
<hr>
<label>Schedule at (HH:MM): <input id="timeAt" type="time"></label><br>
<button id="btnSetAt">Set Time</button>
</div>
<div class="box">
<label>Brightness: <span id="brightLabel">200</span></label><br>
<input id="brightness" class="slider" type="range" min="0" max="255" value="200">
</div>
<div id="status"></div>
<script>
async function api(path, opts){
  const resp = await fetch(path, opts);
  return resp.json();
}
document.getElementById('btnAll').onclick = async ()=>{
  const r = await api('/toggleAll');
  document.getElementById('status').innerText = r.message;
};
document.getElementById('btnSeq').onclick = async ()=>{
  const r = await api('/toggleSequence'); 
  document.getElementById('status').innerText = r.message;
};

document.getElementById('btnSetDelay').onclick = async ()=>{
  const s = document.getElementById('delaySec').value;
  const r = await api('/scheduleDelay?sec='+encodeURIComponent(s));
  document.getElementById('status').innerText = r.message;
};
document.getElementById('btnSetAt').onclick = async ()=>{
  const t = document.getElementById('timeAt').value;
  if(!t){ alert('Pick a time'); return; }
  const r = await api('/scheduleAt?time='+encodeURIComponent(t));
  document.getElementById('status').innerText = r.message;
};
const brightness = document.getElementById('brightness');
brightness.oninput = async ()=>{
  const v = brightness.value;
  document.getElementById('brightLabel').innerText = v;
  await api('/setBrightness?val='+v);
};
async function refreshStatus(){
  const r = await api('/status');
  document.getElementById('status').innerText = r.message;
  document.getElementById('brightLabel').innerText = r.brightness;
  document.getElementById('brightness').value = r.brightness;
  // Update button text based on sequence state
  document.getElementById('btnSeq').innerText = r.sequence ? 'Stop Sequence' : 'Start Sequence'; 
}
setInterval(refreshStatus, 2000);
refreshStatus();
</script>
</body>
</html>
  )rawliteral";

  server.send(200, "text/html", page);
}

void handleToggleAll() {
  toggleAll();
  server.send(200, "application/json", String("{\"message\":\"All toggled\",\"allOn\":") + (allOnState? "true":"false") + "}");
}

void handleToggleSequence() {
  if (sequenceRunning) {
    sequenceRunning = false;
    // When stopping, ensure all LEDs are turned off 
    // This is handled in the loop, but explicitly setting state here is cleaner
    for (int i=0; i<4; i++) setLedRaw(i, false); 
    server.send(200, "application/json", "{\"message\":\"Sequence stopped\",\"sequence\":false}");
  } else {
    sequenceRunning = true;
    seqIndex = 0;
    seqDirectionForward = true;
    lastSeqMillis = millis();
    server.send(200, "application/json", "{\"message\":\"Sequence started\",\"sequence\":true}");
  }
}

void handleSetBrightness() {
  if (!server.hasArg("val")) {
    server.send(400, "application/json", "{\"error\":\"missing val\"}");
    return;
  }
  int v = server.arg("val").toInt();
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  globalBrightness = v;
  server.send(200, "application/json", String("{\"message\":\"brightness set\",\"brightness\":") + v + "}");
}

void handleScheduleDelay() {
  if (!server.hasArg("sec")) {
    server.send(400, "application/json", "{\"error\":\"missing sec\"}");
    return;
  }
  long s = server.arg("sec").toInt();
  if (s <= 0) { server.send(400, "application/json", "{\"error\":\"invalid secs\"}"); return; }
  scheduleMode = "delay";
  scheduleActive = true;
  scheduleMillis = millis() + (unsigned long)(s * 1000UL);
  scheduledAtStr = String("delay ") + String(s) + "s";
  server.send(200, "application/json", String("{\"message\":\"Scheduled in ") + s + " seconds\",\"mode\":\"delay\"}");
}

void handleScheduleAt() {
  if (!server.hasArg("time")) {
    server.send(400, "application/json", "{\"error\":\"missing time\"}");
    return;
  }
  String t = server.arg("time"); // "HH:MM"
  // parse
  int hh = 0, mm = 0;
  if (sscanf(t.c_str(), "%d:%d", &hh, &mm) != 2) {
    server.send(400, "application/json", "{\"error\":\"bad time format\"}");
    return;
  }
  // compute next occurrence using local time
  struct tm timeinfo;
  time_t now;
  time(&now);
  localtime_r(&now, &timeinfo);
  timeinfo.tm_hour = hh;
  timeinfo.tm_min = mm;
  timeinfo.tm_sec = 0;
  time_t target = mktime(&timeinfo);
  if (target <= now) target += 24*3600; // next day
  scheduleMillis = millis() + (unsigned long)((target - now) * 1000UL);
  scheduleActive = true;
  scheduleMode = "at";
  scheduledAtStr = t;
  server.send(200, "application/json", String("{\"message\":\"Scheduled at ") + t + "\"}");
}

void handleStatus() {
  String msg = String("Time: ") + getTimeStr() + ", AllOn=" + (allOnState?"1":"0") + ", Seq=" + (sequenceRunning?"1":"0");
  if (scheduleActive) msg += String(", Scheduled: ") + scheduleMode + " " + scheduledAtStr;
  String body = String("{\"message\":\"") + msg + "\",\"brightness\":" + globalBrightness + ",\"sequence\":" + (sequenceRunning? "true":"false") + "}";
  server.send(200, "application/json", body);
}

// ---------- Setup ----------
void setupPins() {
  for (int i=0;i<4;i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW); // Turn off
    setLedRaw(i, false);
  }
}

void startWiFi() {
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(200);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connect failed, starting AP mode.");
    WiFi.softAP("ESP32-LED-AP");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }
}

void startNTP() {
  configTime(timezoneOffsetSeconds, 0, ntpServer);
  // wait for time to be set (short)
  Serial.print("Waiting for NTP ");
  int tries=0;
  struct tm timeinfo;
  while (tries < 10) {
    time_t now = time(nullptr);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year > (2016 - 1900)) break;
    delay(500);
    tries++;
    Serial.print(".");
  }
  Serial.println();
}

void setupServerRoutes() {
  server.on("/", handleRoot);
  server.on("/toggleAll", handleToggleAll);
  server.on("/toggleSequence", handleToggleSequence); 
  server.on("/setBrightness", handleSetBrightness);
  server.on("/scheduleDelay", handleScheduleDelay);
  server.on("/scheduleAt", handleScheduleAt);
  server.on("/status", handleStatus);
  server.begin();
  Serial.println("HTTP server started.");
}

void setup() {
  Serial.begin(115200);
  setupPins();
  startWiFi();
  startNTP();
  setupServerRoutes();
}

// ---------- Loop ----------
void loop() {
  server.handleClient();

  unsigned long now = millis();

  // Sequence logic (non-blocking)
  if (sequenceRunning) {
    if (now - lastSeqMillis >= sequenceInterval) {
      lastSeqMillis = now;
      
      // Turn OFF the LED from the previous step (if any)
      int prevIndex = seqIndex;
      if (seqDirectionForward) {
         prevIndex = (seqIndex == 0) ? 3 : seqIndex - 1; 
      } else {
         prevIndex = (seqIndex == 3) ? 0 : seqIndex + 1;
      }
      setLedRaw(prevIndex, false);
      
      // Turn ON the LED for the current step
      setLedRaw(seqIndex, true);

      // Move to the next index
      if (seqDirectionForward) {
          seqIndex+=2;
          if (seqIndex >= 4) {
              seqIndex = 2; 
              seqDirectionForward = false;
          }
      } else {
          seqIndex-=2;
          if (seqIndex < 0) {
              seqIndex = 1; 
              seqDirectionForward = true;
          }
      }
    }
  } 
  // REMOVED: The logic here that previously turned off all LEDs when sequenceRunning was false.
  // The 'ledsOn' state is now respected when sequence is off.

  // Schedule handling
  if (scheduleActive) {
    if ((long)(scheduleMillis - now) <= 0) {
      // time reached
      scheduleActive = false;
      // action: turn ALL ON
      setAll(true); // This sets all ledsOn flags to true
      Serial.println("Scheduled ON executed.");
    }
  }

  // Run the software PWM routine
  softwarePwmLoop();

  // small delay to avoid busy loop
  delay(1);
}