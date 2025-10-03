#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

#define LED 2
#define LED_R 14
#define LED_G 12
#define LED_B 13

const char* ssid = "ssid";      // WLAN-Name,
const char* password = "passwd";    // WLAN-Passwort
// Upload: pio run --target upload --upload-port ipaddresslocal

ESP8266WebServer server(80);

int lerp(int start, int end, float t)
{
  if (t <= 0.0f) return start;
  if (t >= 1.0f) return end;
  return (int)roundf(start + t * (end - start));
}

void RGBtoHSV(float fR, float fG, float fB, float& fH, float& fS, float& fV) {
  fR /= 255.0f; fG /= 255.0f; fB /= 255.0f;

  float fCMax = max(max(fR, fG), fB);
  float fCMin = min(min(fR, fG), fB);
  float fDelta = fCMax - fCMin;

  if(fDelta > 0.0f) {
    if(fCMax == fR) {
      fH = 60.0f * fmodf(((fG - fB) / fDelta), 6.0f);
    } else if(fCMax == fG) {
      fH = 60.0f * (((fB - fR) / fDelta) + 2.0f);
    } else { // fCMax == fB
      fH = 60.0f * (((fR - fG) / fDelta) + 4.0f);
    }

    if(fCMax > 0.0f) {
      fS = fDelta / fCMax;
    } else {
      fS = 0.0f;
    }

    fV = fCMax;
  } else {
    fH = 0.0f;
    fS = 0.0f;
    fV = fCMax;
  }

  if(fH < 0.0f) {
    fH += 360.0f;
  }
}

void HSVtoRGB(float& fR, float& fG, float& fB, float fH, float fS, float fV) {
  float fC = fV * fS; // Chroma
  float fHPrime = fmodf(fH / 60.0f, 6.0f);
  if (fHPrime < 0) fHPrime += 6.0f;
  float fX = fC * (1.0f - fabsf(fmodf(fHPrime, 2.0f) - 1.0f));
  float fM = fV - fC;

  if(0.0f <= fHPrime && fHPrime < 1.0f) {
    fR = fC; fG = fX; fB = 0.0f;
  } else if(1.0f <= fHPrime && fHPrime < 2.0f) {
    fR = fX; fG = fC; fB = 0.0f;
  } else if(2.0f <= fHPrime && fHPrime < 3.0f) {
    fR = 0.0f; fG = fC; fB = fX;
  } else if(3.0f <= fHPrime && fHPrime < 4.0f) {
    fR = 0.0f; fG = fX; fB = fC;
  } else if(4.0f <= fHPrime && fHPrime < 5.0f) {
    fR = fX; fG = 0.0f; fB = fC;
  } else if(5.0f <= fHPrime && fHPrime < 6.0f) {
    fR = fC; fG = 0.0f; fB = fX;
  } else {
    fR = 0.0f; fG = 0.0f; fB = 0.0f;
  }

  fR += fM;
  fG += fM;
  fB += fM;

  fR *= 255.0f; fG *= 255.0f; fB *= 255.0f;
}

struct _SelectedState
{
  enum Animation {
    STATIC,
    BLINK,
    FADE,
    RAINBOW,
    PULSE
  } animation = _SelectedState::Animation::STATIC;

  uint8_t startCol_r = 0;
  uint8_t startCol_g = 0;
  uint8_t startCol_b = 0;
  uint8_t endCol_r = 0;
  uint8_t endCol_g = 0;
  uint8_t endCol_b = 0;
  int speed = 0;
  float t = 0;
};

String mainPageHTML;
_SelectedState LED_STATE;

float progress(unsigned long& start, int duration, bool pingpong = false) {
  unsigned long now = millis();
  float t = (now - start) / (float)duration;

  if (pingpong) {
    // back and forth (0→1→0)
    t = fmod(t, 2.0f);
    if (t > 1.0f) t = 2.0f - t;
  } else {
    // wrap (0→1→0)
    t = fmod(t, 1.0f);
  }

  // so now - start doesnt get too big
  if (t < 1e-7)
  {
    start = now;
  }

  return constrain(t, 0.0f, 1.0f);
}




class _Strip
{
  public:
    int curr_val_r = 0;
    int curr_val_g = 0;
    int curr_val_b = 0;
    bool fwd = true;
    ulong prevmillis = 0;

