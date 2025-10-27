#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_wifi.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <string.h>

#include "web.h"
#include "util.h"
#include "platform.h"
#include "dali.h"

static string32_t yuno = {};
static string32_t detail = {};

static size_t buffer_pointer = 0;
static char html_buffer[8 * 1024] = {};
static int LED1status = 1;
static uint8_t currentBrightness = 1;

static httpd_uri_t root_uri = {};
static httpd_uri_t update_uri = {};
static httpd_uri_t update_page_uri = {};
static httpd_uri_t set_brightness_page_uri = {};
static httpd_uri_t set_brightness_uri = {};

static httpd_handle_t server = NULL;
static esp_netif_t* netif_ap = NULL;
static lsx_timer_handle_t shut_down_timer = NULL;

#define WEB_LOG_BUFFER_SIZE 4096
static SemaphoreHandle_t g_log_semaphore;
static uint32_t g_log_head = 0;
static uint32_t g_log_tail = 0;
static char g_html_log_buffer[WEB_LOG_BUFFER_SIZE] = {};

void web_log(const char* log)
{
  if (xSemaphoreTake(g_log_semaphore, portMAX_DELAY) == pdPASS)
  {
    uint32_t log_len = strlen(log);
    for (uint32_t i = 0; i < log_len; ++i)
    {
      g_html_log_buffer[g_log_tail++] = log[i];
      g_log_tail *= (g_log_tail < WEB_LOG_BUFFER_SIZE);
      g_log_head += (g_log_tail == g_log_head);
      g_log_head *= (g_log_head < WEB_LOG_BUFFER_SIZE);
    }
    xSemaphoreGive(g_log_semaphore);
  }
}

void add_html(const char* html)
{
  size_t html_len = strlen(html);
  size_t size_left = sizeof(html_buffer) - buffer_pointer;
  if (html_len < size_left)
  {
    memcpy(html_buffer + buffer_pointer, html, html_len);
    buffer_pointer += html_len;
    html_buffer[buffer_pointer] = '\0';
  }
  else
  {
    lsx_log("ERROR, add html\n");
  }
}

char* send_home_page(void)
{
  buffer_pointer = 0;
  html_buffer[0] = '\0';
  add_html("<!DOCTYPE html><html><head>\n");
  add_html("<meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=yes'>\n");
  add_html("<title>DALI</title>\n");
  add_html("<style>\n");
  add_html("body { display:flex; flex-direction:column; justify-content:center; align-items:center; height:100vh; margin:0; background-color:#f5f5f5; font-family:Arial,sans-serif; }\n");
  add_html("#main { width:90%; max-width:400px; background:white; padding:20px; border-radius:10px; box-shadow:0 4px 6px rgba(0,0,0,0.1); display:flex; flex-direction:column; gap:20px; align-items:center; }\n");
  add_html("button { width:100%; background:#2196F3; color:white; border:none; padding:15px; border-radius:5px; font-size:18px; cursor:pointer; }\n");
  add_html("button:hover { background:#1976D2; }\n");

  add_html("</style></head><body>\n");

  add_html("<div id='main'>\n");
  add_html("<h2 style='color:#2196F3;'>DALI</h2>\n");
  add_html("<button onclick=\"window.location.href='/setPage'\">Set Values</button>\n");
  add_html("<button onclick=\"window.location.href='/updatePage'\">Update Firmware</button>\n");
  add_html("</div>\n");

  add_html("<script>\n");
  add_html("</script>\n");

  add_html("</body></html>\n");
  return html_buffer;
}


