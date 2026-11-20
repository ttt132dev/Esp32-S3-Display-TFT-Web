#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

const char* apSSID = "ESP32-S3-Display";
const int proxyPort = 5000;

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

String savedSSID = "", savedPass = "", pcIP = "192.168.100.64";
String wifiStatusMsg = "Chưa kết nối WiFi", scannedOptions = "", webPageUrl = "";
bool isStreaming = false, apEnabled = true, hasCustomPcIP = false;

int screenW = 240, screenH = 240, centerX = 120, centerY = 120;
uint8_t uiTextSize = 2;

uint8_t *jpgBuffer = nullptr, *netBuffer = nullptr;
size_t maxJpgSize = 32 * 1024;
const size_t NET_CHUNK_SIZE = 4096;

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height() || x >= tft.width()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void initDisplayMetrics() {
  screenW = tft.width();
  screenH = tft.height();
  centerX = screenW / 2;
  centerY = screenH / 2;

  int minDim = min(screenW, screenH);
  if (minDim < 140) uiTextSize = 1;
  else if (minDim < 320) uiTextSize = 2;
  else uiTextSize = 3;

  size_t pixels = (size_t)screenW * (size_t)screenH;
  maxJpgSize = max((size_t)(32 * 1024), min((size_t)(256 * 1024), pixels / 2 + 16 * 1024));

  if (jpgBuffer) free(jpgBuffer);
  if (netBuffer) free(netBuffer);
  jpgBuffer = (uint8_t*)malloc(maxJpgSize);
  netBuffer = (uint8_t*)malloc(NET_CHUNK_SIZE);
}

String urlEncode(String str) {
  String out = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (isalnum(c) || strchr("-_.~", c)) out += c;
    else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
      out += buf;
    }
  }
  return out;
}

int yPos(float ratio) {
  return (int)(screenH * ratio);
}

void tftCenterMsg(const char* msg, uint16_t color = TFT_CYAN, float yRatio = 0.46f) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(uiTextSize);
  tft.setTextColor(color);
  tft.drawString(msg, centerX, yPos(yRatio));
  tft.setTextDatum(TL_DATUM);
}

void showInfoOnTFT() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(uiTextSize);

  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(TFT_GREEN);  tft.drawString("Da ket noi WiFi", centerX, yPos(0.18f));
    tft.setTextColor(TFT_YELLOW); tft.drawString(WiFi.SSID(), centerX, yPos(0.33f));
    tft.setTextColor(TFT_CYAN);   tft.drawString("Web dieu khien:", centerX, yPos(0.56f));
    tft.setTextColor(TFT_WHITE);  tft.drawString(WiFi.localIP().toString(), centerX, yPos(0.71f));
  } else {
    tft.setTextColor(TFT_YELLOW); tft.drawString("Che do Cai Dat", centerX, yPos(0.16f));
    tft.setTextColor(TFT_CYAN);   tft.drawString("Ket noi WiFi:", centerX, yPos(0.33f));
    tft.setTextColor(TFT_WHITE);  tft.drawString(apSSID, centerX, yPos(0.46f));
    tft.setTextColor(TFT_CYAN);   tft.drawString("Web cai dat:", centerX, yPos(0.65f));
    tft.setTextColor(TFT_GREEN);  tft.drawString(WiFi.softAPIP().toString(), centerX, yPos(0.78f));
  }
  tft.setTextDatum(TL_DATUM);
}

void redirect(const String& loc = "/") {
  server.sendHeader("Location", loc);
  server.send(303);
}

void savePrefs(const char* key, const String& val) {
  prefs.begin("wifi-cfg", false);
  prefs.putString(key, val);
  prefs.end();
}

bool connectSavedWiFi(uint32_t timeoutMs) {
  if (!savedSSID.length()) return false;
  tftCenterMsg("Dang ket noi WiFi...", TFT_WHITE);
  WiFi.disconnect();
  delay(200);
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(300);

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apEnabled = false;
    wifiStatusMsg = "Đã kết nối tới " + savedSSID + " (Đã tắt WiFi cá nhân)";
    return true;
  }
  return false;
}

void scanNetworks() {
  int n = WiFi.scanNetworks();
  scannedOptions = "<option value=''>-- Chọn WiFi quét được --</option>";
  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    if (s.length()) scannedOptions += "<option value='" + s + "'>" + s + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  WiFi.scanDelete();
}

void syncPcIp() {
  if (!server.hasArg("pc_ip")) return;
  String val = server.arg("pc_ip");
  val.trim();
  if (val.length() && val != WiFi.softAPIP().toString() && val != WiFi.localIP().toString()) {
    pcIP = val;
    hasCustomPcIP = true;
    savePrefs("pc_ip", pcIP);
  }
}