    void setup()
    {
      pinMode(LED_R, OUTPUT);
      pinMode(LED_G, OUTPUT);
      pinMode(LED_B, OUTPUT);
      prevmillis = millis();
    }

    void write_col(int r, int g, int b)
    {
      r = constrain(r, 0, 255);
      g = constrain(g, 0, 255);
      b = constrain(b, 0, 255);

      analogWrite(LED_R, r);
      analogWrite(LED_G, g);
      analogWrite(LED_B, b);
      curr_val_r = r;
      curr_val_g = g;
      curr_val_b = b;
    }

    void handleState()
    {
      if (LED_STATE.animation == _SelectedState::Animation::STATIC)
      {
        write_col(LED_STATE.startCol_r, LED_STATE.startCol_g, LED_STATE.startCol_b);
      }
      else if (LED_STATE.animation == _SelectedState::Animation::BLINK)
      {
        unsigned long currmillis = millis();

        if ((int)(currmillis - prevmillis) < LED_STATE.speed && (curr_val_r != 0 || curr_val_b != 0 || curr_val_g != 0))
        {
          write_col(0,0,0);
        }
        else if ((int)(currmillis - prevmillis) >= LED_STATE.speed && (currmillis - prevmillis) <= (unsigned long)(1.5 * LED_STATE.speed))
        {
          int r = lerp(LED_STATE.startCol_r, LED_STATE.endCol_r, LED_STATE.t);
          int g = lerp(LED_STATE.startCol_g, LED_STATE.endCol_g, LED_STATE.t);
          int b = lerp(LED_STATE.startCol_b, LED_STATE.endCol_b, LED_STATE.t);

          write_col(r,g,b);
        }
        else if ((int)(currmillis - prevmillis) > (1.5 * LED_STATE.speed))
        {
          float precision = 0.1f;
          if (LED_STATE.t + precision * ((int)fwd * 2 - 1) > 1.0f) fwd = false;
          if (LED_STATE.t + precision * ((int)fwd * 2 - 1) < 0.0f) fwd = true;

          LED_STATE.t = LED_STATE.t + precision * ((int)fwd * 2 - 1);
          prevmillis = currmillis;
        }
      }
      else if (LED_STATE.animation == _SelectedState::Animation::FADE)
      {
        LED_STATE.t = progress(prevmillis, LED_STATE.speed, true);

        int r = lerp(LED_STATE.startCol_r, LED_STATE.endCol_r, LED_STATE.t);
        int g = lerp(LED_STATE.startCol_g, LED_STATE.endCol_g, LED_STATE.t);
        int b = lerp(LED_STATE.startCol_b, LED_STATE.endCol_b, LED_STATE.t);
        write_col(r,g,b);
      }
      else if (LED_STATE.animation == _SelectedState::Animation::RAINBOW)
      {
        LED_STATE.t = progress(prevmillis, LED_STATE.speed, true);

        float r = 0.0f, g = 0.0f, b = 0.0f;
        float Sh = 0.0f, Ss = 0.0f, Sv = 0.0f;
        float Eh = 0.0f, Es = 0.0f, Ev = 0.0f;
        float h = 0.0f, s = 0.0f, v = 0.0f;

        RGBtoHSV(LED_STATE.startCol_r, LED_STATE.startCol_g, LED_STATE.startCol_b, Sh, Ss, Sv);
        RGBtoHSV(LED_STATE.endCol_r, LED_STATE.endCol_g, LED_STATE.endCol_b, Eh, Es, Ev);

        // lerp HSV (h might wrap; simple lerp - for true hue shortest-path you'd need extra logic)
        h = (float)lerp((int)Sh, (int)Eh, LED_STATE.t);
        s = (float)lerp((int)(Ss*1000), (int)(Es*1000), LED_STATE.t) / 1000.0f;
        v = (float)lerp((int)(Sv*1000), (int)(Ev*1000), LED_STATE.t) / 1000.0f;

        HSVtoRGB(r,g,b, h, s, v);
        write_col((int)roundf(r), (int)roundf(g), (int)roundf(b));
      }
      else if (LED_STATE.animation == _SelectedState::Animation::PULSE)
      {
        LED_STATE.t = progress(prevmillis, LED_STATE.speed, true);
      
        int r = lerp(LED_STATE.startCol_r, LED_STATE.endCol_r, LED_STATE.t);
        int g = lerp(LED_STATE.startCol_g, LED_STATE.endCol_g, LED_STATE.t);
        int b = lerp(LED_STATE.startCol_b, LED_STATE.endCol_b, LED_STATE.t);

        float pulseVal = (sinf(millis() * (2.0f / max(1, LED_STATE.speed))) + 1.0f) * 0.5f;
        r = (int)roundf(r * pulseVal);
        g = (int)roundf(g * pulseVal);
        b = (int)roundf(b * pulseVal);

        write_col(r,g,b);
      }
    }
} Strip;

