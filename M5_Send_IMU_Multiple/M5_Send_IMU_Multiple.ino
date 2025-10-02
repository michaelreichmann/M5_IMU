#include "M5StickCPlus2.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <Preferences.h>

//store id on shutdown
Preferences prefs;

static const char* NVS_NS  = "m5cfg";
static const char* KEY_ID  = "channelId";

unsigned long lastIdChangeMs = 0;
const unsigned long SAVE_DELAY_MS = 500;   // coalesce rapid presses
bool idDirty = false;

//WIFI config: change to your WIFI settings
const char* WIFI_SSID     = "IMU_In";
const char* WIFI_PASSWORD = "sonification";

//find IP: terminal: ifconfig en0
WiFiUDP Udp;
//Pi static IP 
const IPAddress OUT_IP(192, 168, 1, 50);

//send to the Pi's fan-in port (9000), not 8000
const uint16_t OUT_PORT = 9000;           
const uint16_t LOCAL_PORT = 9000;       

//device
int channelId = 0;                   
unsigned long lastInfoMs = 0;
int batteryPct = 0;
int infoRefreshRate = 5000;

//load saved device id
void loadchannelId() 
{
  if(prefs.begin(NVS_NS, /*readOnly=*/true)) 
  {
    channelId = prefs.getInt(KEY_ID, channelId);
    prefs.end();
  }
}

void savechannelId() 
{
  if (prefs.begin(NVS_NS, /*readOnly=*/false)) {
    prefs.putInt(KEY_ID, channelId);
    prefs.end();
  }
  idDirty = false;
}

//drawing
void drawchannelId() 
{
  auto& d = StickCP2.Display;
  d.fillScreen(BLACK);
  d.setTextDatum(middle_center);
  d.setTextColor(WHITE);
  d.setTextFont(&fonts::Orbitron_Light_24);
  d.setTextSize(2);
  d.drawString("id: " + String(channelId), d.width() / 2, d.height() / 2);
}

void drawBattery() 
{
  int pct = StickCP2.Power.getBatteryLevel();

  auto& d = StickCP2.Display;
  const int pad = 2;
  const int boxW = 60; 
  const int boxH = 28;
  int x0 = d.width() - boxW - pad;
  int y0 = pad;
  d.fillRect(x0, y0, boxW, boxH, BLACK);

  d.setTextDatum(top_right);

  //color based on percentage
  if(pct > 75)
  {
    d.setTextColor(GREEN, BLACK);
  }
  else if(pct > 25)
  {
    d.setTextColor(YELLOW, BLACK);
  }
  else
  {
    d.setTextColor(RED, BLACK);
  }

  d.setTextFont(&fonts::Orbitron_Light_24);
  d.setTextSize(1);
  d.drawString(String(pct) + "%", d.width() - pad, pad);
}

void drawWiFiStatus() 
{
  auto& d = StickCP2.Display;
  d.setTextDatum(top_left);
  if (WiFi.status() == WL_CONNECTED) 
  {
    d.setTextColor(BLUE, BLACK);
    d.drawString("WiFi", 2, 2); 
  } 
  else 
  {
    d.setTextColor(RED, BLACK);
    d.drawString("X", 2, 2);
  }
}

//network
void wifiEnsureConnected()
{
  if (WiFi.status() == WL_CONNECTED) 
  {
    return;
  }
  
  static unsigned long lastAttempt = 0;
  unsigned long now = millis();
  if (now - lastAttempt < 2000) 
  {
    return;
  }
  
  lastAttempt = now;

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup() 
{
  auto cfg = M5.config();
  StickCP2.begin(cfg);

  StickCP2.Display.setRotation(1);
  StickCP2.Display.fillScreen(BLACK);

  StickCP2.Display.setBrightness(1);

  //start Wi-Fi
  WiFi.mode(WIFI_STA);

  //keep radio awake for lower latency
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(false);
  wifiEnsureConnected();

  Udp.begin(LOCAL_PORT);

  loadchannelId();
  drawchannelId();
  drawBattery();
  drawWiFiStatus();
}

void loop() 
{
  StickCP2.update();
  wifiEnsureConnected();

  //buttons
  //increase id
  if (StickCP2.BtnA.wasPressed()) 
  {
    channelId++;
    StickCP2.Speaker.tone(2500, 100);
    drawchannelId();

    //save id
    lastIdChangeMs = millis();
    idDirty = true;

    //redraw info
    drawBattery();
    drawWiFiStatus();

  }
  //decrease id
  if (StickCP2.BtnB.wasPressed()) 
  {
    //dont go below 0
    if (channelId > 0)
    {
      channelId--;
    } 
    
    StickCP2.Speaker.tone(2000, 100);
    drawchannelId();

    //save id
    lastIdChangeMs = millis();
    idDirty = true;

    //redraw info
    drawBattery();
    drawWiFiStatus();
  }

  //store id
  if(idDirty && (millis() - lastIdChangeMs >= SAVE_DELAY_MS)) 
  {
    savechannelId();
  }

  //info
  unsigned long now = millis();
  if (now - lastInfoMs >= infoRefreshRate) 
  {
    lastInfoMs = now;

    //draw battery
    drawBattery();
    
    //draw WIFI status
    drawWiFiStatus(); 

  }

  //IMU
  if (StickCP2.Imu.update()) 
  {
    auto data = StickCP2.Imu.getImuData();

    //accel
    float ax = data.accel.x;
    float ay = data.accel.y;
    float az = data.accel.z;

    //gyro
    float gx = data.gyro.x;
    float gy = data.gyro.y;
    float gz = data.gyro.z;

    if (WiFi.status() == WL_CONNECTED) 
    {
      // /m5/<channelId>/imu  -> [ax ay az gx gy gz]
      String addr = "/m5/" + String(channelId) + "/imu";
      OSCMessage msg(addr.c_str());
      msg.add(ax); msg.add(ay); msg.add(az);
      msg.add(gx); msg.add(gy); msg.add(gz);

      Udp.beginPacket(OUT_IP, OUT_PORT);  // CHANGED: OUT_PORT = 9000
      msg.send(Udp);
      Udp.endPacket();
      msg.empty();
    }
  }

  delay(0);
}