void handleRoot() {
  String clientIP = server.client().remoteIP().toString();
  String espLAN = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Chưa có IP LAN";
  String espAP  = apEnabled ? WiFi.softAPIP().toString() : "Đã tắt WiFi phát";

  if (!hasCustomPcIP && !clientIP.startsWith("192.168.4.") && clientIP != espLAN && clientIP != "0.0.0.0") {
    pcIP = clientIP;
  }

  String displayVal = (webPageUrl == "screen") ? "" : webPageUrl;

  String html = R"rawliteral(
  <!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><meta charset="UTF-8">
  <title>ESP32-S3 Control Center</title>
  <style>
    *{box-sizing:border-box} body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#121212;color:#fff}
    h2{text-align:center;margin-bottom:20px;color:#00e676}
    .container{display:flex;flex-wrap:wrap;gap:20px;max-width:1100px;margin:0 auto}
    .panel{flex:1;min-width:310px;background:#1e1e1e;padding:20px;border-radius:10px;border:1px solid #333}
    .panel h3{margin-top:0;border-bottom:1px solid #333;padding-bottom:10px;color:#40c4ff}
    label{display:block;margin-top:12px;margin-bottom:5px;font-size:14px;color:#ccc}
    input,select{width:100%;padding:11px;border-radius:6px;border:1px solid #444;background:#2a2a2a;color:#fff;font-size:15px}
    .btn-group{margin-top:16px;display:flex;flex-wrap:wrap;gap:8px}
    button{padding:11px 16px;font-size:15px;border-radius:6px;border:none;color:#fff;cursor:pointer;font-weight:bold;flex:1;min-width:110px}
    .btn-green{background:#00c853} .btn-blue{background:#2979ff} .btn-red{background:#d50000} .btn-gray{background:#546e7a}
    .status-box{margin-top:15px;padding:10px;background:#262626;border-radius:6px;font-size:14px;line-height:1.6}
    .ip-highlight{color:#00e676;font-weight:bold} .pc-highlight{color:#ffd54f;font-weight:bold}
  </style>
  <script>
    function pickWifi(sel){ if(sel.value) document.getElementById("ssid_input").value = sel.value; }
    function syncIp(form){
      var v = document.getElementById("pc_ip_input").value.trim();
      var h = form.querySelector("input[name='pc_ip']");
      if(h && v) h.value = v;
      return true;
    }
    function goScreen(){
      var ip = encodeURIComponent(document.getElementById("pc_ip_input").value.trim());
      location.href = "/screen?pc_ip=" + ip;
    }
  </script></head><body>
  <h2>ESP32-S3 TFT Display Center</h2>
  <div class="container">
    <div class="panel">
      <h3>1. Kết Nối WiFi</h3>
      <form action="/scan" method="POST" style="margin-bottom:10px">
        <button type="submit" class="btn-gray" style="width:100%">Quét Danh Sách WiFi</button>
      </form>
      <select onchange="pickWifi(this)">)rawliteral" + scannedOptions + R"rawliteral(</select>
      <form action="/wifi" method="POST" onsubmit="return syncIp(this)">
        <input type="hidden" name="pc_ip" value=")rawliteral" + pcIP + R"rawliteral(">
        <label>Tên WiFi (SSID):</label>
        <input type="text" id="ssid_input" name="ssid" value=")rawliteral" + savedSSID + R"rawliteral(" required>
        <label>Mật khẩu WiFi:</label>
        <input type="password" name="pass" value=")rawliteral" + savedPass + R"rawliteral(">
        <div class="btn-group"><button type="submit" class="btn-green">Kết Nối WiFi</button></div>
      </form>
      <div class="status-box">
        <div>Trạng thái: <b>)rawliteral" + wifiStatusMsg + R"rawliteral(</b></div>
        <div>IP của ESP32-S3 (LAN): <span class="ip-highlight">)rawliteral" + espLAN + R"rawliteral(</span></div>
        <div>IP WiFi phát của ESP32-S3: <span>)rawliteral" + espAP + R"rawliteral(</span></div>
        <div>IP thiết bị đang mở web: <span>)rawliteral" + clientIP + R"rawliteral(</span></div>
        <div>Màn hình đang dùng: <span>)rawliteral" + String(screenW) + "x" + String(screenH) + R"rawliteral(</span></div>
      </div>
    </div>
    <div class="panel">
      <h3>2. Phát Video / Ảnh / Chiếu PC</h3>
      <form action="/play" method="POST" onsubmit="return syncIp(this)">
        <input type="hidden" name="pc_ip" value=")rawliteral" + pcIP + R"rawliteral(">
        <label>IP Máy Tính chạy Python (cổng 5000):</label>
        <input type="text" id="pc_ip_input" value=")rawliteral" + pcIP + R"rawliteral(" required>
        <label>Đường dẫn Media (URL):</label>
        <input type="text" name="url" placeholder="https://..." value=")rawliteral" + displayVal + R"rawliteral(" required>
        <div class="btn-group">
          <button type="submit" class="btn-green">Play</button>
          <button type="button" class="btn-blue" onclick="goScreen()">Chieu Man Hinh</button>
          <button type="button" class="btn-red" onclick="location.href='/stop'">Stop</button>
        </div>
      </form>
      <div class="status-box">
        <div>IP Máy tính đang dùng: <span class="pc-highlight">)rawliteral" + pcIP + R"rawliteral(</span></div>
      </div>
    </div>
  </div></body></html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleScan() {
  isStreaming = false;
  scanNetworks();
  redirect();
}

void handleWifiConnect() {
  isStreaming = false;
  syncPcIp();
  if (server.hasArg("ssid")) {
    savedSSID = server.arg("ssid"); savedSSID.trim();
    savedPass = server.arg("pass");
    prefs.begin("wifi-cfg", false);
    prefs.putString("ssid", savedSSID);
    prefs.putString("pass", savedPass);
    prefs.end();

    if (connectSavedWiFi(10000)) {
      showInfoOnTFT();
      redirect("http://" + WiFi.localIP().toString() + "/");
      return;
    }
    wifiStatusMsg = "Kết nối thất bại tới " + savedSSID + ".";
    showInfoOnTFT();
  }
  redirect();
}

void startStreamMode(const String& url, const char* msg) {
  syncPcIp();
  webPageUrl = url;
  isStreaming = true;
  tftCenterMsg(msg, TFT_CYAN);
  redirect();
}

void handlePlay()   { if (server.hasArg("url")) { String u = server.arg("url"); u.trim(); startStreamMode(u, "Dang tai media..."); } else redirect(); }
void handleScreen() { startStreamMode("screen", "Chieu man hinh PC..."); }
void handleStop()   { isStreaming = false; showInfoOnTFT(); redirect(); }

void streamMJPEG(String rawWebUrl) {
  if (WiFi.status() != WL_CONNECTED || !jpgBuffer || !netBuffer) {
    tftCenterMsg("Chua ket noi WiFi!", TFT_RED);
    isStreaming = false;
    return;
  }

  String proxyUrl = "http://" + pcIP + ":" + String(proxyPort) + "/video?url=" + urlEncode(rawWebUrl) +
                    "&w=" + String(screenW) + "&h=" + String(screenH);
  HTTPClient http;
  http.setTimeout(15000);
  http.begin(proxyUrl);
  if (http.GET() != HTTP_CODE_OK) {
    tftCenterMsg("Khong tai duoc!", TFT_RED);
    isStreaming = false;
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  stream->setNoDelay(true);
  size_t jpgIndex = 0;
  bool inJpeg = false;
  uint8_t prevByte = 0;

  tft.startWrite();
  while (http.connected() && isStreaming) {
    server.handleClient();
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(netBuffer, min(avail, (int)NET_CHUNK_SIZE));
      for (int i = 0; i < n; i++) {
        uint8_t b = netBuffer[i];
        if (!inJpeg) {
          if (prevByte == 0xFF && b == 0xD8) { inJpeg = true; jpgBuffer[0] = 0xFF; jpgBuffer[1] = 0xD8; jpgIndex = 2; }
        } else {
          if (jpgIndex < maxJpgSize) jpgBuffer[jpgIndex++] = b;
          else { inJpeg = false; jpgIndex = 0; }
          if (prevByte == 0xFF && b == 0xD9) {
            int drawX = 0, drawY = 0;
            TJpgDec.drawJpg(drawX, drawY, jpgBuffer, jpgIndex);
            inJpeg = false;
            jpgIndex = 0;
          }
        }
        prevByte = b;
      }
    } else {
      delay(1);
    }
  }
  tft.endWrite();
  http.end();
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);
  tft.invertDisplay(true);
  initDisplayMetrics();
  tft.fillScreen(TFT_BLACK);

  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tft_output);

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.softAP(apSSID);

  prefs.begin("wifi-cfg", true);
  savedSSID = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  pcIP = prefs.getString("pc_ip", "192.168.100.64");
  hasCustomPcIP = prefs.isKey("pc_ip");
  prefs.end();

  scannedOptions = "<option value=''>-- Bấm Quét để tìm WiFi --</option>";
  if (savedSSID.length() && !connectSavedWiFi(8000)) {
    wifiStatusMsg = "Không thấy WiFi cũ (" + savedSSID + ").";
  }

  showInfoOnTFT();

  server.on("/", handleRoot);
  server.on("/scan", HTTP_POST, handleScan);
  server.on("/wifi", HTTP_POST, handleWifiConnect);
  server.on("/play", HTTP_POST, handlePlay);
  server.on("/screen", handleScreen);
  server.on("/stop", handleStop);
  server.begin();
}

void loop() {
  server.handleClient();
  if (isStreaming && webPageUrl.length()) streamMJPEG(webPageUrl);
}