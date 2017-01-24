#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

//ESP-12
#define IN1 14 //GPIO 14
#define IN2 12 //GPIO 12
#define IN3 13 //GPIO 13
#define IN4 16 //GPIO 16
#define UP 4   //GPIO 4
#define DOWN 5 //GPIO 5
#define RST 2  //GPIO 2
#define LED 15 //GPIO 15
int deviceId;

//ESP-12 Wifi Server
#define SETUP_PAGE 0
#define NORMAL_PAGE 1
ESP8266WebServer server(80);
const String SSID = "ESP8266";
const String PASSWORD = "1234567890";
const String PRODUCT = "Auto Motor";
String wifiList;
String wifiListOption;
String content;
bool wifiConnected = false;
int statusCode;

//ESP-12 Wifi Client
#define API_HOST "api.thingspeak.com"
#define API_PORT 80
int deviceIdInServer;
int currentTurnInServer;
int totalTurnsInServer;
boolean sendSuccess = false;
boolean getSuccess = false;
boolean apiAllSuccess = false;

//EEPROM
//  0 ~  31: Wifi AP SSID
// 32 ~  95: Wifi AP Password
// 96 ~ 100: Total Number of Turns (binary)
// 101 ~ 105: Current Turn (binary)
// 106 ~ 110: ThingSpak Connection Period
// 111 ~ 130: ThingSpeak Write API key
// 131 ~ 150: ThingSpeak Read API key
// 151 ~ 160: ThingSpeak Channel ID
int totalTurns;
int currentTurn;
int period;
String writeApiKey;
String readApiKey;
String channelId;

//Motor
const int NBSTEPS = 4095;
const int STEPTIME = 900;
int Step = 0;
boolean clockwise = true;
boolean runStep = false;
boolean runningStep = false;
boolean settingTotalTurns = false;
boolean stopStep = false;
boolean runToTop = false;
boolean runToBottom = false;
boolean manual = false;

int stepsDefault[4] = {LOW, LOW, LOW, LOW};

int stepsMatrix[8][4] = {
  {LOW, LOW, LOW, HIGH},
  {LOW, LOW, HIGH, HIGH},
  {LOW, LOW, HIGH, LOW},
  {LOW, HIGH, HIGH, LOW},
  {LOW, HIGH, LOW, LOW},
  {HIGH, HIGH, LOW, LOW},
  {HIGH, LOW, LOW, LOW},
  {HIGH, LOW, LOW, HIGH},
};

unsigned long lastTurnTime = 0L;
unsigned long lastRstTime = 0L;
unsigned long lastApiTime = 0L;
unsigned long tmpTime = 0L;

void cleanWifiData();
void scanAccessWifi();
bool testWifi(void);
void createWebServer(int webType);
void launchWeb(int webType);
void setupAP();

void getPeriod();
void setPeriod(int inputPeriod);

void getApiKeys();
void setApiKeys(String inputWriteKey, String inputReadKey, String inputChannelId);
void cleanApiKeys();
boolean checkApiKeys();
boolean checkApiServer();
void sendToApiServer(int inputCurrentTurn, int inputTotalTurns);
void getFromApiServer();

void getTotalTurns();
void getCurrentTurn();
void setTotalTurns(int turns);
void setCurrentTurn();

void writeStep(int outArray[4]);
void stepper();
void setDirection();
void oneTurn(); //let motor turn one cicle
void top(); //let motor turn to 0
void bottom(); //let motor turn to number of total turns
void upPressInterrupt(); //press up button
void upReleaseInterrupt(); //release up button
void downPressInterrupt(); //press down button
void downReleaseInterrupt(); //release down button
void rstPressInterrupt(); //press reset button
void rstReleaseInterrupt(); //release release button

