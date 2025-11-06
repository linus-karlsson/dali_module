#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_wifi.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <string.h>
#include <ctype.h>

#include "web.h"
#include "util.h"
#include "platform.h"
#include "dali.h"
#include "version.h"

static string32_t yuno = {};
static string32_t detail = {};

static string32_t g_name = {};
static string32_t g_adp = {};

static httpd_uri_t log_uri = {};
static httpd_uri_t wifi_uri = {};
static httpd_uri_t root_uri = {};
static httpd_uri_t update_uri = {};
static httpd_uri_t update_page_uri = {};
static httpd_uri_t set_brightness_page_uri = {};
static httpd_uri_t set_brightness_uri = {};
static httpd_uri_t set_wifi_uri = {};

static uint32_t g_log_pointer = 0;
static char g_log_buffer[6 * 1024] = {};

static httpd_handle_t server = NULL;
static esp_netif_t* netif_ap = NULL;
static lsx_timer_handle_t shut_down_timer = NULL;

static dali_config_t g_config = {};

static volatile bool g_wifi_on = true;

static nvs_t g_nvs = {};

void web_log(const char* format, ...)
{
  if (g_wifi_on)
  {
    uint32_t capacity = sizeof(g_log_buffer);

    uint32_t capacity_left = capacity - g_log_pointer;
    if (capacity_left < 256)
    {
      uint32_t i = 0;
      for (uint32_t j = 1024; j < capacity; ++i, ++j)
      {
        g_log_buffer[i] = g_log_buffer[j];
      }
      g_log_pointer = i - capacity_left;
      g_log_buffer[g_log_pointer] = '\0';
    }
    capacity_left = capacity - g_log_pointer;

    va_list args;
    va_start(args, format);

    int32_t length =
      vsnprintf(g_log_buffer + g_log_pointer, capacity_left, format, args) +
      g_log_pointer;

    g_log_pointer = length;
    if (length > capacity - 1)
    {
      g_log_pointer = capacity - 1;
    }
    g_log_buffer[g_log_pointer] = '\0';

    va_end(args);
  }
}

void add_html_(const char* html, char* buffer, size_t buffer_size,
               size_t* buffer_pointer_out)
{
  size_t buffer_pointer = (*buffer_pointer_out);
  size_t html_len = strlen(html);
  size_t size_left = buffer_size - buffer_pointer;
  if (html_len < size_left)
  {
    memcpy(buffer + buffer_pointer, html, html_len);
    buffer_pointer += html_len;
    buffer[buffer_pointer] = '\0';
  }
  (*buffer_pointer_out) = buffer_pointer;
}

#define add_html_d(html) add_html_(html, html_buffer, html_size, &html_pointer)

static size_t home_page_buffer_pointer = 0;
static char home_page_html_buffer[3 * 1024] = {};

void add_home_html(const char* html)
{
  add_html_(html, home_page_html_buffer, sizeof(home_page_html_buffer),
            &home_page_buffer_pointer);
}

void set_home_page(void)
{
  add_home_html("<!DOCTYPE html><html><head>\n");
  add_home_html(
    "<meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=yes'>\n");
  add_home_html("<title>DALI</title>\n");
  add_home_html("<style>\n");
  add_home_html(
    "body { display:flex; flex-direction:column; justify-content:center; align-items:center; "
    "height:100vh; margin:0; background-color:#121212; color:#E0E0E0; "
    "font-family:Arial,sans-serif; }\n");

  add_home_html(
    "#main { width:90%; max-width:400px; background:#1E1E1E; padding:20px; border-radius:12px; "
    "box-shadow:0 4px 10px rgba(0,0,0,0.6); display:flex; flex-direction:column; "
    "gap:20px; align-items:center; }\n");

  add_home_html(
    "button { width:100%; background:#2196F3; color:white; border:none; padding:15px; "
    "border-radius:6px; font-size:18px; cursor:pointer; transition:background 0.2s ease, transform 0.1s ease; }\n");

  add_home_html("button:hover { background:#1976D2; transform:scale(1.02); }\n");

  add_home_html("</style></head><body>\n");

  add_home_html("<div id='main'>\n");
  add_home_html("<h2 style='color:#2196F3;'>DALI v");
  add_home_html(VERSION);
  add_home_html("</h2>\n");
  add_home_html(
    "<button onclick=\"window.location.href='/setPage'\">Set Scenes</button>\n");
  add_home_html(
    "<button onclick=\"window.location.href='/updatePage'\">Update Firmware</button>\n");
  add_home_html(
    "<button onclick=\"window.location.href='/wifiPage'\">Wifi Config</button>\n");
  add_home_html("</div>\n");

  add_home_html("<script>\n");
  add_home_html("</script>\n");

  add_home_html("</body></html>\n");
}

static size_t update_page_buffer_pointer = 0;
static char update_page_html_buffer[8 * 1024] = {};

