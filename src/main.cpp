
#include <Arduino.h>  
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>

WebServer server(80);      // Port 80 = normal HTTP web page port

const char* ssid = "Airtel_jaic_9128";
const char* password = "air91351";

#define RXD2 21
#define TXD2 22
#define RS485_EN 17
#define RS485_SE 19

#define readHolding 0x03
#define writeSingle  0x06
#define readRegCount 0x0001

uint8_t activeSlaves = 1;
uint8_t slaveIds[] = {0x01, 0x02, 0x03, 0x04, 0x05};

String deviceId;
String lastJson = "";

struct RegisterMap
{
    uint8_t id;
    uint16_t code;
    const char *name;
    float scale;
};

RegisterMap regConfig[] =
{
    {0, 0x2000, "runCmd", 1.0},
    {1, 0x2001, "freqCmd", 100.0},

    {2, 0x2100, "faultReg", 1.0},
    {3, 0x2101, "statusReg", 1.0},
    {4, 0x2102, "cmdFreq", 100.0},
    {5, 0x2103, "outFreq", 100.0},
    {6, 0x2104, "outCurr", 100.0},
    {7, 0x2105, "dcBus", 1.0},
    {8, 0x2106, "outVolt", 10.0},
    {9, 0x2107, "stepNo", 1.0},
    {10, 0x2108, "plcStep", 1.0},
    {11, 0x2109, "plcTime", 1.0},
    {12, 0x210A, "counter", 1.0},

    {13, 0x0001, "stopCmd", 1.0},
    {14, 0x0012, "fwdCmd", 1.0},
    {15, 0x0022, "revCmd", 1.0}
};

struct FaultMap
{
    uint16_t code;
    const char *name;
};

FaultMap faultTable[] =
{
    {0,  "No Fault"},
    {1,  "OC - Over Current"},
    {2,  "OV - Over Voltage"},
    {3,  "OH - Over Heat"},
    {4,  "Reserved"},
    {5,  "OL1 - Overload1"},
    {6,  "EF - External Fault"},
    {7,  "cF3 - CPU Failure"},
    {8,  "HPF - Hardware Protection Failure"},
    {9,  "ocA - Over Current During Accel"},
    {10, "ocd - Over Current During Decel"},
    {11, "ocn - Over Current During Steady State"},
    {12, "Reserved"},
    {13, "Reserved"},
    {14, "Lv - Low Voltage"},
    {15, "cF1 - CPU Failure 1"},
    {16, "cF2 - CPU Failure 2"},
    {17, "bb - Base Block"},
    {18, "oL2 - Overload"},
    {19, "cFA - Auto Accel/Decel Failure"},
    {20, "codE - Software Protection"}
};

String getDeviceId()
{
  uint64_t mac = ESP.getEfuseMac();
  uint8_t b4 = (mac >> 16) & 0xFF;
  uint8_t b5 = (mac >> 8)  & 0xFF;
  uint8_t b6 = mac & 0xFF;

  char buf[20];
  snprintf(buf, sizeof(buf), "%d%d%d", b4, b5, b6);
  return String(buf);
}