void setup() {
  deviceId = ESP.getChipId();
  
  Serial.begin(115200);
  EEPROM.begin(512);
  
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(UP, INPUT_PULLUP);
  pinMode(DOWN, INPUT_PULLUP);
  pinMode(RST, INPUT_PULLUP);
  attachInterrupt(UP, upPressInterrupt, FALLING);
  attachInterrupt(DOWN, downPressInterrupt, FALLING);
  attachInterrupt(RST, rstPressInterrupt, FALLING);

  Serial.println("Starting...");
  
  Serial.println("Reading EEPROM SSID");
  String eeSsid;
  for (int i = 0; i < 32; ++i) {
    eeSsid += char(EEPROM.read(i));
  }
  Serial.print("SSID: ");
  Serial.println(eeSsid);
  Serial.println("Reading EEPROM pass");
  String eePassword = "";
  for (int i = 32; i < 96; ++i) {
    eePassword += char(EEPROM.read(i));
  }
  Serial.print("PASS: ");
  Serial.println(eePassword);
  getTotalTurns();
  getCurrentTurn();
  totalTurns = 10;
  Serial.print("Total Number of Turns: ");
  Serial.println(totalTurns);
  Serial.print("Current Turn: ");
  Serial.println(currentTurn);
  
  WiFi.persistent(false);

  setupAP();

  lastApiTime = micros();
  getPeriod();
  getApiKeys();
  
  if (eeSsid.length() > 1) {
    Serial.println("Wifi begin");
    WiFi.begin(eeSsid.c_str(), eePassword.c_str());
    Serial.println("Test wifi");
    if (testWifi()) {
      launchWeb(NORMAL_PAGE);
      apiAllSuccess = checkApiKeys();
      return;
    } else {
      launchWeb(SETUP_PAGE);
      return;
    }
  } else {
    launchWeb(SETUP_PAGE);
    return;
  }
}

void loop() {
  unsigned long currentApiTime = micros();
  server.handleClient();

  if (runStep) {
    oneTurn();
  }

  if (runToTop) {
    top();

    if (wifiConnected && checkApiServer() && apiAllSuccess) {
      sendToApiServer(currentTurn, totalTurns);
      lastApiTime = micros();
      manual = false;
    }
  }

  if (runToBottom) {
    bottom();
    if (wifiConnected && checkApiServer() && apiAllSuccess) {
      sendToApiServer(currentTurn, totalTurns);
      lastApiTime = micros();
      manual = false;
    }
  }
  
  if (settingTotalTurns) {
    boolean light = false;
    currentTurn = 0;
    setCurrentTurn();
    stopStep = false;
    clockwise = true;
    while(!stopStep) {
      if (light) {
        digitalWrite(LED, LOW);
        light = false;
      } else {
        digitalWrite(LED, HIGH);
        light = true;
      }
      
      oneTurn();
    }
    settingTotalTurns = false;
    setTotalTurns(currentTurn);
    digitalWrite(LED, LOW);
  }

  if (wifiConnected) {
    if ((currentApiTime - lastApiTime) / 1000000L > 1 * 60 && period > 0 && checkApiServer()) {
      getApiKeys();
      getFromApiServer();
      if ((deviceIdInServer != deviceId  || currentTurnInServer != currentTurn) && totalTurns > 0) {
        if (manual) {
          sendToApiServer(currentTurn, totalTurns);
          manual = false;
        } else {
          if (currentTurnInServer == 0) {
            top();
          } else {
            bottom();
          }
          sendToApiServer(currentTurn, totalTurns);
        }
      }

      lastApiTime = micros();
    }
  }
}


void cleanWifiData() {
  for (int i = 0; i < 96; ++i) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

void scanAccessWifi() {
  Serial.println("scan start");
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
  wifiList = "<ol>";
  wifiListOption = "";
  for (int i = 0; i < n; ++i) {
    // Print SSID and RSSI for each network found
    wifiList += "<li>";
    wifiList += WiFi.SSID(i);
    wifiList += " (";
    wifiList += WiFi.RSSI(i);
    wifiList += ")";
    wifiList += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    wifiList += "</li>";
    wifiListOption += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
  }
  wifiList += "</ol>";
  delay(100);
}

bool testWifi(void) {
  int testCounts = 0;
  Serial.println("Waiting for Wifi to connect");
  while (testCounts < 20) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      return true;
    }
    delay(500);
    Serial.print(WiFi.status());
    testCounts++;
  }
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
  return false;
}