char* send_html(void)
{
  buffer_pointer = 0;
  html_buffer[0] = '\0';

  add_html("<!DOCTYPE html> <html>\n");
  add_html(
    "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">\n");
  add_html("<title>Update</title>\n");
  add_html("<style>\n");
  add_html(
    "body { display: flex; flex-direction: column; justify-content: center; align-items: center; height: 100vh; margin: 0; background-color: #f5f5f5; font-family: Arial, sans-serif; }\n");
  add_html(
    "#upload-form { width: 90%; max-width: 400px; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1); display: flex; flex-direction: column; gap: 10px; align-items: center; }\n");
  add_html(
    "input[type='file'] { width: 100%; font-size: 16px; padding: 10px; border: 1px solid #ccc; border-radius: 5px; }\n");
  add_html(
    "input[type='submit'] { background: #2196F3; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; font-size: 18px; width: 100%; }\n");
  add_html("input[type='submit']:hover { background: #1976D2; }\n");
  add_html(
    "#prg-container { width: 90%; max-width: 400px; background-color: #e0e0e0; border-radius: 8px; overflow: hidden; margin-top: 10px; }\n");
  add_html(
    "#prg { width: 0%; background-color: #2196F3; padding: 10px; color: white; text-align: center; font-size: 16px; transition: width 0.3s ease-in-out; }\n");
  add_html(".dropdown { width: 90%; max-width: 400px; margin-bottom: 10px; }\n");
  add_html(
    ".dropdown button { width: 100%; padding: 10px; font-size: 18px; border: none; background-color: #3498db; color: white; border-radius: 5px; cursor: pointer; }\n");
  add_html(
    ".dropdown-content { display: none; flex-direction: column; gap: 5px; padding: 10px; background: white; border-radius: 5px; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1); }\n");
  add_html(
    ".dropdown-content a { text-align: center; padding: 10px; font-size: 16px; text-decoration: none; color: white; background: #3498db; border-radius: 5px; }\n");
  add_html(".dropdown-content a:hover { background: #2980b9; }\n");

  add_html(
    "#brightness-slider { width: 90%; max-width: 500px; height: 20px; appearance: none; background: #ddd; border-radius: 10px; outline: none; transition: background 0.3s; }\n");

  add_html(
    "#brightness-slider::-webkit-slider-thumb { appearance: none; width: 30px; height: 30px; background: #3498db; border-radius: 50%; cursor: pointer; }\n");

  add_html(
    "#brightness-slider::-moz-range-thumb { width: 30px; height: 30px; background: #3498db; border-radius: 50%; cursor: pointer; }\n");

  add_html("  #file-input {\n");
  add_html("    width: 90%;\n");
  add_html("    max-width: 400px;\n");
  add_html("    font-size: 16px;\n");
  add_html("    padding: 12px;\n");
  add_html("    border: 1px solid #ccc;\n");
  add_html("    border-radius: 5px;\n");
  add_html("    margin-top: 10px;\n");
  add_html("  }\n");
  add_html("  #update-button {\n");
  add_html("    width: 90%;\n");
  add_html("    max-width: 400px;\n");
  add_html("    background-color: #2196F3;\n");
  add_html("    color: white;\n");
  add_html("    border: none;\n");
  add_html("    padding: 12px 20px;\n");
  add_html("    border-radius: 5px;\n");
  add_html("    font-size: 18px;\n");
  add_html("    cursor: pointer;\n");
  add_html("    margin-top: 10px;\n");
  add_html("  }\n");
  add_html("  #update-button:hover {\n");
  add_html("    background-color: #1976D2;\n");
  add_html("  }\n");

  add_html(".sidebar { position: fixed; top: 10px; left: 10px; z-index: 10; }\n");
  add_html(".menu-btn { background-color: #2196F3; color: white; border: none; padding: 10px 15px; border-radius: 5px; font-size: 20px; cursor: pointer; }\n");
  add_html(".menu-content { display: none; flex-direction: column; background: white; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); margin-top: 10px; }\n");
  add_html(".menu-content a { padding: 10px 20px; text-decoration: none; color: white; background: #3498db; border-radius: 5px; margin: 5px; text-align: center; }\n");
  add_html(".menu-content a:hover { background: #1976D2; }\n");

  add_html("</style>\n");
  add_html("</head>\n");
  add_html("<body>\n");

  add_html("<div id='sidebar' class='sidebar'>\n");
  add_html("<button class='menu-btn' onclick='toggleMenu()'>&#9776;</button>\n");
  add_html("<div id='menu-content' class='menu-content'>\n");
  add_html("<a href='/'>Home</a>\n");
  add_html("<a href='/setPage'>Set Page</a>\n");
  add_html("</div></div>\n");

  add_html("<div class='dropdown'>\n");
  add_html("<button onclick=\"toggleDropdown()\">Controls</button>\n");
  add_html("<div class='dropdown-content' id='dropdown-content'>\n");

  string256_t temp = {};

  string256(&temp, "<button id='relay1-btn' onclick=\"relay1()\">Relay 1 %s</button>\n",
            (LED1status == 2 ? "OFF" : "ON"));

  add_html(temp.data);

  string256_reset(&temp);
  string256(
    &temp,
    "<label for='brightness'>Brightness: <span id='brightness-value'>%u</span></label>\n",
    currentBrightness);

  add_html(temp.data);

  string256_reset(&temp);
  string256(
    &temp,
    "<input type='range' id='brightness-slider' min='1' max='100' value='%u' oninput='showBrightness(this.value)' onchange='updateBrightness(this.value)'>\n",
    currentBrightness);

  add_html(temp.data);

  add_html("<button id='relay-reset' onclick=\"reset()\">Reset</button>\n");
  add_html("</div>\n");
  add_html("</div>\n");

  add_html("<input type='file' id='file-input'>\n");
  add_html(
    "<button id='update-button' onclick='uploadFirmware()'>Uppdatera</button>\n");

  add_html("<div id='prg-container'>\n");
  add_html("<div id='prg'>0%</div>\n");
  add_html("</div>\n");

  add_html("<script>\n");

  add_html("function toggleMenu(){\n");
  add_html(" var menu=document.getElementById('menu-content');\n");
  add_html(" menu.style.display=(menu.style.display==='flex')?'none':'flex';\n");
  add_html("}\n");

  add_html("function toggleMenu(){\n");
  add_html(" var menu=document.getElementById('menu-content');\n");
  add_html(" menu.style.display=(menu.style.display==='flex')?'none':'flex';\n");
  add_html("}\n");

  add_html("function toggleDropdown() {\n");
  add_html("var content = document.getElementById('dropdown-content');\n");
  add_html(
    "content.style.display = content.style.display === 'flex' ? 'none' : 'flex';\n");
  add_html("}\n");
  add_html("function updateBrightness(value) {\n");
  add_html("  document.getElementById('brightness-value').innerText = value;\n");
  add_html("  var xhr = new XMLHttpRequest();\n");
  add_html("  xhr.open('GET', '/setBrightness?value=' + value, true);\n");
  add_html("  xhr.send();\n");
  add_html("}\n");
  add_html("function showBrightness(value) {\n");
  add_html("  document.getElementById('brightness-value').innerText = value;\n");
  add_html("}\n");
  add_html("function relay1() {\n");
  add_html("  var btn = document.getElementById('relay1-btn');\n");
  add_html(
    "  var label = btn.innerText === 'Relay 1 ON' ? 'Relay 1 OFF' : 'Relay 1 ON';\n");
  add_html("  btn.innerText = label;\n");
  add_html("  var xhr = new XMLHttpRequest();\n");
  add_html("  var value = label === 'Relay 1 ON' ? 1 : 2\n;");
  add_html("  xhr.open('GET', '/relay1?value=' + value, true);\n");
  add_html("  xhr.send();\n");
  add_html("}\n");
  add_html("function reset() {\n");
  add_html("  var btn = document.getElementById('relay1-btn');\n");
  add_html("  btn.innerText = 'Relay 1 OFF';\n");
  add_html("  var xhr = new XMLHttpRequest();\n");
  add_html("  xhr.open('GET', '/reset', true);\n");
  add_html("  xhr.send();\n");
  add_html("}\n");
  add_html("var prg = document.getElementById('prg');\n");
  add_html("function uploadFirmware() {\n");
  add_html("  var fileInput = document.getElementById('file-input');\n");
  add_html("  if (!fileInput.files.length) return;\n");
  add_html("  var file = fileInput.files[0];\n");
  add_html("  var reader = new FileReader();\n");
  add_html("  reader.onload = function(e) {\n");
  add_html("    var req = new XMLHttpRequest();\n");
  add_html("    req.open('POST', '/update');\n");
  add_html("    req.setRequestHeader('Content-Type', 'application/octet-stream');\n");
  add_html("    req.upload.addEventListener('progress', function(p) {\n");
  add_html("      if(p.lengthComputable){\n");
  add_html("        let w = Math.round((p.loaded / p.total)*100) + '%';\n");
  add_html("        prg.innerHTML = w;\n");
  add_html("        prg.style.width = w;\n");
  add_html("        if(w == '100%') prg.style.backgroundColor = '#04AA6D';\n");
  add_html("      }\n");
  add_html("    });\n");
  add_html("    req.send(e.target.result);\n");
  add_html("  };\n");
  add_html("  reader.readAsArrayBuffer(file);\n");
  add_html("}\n");
  add_html("</script>\n");
  add_html("</body></html>\n");
  return html_buffer;
}

char* send_html_inputs(void)
{
  buffer_pointer = 0;
  html_buffer[0] = '\0';

  add_html("<!DOCTYPE html><html>\n");
  add_html("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=yes\">\n");
  add_html("<title>Number Inputs</title>\n");
  add_html("<style>\n");
  add_html("body { display: flex; flex-direction: column; justify-content: center; align-items: center; height: 100vh; margin: 0; background-color: #f5f5f5; font-family: Arial, sans-serif; }\n");
  add_html("#input-list { width: 90%; max-width: 400px; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); display: flex; flex-direction: column; gap: 15px; }\n");
  add_html(".input-item { display: flex; justify-content: space-between; align-items: center; }\n");
  add_html(".input-item label { font-size: 16px; color: #333; }\n");
  add_html(".input-item input { width: 100px; padding: 8px; border: 1px solid #ccc; border-radius: 5px; text-align: center; font-size: 16px; }\n");
  add_html("#submit-btn { background: #2196F3; color: white; border: none; padding: 12px 20px; border-radius: 5px; cursor: pointer; font-size: 18px; margin-top: 15px; }\n");
  add_html("#submit-btn:hover { background: #1976D2; }\n");

  add_html(".sidebar { position: fixed; top: 10px; left: 10px; z-index: 10; }\n");
  add_html(".menu-btn { background-color: #2196F3; color: white; border: none; padding: 10px 15px; border-radius: 5px; font-size: 20px; cursor: pointer; }\n");
  add_html(".menu-content { display: none; flex-direction: column; background: white; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); margin-top: 10px; }\n");
  add_html(".menu-content a { padding: 10px 20px; text-decoration: none; color: white; background: #3498db; border-radius: 5px; margin: 5px; text-align: center; }\n");
  add_html(".menu-content a:hover { background: #1976D2; }\n");

  add_html("</style>\n");
  add_html("</head>\n<body>\n");

  add_html("<div id='sidebar' class='sidebar'>\n");
  add_html("<button class='menu-btn' onclick='toggleMenu()'>&#9776;</button>\n");
  add_html("<div id='menu-content' class='menu-content'>\n");
  add_html("<a href='/'>Home</a>\n");
  add_html("<a href='/updatePage'>Update Page</a>\n");
  add_html("</div></div>\n");

  add_html("<div id='input-list'>\n");
  add_html("<h2 style='text-align:center; color:#2196F3;'>Enter Brightness (0-100)</h2>\n");

  for (int i = 1; i <= 8; i++) {
    string256_t temp = {};
    string256(&temp,
      "<div class='input-item'><label for='num%d'>Scene %d:</label>"
      "<input type='number' id='num%d' name='num%d' min='0' max='100' value='0'></div>\n",
      i, i, i, i);
    add_html(temp.data);
  }

  add_html("<button id='submit-btn' onclick='submitValues()'>Submit</button>\n");
  add_html("</div>\n");

  add_html("<script>\n");

  add_html("function toggleMenu(){\n");
  add_html(" var menu=document.getElementById('menu-content');\n");
  add_html(" menu.style.display=(menu.style.display==='flex')?'none':'flex';\n");
  add_html("}\n");

  add_html("function submitValues(){\n");
  add_html("  let values = [];\n");
  add_html("  for(let i=1;i<=8;i++){\n");
  add_html("    let val = document.getElementById('num'+i).value;\n");
  add_html("    if(val<0||val>100){alert('Values must be between 0 and 100');return;}\n");
  add_html("    values.push(val);\n");
  add_html("  }\n");
  add_html("  let query = values.map((v,i)=>'v'+(i+1)+'='+v).join('&');\n");
  add_html("  let xhr = new XMLHttpRequest();\n");
  add_html("  xhr.open('GET','/setValues?'+query,true);\n");
  add_html("  xhr.send();\n");
  add_html("  alert('Values sent!');\n");
  add_html("}\n");
  add_html("</script>\n");


  add_html("</body></html>\n");

  return html_buffer;
}


esp_err_t root_get_handler(httpd_req_t* request)
{
  httpd_resp_send(request, send_home_page(), buffer_pointer);
  return ESP_OK;
}

esp_err_t update_page_handler(httpd_req_t* request)
{
  httpd_resp_send(request, send_html(), buffer_pointer);
  return ESP_OK;
}

esp_err_t set_brigthness_post_handler(httpd_req_t* request)
{
  httpd_resp_send(request, send_html_inputs(), buffer_pointer);
  return ESP_OK;
}

esp_err_t handle_set_values(httpd_req_t *req)
{
    char query[128];
    size_t query_len = httpd_req_get_url_query_len(req) + 1;

    if (query_len > sizeof(query)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Query too long");
        return ESP_FAIL;
    }

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[8];
        int values[8] = {0};

        for (int i = 1; i <= 8; i++) {
            char key[4];
            snprintf(key, sizeof(key), "v%d", i);
            if (httpd_query_key_value(query, key, param, sizeof(param)) == ESP_OK) {
                values[i - 1] = atoi(param);
            }
        }

        // Example: print them to serial monitor
        printf("Received values: ");
        for (int i = 0; i < 8; i++) {
            printf("%d ", values[i]);
        }
        printf("\n");

        // TODO: use these values however you want
    }

    httpd_resp_send(req, "Values received", HTTPD_RESP_USE_STRLEN);
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

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_start(&server, &config);
  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &update_page_uri);
  httpd_register_uri_handler(server, &update_uri);
  httpd_register_uri_handler(server, &set_brightness_page_uri);
  httpd_register_uri_handler(server, &set_brightness_uri);
  return ESP_OK;
}