// CRC
uint16_t crc16(uint8_t *buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

// print helper
void printHex(uint8_t *buf, int len) {
  for (int i = 0; i < len; i++) {
    if (buf[i] < 0x10) Serial.print("0");
    Serial.print(buf[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void sendFrame(uint8_t *f, int len)
{
    while (Serial2.available()) Serial2.read();

    digitalWrite(RS485_EN, HIGH);      // Transmit mode
    digitalWrite(RS485_SE, HIGH);

    Serial2.write(f, len);
    Serial2.flush();
    delay(50);

    digitalWrite(RS485_EN, LOW);      // Receive mode
    digitalWrite(RS485_SE, LOW);
}

// generic write
int writeReg(uint16_t reg, uint16_t val)
{
  lastJson = "{\"status\":\"Failed\",\"message\":\"No VFD Response\"}";

  int result = 1;
  for (int i = 0; i < activeSlaves; i++)
  {
    uint8_t slaveId = slaveIds[i];

    uint8_t f[8] = {
      slaveId,
      writeSingle,
      reg >> 8,
      reg & 0xFF,
      val >> 8,
      val & 0xFF,
      0,
      0
    };

    uint16_t crc = crc16(f, 6);
    f[6] = crc & 0xFF;
    f[7] = crc >> 8;

    Serial.print("TX: ");
    printHex(f, 8);

    sendFrame(f, 8);

    uint8_t resp[8];
    int len = 0;
    unsigned long t = millis();

    while (millis() - t < 100)
    {
      if (Serial2.available())
      {
        resp[len++] = Serial2.read();
        if (len >= 8) break;
      }
    }
    
    const char *name = "";
    if (reg == regConfig[1].code) {
       name = regConfig[1].name;
    }
    else {
    for (int i = 0; i < sizeof(regConfig)/sizeof(regConfig[0]); i++)
    {
        if (regConfig[i].code == val)
        {
           name = regConfig[i].name;
           break;
        }
     }
   }

    Serial.print("RX: ");

    if (len > 0)
    {
      printHex(resp, len);
      Serial.println();

     JsonDocument doc;

       doc["device_id"] = deviceId;
       doc["slave_id"] = slaveId;
       doc["vfd_version"] = "VFD004L21A";
       doc["command"] = name;
       doc["status"] = "Success";
       if (reg == regConfig[1].code)     
          doc["frequency_hz"] = val / regConfig[1].scale;

       serializeJson(doc, Serial);
       Serial.println();

       lastJson = "";
       serializeJson(doc, lastJson);
    }
    else
    {
      JsonDocument doc;

       doc["device_id"] = deviceId;
       doc["slave_id"] = slaveId;
       doc["vfd_version"] = "VFD004L21A";
       doc["command"] = name;
       doc["status"] = "Failed";

       serializeJson(doc, Serial);
       Serial.println();

       lastJson = "";
       serializeJson(doc, lastJson);
      result = -1;
    }
  }
  return result;
}

void readFault(uint16_t reg)
{
  lastJson = "{\"status\":\"Failed\",\"message\":\"No VFD Response\"}";

  for (int i = 0; i < activeSlaves; i++)
  {
    uint8_t slaveId = slaveIds[i];

  uint8_t f[8];

  f[0] = slaveId;
  f[1] = readHolding;
  f[2] = reg >> 8;
  f[3] = reg & 0xFF;
  f[4] = readRegCount >> 8;
  f[5] = readRegCount & 0xFF;      // No of Registers 1

  uint16_t crc = crc16(f, 6);
  f[6] = crc & 0xFF;
  f[7] = crc >> 8;
 
  Serial.print("TX: ");
  printHex(f, 8);

  sendFrame(f, 8);

  uint8_t resp[16];
  int len = 0;
  unsigned long t = millis();
  Serial.print("RX: ");

while (millis() - t < 1000)
{
  while (Serial2.available())
  {
    uint8_t b = Serial2.read();

    if (b < 0x10)
      Serial.print("0");
      Serial.print(b, HEX);
      Serial.print(" ");

    resp[len++] = b;      
    t = millis();
  }
}

Serial.println();

if (len >= 5)
{
  uint16_t fault = (resp[3] << 8) | resp[4];

  Serial.print("Fault Code = ");
  Serial.println(fault);

  for (int i = 15; i >= 0; i--)
  { 
    Serial.print("Bit[");
    Serial.print(i);
    Serial.print("] = ");
    Serial.println((fault >> i) & 1);
  }
  
  bool found = false;
  for (int i = 0; i < sizeof(faultTable)/sizeof(faultTable[0]); i++)
  {
    if (faultTable[i].code == fault)
    {
      JsonDocument doc;

      doc["device_id"] = deviceId;
      doc["slave_id"] = slaveId;
      doc["vfd_version"] = "VFD004L21A";
      doc["fault_code"] = faultTable[i].code;
      doc["fault_name"] = faultTable[i].name;
      serializeJson(doc, Serial);
      Serial.println();

      lastJson = "";
      serializeJson(doc, lastJson);
      found = true;
      break;
    }
  }
  if (!found) {
  JsonDocument doc;

  doc["device_id"] = deviceId;
  doc["slave_id"] = slaveId;
  doc["vfd_version"] = "VFD004L21A";
  doc["fault_code"] = fault;
  doc["fault_name"] = "Unknown Fault";
  serializeJson(doc, Serial);
  Serial.println();
  lastJson = "";
  serializeJson(doc, lastJson);
  }
 }
}
}

void readMonitor(uint16_t reg)
{
  lastJson = "{\"status\":\"Failed\",\"message\":\"No VFD Response\"}";

  for (int s = 0; s < activeSlaves; s++)
  {
    uint8_t slaveId = slaveIds[s];

  uint8_t f[8];

  f[0] = slaveId;
  f[1] = readHolding;
  f[2] = reg >> 8;
  f[3] = reg & 0xFF;
  f[4] = readRegCount >> 8;
  f[5] = readRegCount & 0xFF;

  uint16_t crc = crc16(f, 6);
  f[6] = crc & 0xFF;
  f[7] = crc >> 8;

  Serial.print("TX: ");
  printHex(f, 8);

  sendFrame(f, 8);

  uint8_t resp[16];
  int len = 0;
  unsigned long t = millis();

  while (millis() - t < 1000)
  {
    while (Serial2.available())
    {
      resp[len++] = Serial2.read();
      t = millis();
    }
  }

  if (len >= 5)
  {
    uint16_t value = (resp[3] << 8) | resp[4];

  for (int i = 15; i >= 0; i--)
  { 
    Serial.print("Bit[");
    Serial.print(i);
    Serial.print("] = ");
    Serial.println((value >> i) & 1);
  }
    Serial.println();

    const char *name = "";
    float scale = 1.0;
    for (int i = 0; i < sizeof(regConfig)/sizeof(regConfig[0]); i++)
    {
        if (regConfig[i].code == reg)
        {
           name = regConfig[i].name;
           scale = regConfig[i].scale;
           break;
        }
    }

    JsonDocument doc;

    doc["device_id"] = deviceId;
    doc["vfd_version"] = "VFD004L21A";
    doc["slave_id"] = slaveId;
    doc["parameter"] = name;
    doc["value"] = value / scale;

    serializeJson(doc, Serial);
    Serial.println();
    lastJson = "";
    serializeJson(doc, lastJson);
  }
}
}

void readDriveStatus(uint16_t reg)
{
  lastJson = "{\"status\":\"Failed\",\"message\":\"No VFD Response\"}";

  for (int i = 0; i < activeSlaves; i++)
  {
    uint8_t slaveId = slaveIds[i];

  uint8_t f[8];

  f[0] = slaveId;
  f[1] = readHolding;
  f[2] = reg >> 8;
  f[3] = reg & 0xFF;
  f[4] = readRegCount >> 8;
  f[5] = readRegCount & 0xFF;

  uint16_t crc = crc16(f, 6);
  f[6] = crc & 0xFF;
  f[7] = crc >> 8;

  sendFrame(f, 8);

  uint8_t resp[16];
  int len = 0;
  unsigned long t = millis();

  while (millis() - t < 1000)
  {
    while (Serial2.available())
    {
      resp[len++] = Serial2.read();
      t = millis();
    }
  }

  if (len >= 5)
  {
    uint16_t status = (resp[3] << 8) | resp[4];

    for (int i = 15; i >= 0; i--)
    {
        Serial.print("Bit[");
        Serial.print(i);
        Serial.print("] = ");
        Serial.println((status >> i) & 1);
    }
  Serial.println();

    String VFD_Status = "";
    String direction = "";
    String freqSource = "";
    String cmdSource = "";

    // Bit0-1
    switch (status & 0x03)
    {
       case 0: VFD_Status = "STOP"; break;
       case 1: VFD_Status = "RUN LED blink, STOP LED light up"; break;
       case 2: VFD_Status = "RUN LED light up, STOP LED blink"; break;
       case 3: VFD_Status = "Running"; break;
    }

    // Bit3-4
    switch ((status >> 3) & 0x03)
    {
       case 0: direction = "Forward"; break;
       case 1: direction = "REV LED blink, FWD LED light up"; break;
       case 2: direction = "REV LED light up, FWD LED blink"; break;
       case 3: direction = "Reverse"; break;
    }

    if (status & (1 << 8))
        freqSource = "Controlled by Communication";

    if (status & (1 << 9))
      freqSource = "Controlled by external terminal";

    if (status & (1 << 10))
      cmdSource = "Controlled by communication";

      JsonDocument doc;

      doc["device_id"] = deviceId;
      doc["slave_id"] = slaveId;
      doc["vfd_version"] = "VFD004L21A";
      doc["status"] = VFD_Status;
      doc["direction"] = direction;
      doc["freq_source"] = freqSource;
      doc["command_source"] = cmdSource;

      // Bit2
      if (status & (1 << 2))
         doc["jog_status"] = "Jog active";

      if (status & (1 << 11))
         doc["parameters"] = "Parameters have been locked";   

      serializeJson(doc, Serial);
      Serial.println();
      lastJson = "";
      serializeJson(doc, lastJson);
  }
}
}

void testFault(uint8_t highByte, uint8_t lowByte)
{
  uint16_t fault = (highByte << 8) | lowByte;

  Serial.print("High Byte = 0x");
  Serial.println(highByte, HEX);

  Serial.print("Low Byte = 0x");
  Serial.println(lowByte, HEX);

  Serial.print("Fault Code = ");
  Serial.println(fault);

  for (int i = 15; i >= 0; i--)
  {
    Serial.print("Bit[");
    Serial.print(i);
    Serial.print("] = ");
    Serial.println((fault >> i) & 1);
  }

  for (int i = 0; i < sizeof(faultTable)/sizeof(faultTable[0]); i++)
  {
    if (faultTable[i].code == fault)
    {
      JsonDocument doc;

      doc["device_id"] = deviceId;
      doc["vfd_version"] = "VFD004L21A";
      doc["fault_code"] = faultTable[i].code;
      doc["fault_name"] = faultTable[i].name;
      serializeJson(doc, Serial);
      Serial.println();
      lastJson = "";
      serializeJson(doc, lastJson);
      return;
    }
  }
  JsonDocument doc;

  doc["device_id"] = deviceId;
  doc["vfd_version"] = "VFD004L21A";
  doc["fault_code"] = fault;
  doc["fault_name"] = "Unknown Fault";
  serializeJson(doc, Serial);
  Serial.println();
  lastJson = "";
  serializeJson(doc, lastJson);
}

// HTML Web page
const char login_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>

<body style="
background:#1b2430;
font-family:Arial;
display:flex;
justify-content:center;
align-items:center;
height:100vh;
margin:0;
">

<div style="
background:#2c3e50;
padding:60px 50px;
border-radius:12px;
width:450px;
min-height:280px;
text-align:center;
box-shadow:0 4px 12px rgba(0,0,0,0.3);
">

<h2 style="
color:#4aa3ff;
font-size:32px;
margin-top:0;
margin-bottom:35px;
">
Login
</h2>

<input id="user"
placeholder="Username"
style="
width:80%;
padding:18px;
margin:15px;
border:none;
border-radius:6px;
font-size:18px;
"><br>

<input id="pass"
type="password"
placeholder="Password"
style="
width:80%;
padding:18px;
margin:15px;
border:none;
border-radius:6px;
font-size:18px;
"><br>

<button onclick="login()"
style="
width:80%;
padding:18px;
background:#4aa3ff;
color:white;
border:none;
border-radius:6px;
font-size:20px;
font-weight:bold;
cursor:pointer;
margin-top:15px;
">
Login
</button>

</div>

<script>

function login()
{
    if(user.value=="admin" && pass.value=="1234")
        location="/dashboard";
    else
        alert("Wrong Login");
}

</script>

</body>
</html>
)rawliteral";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>

<head>
<title>Delta VFD Dashboard</title>

<style>

body{
    margin:0;
    padding:20px;
    font-family:Arial, sans-serif;
    background:#0f172a;
    color:white;
}

h1{
    text-align:center;
    color:#38bdf8;
    margin-bottom:20px;
}

.container{
    display:flex;
    flex-wrap:wrap;
    gap:15px;
}

.card{
    background:#1e293b;
    border-radius:12px;
    padding:15px;
    box-shadow:0 3px 10px rgba(0,0,0,0.3);
}

.half{
    flex:1;
    min-width:350px;
}

.full{
    width:100%;
}

.section-title{
    font-size:18px;
    color:#60a5fa;
    margin-bottom:12px;
    font-weight:bold;
}

button{
    background:#4aa3ff;
    color:white;
    border:none;
    border-radius:8px;
    padding:10px;
    margin:4px;
    width:130px;
    cursor:pointer;
    font-weight:bold;
}

button:hover{
    background:#6bb7ff;
}

input{
    background:#0f172a;
    color:white;
    border:1px solid #334155;
    border-radius:8px;
    padding:10px;
    margin:4px;
}

#freq{
    width:120px;
}

#faultHigh,#faultLow{
    width:100px;
}

#log{
    background:#020617;
    color:#22c55e;
    border-radius:8px;
    padding:10px;
    height:320px;
    overflow:auto;
    white-space:pre-wrap;
    font-family:monospace;
}

</style>

</head>

<body>

<h1>Delta VFD Dashboard</h1>

<div class="container">

<div class="card half">
<div class="section-title">Frequency Control</div>

<input type="number" id="freq" placeholder="0-60 Hz">
<button onclick="setFreq()">Set Frequency</button>
</div>

<div class="card half">
<div class="section-title">Drive Commands</div>

<button onclick="callApi('/forward')">Forward</button>
<button onclick="callApi('/reverse')">Reverse</button>
<button onclick="callApi('/stop')">Stop</button>
</div>

<div class="card full">
<div class="section-title">Monitoring</div>

<button onclick="callApi('/fault')">Fault</button>
<button onclick="callApi('/status')">Status</button>
<button onclick="callApi('/cmdfreq')">Cmd Freq</button>
<button onclick="callApi('/outfreq')">Out Freq</button>
<button onclick="callApi('/outcurr')">Current</button>
<button onclick="callApi('/dcbus')">DC Bus</button>
<button onclick="callApi('/outvolt')">Voltage</button>
</div>

<div class="card half">
<div class="section-title">PLC & Multi-Step</div>

<button onclick="callApi('/stepno')">Step No</button>
<button onclick="callApi('/plcstep')">PLC Step</button>
<button onclick="callApi('/plctime')">PLC Time</button>
<button onclick="callApi('/counter')">Counter</button>
</div>

<div class="card half">
<div class="section-title">Fault Simulator</div>

<input type="number" id="faultCode" placeholder="Fault Code">

<br><br>

<button onclick="testFault()">Test Fault</button>
<button onclick="clearLog()">Clear Log</button>
</div>

<div class="card full">
<div class="section-title">Response Log</div>

<pre id="log"></pre>
</div>

</div>

<script>

async function setFreq()
{
    let f = document.getElementById('freq').value;

    let r = await fetch('/freq?value=' + f);
    let j = await r.text();

    let log = document.getElementById("log");
    log.textContent += j + "\n\n";
    log.scrollTop = log.scrollHeight;
}

async function testFault()
{
    let code = document.getElementById('faultCode').value;

    let r = await fetch('/testfault?code=' + code);
    let j = await r.text();

    let log = document.getElementById("log");
    log.textContent += j + "\n\n";
    log.scrollTop = log.scrollHeight;
}

function clearLog()
{
    document.getElementById("log").textContent = "";
}

async function callApi(url)
{
    let r = await fetch(url);
    let j = await r.text();

    let log = document.getElementById("log");
    log.textContent += j + "\n\n";
    log.scrollTop = log.scrollHeight;
}

</script>

</body>
</html>
)rawliteral";