void createWebServer(int webType) {
  if (webType == SETUP_PAGE) {
    Serial.println("Create Web Server " + String(webType));
    server.on("/", []() {
      scanAccessWifi();
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head>";
      content += "<html>";
      content += "<style type='text/css'>\n";
      content += "input[type=\"button\"] {\n";
      content += "  width:120px;height:25px;margin:5px 0 5px 0;\n";
      content += "}\n";
      content += "</style>\n";
      content += "<script>\n";
      content += "var xmlHttp;\n";
      content += "function sendCmd(url) {\n";
      content += "  xmlHttp = null;\n";
      content += "  if (window.XMLHttpRequest) {\n";
      content += "    xmlHttp = new XMLHttpRequest();\n";
      content += "  } else {\n";
      content += "    xmlHttp = new ActiveXObject('Microsoft.XMLHTTP');\n";
      content += "  }\n";
      content += "  if (xmlHttp != null) {\n";
      content += "    xmlHttp.onreadystatechange = stateChange;\n";
      content += "    xmlHttp.open('GET', url, true);\n";
      content += "    xmlHttp.responseType = 'text';\n";
      content += "    xmlHttp.send(null);\n";
      content += "  } else {\n";
      content += "    console.log('your browser does not support XMLHTTP.');\n";
      content += "  }\n";
      content += "  function stateChange() {\n";
      content += "    if (xmlHttp.readyState == 4) {\n";
      content += "      if(xmlHttp.status == 200) {\n";
      content += "        var data;\n";
      content += "        data = JSON.parse(xmlHttp.responseText);\n";
      content += "        document.getElementById('response').innerHTML = data.msg;\n";
      content += "      } else {\n";
      content += "      }\n";
      content += "    } else {\n";
      content += "    }\n";
      content += "  }\n";
      content += "}\n";
      content += "</script>";
      content += "<body>Hello, this is  ";
      content += PRODUCT + " at ";
      content += ipStr;
      content += ".<br>These are the Wifi signals we found near by you, please select one.<p>";
      content += wifiList;
      content += "</p><form method='get' action='setting'><label style='width:80px;display:inline-block;'>SSID: </label><select name='ssid'>";
      //content += "<input name='ssid' length=32>";
      content += wifiListOption;
      content += "</select><br><label style='width:80px;display:inline-block;'>Password: </label><input name='pass' length=64><br><input type='submit' value='submit'></form>";
      content += "<hr>";
      if (currentTurn == 0) {
        content += "Your Motor is on the top.<br>";
      } else if (currentTurn == totalTurns) {
        content += "Your Motor is on the bottom.<br>";
      } else {
        content += "Your Motor is on the " + String((float)currentTurn / (float)totalTurns * (float)100, 1) +"%.<br>";
      }
      content += "<div><input type=button onclick='javascript:sendCmd(\"cleanWifi\");' value='clean wifi data'></div>";
      content += "<div><input type=button onclick='javascript:sendCmd(\"up\");' value='up'></div>";
      content += "<div><input type=button onclick='javascript:sendCmd(\"down\");' value='down'></div>";
      content += "<div><input type=button onclick='javascript:sendCmd(\"checkPosition\");' value='check position'></div>";
      content += "</body>";
      content += "</html>";
      server.send(200, "text/html", content);
    });

    server.on("/setting", []() {
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      String qSsid = server.arg("ssid");
      String qPassword = server.arg("pass");
      if (qSsid.length() > 0 && qPassword.length() > 0) {
        Serial.println("cleaning eeprom");
        for (int i = 0; i < 96; ++i) {
          EEPROM.write(i, 0);
        }
        Serial.print("SSID: ");
        Serial.println(qSsid);
        Serial.print("Password: ");
        Serial.println(qPassword);

        for (int i = 0; i < qSsid.length(); ++i) {
          EEPROM.write(i, qSsid[i]);
        }
        for (int i = 0; i < qPassword.length(); ++i) {
          EEPROM.write(32 + i, qPassword[i]);
        }
        EEPROM.commit();
        content = "<!DOCTYPE HTML><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head>";
        content += "<html>Hello, this is  ";
        content += PRODUCT + " at ";
        content += ipStr;
        content += ".<br>Save your SSID and Password success.<br>";
        content += ".<br>I'll be rebooted later.<br>";
        content += "</html>";
        statusCode = 200;
      } else {
        content = "<!DOCTYPE HTML><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head>";
        content += "<html>404 not found</html>";
        statusCode = 404;
        Serial.println("Sending 404");
      }
      server.send(statusCode, "text/html", content);
      ESP.restart();
    });
  } else if (webType == NORMAL_PAGE) {
    server.on("/", []() {
      IPAddress ip = WiFi.localIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head>";
      content += "<html>";
      content += "<style type='text/css'>\n";
      content += "input[type=\"button\"] {\n";
      content += "  width:120px;height:25px;margin:5px 0 5px 0;\n";
      content += "}\n";
      content += "</style>\n";
      content += "<script>\n";
      content += "var xmlHttp;\n";
      content += "function sendCmd(url) {\n";
      content += "  xmlHttp = null;\n";
      content += "  if (window.XMLHttpRequest) {\n";
      content += "    xmlHttp = new XMLHttpRequest();\n";
      content += "  } else {\n";
      content += "    xmlHttp = new ActiveXObject('Microsoft.XMLHTTP');\n";
      content += "  }\n";
      content += "  if (xmlHttp != null) {\n";
      content += "    xmlHttp.onreadystatechange = stateChange;\n";
      content += "    xmlHttp.open('GET', url, true);\n";
      content += "    xmlHttp.responseType = 'text';\n";
      content += "    xmlHttp.send(null);\n";
      content += "  } else {\n";
      content += "    console.log('your browser does not support XMLHTTP.');\n";
      content += "  }\n";
      content += "  function stateChange() {\n";
      content += "    if (xmlHttp.readyState == 4) {\n";
      content += "      if(xmlHttp.status == 200) {\n";
      content += "        var data;\n";
      content += "        data = JSON.parse(xmlHttp.responseText);\n";
      content += "        document.getElementById('response').innerHTML = data.msg;\n";
      content += "      } else {\n";
      content += "      }\n";
      content += "    } else {\n";
      content += "    }\n";
      content += "  }\n";
      content += "}\n";
      content += "</script>";
      content += "<body>Hello, this is  ";
      content += PRODUCT;
      content += "<br>Your wifi IP is ";
      content += ipStr;
      content += ".<br>";
      getApiKeys();
      getPeriod();

      checkApiKeys();

      if (apiAllSuccess) {
        content += "Connection to ThingSpeak server is success.<br>Send data to ThingSpeak server every " + String(period) + "minute(s).";
      } else {
        content += "Please setup the following configuration for ThingSpeak server.<br>";
        content += "<form method='get' action='setting'><label style='width:140px;display:inline-block;'>Write API Key: </label><input type='text' name='writeApiKey' length=32><br>";
        content += "<label style='width:140px;display:inline-block;'>Read API Key: </label><input type='text' name='readApiKey' length=32><br>";
        content += "<label style='width:140px;display:inline-block;'>Channel ID: </label><input type='text' name='channelId' length=32><br>";
        content += "<label style='width:140px;display:inline-block;'>Connection Period:</label><select name='period'>";
        content += "<option value=5>5 mins</option><option value=10>10 mins</option><option value=20>20 mins</option><option value=30>30 mins</option><option value=40>40 mins</option><option value=50>50 mins</option><option value=60>60 mins</option>";
        content += "</select><br><input type='submit' value='submit'></form><br>";
      }
      
      if (currentTurn == 0) {
        content += "Your Motor is on the top.<br>";
      } else if (currentTurn == totalTurns) {
        content += "Your Motor is on the bottom.<br>";
      } else {
        content += "Your Motor is on the " + String((float)currentTurn / (float)totalTurns * (float)100, 1) +"%.<br>";
      }
      content += "<div><input type=button onclick='javascript:sendCmd(\"cleanWifi\");' value='clean wifi data'></div>";
      content += "<div><input type=button onclick='javascript:sendCmd(\"cleanApiKeys\");' value='clean Api keys'></div>";
      content += "<div><input type=button onclick='javascript:sendCmd(\"up\");' value='up'></div>";
      content += "<div><input type=button onclick='javascript:sendCmd(\"down\");' value='down'></div>";
      content += "<div><input type=button onclick='javascript:sendCmd(\"checkPosition\");' value='check position'></div>";
      content += "<div id='response'></div>";
      content += "</body></html>";
      server.send(200, "text/html", content);
    });

    server.on("/cleanWifi", []() {
      content = "{\"msg\":\"Clearing the Wifi data!\"}";
      server.send(200, "text/html", content);
      Serial.println("disconnect wifi");
      WiFi.disconnect();
      Serial.println("cleaning wifi eeprom");
      cleanWifiData();
    });

    server.on("/cleanApiKeys", []() {
      content = "{\"msg\":\"Clearing the API keys and channel ID!\"}";
      server.send(200, "text/html", content);
      cleanApiKeys();
      getApiKeys();
    });

    server.on("/setting", []() {
      String qWriteApiKey = server.arg("writeApiKey");
      String qReadApiKey = server.arg("readApiKey");
      String qChannelId = server.arg("channelId");
      String qPeriod = server.arg("period");
      if (qWriteApiKey.length() > 0 && qReadApiKey.length() > 0) {
        setApiKeys(qWriteApiKey, qReadApiKey, qChannelId);
        setPeriod(qPeriod.toInt());
        content = "<!DOCTYPE HTML><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head>";
        content += "<html>";
        content = "<script>alert('Setup success!');window.location.href='/';</script>";
        content += "</html>";
        statusCode = 200;
      } else {
        content = "<!DOCTYPE HTML><head><meta name='viewport' content='width=device-width, initial-scale=1.0'></head>";
        content += "<html>404 not found</html>";
        statusCode = 404;
        Serial.println("Sending 404");
      }
      server.send(statusCode, "text/html", content);
    });
  }

  server.on("/up", []() {
    Serial.println("run to top");
    runToTop = true;
  });

  server.on("/down", []() {
    Serial.println("run to bottom");
    runToBottom = true;
  });

  server.on("/checkPosition", []() {
    String msg = "";
    if (currentTurn == 0) {
      msg = "Your Motor is on the top.";
    } else if (currentTurn == totalTurns) {
      msg = "Your Motor is on the bottom.";
    } else {
      msg = "Your Motor is on the " + String((float)currentTurn / (float)totalTurns * (float)100, 1) + "%.";
    }
    content = "{\"msg\":\"" + msg + "\"}";
    server.send(200, "application/json", content);
    Serial.println("check position");
  });

}

