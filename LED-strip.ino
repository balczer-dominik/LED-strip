/*
  D1 Mini RGBW LED Strip Controller (ESP8266/Arduino)
*/

#include <FS.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

/***** USER CONFIG *****/
const char* WIFI_SSID = "HUAWEI-2.4G-zNP6";
const char* WIFI_PASSWORD = "";
const char* AP_SSID = "LED-Strip";
const char* AP_PASSWORD = "ledstrip123";
const char* HOSTNAME = "ledstrip";

const uint8_t PIN_R = D8;
const uint8_t PIN_G = D7;
const uint8_t PIN_B = D5;
const uint8_t PIN_W = D6;

const bool INVERT_R = true;
const bool INVERT_G = true;
const bool INVERT_B = true;
const bool INVERT_W = true;

const bool ENABLE_GAMMA = false;

/***** GLOBALS *****/
ESP8266WebServer server(80);
uint8_t chR = 0, chG = 0, chB = 0, chW = 0, master = 255;
uint8_t lastMaster = 255;
static uint16_t gammaTable[256];

static inline uint16_t toPWM(uint8_t v) {
  if (ENABLE_GAMMA) return gammaTable[v];
  return (uint16_t)map(v, 0, 255, 0, 1023);
}
static inline uint16_t applyInvert(uint16_t pwm, bool inv) {
  return inv ? (1023 - pwm) : pwm;
}

void applyPWM() {
  auto scale = [](uint8_t v, uint8_t m) {
    return (uint16_t)v * m / 255;
  };
  analogWrite(PIN_R, applyInvert(toPWM(scale(chR, master)), INVERT_R));
  analogWrite(PIN_G, applyInvert(toPWM(scale(chG, master)), INVERT_G));
  analogWrite(PIN_B, applyInvert(toPWM(scale(chB, master)), INVERT_B));
  analogWrite(PIN_W, applyInvert(toPWM(scale(chW, master)), INVERT_W));
}