//  SETUP 
void setup() {
  Serial.begin(115200);
  pinMode(16, OUTPUT);
  digitalWrite(16, HIGH);  // Enable RS485 

  pinMode(RS485_EN, OUTPUT);
  pinMode(RS485_SE, OUTPUT);

  digitalWrite(RS485_EN, LOW);
  digitalWrite(RS485_SE, LOW);

  Serial2.begin(9600, SERIAL_8E1, RXD2, TXD2);
  deviceId = getDeviceId();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
      delay(500);
      Serial.print(".");
  }
  Serial.println();   Serial.println(WiFi.localIP());

server.on("/", []()
{
    server.send(200, "text/html", login_html);
});

server.on("/dashboard", []()
{
    server.send(200, "text/html", index_html);
});

server.on("/forward", []()
{
    writeReg(regConfig[0].code, regConfig[14].code);
    server.send(200, "application/json", lastJson);
});

server.on("/reverse", []()
{
    writeReg(regConfig[0].code, regConfig[15].code);
    server.send(200, "application/json", lastJson);
});

server.on("/stop", []()
{
    writeReg(regConfig[0].code, regConfig[13].code);
    server.send(200, "application/json", lastJson);
});

server.on("/freq", []()
{
    float f = server.arg("value").toFloat();
    if (f <= 60)
    {
        writeReg(regConfig[1].code, f * regConfig[1].scale);
        server.send(200, "application/json", lastJson);
    }
    else
    {
        server.send(200, "application/json", "{\"status\":\"Failed\",\"message\":\"Max Frequency = 60 Hz\"}");
    }
});