void launchWeb(int webType) {
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());
  createWebServer(webType);
  // Start the server
  server.begin();
  Serial.println("Server started");
}

void setupAP() {
  Serial.println("Set Wifi Mode");
  WiFi.mode(WIFI_AP_STA);
  Serial.println("Wifi disconnect");
  WiFi.disconnect();
  delay(100);
  scanAccessWifi();
  WiFi.softAP(SSID.c_str(), PASSWORD.c_str(), 6);
  Serial.println("softap");
}


void getPeriod() {
  String tmpPeriod = "";
  for (int i = 106; i < 111; ++i) {
    tmpPeriod += char(EEPROM.read(i));
  }
  period = tmpPeriod.toInt();
  Serial.print("Connection Period: ");
  Serial.println(period);
}

void setPeriod(int inputPeriod) {
  String tmpPeriod = String(inputPeriod);
  for (int i = 0; i < tmpPeriod.length(); ++i) {
    EEPROM.write(106 + i, tmpPeriod[i]);
  }
  EEPROM.commit();
}


void getApiKeys() {
  char tmpKeys[20];
  char tmpChannelId[10];
  
  for (int i = 111, j = 0; i < 131; ++i, ++j) {
    tmpKeys[j] = char(EEPROM.read(i));
  }
  writeApiKey = String(tmpKeys);
  Serial.print("Write API key: ");
  Serial.println(writeApiKey);
  
  for (int i = 131, j = 0; i < 151; ++i, ++j) {
    tmpKeys[j] = char(EEPROM.read(i));
  }
  readApiKey = String(tmpKeys);
  Serial.print("Read API key: ");
  Serial.println(readApiKey);
  
  for (int i = 151, j = 0; i < 160; ++i, ++j) {
    tmpChannelId[j] = char(EEPROM.read(i));
  }
  channelId = String(tmpChannelId).toInt();
  Serial.print("Read channel ID: ");
  Serial.println(channelId);
}