String ipToStr(IPAddress ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

/***** HTML UI *****/
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>LED Strip Controller</title>
<style>
  :root{font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;}
  body{margin:0;padding:16px;background:#0b0b0f;color:#eaeaea;}
  .card{max-width:800px;margin:0 auto;background:#14151a;border:1px solid #262832;border-radius:16px;padding:20px;box-shadow:0 10px 30px rgba(0,0,0,.3);}
  h1{margin-top:0;font-size:1.6rem;}
  .row{display:grid;grid-template-columns:1fr;gap:16px;}
  @media(min-width:720px){.row{grid-template-columns:1fr 1fr;}}
  .group{background:#0f1015;border:1px solid #22242d;border-radius:12px;padding:16px;}
  label{display:block;font-size:.9rem;opacity:.9;margin-bottom:8px;}
  input[type=range]{width:100%;}
  .flex{display:flex;gap:12px;align-items:center;flex-wrap:wrap;}
  .btn{padding:10px 14px;border-radius:12px;background:#1f2230;color:#fff;border:1px solid #303348;cursor:pointer;}
  .btn:active{transform:translateY(1px);}
  .pill{background:#10121a;border:1px dashed #303348;border-radius:999px;padding:6px 12px;font-size:.85rem;}
  .swatch{width:36px;height:36px;border-radius:10px;border:1px solid #333;}
  .grid2{display:grid;grid-template-columns:120px 1fr;column-gap:12px;row-gap:12px;align-items:center;}
  .foot{margin-top:16px;font-size:.85rem;color:#aab;}
  a{color:#99c6ff;}
  .favorites{margin-top:16px;}
  .fav-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px;}
  .fav{position:relative;background:#1a1a22;border:1px solid #333;border-radius:12px;padding:8px;display:flex;flex-direction:column;gap:4px;cursor:pointer;}
  .fav-name{font-size:0.85rem;text-align:center;margin-bottom:4px;color:#ddd;}
  .fav-bar{height:12px;border-radius:6px;background:#333;overflow:hidden;}
  .fav-bar-fill{height:100%;}
  .fav-del{position:absolute;top:4px;right:4px;background:#c33;border:none;border-radius:50%;color:white;font-size:0.7rem;width:20px;height:20px;cursor:pointer;}
  #btnToggle{font-size:1.5rem;border:none;border-radius:12px;padding:12px;cursor:pointer;transition:all 0.2s;}
  #btnToggle.on{background:#fff;color:#000;}
  #btnToggle.off{background:#000;color:#fff;}
</style>
</head>
<body>
<div class="card">
<h1>LED Strip Controller</h1>
<div class="row">
  <div class="group">
    <label>Quick Color</label>
    <div class="flex">
      <input id="color" type="color" value="#000000" style="width:100%;height:40px;border:none;background:transparent;"/>
      <div id="swatch" class="swatch" title="Preview"></div>
    </div>
    <div class="grid2" style="margin-top:12px;">
      <label>Brightness</label>
      <input id="bright" type="range" min="0" max="255" value="255">
      <label>White (W)</label>
      <input id="w" type="range" min="0" max="255" value="0">
    </div>
    <div class="flex" style="margin-top:12px;">
      <button id="btnToggle" class="off">💡</button>
      <button class="btn" id="btnFav">Add to Favorites</button>
      <span class="pill" id="status">—</span>
    </div>
  </div>
  <div class="group">
    <label>Fine-tune RGB</label>
    <div class="grid2">
      <label>Red</label><input id="r" type="range" min="0" max="255" value="0">
      <label>Green</label><input id="g" type="range" min="0" max="255" value="0">
      <label>Blue</label><input id="b" type="range" min="0" max="255" value="0">
    </div>
    <div style="margin-top:12px;font-size:.9rem;opacity:.9;">
      <div>IP: <span id="ip">—</span></div>
      <div>mDNS: <code>http://ledstrip.local</code></div>
    </div>
  </div>
</div>
<div class="group favorites">
  <label>Favorites</label>
  <div id="favGrid" class="fav-grid"></div>
</div>
<div class="foot">Use the sliders or the color picker. Save favorites for quick recall. API: <code>/api/set?r=255&g=128&b=64&w=0&br=200</code></div>
</div>
<script>
(function(){
const qs=id=>document.getElementById(id);
const r=qs('r'),g=qs('g'),b=qs('b'),w=qs('w'),br=qs('bright');
const color=qs('color'),swatch=qs('swatch'),status=qs('status'),favGrid=qs('favGrid');
const btnToggle=qs('btnToggle');

function rgbToHex(r,g,b){return '#'+[r,g,b].map(v=>('0'+v.toString(16)).slice(-2)).join('');}
function hexToRgb(hex){if(hex.startsWith('#')) hex=hex.slice(1);const v=parseInt(hex,16);return {r:(v>>16)&255,g:(v>>8)&255,b:v&255};}
function updateSwatch(){swatch.style.background=rgbToHex(+r.value,+g.value,+b.value);}

let t;
function send(){clearTimeout(t);t=setTimeout(()=>{
fetch(`/api/set?r=${+r.value}&g=${+g.value}&b=${+b.value}&w=${+w.value}&br=${+br.value}`)
.then(x=>x.json()).then(j=>{status.textContent=j.ok?'OK':'ERR';});
updateSwatch();color.value=rgbToHex(+r.value,+g.value,+b.value);
},60);}

[r,g,b,w,br].forEach(el=>el.addEventListener('input',send));
color.addEventListener('input',()=>{const v=hexToRgb(color.value);r.value=v.r;g.value=v.g;b.value=v.b;send();});

function updateToggle(bright){btnToggle.className=bright>0?'on':'off';btnToggle.textContent='💡';}
btnToggle.addEventListener('click',async()=>{
  const r=await fetch('/api/toggle');const data=await r.json();updateToggle(data.br);
});

function makeBar(val,color){const o=document.createElement('div');o.className='fav-bar';const i=document.createElement('div');i.className='fav-bar-fill';i.style.width=(val/255*100)+'%';i.style.background=color;o.appendChild(i);return o;}
function loadFavs(){
const favs=JSON.parse(localStorage.getItem('ledFavs')||'[]');favGrid.innerHTML='';
favs.forEach((f,i)=>{
const div=document.createElement('div');div.className='fav';
const name=document.createElement('div');name.className='fav-name';name.textContent=f.name||('Fav '+(i+1));div.appendChild(name);
div.appendChild(makeBar(f.br,'#888'));
div.appendChild(makeBar(f.w,'#fff'));
div.appendChild(makeBar(f.r,'#f33'));
div.appendChild(makeBar(f.g,'#3f3'));
div.appendChild(makeBar(f.b,'#33f'));
const del=document.createElement('button');del.className='fav-del';del.textContent='×';
del.addEventListener('click',(ev)=>{ev.stopPropagation();if(confirm("Delete favorite '"+f.name+"'?")){favs.splice(i,1);localStorage.setItem('ledFavs',JSON.stringify(favs));loadFavs();}});
div.appendChild(del);
div.addEventListener('click',()=>{r.value=f.r;g.value=f.g;b.value=f.b;w.value=f.w;br.value=f.br;send();});
favGrid.appendChild(div);
});
}
qs('btnFav').addEventListener('click',()=>{const name=prompt("Name for this favorite:","New Favorite");if(!name)return;const favs=JSON.parse(localStorage.getItem('ledFavs')||'[]');favs.push({name,r:+r.value,g:+g.value,b:+b.value,w:+w.value,br:+br.value});localStorage.setItem('ledFavs',JSON.stringify(favs));loadFavs();});

fetch('/api/state').then(x=>x.json()).then(j=>{
r.value=j.r;g.value=j.g;b.value=j.b;w.value=j.w;br.value=j.br;color.value=rgbToHex(j.r,j.g,j.b);document.getElementById('ip').textContent=j.ip||'—';updateSwatch();updateToggle(j.br);
});

loadFavs();
})();
</script>
</body>
</html>
)HTML";

/***** HTTP HANDLERS *****/
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}
void handleState() {
  String ipStr = (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) ? ipToStr(WiFi.localIP()) : (WiFi.getMode() == WIFI_AP ? ipToStr(WiFi.softAPIP()) : "");
  String json = "{\"r\":" + String(chR) + ",\"g\":" + String(chG) + ",\"b\":" + String(chB) + ",\"w\":" + String(chW) + ",\"br\":" + String(master) + ",\"ip\":\"" + ipStr + "\"}";
  server.send(200, "application/json", json);
}
uint8_t getArg8(const String& name, uint8_t defVal) {
  if (!server.hasArg(name)) return defVal;
  int v = server.arg(name).toInt();
  return (v < 0 ? 0 : (v > 255 ? 255 : v));
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
void handleToggle() {
  master = (master > 0) ? 0 : 255;
  applyPWM();
  server.send(200, "application/json", "{\"ok\":true,\"br\":" + String(master) + "}");
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
void handleNotFound() {
  server.send(404, "text/plain", "Not found: " + server.uri());
}

/***** WIFI *****/
void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(200);
  Serial.print("AP mode. SSID: ");
  Serial.print(AP_SSID);
  Serial.print(" IP: ");
  Serial.println(WiFi.softAPIP());
}
bool connectWiFiSTA(unsigned long timeoutMs = 15000) {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("WiFi connect timeout.");
  return false;
}

/***** SETUP/LOOP *****/
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nBooting LED Strip Controller...");
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
  if (ENABLE_GAMMA) {
    for (int i = 0; i < 256; i++) {
      float x = i / 255.0f;
      gammaTable[i] = (uint16_t)roundf(powf(x, 2.2f) * 1023.0f);
    }
  }
  if (!connectWiFiSTA()) { startAP(); }
  if (MDNS.begin(HOSTNAME)) {
    Serial.println("mDNS responder started.");
  } else {
    Serial.println("mDNS failed.");
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/set", HTTP_ANY, handleSet);
  server.on("/api/toggle", HTTP_ANY, handleToggle);
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