server.on("/fault", []()
{
    readFault(regConfig[2].code);
    server.send(200, "application/json", lastJson);
});

server.on("/status", []()
{
    readDriveStatus(regConfig[3].code);
    server.send(200, "application/json", lastJson);
});

server.on("/cmdfreq", []()
{
    readMonitor(regConfig[4].code);
    server.send(200, "application/json", lastJson);
});

server.on("/outfreq", []()
{
    readMonitor(regConfig[5].code);
    server.send(200, "application/json", lastJson);
});

server.on("/outcurr", []()
{
    readMonitor(regConfig[6].code);
    server.send(200, "application/json", lastJson);
});

server.on("/dcbus", []()
{
    readMonitor(regConfig[7].code);
    server.send(200, "application/json", lastJson);
});

server.on("/outvolt", []()
{
    readMonitor(regConfig[8].code);
    server.send(200, "application/json", lastJson);
});

server.on("/stepno", []()
{
    readMonitor(regConfig[9].code);
    server.send(200, "application/json", lastJson);
});

server.on("/plcstep", []()
{
    readMonitor(regConfig[10].code);
    server.send(200, "application/json", lastJson);
});

server.on("/plctime", []()
{
    readMonitor(regConfig[11].code);
    server.send(200, "application/json", lastJson);
});