void setApiKeys(String inputWriteKey, String inputReadKey, String apiId) {
  for (int i = 0; i < inputWriteKey.length(); ++i) {
    EEPROM.write(111 + i, inputWriteKey[i]);
  }
  Serial.println("set write API key");
  for (int i = 0; i < inputReadKey.length(); ++i) {
    EEPROM.write(131 + i, inputReadKey[i]);
  }
  Serial.println("set read API key");
  for (int i = 0; i < apiId.length(); ++i) {
    EEPROM.write(151 + i, apiId[i]);
  }
  Serial.println("set channel ID");
  
  EEPROM.commit();
}

void cleanApiKeys() {
  for (int i = 106; i < 161; ++i) {
     EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("clean API keys and channel ID.");

}

boolean checkApiKeys() {
  apiAllSuccess = false;
  if (writeApiKey.length() > 0 && readApiKey.length() > 0) {
    getFromApiServer();
    if (getSuccess) {
      sendToApiServer(currentTurnInServer, totalTurnsInServer);
    }

    if (sendSuccess) {
      apiAllSuccess = true;
      return true;
    }
  }

  return false;
}

boolean checkApiServer() {
  if (!wifiConnected) {
    Serial.println("No wifi connection.");
    return false;
  }

  WiFiClient client;
  if (!client.connect(API_HOST, API_PORT)) {
    Serial.println("API server connection failed.");
    client.stop();
    return false;
  }
 
  client.stop();
  Serial.println("API server connection success.");
  return true;
}

void sendToApiServer(int inputCurrentTurn, int inputTotalTurns) {
  sendSuccess = false;
  
  if (!checkApiServer()) {
    Serial.println("Send data failed");
  }
  
  WiFiClient client;
  client.connect(API_HOST, API_PORT);
  
  String cmdStr = "GET /update.json?api_key=" + writeApiKey + \
                  "&field1=" + String((float)inputCurrentTurn / (float)inputTotalTurns * (float)100, 1) + \
                  "&field2=" + String(inputCurrentTurn) + \
                  "&field3=" + String(inputTotalTurns) + \
                  "&field4=" + String(deviceId) + " HTTP/1.1\r\n" + \
                  "Host: " + API_HOST + "\n" + \
                  "Connection: close\r\n\r\n";

  client.print(cmdStr);
  Serial.print("Send data:");
  Serial.println(cmdStr);
  delay(10);

  int i = 0;
  while((!client.available()) && (i < 1000)) {
    delay(10);
    i++;
  }
  
  while(client.available()) {
    String getData = "";
    getData = client.readStringUntil('\r');
    getData.trim();

    if (getData.substring(0,1) == "{") {
      char json[getData.length() + 1];
      getData.toCharArray(json, getData.length() + 1);
      StaticJsonBuffer<350> jsonBuffer;
      JsonObject& jsonData = jsonBuffer.parseObject(json);
      Serial.println(getData);
      if (jsonData.success()) {
        sendSuccess = true;
        Serial.println("send success");
      } else {
        Serial.println("json parse failed");
      }
    }
  }
  
  client.stop();
}

void getFromApiServer() {
  getSuccess = false;
  
  if (!checkApiServer()) {
    Serial.println("get data failed");
  }
  
  WiFiClient client;
  client.connect(API_HOST, API_PORT);
  
  String cmdStr = "GET /channels/" + String(channelId) + "/feeds/last?api_key=" + readApiKey + \
                  " HTTP/1.1\r\n" + \
                  "Host: " + API_HOST + "\n" + \
                  "Connection: close\r\n\r\n";
 
  client.print(cmdStr);
  Serial.print("Send data:");
  Serial.println(cmdStr);

  int i = 0;
  while((!client.available()) && (i < 1000)) {
    delay(10);
    i++;
  }
  
  while(client.available()) {
    String getData = "";
    getData = client.readStringUntil('\r');
    getData.trim();

    if (getData.substring(0,1) == "{") {
      char json[getData.length() + 1];
      getData.toCharArray(json, getData.length() + 1);
      StaticJsonBuffer<200> jsonBuffer;
      JsonObject& jsonData = jsonBuffer.parseObject(json);
      if (jsonData.success()) {
        const char* tmpDeviceId = jsonData["field4"];
        currentTurnInServer = jsonData["field2"];
        totalTurnsInServer = jsonData["field3"];
        
        deviceIdInServer = String(tmpDeviceId).toInt();

        Serial.print("Device ID in API Server: ");
        Serial.println(deviceIdInServer);
        Serial.print("Current Turn in API Server: ");
        Serial.println(currentTurnInServer);

        getSuccess = true;
      } else {
        Serial.println("json parse failed");
      }
    }
  }
  
  client.stop();
}


void getTotalTurns() {
  String readTmp;
  for (int i = 96; i < 101; i++) {
    readTmp += char(EEPROM.read(i));
  }
  totalTurns = readTmp.toInt();
}

void getCurrentTurn() {
  String readTmp;
  for (int i = 101; i < 106; i++) {
    readTmp += char(EEPROM.read(i));
  }
  currentTurn = readTmp.toInt();
}

void setTotalTurns(int turns) {
  char writeTmp[5];
  sprintf(writeTmp, "%05d", turns);
  for (int i = 96, j = 0; i < 101; i++, j++) {
    EEPROM.write(i, writeTmp[j]);
  }
  EEPROM.commit();
  Serial.println("write total turns");
}

void setCurrentTurn() {
  char writeTmp[5];
  sprintf(writeTmp, "%05d", currentTurn);
  for (int i = 101, j = 0; i < 106; i++, j++) {
    EEPROM.write(i, writeTmp[j]);
  }
  EEPROM.commit();
  Serial.println("write current turn");
}

void writeStep(int outArray[4]) {
  digitalWrite(IN1, outArray[0]);
  digitalWrite(IN2, outArray[1]);
  digitalWrite(IN3, outArray[2]);
  digitalWrite(IN4, outArray[3]);
}

void stepper() {
  if ((Step >= 0) && (Step < 8)) {
    writeStep(stepsMatrix[Step]);
  } else {
    writeStep(stepsDefault);
  }
  setDirection();
}

void setDirection() {
  (clockwise == true) ? (Step++) : (Step--);

  if (Step > 7) {
    Step = 0;
  } else if (Step < 0) {
    Step = 7;
  }
}

void oneTurn() {
  if (!settingTotalTurns && ((!clockwise && currentTurn == 0) || (clockwise && currentTurn >= totalTurns))) {
    return;
  }

  unsigned long currentMicros;
  int stepsLeft = NBSTEPS;
  tmpTime = 0;
  lastTurnTime = micros();
  runningStep = true;
  while (stepsLeft > 0) {
    currentMicros = micros();
    if (currentMicros - lastTurnTime >= STEPTIME) {
      stepper();
      tmpTime += micros() - lastTurnTime;
      lastTurnTime = micros();
      stepsLeft--;
    }
    delay(1);
  }
  //Serial.println(tmpTime);
  //Serial.println("Wait...!");
  //Serial.println(digitalRead(UP));
  //delay(1000);
  //clockwise = !clockwise;
  Serial.println("one turn");
  stepsLeft = NBSTEPS;

  if (clockwise) {
    currentTurn ++;
  } else {
    currentTurn --;
  }

  setCurrentTurn();
  runningStep = false;
  manual = true;
}

void top() {
  if (currentTurn > 0 && totalTurns > 0) {
    clockwise = false;
  }
  while(currentTurn > 0 && totalTurns > 0) {
    oneTurn();
  }
}

void bottom() {
  if (currentTurn <= totalTurns && totalTurns > 0) {
    clockwise = true;
  }
  while(currentTurn <= totalTurns && totalTurns > 0) {
   oneTurn();
  }
}

void upPressInterrupt(){
  if (!runningStep) {
    clockwise = true;
    runStep = true;
  }
  
  detachInterrupt(UP);
  attachInterrupt(UP, upReleaseInterrupt, RISING);
  Serial.println("up press!");
}

void upReleaseInterrupt(){
  runStep = false;
  
  detachInterrupt(UP);
  attachInterrupt(UP, upPressInterrupt, FALLING);
  Serial.println("up release!");
}

void downPressInterrupt(){
  if (!runningStep) {
    clockwise = false;
    runStep = true;
  }

  detachInterrupt(DOWN);
  attachInterrupt(DOWN, downReleaseInterrupt, RISING);
  Serial.println("down press!");
}

void downReleaseInterrupt(){
  runStep = false;
  
  detachInterrupt(DOWN);
  attachInterrupt(DOWN, downPressInterrupt, FALLING);
  Serial.println("down release!");
}

void rstPressInterrupt(){
  detachInterrupt(RST);
  attachInterrupt(RST, rstReleaseInterrupt, RISING);
  Serial.println("rst press!");
  lastRstTime = micros();
}

void rstReleaseInterrupt(){
  unsigned long currentRstTime;
  currentRstTime = micros();
  detachInterrupt(RST);
  attachInterrupt(RST, rstPressInterrupt, FALLING);
  Serial.println("rst release!");
  Serial.println((currentRstTime - lastRstTime) / 1000000L);
  if (settingTotalTurns) {
    stopStep = true;
    Serial.println("stop step!");
  } else {
    if ((currentRstTime - lastRstTime) / 1000000L > 3) {
      settingTotalTurns = true;
      Serial.println("start setting total turns!");
      for (int i = 0; i < 3; i ++) {
        digitalWrite(LED, HIGH);
        delay(300);
        digitalWrite(LED, LOW);
        delay(300);
      }
    }
  }
}

