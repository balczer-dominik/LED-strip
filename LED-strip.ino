/*
  D1 Mini RGBW LED Strip Controller (ESP8266/Arduino)
  ---------------------------------------------------
  Controls a 5-pin RGBW analog LED strip (common anode typically) using
  D5-D8 pins on a Wemos D1 mini (ESP8266). Hosts a Wi‑Fi web UI and JSON API.

  Hardware (typical):
  - LED strip: 12V RGBW (5 pins: V+ plus R,G,B,W channels)
  - 4x N‑channel logic‑level MOSFETs (e.g., IRLZ44N, AO3400, AO4407, AOD508)
  - Gate resistors ~100–220Ω, pulldown resistors ~100kΩ to GND on each gate
  - Common ground between 12V supply and D1 mini GND
  - IMPORTANT: D8 (GPIO15) must be LOW at boot; keep a pulldown on its MOSFET gate

  Libraries: Built-in with ESP8266 Arduino core
  - ESP8266WiFi.h
  - ESP8266WebServer.h
  - ESP8266mDNS.h

  Tested with: ESP8266 core 3.x
*/

#include <FS.h>        // SPIFFS
#include <ArduinoJson.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

/***** ====== USER CONFIG ====== *****/
const char* WIFI_SSID     = "HUAWEI-2.4G-zNP6";
const char* WIFI_PASSWORD = "";

const char* AP_SSID       = "LED-Strip";
const char* AP_PASSWORD   = "ledstrip123";

const char* HOSTNAME      = "ledstrip";

const uint8_t PIN_R = D8;  // R -> D8 (red wire)
const uint8_t PIN_G = D7;  // G -> D7 (orange wire)
const uint8_t PIN_B = D5;  // B -> D5 (yello wire)
const uint8_t PIN_W = D6;  // W -> D6 (green wire)

const bool INVERT_R = true;
const bool INVERT_G = true;
const bool INVERT_B = true;
const bool INVERT_W = true;

/***** ====== GLOBALS ====== *****/
ESP8266WebServer server(80);

uint8_t chR = 0,   chG = 0,   chB = 0,   chW = 0,   master = 255;

static inline uint16_t toPWM(uint8_t v) {
  // Linear scale 0..255 → 0..1023
  return (uint16_t)map(v, 0, 255, 0, 1023);
}

static inline uint16_t applyInvert(uint16_t pwm, bool inv) {
  return inv ? (1023 - pwm) : pwm;
}

void applyPWM() {
  auto scale = [](uint8_t v, uint8_t m) -> uint8_t {
    return (uint16_t)v * m / 255;
  };

  uint16_t pr = applyInvert(toPWM(scale(chR, master)), INVERT_R);
  uint16_t pg = applyInvert(toPWM(scale(chG, master)), INVERT_G);
  uint16_t pb = applyInvert(toPWM(scale(chB, master)), INVERT_B);
  uint16_t pw = applyInvert(toPWM(scale(chW, master)), INVERT_W);

  analogWrite(PIN_R, pr);
  analogWrite(PIN_G, pg);
  analogWrite(PIN_B, pb);
  analogWrite(PIN_W, pw);
}