void MainPG();
void rest();
void state();

void setup() {
  analogWriteRange(255);
  Strip.setup();
  Serial.begin(115200);

  Serial.println("\n\n" + String(ssid));
  Serial.println(String(password));
  Serial.println("\nConnecting...");

  WiFi.mode(WIFI_STA);

  delay(1000);
  
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult(300000) != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  if( LittleFS.begin() ){
    Serial.println("\n\nFS init success");
  }else{
    Serial.println("\n\nFS init failure");
  }

  File mainPage = LittleFS.open("index.html", "r");
  mainPageHTML = mainPage.readString();

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }
    
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });

  ArduinoOTA.begin();

  server.on("/getState", state);
  server.on("/api", rest);
  server.on("/", MainPG);

  server.begin();
}

void MainPG()
{
  server.send(200, "text/html", mainPageHTML);
}

int StrToHex(const char * str)
{
  return (int) strtol(str, 0, 16);
}

String getColStr(int r, int g, int b)
{
  return (String(r) + ";" + String(g) + ";" + String(b) + ";");
}


String getNameAN(uint an)
{
  switch (an) {
    case 0:
      return "STATIC";
      break;
    case 1:
      return "BLINK";
      break;
    case 2:
      return "FADE";
      break;
    case 3:
      return "RAINBOW";
      break;
    case 4:
      return "PULSE";
      break;
  }

  return "NIGGA";
}

void state()
{
  String resp = "";

  resp += getColStr(Strip.curr_val_r, Strip.curr_val_g, Strip.curr_val_b);
  resp += getNameAN(LED_STATE.animation) + ";";
  resp += String(LED_STATE.speed) + ";";
  resp += getColStr(LED_STATE.startCol_r, LED_STATE.startCol_g, LED_STATE.startCol_b);
  resp += getColStr(LED_STATE.endCol_r, LED_STATE.endCol_g, LED_STATE.endCol_b);
  
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("pragma","no-cache");
  server.sendHeader("Expires", "-1");
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.send(200, "text/plain", resp.c_str());
}

void rest()
{
  if (server.args() == 4)
  {
    if (String(server.arg("speed")).toInt() > 0)
    {
      LED_STATE.speed = String(server.arg("speed")).toInt();
    }

    if (server.arg("animation") == "static")
    {
      LED_STATE.animation = LED_STATE.STATIC;
    } else if (server.arg("animation") == "blink")
    {
      LED_STATE.animation = LED_STATE.BLINK;
    } else if (server.arg("animation") == "fade")
    {
      LED_STATE.animation = LED_STATE.FADE;
    } else if (server.arg("animation") == "rainbow")
    {
      LED_STATE.animation = LED_STATE.RAINBOW;
    } else if (server.arg("animation") == "pulse")
    {
      LED_STATE.animation = LED_STATE.PULSE;
    }

    if (server.arg("end").length() == 6)
    {
      LED_STATE.endCol_r = StrToHex(server.arg("end").substring(0, 2).c_str());
      LED_STATE.endCol_g = StrToHex(server.arg("end").substring(2, 4).c_str());
      LED_STATE.endCol_b = StrToHex(server.arg("end").substring(4, 6).c_str());    
    }
    if (server.arg("start").length() == 6)
    {
      LED_STATE.startCol_r = StrToHex(server.arg("start").substring(0, 2).c_str());
      LED_STATE.startCol_g = StrToHex(server.arg("start").substring(2, 4).c_str());
      LED_STATE.startCol_b = StrToHex(server.arg("start").substring(4, 6).c_str());   
    }

    LED_STATE.t = 0;

    server.send ( 200, "text/plain", "OK");
  }
  else
  {
    server.send(200, "text/plain", "NIGGER BALLS");
  }
}

void loop() {
  server.handleClient();
  Strip.handleState();
  ArduinoOTA.handle();
}