server.on("/counter", []()
{
    readMonitor(regConfig[12].code);
    server.send(200, "application/json", lastJson);
});

server.on("/testfault", []()
{
    uint16_t code = server.arg("code").toInt();

    uint8_t high = (code >> 8) & 0xFF;
    uint8_t low  = code & 0xFF;

    testFault(high, low);

    server.send(200, "application/json", lastJson);
});

  server.begin();

  Serial.println("Commands:");
  Serial.println("wXX  --> set freq ");       
  Serial.println("f    --> forward");
  Serial.println("r    --> reverse");
  Serial.println("s    --> stop");
  Serial.println("e --> read fault");
  Serial.println("c --> command freq");
  Serial.println("h --> output freq");
  Serial.println("i --> output current");
  Serial.println("v --> dc bus voltage");
  Serial.println("o --> output voltage");
  Serial.println("m --> multi-step number");
  Serial.println("p --> plc step number");
  Serial.println("t --> plc time");
  Serial.println("n --> counter value");
  Serial.println("d --> drive status");
  Serial.println("x HH LL --> test fault bytes");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED)
  {
     static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 5000)
    {
        lastReconnect = millis();
        Serial.println("WiFi disconnected. Reconnecting...");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
    }
  }
  server.handleClient();

  if (Serial.available()) {
    char c = Serial.read();

    if (c == 'w') {
     float f = Serial.parseFloat();
     Serial.printf("\nInput Frequency = %.2f\n", f);
     if (f <= 60)
          (writeReg(regConfig[1].code, f * regConfig[1].scale));
     else
          Serial.println("Max Frequency = 60 Hz");
    }
    else if (c == 'f') {
      Serial.printf("\nInput = Forward\n");
       (writeReg(regConfig[0].code, regConfig[14].code));
    }
    else if (c == 'r') {
      Serial.printf("\nInput = Reverse\n");
       (writeReg(regConfig[0].code, regConfig[15].code));
    }
    else if (c == 's') {
      Serial.printf("\nInput = Stop\n");
       (writeReg(regConfig[0].code, regConfig[13].code ));
    }
    else if (c == 'e') {
      Serial.printf("\nInput = Read Fault\n");
        readFault(regConfig[2].code);
    }
    else if (c == 'c') {
      Serial.printf("\nInput = Command Frequency\n");
        readMonitor(regConfig[4].code);
    }
    else if (c == 'h') {
      Serial.printf("\nInput = Output Frequency\n");
        readMonitor(regConfig[5].code);
    }
    else if (c == 'i') {
      Serial.printf("\nInput = Output Current\n");
        readMonitor(regConfig[6].code);
    }
    else if (c == 'v') {
      Serial.printf("\nInput = DC Bus Voltage\n");
        readMonitor(regConfig[7].code);
    }
    else if (c == 'o') {
      Serial.printf("\nInput = Output Voltage\n");
        readMonitor(regConfig[8].code);
    }
    else if (c == 'm') {
      Serial.printf("\nInput = Multi Step Number\n");
        readMonitor(regConfig[9].code);
    }
    else if (c == 'p') {
      Serial.printf("\nInput = PLC Step Number\n");
        readMonitor(regConfig[10].code);
    }
    else if (c == 't') {
      Serial.printf("\nInput = PLC Time\n");
        readMonitor(regConfig[11].code);
    }
    else if (c == 'n')
    {
      Serial.printf("\nInput = Counter Value\n");
        readMonitor(regConfig[12].code); 
    }
    else if (c == 'd')
    {
      Serial.printf("\nInput = Drive Status\n");
        readDriveStatus(regConfig[3].code);
    }
    else if (c == 'x')
    {
        String line = Serial.readStringUntil('\n');
        Serial.printf("\nInput = Test Fault\n");
        uint8_t highByte, lowByte;
        sscanf(line.c_str(), "%hhx %hhx", &highByte, &lowByte);
        testFault(highByte, lowByte);
    }
  }
}















































