String ipToStr(IPAddress ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

/***** ====== HTML UI ====== *****/
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>LED Strip Controller</title>
<style>
  :root { font-family: system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial, sans-serif; }
  body { margin: 0; padding: 16px; background: #0b0b0f; color: #eaeaea; }
  .card { max-width: 800px; margin: 0 auto; background: #14151a; border: 1px solid #262832; border-radius: 16px; padding: 20px; box-shadow: 0 10px 30px rgba(0,0,0,.3); }
  h1 { margin-top: 0; font-size: 1.6rem; }
  .row { display: grid; grid-template-columns: 1fr; gap: 16px; }
  @media (min-width: 720px) { .row { grid-template-columns: 1fr 1fr; } }
  .group { background: #0f1015; border: 1px solid #22242d; border-radius: 12px; padding: 16px; }
  label { display: block; font-size: .9rem; opacity: .9; margin-bottom: 8px; }
  input[type="range"] { width: 100%; }
  .flex { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
  .btn { padding: 10px 14px; border-radius: 12px; background: #1f2230; color: #fff; border: 1px solid #303348; cursor: pointer; }
  .btn:active { transform: translateY(1px); }
  .pill { background: #10121a; border: 1px dashed #303348; border-radius: 999px; padding: 6px 12px; font-size: .85rem; }
  .swatch { width: 36px; height: 36px; border-radius: 10px; border: 1px solid #333; }
  .grid2 { display: grid; grid-template-columns: 120px 1fr; column-gap: 12px; row-gap: 12px; align-items: center; }
  .foot { margin-top: 16px; font-size: .85rem; color: #aab; }
  a { color: #99c6ff; }
  /* Favorites grid */
  .favorites { margin-top: 16px; }
  .fav-grid { display: grid; grid-template-columns: repeat(5, 1fr); gap: 10px; }
  .fav { width: 100%; padding-top: 100%; position: relative; border-radius: 12px; border: 1px solid #333; cursor: pointer; }
  .fav-inner { position: absolute; top:0; left:0; right:0; bottom:0; border-radius: 12px; }
</style>
</head>
<body>
<div class="card">
  <h1>LED Strip Controller</h1>
  <div class="row">
    <div class="group">
      <label>Quick Color</label>
      <div class="flex">
        <input id="color" type="color" value="#000000" style="width: 100%; height: 40px; border: none; background: transparent;"/>
        <div id="swatch" class="swatch" title="Preview"></div>
      </div>
      <div class="grid2" style="margin-top: 12px;">
        <label>Brightness</label>
        <input id="bright" type="range" min="0" max="255" value="255">
        <label>White (W)</label>
        <input id="w" type="range" min="0" max="255" value="0">
      </div>
      <div class="flex" style="margin-top: 12px;">
        <button class="btn" id="btnOn">On</button>
        <button class="btn" id="btnOff">Off</button>
        <button class="btn" id="btnFav">Add to Favorites</button>
        <span class="pill" id="status">—</span>
      </div>
    </div>
    <div class="group">
      <label>Fine-tune RGB</label>
      <div class="grid2">
        <label>Red</label>
        <input id="r" type="range" min="0" max="255" value="0">
        <label>Green</label>
        <input id="g" type="range" min="0" max="255" value="0">
        <label>Blue</label>
        <input id="b" type="range" min="0" max="255" value="0">
      </div>
      <div style="margin-top: 12px; font-size: .9rem; opacity: .9;">
        <div>IP: <span id="ip">—</span></div>
        <div>mDNS: <code>http://ledstrip.local</code></div>
      </div>
    </div>
  </div>
  <!-- Favorites Section -->
  <div class="group favorites">
    <label>Favorites</label>
    <div id="favGrid" class="fav-grid"></div>
  </div>
  <div class="foot">Use the sliders or the color picker. Save favorites for quick recall. API: <code>/api/set?r=255&g=128&b=64&w=0&br=200</code></div>
</div>
<script>
(function(){
  const qs = id => document.getElementById(id);
  const r=qs('r'), g=qs('g'), b=qs('b'), w=qs('w'), br=qs('bright');
  const color=qs('color'), swatch=qs('swatch');
  const status=qs('status'), favGrid=qs('favGrid');

  function rgbToHex(r,g,b){
    return '#' + [r,g,b].map(v => ('0' + v.toString(16)).slice(-2)).join('');
  }
  function hexToRgb(hex){
    if(hex.startsWith('#')) hex = hex.slice(1);
    const v = parseInt(hex, 16);
    return { r: (v>>16)&255, g: (v>>8)&255, b: v&255 };
  }
  function updateSwatch(){
    swatch.style.background = rgbToHex(+r.value, +g.value, +b.value);
  }

  let t;
  function send(){
    clearTimeout(t);
    t = setTimeout(()=>{
      const url = `/api/set?r=${+r.value}&g=${+g.value}&b=${+b.value}&w=${+w.value}&br=${+br.value}`;
      fetch(url).then(x=>x.json()).then(j=>{ status.textContent = j.ok ? 'OK' : 'ERR'; });
      updateSwatch();
      color.value = rgbToHex(+r.value, +g.value, +b.value);
    }, 60);
  }

  [r,g,b,w,br].forEach(el => el.addEventListener('input', send));

  color.addEventListener('input', ()=>{
    const v = hexToRgb(color.value);
    r.value = v.r; g.value = v.g; b.value = v.b;
    send();
  });

  qs('btnOn').addEventListener('click', ()=>{ fetch('/api/on'); });
  qs('btnOff').addEventListener('click', ()=>{ fetch('/api/off'); });

  // ---- Favorites handling ----
  function loadFavs(){
    const favs = JSON.parse(localStorage.getItem('ledFavs')||'[]');
    favGrid.innerHTML='';
    favs.forEach((f,i)=>{
      const div=document.createElement('div');
      div.className='fav';
      div.innerHTML='<div class="fav-inner"></div>';
      div.firstChild.style.background = rgbToHex(f.r,f.g,f.b);
      div.title=`R:${f.r} G:${f.g} B:${f.b} W:${f.w} Br:${f.br}`;

      // left click → apply favorite
      div.addEventListener('click', ()=>{
        r.value=f.r; g.value=f.g; b.value=f.b; w.value=f.w; br.value=f.br;
        send();
      });

      // right click → ask delete
      div.addEventListener('contextmenu', (ev)=>{
        ev.preventDefault();
        if(confirm("Delete this favorite?")){
          const favs = JSON.parse(localStorage.getItem('ledFavs')||'[]');
          favs.splice(i,1);
          localStorage.setItem('ledFavs', JSON.stringify(favs));
          loadFavs();
        }
      });

      favGrid.appendChild(div);
    });
  }

  qs('btnFav').addEventListener('click', ()=>{
    const favs = JSON.parse(localStorage.getItem('ledFavs')||'[]');
    favs.push({r:+r.value,g:+g.value,b:+b.value,w:+w.value,br:+br.value});
    localStorage.setItem('ledFavs', JSON.stringify(favs));
    loadFavs();
  });

  // Initial state load
  fetch('/api/state').then(x=>x.json()).then(j=>{
    r.value=j.r; g.value=j.g; b.value=j.b; w.value=j.w; br.value=j.br;
    color.value = rgbToHex(j.r, j.g, j.b);
    document.getElementById('ip').textContent = j.ip || '—';
    updateSwatch();
  });

  loadFavs();
})();
</script>
</body>
</html>
)HTML";

/***** ====== HTTP HANDLERS ====== *****/
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleState() {
  String ipStr;
  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
    ipStr = ipToStr(WiFi.localIP());
  } else if (WiFi.getMode() == WIFI_AP) {
    ipStr = ipToStr(WiFi.softAPIP());
  } else {
    ipStr = "";
  }
  String json = String("{") +
    "\"r\":" + chR + "," +
    "\"g\":" + chG + "," +
    "\"b\":" + chB + "," +
    "\"w\":" + chW + "," +
    "\"br\":" + master + "," +
    "\"ip\":\"" + ipStr + "\"" +
  "}";
  server.send(200, "application/json", json);
}

uint8_t getArg8(const String& name, uint8_t defVal) {
  if (!server.hasArg(name)) return defVal;
  int v = server.arg(name).toInt();
  if (v < 0) v = 0; if (v > 255) v = 255;
  return (uint8_t)v;
}

void handleSet() {
  chR = getArg8("r", chR);
  chG = getArg8("g", chG);
  chB = getArg8("b", chB);
  chW = getArg8("w", chW);
  master = getArg8("br", master);
  applyPWM();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleOn() {
  master = 255; applyPWM();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleOff() {
  master = 0; applyPWM();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleNotFound() {
  String msg = "Not found: " + server.uri();
  server.send(404, "text/plain", msg);
}

void handleDiag() {
  String ch = server.hasArg("ch") ? server.arg("ch") : "";
  ch.toUpperCase();
  chR = (ch == "R") ? 255 : 0;
  chG = (ch == "G") ? 255 : 0;
  chB = (ch == "B") ? 255 : 0;
  chW = (ch == "W") ? 255 : 0;
  master = 255;
  applyPWM();
  server.send(200, "application/json", "{\"ok\":true}");
}


/***** ====== WIFI ====== *****/
void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(200);
  Serial.print("AP mode. SSID: "); Serial.print(AP_SSID);
  Serial.print("  IP: "); Serial.println(WiFi.softAPIP());
}

bool connectWiFiSTA(unsigned long timeoutMs = 15000) {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi: "); Serial.println(WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP: "); Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("WiFi connect timeout.");
  return false;
}

/***** ====== SETUP/LOOP ====== *****/
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nBooting LED Strip Controller...");

  // PWM setup
  analogWriteRange(1023);
  analogWriteFreq(1000);

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(PIN_W, OUTPUT);

  digitalWrite(PIN_R, HIGH);
  digitalWrite(PIN_G, HIGH);
  digitalWrite(PIN_B, HIGH);
  digitalWrite(PIN_W, HIGH);

  bool staOK = connectWiFiSTA();
  if (!staOK) {
    startAP();
  }

  // mDNS
  if (MDNS.begin(HOSTNAME)) {
    Serial.print("mDNS responder started: http://");
    Serial.print(HOSTNAME); Serial.println(".local");
  } else {
    Serial.println("mDNS failed to start");
  }

  // HTTP routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/set", HTTP_ANY, handleSet);
  server.on("/api/on", HTTP_ANY, handleOn);
  server.on("/api/off", HTTP_ANY, handleOff);
  server.on("/api/diag", HTTP_ANY, handleDiag);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started on port 80");

  applyPWM();
}

void loop() {
  server.handleClient();
  MDNS.update();
}
