#if !defined(EMULATOR)

#include "Esp32Portal.h"

#include <DNSServer.h>
#include <M5Unified.h>
#include <WebServer.h>
#include <WiFi.h>

#include <string>

#include "../../ui/SetupScreen.h"

namespace {

constexpr const char *AP_NAME = "M5SPOTIFY-SETUP";
constexpr const char *PORTAL_URL = "http://192.168.4.1";

// The form. Plain HTML, styled just enough to be comfortable on a phone.
// Secret fields note that blank means "keep what is stored" — re-running the
// portal to change WiFi must not force re-entering the Spotify token.
const char PAGE_HEAD[] =
    "<!doctype html><html><head><meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    "<title>M5 Spotify Setup</title><style>"
    "body{font-family:-apple-system,system-ui,sans-serif;background:#14161c;"
    "color:#e8e8ea;margin:0;padding:24px}h1{font-size:20px;color:#31d07a}"
    "label{display:block;margin:14px 0 4px;font-size:14px;color:#9aa0ac}"
    "input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;"
    "border:1px solid #333;background:#1e2129;color:#e8e8ea;font-size:16px}"
    "button{margin-top:20px;width:100%;padding:12px;border-radius:8px;"
    "border:0;background:#31d07a;color:#0c2a14;font-size:16px;font-weight:600}"
    "p{font-size:13px;color:#9aa0ac}</style></head><body>"
    "<h1>M5 Spotify Setup</h1>"
    "<p>Enter the WiFi this device should use, and the Spotify app "
    "credentials. Blank secret fields keep their stored values.</p>"
    "<form method='POST' action='/save'>";

// One checkbox per view; names must match ViewManager's registration order.
const char *VIEW_NAMES[8] = {"pixel",     "gameboy", "cyberdeck", "synthwave",
                             "daisy",     "snes",    "nes",       "classic"};

const char PAGE_TAIL[] =
    "<label>Spotify Client ID</label>"
    "<input name='cid' value='%CID%' autocapitalize='off' autocorrect='off'>"
    "<label>Spotify Client Secret (blank = keep)</label>"
    "<input name='csec' autocapitalize='off' autocorrect='off'>"
    "<label>Spotify Refresh Token (blank = keep)</label>"
    "<input name='rtok' autocapitalize='off' autocorrect='off'>"
    "%VIEWS%"
    "<button type='submit'>Save &amp; Reboot</button></form>"
    "<p>Get a refresh token with tools/get_refresh_token.py from the repo, "
    "then paste it here.</p></body></html>";

const char PAGE_SAVED[] =
    "<!doctype html><html><head><meta name='viewport' "
    "content='width=device-width,initial-scale=1'><style>"
    "body{font-family:sans-serif;background:#14161c;color:#e8e8ea;"
    "text-align:center;padding-top:30vh}</style></head><body>"
    "<h1>Saved.</h1><p>The device is rebooting into player mode.</p>"
    "</body></html>";

DNSServer g_dns;
WebServer g_http(80);
DeviceConfig g_current;
std::string g_ssid_options;

std::string htmlEscape(const std::string &in) {
  std::string out;
  for (char c : in) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '\'': out += "&#39;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

void sendForm() {
  std::string page = PAGE_HEAD;
  page += "<label>WiFi network</label><input name='ssid' list='nets' value='";
  page += htmlEscape(g_current.wifi_ssid);
  page += "'><datalist id='nets'>";
  page += g_ssid_options;
  page += "</datalist>";
  page +=
      "<label>WiFi password (blank = open network / keep)</label>"
      "<input name='pass' type='password'>";
  std::string tail = PAGE_TAIL;
  const size_t at = tail.find("%CID%");
  tail.replace(at, 5, htmlEscape(g_current.client_id));
  // View toggles, checked from the stored mask (bit 7 = classic).
  std::string views =
      "<label>Views in the rotation</label><div style='display:grid;"
      "grid-template-columns:1fr 1fr;gap:6px;margin-top:6px'>";
  for (int i = 0; i < 8; ++i) {
    const uint32_t bit = 1u << i;
    views += "<span><input type='checkbox' name='v";
    views += static_cast<char>('0' + i);
    views += "' ";
    if (g_current.views_mask & bit) views += "checked";
    views += "> ";
    views += VIEW_NAMES[i];
    views += "</span>";
  }
  views += "</div>";
  const size_t vat = tail.find("%VIEWS%");
  tail.replace(vat, 7, views);
  page += tail;
  g_http.send(200, "text/html", page.c_str());
}

// Any URL that is not the portal redirects to it. This is what makes phones
// pop the "sign in to network" sheet the moment they join the AP.
void sendRedirect() {
  g_http.sendHeader("Location", PORTAL_URL, true);
  g_http.send(302, "text/plain", "");
}

void handleSave() {
  DeviceConfig next;
  next.wifi_ssid = g_http.arg("ssid").c_str();
  next.wifi_password = g_http.arg("pass").c_str();
  next.client_id = g_http.arg("cid").c_str();
  next.client_secret = g_http.arg("csec").c_str();
  next.refresh_token = g_http.arg("rtok").c_str();

  // Checkboxes: only checked ones are submitted at all.
  uint32_t mask = 0;
  for (int i = 0; i < 8; ++i) {
    char key[3] = {'v', static_cast<char>('0' + i), '\0'};
    if (g_http.hasArg(key)) mask |= 1u << i;
  }
  next.views_mask = mask ? mask : 0x80;  // nothing ticked -> classic only

  // Blank password with an unchanged SSID means "keep the stored password";
  // blank with a NEW ssid genuinely means an open network.
  if (next.wifi_password.empty() && next.wifi_ssid == g_current.wifi_ssid) {
    next.wifi_password = g_current.wifi_password;
  }

  DeviceConfig::save(next);
  g_http.send(200, "text/html", PAGE_SAVED);
  delay(1200);
  ESP.restart();
}

}  // namespace

void runSetupPortal(const DeviceConfig &current) {
  g_current = current;

  // Scan first: the results seed the form's network picker. AP_STA so the
  // scan works while the AP comes up.
  WiFi.mode(WIFI_AP_STA);
  const int n = WiFi.scanNetworks();
  for (int i = 0; i < n && i < 12; ++i) {
    g_ssid_options += "<option value='";
    g_ssid_options += htmlEscape(WiFi.SSID(i).c_str());
    g_ssid_options += "'>";
  }
  WiFi.softAP(AP_NAME);  // open on purpose: a 2-minute setup window
  delay(100);

  g_dns.start(53, "*", WiFi.softAPIP());

  g_http.on("/", sendForm);
  g_http.on("/save", HTTP_POST, handleSave);
  g_http.onNotFound(sendRedirect);  // captive-portal detection endpoints
  g_http.begin();

  Serial.printf("portal    : AP '%s', %s\n", AP_NAME, PORTAL_URL);
  setupscreen::draw(AP_NAME, "192.168.4.1");

  for (;;) {
    g_dns.processNextRequest();
    g_http.handleClient();
    delay(2);
  }
}

#endif  // !EMULATOR