/*
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h> 

#define RXD2     21
#define TXD2     22
#define RS485_EN 17
#define RS485_SE 19

const char* ssid = "Realme7";
const char* password = "12345678";

const char* mqtt_server = "52.77.237.146";
const char* token = "I82bVp1VEiRgjzmJiLmT";

WiFiClient espClient;
PubSubClient client(espClient);

volatile int command = 0;   // 1=fwd, 2=rev, 3=stop
volatile int freq = 0;

void setup_wifi() {
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
} 

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32", token, NULL)) {
      client.subscribe("v1/devices/me/attributes");
    } else {
      delay(2000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String msg = String((char*)payload);

  Serial.println("ATTR RX: " + msg);

  // Frequency
  if (msg.indexOf("frequency") >= 0) {
    int val = msg.substring(msg.indexOf(":") + 1, msg.indexOf("}")).toInt();
    freq = val;
  }

  // State
  if (msg.indexOf("forward") >= 0) command = 1;
  else if (msg.indexOf("reverse") >= 0) command = 2;
  else if (msg.indexOf("stop") >= 0) command = 3;
}

void sendTelemetry() {
  String stateStr = "stop";

  if (command == 1) stateStr = "forward";
  else if (command == 2) stateStr = "reverse";

  String payload = "{";
  payload += "\"frequency\":" + String(freq) + ",";
  payload += "\"state\":\"" + stateStr + "\"";
  payload += "}";

  client.publish("v1/devices/me/telemetry", payload.c_str());

  Serial.println("TX: " + payload);
}

uint16_t crc16(uint8_t *buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

void writeReg(uint16_t reg, uint16_t val) {
  uint8_t f[8] = {1, 0x06, reg >> 8, reg & 0xFF, val >> 8, val & 0xFF, 0, 0};
  uint16_t crc = crc16(f, 6);
  f[6] = crc & 0xFF;
  f[7] = crc >> 8;

  digitalWrite(RS485_EN, HIGH);
  digitalWrite(RS485_SE, HIGH);

  Serial2.write(f, 8);
  Serial2.flush();
  delay(2);

  digitalWrite(RS485_EN, LOW);
  digitalWrite(RS485_SE, LOW);
}

void stopMotor() {
  writeReg(0x2000, 0x0001);
}

void runForward() {
  writeReg(0x2000, 0x0012);
}

void runReverse() {
  writeReg(0x2000, 0x0022);
}

void mqttTask(void *pvParameters) {
  while (1) {
    if (!client.connected()) reconnect();
    client.loop();

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void vfdTask(void *pvParameters) {
  int lastCmd = -1;
  int lastFreq = -1;
  unsigned long lastSend = 0; 

  while (1) {
    bool changed = false;

    if (command != lastCmd) {
      lastCmd = command;
      changed = true;

      if (command == 1) runForward();
      else if (command == 2) runReverse();
      else if (command == 3) stopMotor();
    }

    if (freq != lastFreq) {
      lastFreq = freq;
      changed = true;
      writeReg(0x2001, freq * 100);
    }
     if (changed) {
      sendTelemetry();  
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RS485_EN, OUTPUT);
  pinMode(RS485_SE, OUTPUT);

  digitalWrite(RS485_EN, LOW);
  digitalWrite(RS485_SE, LOW);

  Serial2.begin(9600, SERIAL_8E1, RXD2, TXD2);

  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  xTaskCreatePinnedToCore(mqttTask, "MQTT", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(vfdTask, "VFD", 4096, NULL, 1, NULL, 1);
}

void loop() {
} 
*/








