/**
 * WiFi Manager Implementation
 * 
 * Handles WiFi connectivity with station and access point modes.
 * Provides a captive portal web page for initial WiFi configuration.
 * Includes mDNS service discovery and NTP time synchronization.
 */

#include "wifi_manager.h"
#include "config_manager.h"
#include <ESPmDNS.h>
#include <time.h>

static WiFiState wifiState = WiFiState::IDLE;
static WebServer *apServer = nullptr;
static bool apMode = false;

// HTML template for WiFi setup captive portal page
static const char apConfigPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>ESP32-S3 CAM WiFi Setup</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;display:flex;align-items:center;justify-content:center}
.card{background:#fff;border-radius:20px;padding:40px;box-shadow:0 20px 60px rgba(0,0,0,0.3);max-width:400px;width:90%}
h1{text-align:center;color:#333;margin-bottom:20px;font-size:1.5em}
.icon{text-align:center;font-size:60px;margin-bottom:15px}
form{display:flex;flex-direction:column;gap:15px}
label{font-weight:bold;color:#555;font-size:0.9em}
input{padding:12px;border:2px solid #ddd;border-radius:10px;font-size:1em;transition:border-color 0.3s}
input:focus{border-color:#667eea;outline:none}
button{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff;border:none;padding:15px;border-radius:30px;font-size:1.1em;font-weight:bold;cursor:pointer;transition:transform 0.3s}
button:hover{transform:scale(1.05)}
.nets{margin-top:15px;padding:15px;background:#f5f5f5;border-radius:10px}
.nets h3{color:#333;margin-bottom:10px;font-size:0.95em}
.nets div{padding:8px;border-bottom:1px solid #eee;cursor:pointer;font-size:0.9em}
.nets div:hover{background:#e0e0ff}
</style></head><body>
<div class='card'><div class='icon'>📶</div>
<h1>ESP32-S3 CAM WiFi Setup</h1>
<form action='/connect' method='POST'>
<label>WiFi Name (SSID)</label>
<input type='text' name='ssid' placeholder='Enter WiFi name' required>
<label>Password</label>
<input type='password' name='password' placeholder='Enter WiFi password'>
<button type='submit'>Connect</button>
</form>
<div class='nets'><h3>Available Networks</h3>
)rawliteral";

static const char apConfigPageEnd[] PROGMEM = R"rawliteral(
</div></div></body></html>
)rawliteral";

/**
 * Handle captive portal root page request
 * Displays WiFi setup form and scans for available networks
 */
static void handleAPRoot() {
    if (!apServer) return;
    String html = FPSTR(apConfigPage);

    int n = WiFi.scanComplete();
    if (n > 0) {
        for (int i = 0; i < n && i < 15; i++) {
            html += "<div onclick=\"document.querySelector('input[name=ssid]').value='";
            html += WiFi.SSID(i);
            html += "'\">";
            html += WiFi.SSID(i);
            html += " (";
            html += WiFi.RSSI(i);
            html += " dBm)";
            html += "</div>";
        }
    } else {
        html += "<div>No networks found</div>";
    }

    html += FPSTR(apConfigPageEnd);
    apServer->send(200, "text/html", html);
}

/**
 * Handle WiFi connection form submission
 * Saves credentials to config and attempts connection
 * Restarts device if connection successful, otherwise returns to AP mode
 */
static void handleAPConnect() {
    if (!apServer) return;
    String ssid = apServer->arg("ssid");
    String password = apServer->arg("password");

    if (ssid.length() == 0) {
        apServer->send(400, "text/plain", "SSID is required");
        return;
    }

    apServer->send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>Connecting...</title>"
        "<style>body{font-family:Arial;background:#667eea;color:#fff;display:flex;align-items:center;justify-content:center;min-height:100vh}"
        ".msg{text-align:center;font-size:1.5em}</style></head>"
        "<body><div class='msg'>Connecting to " + ssid + "...<br>Device will restart if successful.</div></body></html>");

    delay(1000);

    configSetWiFiSSID(ssid.c_str());
    configSetWiFiPassword(password.c_str());
    configSave();

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        ESP.restart();
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
    }
}

/**
 * Initialize WiFi connectivity
 * Attempts to connect to saved WiFi network; falls back to AP mode if no credentials or connection fails
 */
bool wifiInit() {
    String ssid = configGetWiFiSSID();
    String password = configGetWiFiPassword();

    if (ssid.length() > 0) {
        if (wifiConnect(ssid.c_str(), password.c_str())) {
            wifiState = WiFiState::CONNECTED;
            wifiInitTime();
            wifiInitMDNS();
            return true;
        }
    }

    wifiStartAP();
    return false;
}

/**
 * Connect to specified WiFi network with configurable timeout
 * Returns true if connection successful within timeout period
 */
bool wifiConnect(const char *ssid, const char *password, int timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);

    int attempts = 0;
    int maxAttempts = timeoutMs / 200;
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(200);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiState = WiFiState::CONNECTED;
        Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }

    wifiState = WiFiState::IDLE;
    Serial.println("WiFi connection failed");
    return false;
}

/**
 * Start access point mode and captive portal web server
 * Performs background WiFi scan for available networks
 */
void wifiStartAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    wifiState = WiFiState::AP_MODE;
    apMode = true;

    Serial.printf("AP started: %s\n", WiFi.softAPIP().toString().c_str());

    WiFi.scanNetworks(true);

    apServer = new WebServer(80);
    apServer->on("/", handleAPRoot);
    apServer->on("/connect", HTTP_POST, handleAPConnect);
    apServer->begin();
}

/**
 * Handle incoming HTTP requests in AP mode (captive portal)
 */
void wifiHandleAPClient() {
    if (apMode && apServer) {
        apServer->handleClient();
    }
}

/**
 * Get current WiFi connection state
 */
WiFiState wifiGetState() {
    return wifiState;
}

/**
 * Check if device is connected to a WiFi network
 */
bool wifiIsConnected() {
    return WiFi.status() == WL_CONNECTED;
}

/**
 * Get current IP address based on connection state
 * Returns soft AP IP in AP mode, local IP in station mode
 */
String wifiGetIP() {
    if (wifiState == WiFiState::AP_MODE) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

/**
 * Initialize NTP time synchronization
 * Attempts to sync time up to 10 times with 500ms intervals
 */
void wifiInitTime() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
        delay(500);
        attempts++;
    }
}

/**
 * Initialize mDNS/Bonjour service discovery
 * Registers HTTP service on port 80 for easy device discovery
 */
void wifiInitMDNS() {
    if (!MDNS.begin(MDNS_HOSTNAME)) {
        Serial.println("mDNS init failed");
        return;
    }
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS: http://%s.local\n", MDNS_HOSTNAME);
}