void add_update_html(const char* html)
{
  add_html_(html, update_page_html_buffer, sizeof(update_page_html_buffer),
            &update_page_buffer_pointer);
}

void set_update_page(void)
{
  add_update_html("<!DOCTYPE html> <html>\n");
  add_update_html(
    "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">\n");
  add_update_html("<title>Update</title>\n");
  add_update_html("<style>\n");

  add_update_html(
    ".background-card { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); "
    "width: 95%; max-width: 500px; background: rgba(30, 30, 30, 0.95); "
    "border-radius: 16px; box-shadow: 0 8px 20px rgba(0,0,0,0.7); "
    "padding: 10px; display: flex; flex-direction: column; align-items: center; "
    "backdrop-filter: blur(8px); border: 1px solid rgba(255,255,255,0.05); }\n");

  add_update_html(
    "body { display: flex; flex-direction: column; justify-content: center; align-items: center; "
    "height: 100vh; margin: 0; background-color: #121212; color: #E0E0E0; "
    "font-family: Arial, sans-serif; }\n");

  add_update_html(
    "#upload-form { width: 90%; max-width: 400px; background: #1E1E1E; padding: 20px; "
    "border-radius: 12px; box-shadow: 0 4px 10px rgba(0,0,0,0.6); display: flex; "
    "flex-direction: column; gap: 15px; align-items: center; }\n");

  add_update_html(
    "input[type='file'] { width: 100%; font-size: 16px; padding: 10px; border: 1px solid #555; "
    "border-radius: 6px; background: #2A2A2A; color: #E0E0E0; }\n");
  add_update_html(
    "input[type='file']::file-selector-button { background: #2196F3; color: white; border: none; "
    "padding: 8px 12px; border-radius: 5px; cursor: pointer; }\n");
  add_update_html(
    "input[type='file']::file-selector-button:hover { background: #1976D2; }\n");

  add_update_html(
    "input[type='submit'] { background: #2196F3; color: white; border: none; padding: 12px 20px; "
    "border-radius: 6px; cursor: pointer; font-size: 18px; width: 100%; transition: background 0.2s ease; }\n");
  add_update_html("input[type='submit']:hover { background: #1976D2; }\n");

  add_update_html(
    "#prg-container { width: 90%; max-width: 400px; background-color: #2A2A2A; border-radius: 8px; "
    "overflow: hidden; margin-top: 10px; box-shadow: 0 2px 6px rgba(0,0,0,0.5); }\n");
  add_update_html(
    "#prg { width: 0%; background-color: #2196F3; padding: 10px; color: white; text-align: center; "
    "font-size: 16px; transition: width 0.3s ease-in-out; }\n");

  add_update_html(
    "#file-input { width: 90%; max-width: 400px; font-size: 16px; padding: 12px; "
    "border: 1px solid #555; border-radius: 6px; margin-top: 10px; background: #2A2A2A; color: #E0E0E0; }\n");

  add_update_html(
    "#update-button { width: 90%; max-width: 400px; background-color: #2196F3; color: white; "
    "border: none; padding: 12px 20px; border-radius: 6px; font-size: 18px; cursor: pointer; "
    "margin-top: 10px; transition: background 0.2s ease; }\n");
  add_update_html("#update-button:hover { background-color: #1976D2; }\n");

  add_update_html(
    ".sidebar { position: fixed; top: 10px; left: 10px; z-index: 10; }\n");
  add_update_html(
    ".menu-btn { background-color: #2196F3; color: white; border: none; padding: 10px 15px; "
    "border-radius: 5px; font-size: 20px; cursor: pointer; box-shadow: 0 2px 5px rgba(0,0,0,0.4); }\n");
  add_update_html(
    ".menu-content { display: none; flex-direction: column; background: #1E1E1E; border-radius: 10px; "
    "box-shadow: 0 4px 8px rgba(0,0,0,0.6); margin-top: 10px; }\n");
  add_update_html(
    ".menu-content a { padding: 10px 20px; text-decoration: none; color: white; background: #2196F3; "
    "border-radius: 5px; margin: 5px; text-align: center; transition: background 0.2s ease; }\n");
  add_update_html(".menu-content a:hover { background: #1976D2; }\n");

  add_update_html("</style>\n");

  add_update_html("</head>\n");
  add_update_html("<body>\n");

  add_update_html("<div id='sidebar' class='sidebar'>\n");
  add_update_html(
    "<button class='menu-btn' onclick='toggleMenu()'>&#9776;</button>\n");
  add_update_html("<div id='menu-content' class='menu-content'>\n");
  add_update_html("<a href='/'>Home</a>\n");
  add_update_html("<a href='/setPage'>Set Page</a>\n");
  add_update_html("<a href='/wifiPage'>Wifi Page</a>\n");
  add_update_html("</div></div>\n");

  add_update_html("<div class='background-card'>\n");

  add_update_html(
    "<div id='log-container' style='width:90%;max-width:400px;height:300px;overflow-y:auto;background:#1E1E1E;color:white;font-family:Arial;font-size:16px;border-radius:8px;padding:10px;margin-top:15px;box-shadow:0 2px 6px rgba(0,0,0,0.5);'></div>\n");

  add_update_html("<input type='file' id='file-input'>\n");
  add_update_html(
    "<button id='update-button' onclick='uploadFirmware()'>Uppdatera</button>\n");

  add_update_html("<div id='prg-container'>\n");
  add_update_html("<div id='prg'>0%</div>\n");
  add_update_html("</div>\n");

  add_update_html("</div>\n");

  add_update_html("<script>\n");

  add_update_html("var autoScroll = true;\n");
  add_update_html("var log = document.getElementById('log-container');\n");

  add_update_html("log.addEventListener('scroll', function(){\n");
  add_update_html(
    "  autoScroll = (log.scrollHeight - log.scrollTop - log.clientHeight) < 10;\n");
  add_update_html("});\n");

  add_update_html("function logMessage(msg){\n");
  add_update_html("  msg = msg.replace(/\\n/g, '<br>');\n");
  add_update_html("  log.innerHTML = msg+'<br>';\n");
  add_update_html("  if(autoScroll){ log.scrollTop = log.scrollHeight; }\n");
  add_update_html("}\n");

  add_update_html("setInterval(function(){\n");
  add_update_html("  fetch('/log').then(r => r.text()).then(t => {\n");
  add_update_html("    if(t.trim().length) logMessage(t);\n");
  add_update_html("  });\n");
  add_update_html("}, 1000);\n");

  add_update_html("function toggleMenu(){\n");
  add_update_html(" var menu=document.getElementById('menu-content');\n");
  add_update_html(
    " menu.style.display=(menu.style.display==='flex')?'none':'flex';\n");
  add_update_html("}\n");

  add_update_html("var prg = document.getElementById('prg');\n");
  add_update_html("function uploadFirmware() {\n");
  add_update_html("  var fileInput = document.getElementById('file-input');\n");
  add_update_html("  if (!fileInput.files.length) return;\n");
  add_update_html("  var file = fileInput.files[0];\n");
  add_update_html("  var reader = new FileReader();\n");
  add_update_html("  reader.onload = function(e) {\n");
  add_update_html("    var req = new XMLHttpRequest();\n");
  add_update_html("    req.open('POST', '/update');\n");
  add_update_html(
    "    req.setRequestHeader('Content-Type', 'application/octet-stream');\n");
  add_update_html("    req.upload.addEventListener('progress', function(p) {\n");
  add_update_html("      if(p.lengthComputable){\n");
  add_update_html("        let w = Math.round((p.loaded / p.total)*100) + '%';\n");
  add_update_html("        prg.innerHTML = w;\n");
  add_update_html("        prg.style.width = w;\n");
  add_update_html("        if(w == '100%') prg.style.backgroundColor = '#04AA6D';\n");
  add_update_html("      }\n");
  add_update_html("    });\n");
  add_update_html("    req.send(e.target.result);\n");
  add_update_html("  };\n");
  add_update_html("  reader.readAsArrayBuffer(file);\n");
  add_update_html("}\n");
  add_update_html("</script>\n");
  add_update_html("</body></html>\n");
}

char* send_html_inputs(size_t* html_pointer_out)
{
  (*html_pointer_out) = 0;

  size_t html_pointer = 0;
  size_t html_size = 10 * 1024;
  char* html_buffer = (char*)calloc(html_size, sizeof(char));

  if (html_buffer == NULL)
  {
    return html_buffer;
  }

  add_html_d("<!DOCTYPE html><html>\n");
  add_html_d(
    "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">\n");
  add_html_d("<title>Scene Config</title>\n");
  add_html_d("<style>\n");

  add_html_d(
    "body { display: flex; flex-direction: column; justify-content: center; align-items: center; "
    "height: 100vh; margin: 0; background-color: #121212; color: #E0E0E0; "
    "font-family: Arial, sans-serif; }\n");

  add_html_d(
    "#input-list { width: 90%; max-width: 450px; background: #1E1E1E; padding: 20px; "
    "border-radius: 12px; box-shadow: 0 4px 8px rgba(0,0,0,0.5); display: flex; flex-direction: column; gap: 20px; }\n");

  add_html_d(
    ".section-box { background: #2A2A2A; padding: 20px; border-radius: 15px; "
    "box-shadow: 0 4px 10px rgba(0,0,0,0.4); width: 100%; box-sizing: border-box; }\n");
  add_html_d(".section-box + .section-box { margin-top: 25px; }\n");
  add_html_d(
    ".section-box h2 { text-align: center; color: #64B5F6; margin-top: 0; }\n");

  add_html_d(
    ".input-item { display: flex; justify-content: space-between; align-items: center; }\n");
  add_html_d(".input-item label { font-size: 16px; color: #E0E0E0; }\n");
  add_html_d(
    ".input-item input { width: 100px; padding: 8px; border: 1px solid #555; "
    "border-radius: 5px; text-align: center; font-size: 16px; background: #1E1E1E; color: #E0E0E0; }\n");
  add_html_d(".input-item input:focus { outline: none; border-color: #64B5F6; }\n");

  add_html_d(
    "#submit-btn { background: #2196F3; color: white; border: none; padding: 12px 20px; "
    "border-radius: 5px; cursor: pointer; font-size: 18px; margin-top: 15px; transition: background 0.2s ease; }\n");
  add_html_d("#submit-btn:hover { background: #1976D2; }\n");

  add_html_d(".sidebar { position: fixed; top: 10px; left: 10px; z-index: 10; }\n");
  add_html_d(
    ".menu-btn { background-color: #2196F3; color: white; border: none; padding: 10px 15px; "
    "border-radius: 5px; font-size: 20px; cursor: pointer; }\n");
  add_html_d(
    ".menu-content { display: none; flex-direction: column; background: #1E1E1E; border-radius: 10px; "
    "box-shadow: 0 4px 8px rgba(0,0,0,0.4); margin-top: 10px; }\n");
  add_html_d(
    ".menu-content a { padding: 10px 20px; text-decoration: none; color: white; "
    "background: #2196F3; border-radius: 5px; margin: 5px; text-align: center; transition: background 0.2s ease; }\n");
  add_html_d(".menu-content a:hover { background: #1976D2; }\n");

  add_html_d(
    ".bit-display { display: flex; gap: 6px; margin-left: 6px; }\n"
    ".circle { width: 14px; height: 14px; border-radius: 50%; border: 2px solid #999; display: inline-block; }\n"
    ".circle.filled { background-color: #64B5F6; border-color: #64B5F6; }\n"
    ".circle.empty { background-color: transparent; border-color: #555; }\n");

  add_html_d(".bit-explanation h3 { color: #64B5F6; margin-bottom: 8px; }\n");

  add_html_d("</style>\n");
  add_html_d("</head>\n<body>\n");

  add_html_d("<div id='sidebar' class='sidebar'>\n");
  add_html_d("<button class='menu-btn' onclick='toggleMenu()'>&#9776;</button>\n");
  add_html_d("<div id='menu-content' class='menu-content'>\n");
  add_html_d("<a href='/'>Home</a>\n");
  add_html_d("<a href='/updatePage'>Update Page</a>\n");
  add_html_d("<a href='/wifiPage'>Wifi Page</a>\n");
  add_html_d("</div></div>\n");

  add_html_d("<div id='input-list'>\n");

  add_html_d("<div class='section-box'>\n");
  add_html_d("<h2>Turn Off Blink</h2>\n");

  add_html_d("<div class='input-item'>");

  add_html_d("<label for='fadeTime'>Fade Time (s):</label>");
  {
    string256_t temp = {};
    string256(
      &temp,
      "<input type='number' id='fadeTime' name='fadeTime' min='0' max='8' value='%lu'></div>\n",
      g_config.fade_time);
    add_html_d(temp.data);
  }

  add_html_d("<div class='input-item'><label for='blinkEnable'>Enable:</label>");
  if (g_config.blink_enabled)
  {
    add_html_d("<input type='checkbox' id='blinkEnable' name='blinkEnable' checked>");
  }
  else
  {
    add_html_d("<input type='checkbox' id='blinkEnable' name='blinkEnable'>");
  }

  add_html_d("<label for='blinkTimer'>Duration (s):</label>");

  {
    string256_t temp = {};
    string256(
      &temp,
      "<input type='number' id='blinkTimer' name='blinkTimer' min='0' max='1200' value='%lu'></div>\n",
      g_config.blink_duration);
    add_html_d(temp.data);
  }

  add_html_d("</div>\n");

  add_html_d("<div class='section-box'>\n");
  add_html_d("<h2>Enter Brightness (0-100)</h2>\n");

  for (uint32_t i = 1; i <= 8; i++)
  {
    uint32_t bits = i - 1;
    uint32_t b0 = (bits >> 2) & 1;
    uint32_t b1 = (bits >> 1) & 1;
    uint32_t b2 = bits & 1;

    const char* c2 = b2 ? "filled" : "empty";
    const char* c1 = b1 ? "filled" : "empty";
    const char* c0 = b0 ? "filled" : "empty";

    string512_t temp = {};
    string512(
      &temp,
      "<div class='input-item'>"
      "<label for='num%lu'>Scene %lu:</label>"
      "<input type='number' id='num%lu' name='num%lu' min='0' max='100' value='%u'>"
      "<div class='bit-display'>"
      "<span class='circle %s'></span>"
      "<span class='circle %s'></span>"
      "<span class='circle %s'></span>"
      "</div>"
      "</div>\n",
      i, i, i, i, g_config.scenes[i - 1], c2, c1, c0);
    add_html_d(temp.data);
  }

  add_html_d(
    "<div class='bit-explanation'>"
    "<h3>Input Explanation</h3>"
    "<div class='bit-display'>"
    "<span class='circle filled'></span><span class='bit-label'>Input 1</span>"
    "<span class='circle filled'></span><span class='bit-label'>Input 2</span>"
    "<span class='circle filled'></span><span class='bit-label'>Input 3</span>"
    "</div>"
    "</div>\n");

  add_html_d("</div>\n");

  add_html_d("<button id='submit-btn' onclick='submitValues()'>Save</button>\n");
  add_html_d("</div>\n");

  add_html_d("<script>\n");

  add_html_d("function toggleMenu(){\n");
  add_html_d(" var menu=document.getElementById('menu-content');\n");
  add_html_d(" menu.style.display=(menu.style.display==='flex')?'none':'flex';\n");
  add_html_d("}\n");

  add_html_d("function submitValues(){\n");
  add_html_d("  let values = [];\n");
  add_html_d("  for(let i=1;i<=8;i++){\n");
  add_html_d("    let val = document.getElementById('num'+i).value;\n");
  add_html_d(
    "    if(val<0||val>100){alert('Values must be between 0 and 100');return;}\n");
  add_html_d("    values.push(val);\n");
  add_html_d("  }\n");
  add_html_d("  // Get blink settings\n");
  add_html_d(
    "  let blinkEnable = document.getElementById('blinkEnable').checked ? 1 : 0;\n");
  add_html_d("  let blinkTimer = document.getElementById('blinkTimer').value;\n");
  add_html_d("  let fadeTime = document.getElementById('fadeTime').value;\n");
  add_html_d(
    "  if(blinkTimer < 0){ alert('Blink timer must be positive'); return; }\n");
  add_html_d("  let query = values.map((v,i)=>'v'+(i+1)+'='+v).join('&');\n");
  add_html_d(
    "  query += '&blinkEnable=' + blinkEnable + '&blinkTimer=' + blinkTimer + '&fadeTime=' + fadeTime;\n");
  add_html_d("  let xhr = new XMLHttpRequest();\n");
  add_html_d("  xhr.open('GET','/setValues?'+query,true);\n");
  add_html_d("  xhr.send();\n");
  add_html_d("  alert('Values sent!');\n");
  add_html_d("}\n");
  add_html_d("</script>\n");

  add_html_d("</body></html>\n");

  (*html_pointer_out) = html_pointer;
  return html_buffer;
}

char* send_html_wifi(size_t* html_pointer_out)
{
  (*html_pointer_out) = 0;

  size_t html_pointer = 0;
  size_t html_size = 6 * 1024;
  char* html_buffer = (char*)calloc(html_size, sizeof(char));

  if (html_buffer == NULL)
  {
    return html_buffer;
  }

  add_html_d("<!DOCTYPE html><html>\n");
  add_html_d(
    "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
  add_html_d("<title>WiFi Config</title>\n");
  add_html_d("<style>\n");

  add_html_d(".sidebar { position: fixed; top: 10px; left: 10px; z-index: 10; }\n");
  add_html_d(
    ".menu-btn { background-color: #2196F3; color: white; border: none; padding: 10px 15px; "
    "border-radius: 5px; font-size: 20px; cursor: pointer; }\n");
  add_html_d(
    ".menu-content { display: none; flex-direction: column; background: #1E1E1E; border-radius: 10px; "
    "box-shadow: 0 4px 8px rgba(0,0,0,0.4); margin-top: 10px; }\n");
  add_html_d(
    ".menu-content a { padding: 10px 20px; text-decoration: none; color: white; "
    "background: #2196F3; border-radius: 5px; margin: 5px; text-align: center; transition: background 0.2s ease; }\n");
  add_html_d(".menu-content a:hover { background: #1976D2; }\n");

  add_html_d(
    "body { display: flex; justify-content: center; align-items: center; height: 100vh; "
    "margin: 0; background-color: #121212; color: #E0E0E0; font-family: Arial, sans-serif; }\n");

  add_html_d(
    "#wifi-form { width: 90%; max-width: 400px; background: #1E1E1E; padding: 20px; "
    "border-radius: 12px; box-shadow: 0 4px 8px rgba(0,0,0,0.5); display: flex; flex-direction: column; gap: 20px; }\n");

  add_html_d(".input-item { display: flex; flex-direction: column; }\n");
  add_html_d(".input-item label { font-size: 16px; margin-bottom: 8px; }\n");
  add_html_d(
    ".input-item input { padding: 10px; border: 1px solid #555; border-radius: 5px; "
    "font-size: 16px; background: #2A2A2A; color: #E0E0E0; }\n");
  add_html_d(".input-item input:focus { outline: none; border-color: #64B5F6; }\n");

  add_html_d(
    "#submit-btn { background: #2196F3; color: white; border: none; padding: 12px 20px; "
    "border-radius: 5px; cursor: pointer; font-size: 18px; transition: background 0.2s ease; }\n");
  add_html_d("#submit-btn:hover { background: #1976D2; }\n");

  add_html_d(
    "#restart-btn { background: #2196F3; color: white; border: none; padding: 12px 20px; "
    "border-radius: 5px; cursor: pointer; font-size: 18px; transition: background 0.2s ease; }\n");
  add_html_d("#restart-btn:hover { background: #1976D2; }\n");

  add_html_d("</style>\n</head>\n<body>\n");

  add_html_d("<div id='sidebar' class='sidebar'>\n");
  add_html_d("<button class='menu-btn' onclick='toggleMenu()'>&#9776;</button>\n");
  add_html_d("<div id='menu-content' class='menu-content'>\n");
  add_html_d("<a href='/'>Home</a>\n");
  add_html_d("<a href='/setPage'>Set Page</a>\n");
  add_html_d("<a href='/updatePage'>Update Page</a>\n");
  add_html_d("</div></div>\n");

  add_html_d("<div id='wifi-form'>\n");
  add_html_d("<div class='input-item'>\n");
  add_html_d("<label for='wname'>WiFi Name:</label>\n");
  add_html_d(
    "<input type='text' id='wname' name='wname' placeholder='Enter WiFi Name' value='");
  add_html_d(g_name.data);
  add_html_d("'>\n");
  add_html_d("</div>\n");
#if 0
  add_html_d("<div class='input-item'>\n");
  add_html_d("<label for='appd'>WiFi Password:</label>\n");
  add_html_d(
    "<input type='text' id='appd' name='appd' placeholder='Enter WiFi Password' value='");
  add_html_d(g_adp.data);
  add_html_d("'>\n");
  add_html_d("</div>\n");
#endif
  add_html_d("<button id='submit-btn' onclick='submitWiFi()'>Save</button>\n");
  add_html_d("<button id='restart-btn' onclick='restart()'>Restart</button>\n");
  add_html_d("</div>\n");

  add_html_d("<script>\n");

  add_html_d("function toggleMenu(){\n");
  add_html_d(" var menu=document.getElementById('menu-content');\n");
  add_html_d(" menu.style.display=(menu.style.display==='flex')?'none':'flex';\n");
  add_html_d("}\n");

  add_html_d("function restart(){\n");
  add_html_d("  const xhr = new XMLHttpRequest();\n");
  add_html_d("  xhr.open('GET', '/setWiFi?restart=1', true);\n");
  add_html_d("  xhr.send();\n");
  add_html_d("  alert('Restart sent!');\n");
  add_html_d("}\n");

  add_html_d("function submitWiFi(){\n");
  add_html_d("  const name = document.getElementById('wname').value;\n");
  add_html_d("  const app = document.getElementById('appd').value;\n");
  add_html_d("  const xhr = new XMLHttpRequest();\n");
  add_html_d(
    "  xhr.open('GET', '/setWiFi?name=' + encodeURIComponent(name) + '&pass=' + encodeURIComponent(app), true);\n");
  add_html_d("  xhr.send();\n");
  add_html_d("  alert('WiFi config sent!');\n");
  add_html_d("}\n");
  add_html_d("</script>\n");

  add_html_d("</body></html>\n");

  (*html_pointer_out) = html_pointer;
  return html_buffer;
}

esp_err_t log_handler(httpd_req_t* req)
{
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, g_log_buffer);
  return ESP_OK;
}

esp_err_t root_get_handler(httpd_req_t* request)
{
  httpd_resp_send(request, home_page_html_buffer, home_page_buffer_pointer);
  return ESP_OK;
}

esp_err_t update_page_handler(httpd_req_t* request)
{
  httpd_resp_send(request, update_page_html_buffer, update_page_buffer_pointer);
  return ESP_OK;
}

esp_err_t set_brigthness_post_handler(httpd_req_t* request)
{
  size_t html_pointer = 0;
  char* html = send_html_inputs(&html_pointer);
  if (html)
  {
    httpd_resp_send(request, html, html_pointer);
    free(html);
  }
  return ESP_OK;
}

esp_err_t wifi_config_post_handler(httpd_req_t* request)
{
  size_t html_pointer = 0;
  char* html = send_html_wifi(&html_pointer);
  if (html)
  {
    httpd_resp_send(request, html, html_pointer);
    free(html);
  }
  return ESP_OK;
}

esp_err_t handle_set_values(httpd_req_t* req)
{
  char query[128] = {};
  size_t query_len = httpd_req_get_url_query_len(req) + 1;

  if (query_len > sizeof(query))
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Query too long");
    return ESP_FAIL;
  }

  const char* response = "Error setting scenes";
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
  {
    char param[8];
    uint8_t values[8] = { 0 };

    for (int i = 1; i <= 8; i++)
    {
      char key[4];
      snprintf(key, sizeof(key), "v%d", i);
      if (httpd_query_key_value(query, key, param, sizeof(param)) == ESP_OK)
      {
        values[i - 1] = (uint8_t)atoi(param);
      }
    }
    int32_t blink_enable = 0;
    if (httpd_query_key_value(query, "blinkEnable", param, sizeof(param)) == ESP_OK)
    {
      blink_enable = atoi(param);
    }

    int32_t blink_timer = 0;
    if (httpd_query_key_value(query, "blinkTimer", param, sizeof(param)) == ESP_OK)
    {
      blink_timer = atoi(param);
    }

    int32_t fade_time = 0;
    if (httpd_query_key_value(query, "fadeTime", param, sizeof(param)) == ESP_OK)
    {
      fade_time = atoi(param);
    }

    dali_config_t temp_config = {};
    temp_config.blink_enabled = (uint8_t)blink_enable;
    temp_config.fade_time = (uint8_t)fade_time;
    temp_config.blink_duration = (uint32_t)blink_timer;
    memcpy(temp_config.scenes, values, sizeof(temp_config.scenes));
    if (dali_set_config(temp_config))
    {
      g_config = temp_config;
      response = "Scene set successfully";
    }
  }
  httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static void url_decode(char* dst, const char* src)
{
  char a, b;
  while (*src)
  {
    if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
        (isxdigit(a) && isxdigit(b)))
    {
      if (a >= 'a') a -= 'a' - 'A';
      if (a >= 'A')
        a -= ('A' - 10);
      else
        a -= '0';
      if (b >= 'a') b -= 'a' - 'A';
      if (b >= 'A')
        b -= ('A' - 10);
      else
        b -= '0';
      *dst++ = 16 * a + b;
      src += 3;
    }
    else if (*src == '+')
    {
      *dst++ = ' ';
      src++;
    }
    else
    {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

esp_err_t handle_set_wifi(httpd_req_t* req)
{
  char query[128] = {};
  size_t query_len = httpd_req_get_url_query_len(req) + 1;

  if (query_len > sizeof(query))
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Query too long");
    return ESP_FAIL;
  }

  const char* response = "Success";
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
  {
    char wifi_name[32] = { 0 };
    char decoded[32] = { 0 };
    if (httpd_query_key_value(query, "name", wifi_name, sizeof(wifi_name) - 1) ==
        ESP_OK)
    {
      url_decode(decoded, wifi_name);
      if (strlen(decoded) > 0)
      {
        lsx_nvs_set_string(&g_nvs, "ADALN", decoded);
      }
    }

    memset(wifi_name, 0, sizeof(wifi_name));
    memset(decoded, 0, sizeof(decoded));
    if (httpd_query_key_value(query, "pass", wifi_name, sizeof(wifi_name) - 1) ==
        ESP_OK)
    {
      url_decode(decoded, wifi_name);
      if (strlen(decoded) > 0)
      {
        lsx_nvs_set_string(&g_nvs, "ADALP", decoded);
      }
    }
    if (httpd_query_key_value(query, "restart", wifi_name, sizeof(wifi_name) - 1) ==
        ESP_OK)
    {
      esp_restart();
    }
  }

  httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t update_post_handler(httpd_req_t* req)
{
  light_control_remove_interrupt();
  char buffer[1024] = {};

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);

  esp_ota_handle_t ota_handle = 0;
  esp_err_t error =
    esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);

  if (error != ESP_OK)
  {
    light_control_add_interrupt();
    return ESP_FAIL;
  }
  bool all_good = true;
  size_t total_len = req->content_len;
  while (total_len > 0)
  {
    int32_t received = httpd_req_recv(req, buffer, min(total_len, sizeof(buffer)));
    if (received <= 0)
    {
      all_good = false;
      break;
    }
    if (esp_ota_write(ota_handle, (const void*)buffer, received) != ESP_OK)
    {
      all_good = false;
      break;
    }
    total_len -= received;
  }
  if (all_good && esp_ota_end(ota_handle) == ESP_OK)
  {
    if (esp_ota_set_boot_partition(update_partition) == ESP_OK)
    {
      esp_restart();
      light_control_add_interrupt();
      return ESP_OK;
    }
  }
  esp_ota_abort(ota_handle);
  light_control_add_interrupt();
  return ESP_FAIL;
}

esp_err_t start_webserver(void)
{
  root_uri.uri = "/";
  root_uri.method = HTTP_GET;
  root_uri.handler = root_get_handler;

  update_page_uri.uri = "/updatePage";
  update_page_uri.method = HTTP_GET;
  update_page_uri.handler = update_page_handler;

  update_uri.uri = "/update";
  update_uri.method = HTTP_POST;
  update_uri.handler = update_post_handler;

  set_brightness_page_uri.uri = "/setPage";
  set_brightness_page_uri.method = HTTP_GET;
  set_brightness_page_uri.handler = set_brigthness_post_handler;

  set_brightness_uri.uri = "/setValues";
  set_brightness_uri.method = HTTP_GET;
  set_brightness_uri.handler = handle_set_values;

  set_wifi_uri.uri = "/setWiFi";
  set_wifi_uri.method = HTTP_GET;
  set_wifi_uri.handler = handle_set_wifi;

  wifi_uri.uri = "/wifiPage";
  wifi_uri.method = HTTP_GET;
  wifi_uri.handler = wifi_config_post_handler;

  log_uri.uri = "/log";
  log_uri.method = HTTP_GET;
  log_uri.handler = log_handler;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_start(&server, &config);
  httpd_register_uri_handler(server, &log_uri);
  httpd_register_uri_handler(server, &wifi_uri);
  httpd_register_uri_handler(server, &set_wifi_uri);
  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &update_page_uri);
  httpd_register_uri_handler(server, &update_uri);
  httpd_register_uri_handler(server, &set_brightness_page_uri);
  httpd_register_uri_handler(server, &set_brightness_uri);
  return ESP_OK;
}

void web_shutdown_callback(void* arguments)
{
  g_wifi_on = false;

  printf("Shutting down Wi-Fi and network...\n");

  if (server)
  {
    httpd_stop(server);
    server = NULL;
  }
  if (netif_ap)
  {
    esp_netif_dhcps_stop(netif_ap);
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy(netif_ap);

    esp_netif_deinit();
    esp_event_loop_delete_default();

    netif_ap = NULL;
  }
  printf("Wi-Fi shut down complete.\n");
}

void web_uninitialize(void)
{
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_event_loop_delete_default();
  esp_netif_deinit();
}

bool g_start_wifi = false;

void start_wifi(void)
{
  g_start_wifi = true;
}

char g_uid[32] = {};

void web_task(void* parameters)
{
  while (!g_start_wifi)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  lsx_log("Web start\n");

  lsx_nvs_open(&g_nvs, "WIFI_NVS");

  set_home_page();
  set_update_page();

  uint32_t uid_len = strlen(g_uid);
  uint32_t iterations = min(uid_len, 16);
  uint32_t extra = 0;
  for (uint32_t i = 0; i < iterations; ++i)
  {
    extra += ch_int(g_uid[i], i, 0x34274A0E);
  }

  setting_string2(&detail, extra, 5, o_c('L', 0, extra), o_c('S', 1, extra),
                  o_c('X', 2, extra), o_c('D', 3, extra), o_c('_', 4, extra));

  memcpy(detail.data + detail.length, g_uid + 10, uid_len - 10);
  detail.length += uid_len - 10;
  detail.data[detail.length] = '\0';

  setting_string2(&yuno, extra, 3, o_c('L', 0, extra), o_c('i', 1, extra),
                  o_c('g', 2, extra), o_c('h', 3, extra), o_c('t', 5, extra),
                  o_c('_', 6, extra), o_c('s', 7, extra), o_c('Y', 8, extra),
                  o_c('s', 9, extra), o_c('?', 10, extra), o_c('3', 11, extra),
                  o_c('4', 12, extra), o_c('8', 13, extra));

  esp_netif_init();
  esp_event_loop_create_default();

  esp_netif_create_default_wifi_ap();

  netif_ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (netif_ap)
  {
    esp_netif_dhcps_stop(netif_ap);

    memset(&g_name, 0, sizeof(g_name));
    if (!lsx_nvs_get_string(&g_nvs, "ADALN", g_name.data, &g_name.length,
                            sizeof(g_name.data) - 1))
    {
      string32_copy(&g_name, &detail);
    }
    else
    {
      string32_set_length(&g_name);
    }

#if 0
    memset(&g_adp, 0, sizeof(g_adp));
    if (lsx_nvs_get_string(&g_nvs, "ADALP", g_adp.data, &g_adp.length,
                           sizeof(g_adp.data) - 1))
    {
      string32_set_length(&g_adp);
    }
#endif

    string32_copy(&g_adp, &yuno);

    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton("10.10.10.1");
    ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    ip_info.gw.addr = ip_info.ip.addr;

    esp_netif_set_ip_info(netif_ap, &ip_info);
    esp_netif_dhcps_start(netif_ap);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_config);

    wifi_config_t wifi_config = {};
    memcpy(wifi_config.ap.ssid, g_name.data, g_name.length);
    wifi_config.ap.ssid_len = g_name.length;
    memcpy(wifi_config.ap.password, g_adp.data, g_adp.length);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 1;
    if (g_adp.length > 0)
    {
      wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    else
    {
      wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    start_webserver();

    shut_down_timer = lsx_timer_create(web_shutdown_callback, NULL);
    uint64_t fifteen_min = 15ULL * 60ULL * 1000000ULL;
    lsx_timer_start(shut_down_timer, fifteen_min, false);

    g_wifi_on = true;
  }
  else
  {
    g_wifi_on = false;
    esp_netif_deinit();
    esp_event_loop_delete_default();
  }

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

void web_initialize(char* uid, dali_config_t config)
{
  g_config = config;
  memcpy(g_uid, uid, strlen(uid));
  g_start_wifi = true;

  xTaskCreate(web_task, "DALI Task", 4096, NULL, 1, NULL);
}