/*
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h> 

#define RXD2     21
#define TXD2     22
#define RS485_EN 17
#define RS485_SE 19

const char* ssid = "Realme7";
const char* password = "12345678";

const char* mqtt_server = "3.6.38.12";
const char* token = "t7LRlXOEGxmOxqYg7pCK";

WiFiClient espClient;
PubSubClient client(espClient);

volatile int command = 0;   // 1=fwd, 2=rev, 3=stop
volatile int freq = 0;

void setup_wifi() {
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
} 

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32", token, NULL)) {
      client.subscribe("v1/devices/me/attributes");
    } else {
      delay(2000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String msg = String((char*)payload);

  Serial.println("ATTR RX: " + msg);

  // frequency
  int fIndex = msg.indexOf("\"frequency\":");
  if (fIndex >= 0) {
    int start = fIndex + 12;
    int end = msg.indexOf(",", start);
    if (end == -1) end = msg.indexOf("}", start);
    freq = msg.substring(start, end).toInt();
  }

  // state
if (msg.indexOf("forward") >= 0) command = 1;
else if (msg.indexOf("reverse") >= 0) command = 2;
else if (msg.indexOf("stop") >= 0) command = 3;
}

void sendTelemetry(bool success) {
  String payload = "{";

  if (success) {
    String stateStr = "stop";
    if (command == 1) stateStr = "forward";
    else if (command == 2) stateStr = "reverse";

    payload += "\"frequency\":" + String(freq) + ",";
    payload += "\"state\":\"" + stateStr + "\"";
  } else {
    payload += "\"status\":\"fail\"";
  }

  payload += "}";

  client.publish("v1/devices/me/telemetry", payload.c_str());
  Serial.println("TX: " + payload);
}

uint16_t crc16(uint8_t *buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

int writeReg(uint16_t reg, uint16_t val) {
  uint8_t f[8] = {1, 0x06, reg >> 8, reg & 0xFF, val >> 8, val & 0xFF, 0, 0};
  uint16_t crc = crc16(f, 6);
  f[6] = crc & 0xFF;
  f[7] = crc >> 8;

   Serial.print("TX: ");
  for (int i = 0; i < 8; i++) {
    if (f[i] < 0x10) Serial.print("0");
    Serial.print(f[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  while (Serial2.available()) Serial2.read();

  digitalWrite(RS485_EN, HIGH);
  digitalWrite(RS485_SE, HIGH);

  Serial2.write(f, 8);
  Serial2.flush();
  delay(2);

  digitalWrite(RS485_EN, LOW);
  digitalWrite(RS485_SE, LOW);

  uint8_t resp[8];
  int len = 0;
  unsigned long t = millis();

  while (millis() - t < 100) {
    if (Serial2.available()) {
      resp[len++] = Serial2.read();
      if (len >= 8) break;
    }
  }

  Serial.print("RX: ");
  if (len > 0) {
    for (int i = 0; i < len; i++) {
      if (resp[i] < 0x10) Serial.print("0");
      Serial.print(resp[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  } else {
    Serial.println("none");
  }

  if (len < 8) return -1;
  if (resp[0] != 1 || resp[1] != 0x06) return -2;

  uint16_t crcCalc = crc16(resp, 6);
  uint16_t crcRecv = resp[6] | (resp[7] << 8);

  if (crcCalc != crcRecv) return -3;

  for (int i = 0; i < 6; i++) {
    if (resp[i] != f[i]) return -4;
  }

  return 1;  // success
}

void stopMotor() {
  writeReg(0x2000, 0x0001);
}

void runForward() {
  writeReg(0x2000, 0x0012);
}

void runReverse() {
  writeReg(0x2000, 0x0022);
}

void mqttTask(void *pvParameters) {
  while (1) {
    if (!client.connected()) reconnect();
    client.loop();

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void vfdTask(void *pvParameters) {
  int lastCmd = -1;
  int lastFreq = -1;

  while (1) {

    // ---- COMMAND ----
    if (command != lastCmd) {

      int res = -1;

      if (command == 1) res = writeReg(0x2000, 0x0012);
      else if (command == 2) res = writeReg(0x2000, 0x0022);
      else if (command == 3) res = writeReg(0x2000, 0x0001);

      lastCmd = command;
       if (res == 1) {
    Serial.println("CMD OK");
    sendTelemetry(true);     
  } else {
    Serial.println("CMD FAIL");
    sendTelemetry(false);    
  }
}

    // ---- FREQUENCY ----
    if (freq != lastFreq) {

      int res = writeReg(0x2001, freq * 100);

      lastFreq = freq;
      if (res == 1) {
    Serial.println("FREQ OK");
    sendTelemetry(true);    
  } else {
    Serial.println("FREQ FAIL");
    sendTelemetry(false);    
  }
}

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RS485_EN, OUTPUT);
  pinMode(RS485_SE, OUTPUT);

  digitalWrite(RS485_EN, LOW);
  digitalWrite(RS485_SE, LOW);

  Serial2.begin(9600, SERIAL_8E1, RXD2, TXD2);

  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  xTaskCreatePinnedToCore(mqttTask, "MQTT", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(vfdTask, "VFD", 4096, NULL, 1, NULL, 1);
}

void loop() {
} 
*/