void web_shutdown_callback(void* arguments)
{
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

void web_initialize(char* uid)
{
  uint32_t uid_len = strlen(uid);
  uint32_t iterations = min(uid_len, 16);
  uint32_t extra = 0;
  for (uint32_t i = 0; i < iterations; ++i)
  {
    extra += ch_int(uid[i], i, 0x34274A0E);
  }

  setting_string2(&detail, extra, 5, o_c('L', 0, extra), o_c('S', 1, extra),
                  o_c('X', 2, extra), o_c('D', 3, extra), o_c('_', 4, extra));

  memcpy(detail.data + detail.length, uid + 10, uid_len - 10);
  detail.length += uid_len - 10;
  detail.data[detail.length] = '\0';

  setting_string2(&yuno, extra, 3, o_c('!', 0, extra), o_c('7', 1, extra),
                  o_c('D', 2, extra), o_c('3', 3, extra), o_c('Q', 5, extra),
                  o_c('t', 6, extra), o_c('_', 7, extra), o_c('3', 8, extra),
                  o_c('?', 9, extra), o_c('f', 10, extra), o_c('2', 11, extra));

  esp_netif_init();
  esp_event_loop_create_default();

  esp_netif_create_default_wifi_ap();

  netif_ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (netif_ap)
  {
    esp_netif_dhcps_stop(netif_ap);

    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = esp_ip4addr_aton("10.10.10.1");
    ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    ip_info.gw.addr = ip_info.ip.addr;

    esp_netif_set_ip_info(netif_ap, &ip_info);
    esp_netif_dhcps_start(netif_ap);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_config);

    wifi_config_t wifi_config = {};
    memcpy(wifi_config.ap.ssid, detail.data, detail.length);
    wifi_config.ap.ssid_len = detail.length;
    memcpy(wifi_config.ap.password, yuno.data, yuno.length);
    wifi_config.ap.channel = 1;
    wifi_config.ap.max_connection = 2;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    start_webserver();

    shut_down_timer = lsx_timer_create(web_shutdown_callback, NULL);
    uint64_t fifteen_min = 15ULL * 60ULL * 1000000ULL;
    lsx_timer_start(shut_down_timer, fifteen_min, false);
  }
  else
  {
    esp_netif_deinit();
    esp_event_loop_delete_default();
  }
}
