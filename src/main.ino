#include <WiFi.h>
#include "time.h"
#include <TM1637Display.h>
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>   // ArduinoJson v6

// ------------------------
// WiFi
// ------------------------
const char *ssid          = "SSID";
const char *ssid_password = "123456789";

// ------------------------
// NTP
// ------------------------
const char* ntpServer = "pool.ntp.org";

// ------------------------
// TM1637 (ajusta a tu ESP32-C3 mini)
// ------------------------
#define CLK 3
#define DIO 4
TM1637Display display(CLK, DIO);

// ------------------------
// Temporizadores
// ------------------------
unsigned long lastTimeUpdate       = 0;
const unsigned long timeUpdateInterval = 1000; // 1 s

// ------------------------
// Estado del “:”
// ------------------------
bool colonState = false;

// ------------------------
// Brillo
// ------------------------
byte currentBrightness = 1;  // brillo actual del display

// Config remota de brillo (por defecto)
int  dayStartHour      = 8;
int  nightStartHour    = 20;
byte dayBrightness     = 3;  // 0–7
byte nightBrightness   = 1;  // 0–7

// URL JSON remoto (S3 con HTTPS)
const char* configUrl =
  "https://jmg-s3-bucket.s3.us-east-1.amazonaws.com/brightness_config.json";

// Temporizador para refrescar config remota
unsigned long lastConfigFetch = 0;
const unsigned long configFetchInterval = 5UL * 60UL * 1000UL; // cada 5 minutos

// Cliente seguro para HTTPS
WiFiClientSecure secureClient;

// ------------------------
// Resincro NTP diaria
// ------------------------
bool resyncDoneToday = false;
int  lastResyncDay   = -1;

// ----------------------------------------------------
// Conexión WiFi
// ----------------------------------------------------
void connectWifi() {
  Serial.println();
  Serial.println("******************************************************");
  Serial.print("Conectando a WiFi SSID: ");
  Serial.println(ssid);
  WiFi.begin(ssid, ssid_password);

  int maxWait = 20000;
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < maxWait) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("✓ WiFi conectada. IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.println("******************************************************");
  } else {
    Serial.println("✗ No se pudo conectar a la WiFi");
    Serial.println("******************************************************");
  }
}

// ----------------------------------------------------
// Inicializar tiempo con NTP (zona horaria España)
// ----------------------------------------------------
void initTime() {
  configTime(0, 0, ntpServer);

  Serial.println("Sincronizando reloj con NTP...");
  struct tm timeinfo;

  const int maxRetries = 10;
  int retries = 0;

  while (!getLocalTime(&timeinfo) && retries < maxRetries) {
    Serial.print(".");
    retries++;
    delay(1000);
  }
  Serial.println();

  if (!getLocalTime(&timeinfo)) {
    Serial.println("ERROR: No se pudo obtener hora NTP tras varios intentos. Reiniciando ESP32...");
    delay(2000);
    ESP.restart();
    return;
  }

  // Zona horaria Europa/Madrid (CET/CEST)
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  if (getLocalTime(&timeinfo)) {
    Serial.printf("Hora local inicial: %02d/%02d/%04d %02d:%02d:%02d\n",
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    Serial.println("Zona horaria: CET/CEST con cambio automático verano/invierno");
  } else {
    Serial.println("Error al obtener hora local tras aplicar TZ. Reiniciando ESP32...");
    delay(2000);
    ESP.restart();
  }
}

// ----------------------------------------------------
// Lectura de configuración remota de brillo (HTTPS)
// ----------------------------------------------------
void fetchBrightnessConfig() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No WiFi, no se actualiza config de brillo.");
    return;
  }

  // Aceptar cualquier certificado (simplificación, menos seguro)
  secureClient.setInsecure();  // Para producción, usar setCACert con la CA de AWS S3[web:42][web:65]

  HTTPClient https;
  Serial.printf("Obteniendo config brillo desde: %s\n", configUrl);
  if (!https.begin(secureClient, configUrl)) {
    Serial.println("Fallo en begin() HTTPS.");
    return;
  }

  int httpCode = https.GET();  // GET HTTPS[web:42][web:77]

  if (httpCode > 0) {
    Serial.printf("HTTPS GET config, código: %d\n", httpCode);
    if (httpCode == HTTP_CODE_OK) {
      String payload = https.getString();
      Serial.println("Respuesta config:");
      Serial.println(payload);

      StaticJsonDocument<256> doc;  // tamaño suficiente para 4 campos simples[web:66][web:69]
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        if (doc.containsKey("dayStartHour"))
          dayStartHour = doc["dayStartHour"];
        if (doc.containsKey("nightStartHour"))
          nightStartHour = doc["nightStartHour"];
        if (doc.containsKey("dayBrightness"))
          dayBrightness = doc["dayBrightness"];
        if (doc.containsKey("nightBrightness"))
          nightBrightness = doc["nightBrightness"];

        // Normalizar rangos
        if (dayStartHour < 0) dayStartHour = 0;
        if (dayStartHour > 23) dayStartHour = 23;
        if (nightStartHour < 0) nightStartHour = 0;
        if (nightStartHour > 23) nightStartHour = 23;

        if (dayBrightness > 7) dayBrightness = 7;
        if (nightBrightness > 7) nightBrightness = 7;

        Serial.printf("Config brillo actualizada: dayStartHour=%d, nightStartHour=%d, dayBrightness=%d, nightBrightness=%d\n",
                      dayStartHour, nightStartHour, dayBrightness, nightBrightness);
      } else {
        Serial.print("Error parseando JSON: ");
        Serial.println(error.c_str());
      }
    } else {
      Serial.printf("Respuesta HTTP no OK: %d\n", httpCode);
    }
  } else {
    Serial.printf("Error HTTPS GET config: %s\n", https.errorToString(httpCode).c_str());
  }

  https.end();
}

// ----------------------------------------------------
// Brillo automático usando config remota
// ----------------------------------------------------
void setAutoBrightness(int hora) {
  byte newBrightness;

  // Día entre dayStartHour y nightStartHour (sin cruzar medianoche)
  if (hora >= dayStartHour && hora < nightStartHour) {
    newBrightness = dayBrightness;
  } else {
    newBrightness = nightBrightness;
  }

  if (newBrightness != currentBrightness) {
    currentBrightness = newBrightness;
    display.setBrightness(currentBrightness);  // rango 0–7[web:10][web:32]
  }
}

// ----------------------------------------------------
// Resincro NTP diaria a las 03:00:00
// ----------------------------------------------------
void handleDailyResync(struct tm &timeinfo) {
  int day = timeinfo.tm_mday;

  if (day != lastResyncDay) {
    resyncDoneToday = false;
    lastResyncDay   = day;
  }

  if (!resyncDoneToday &&
      timeinfo.tm_hour == 3 &&
      timeinfo.tm_min  == 0 &&
      timeinfo.tm_sec  == 0) {
    Serial.println("Resincronizando NTP (resync diaria 03:00)...");
    initTime();
    resyncDoneToday = true;
  }
}

// ----------------------------------------------------
// Mostrar hora en display HH:MM
// ----------------------------------------------------
void printAndShowTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Fallo obteniendo hora local");
    return;
  }

  Serial.printf("%02d/%02d/%04d %02d:%02d:%02d (local)\n",
                timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  int hora   = timeinfo.tm_hour;
  int minuto = timeinfo.tm_min;
  int valorDisplay = hora * 100 + minuto; // HHMM

  // Brillo según config remota
  setAutoBrightness(hora);

  // Resincro diaria
  handleDailyResync(timeinfo);

  uint8_t dotsMask = colonState ? 0b01000000 : 0b00000000;
  display.showNumberDecEx(valorDisplay, dotsMask, true, 4, 0);  // HH:MM[web:10]
}

// ----------------------------------------------------
// setup()
// ----------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("https://github.com/jorgemachadogallas-ux/DigitalClock");

  display.setBrightness(currentBrightness);
  display.showNumberDec(0, true);  // 0000 al inicio[web:10]

  connectWifi();

  if (WiFi.status() == WL_CONNECTED) {
    initTime();
    fetchBrightnessConfig();  // primera lectura de config remota
    printAndShowTime();
    Serial.println("Reloj listo: HH:MM en TM1637 con ':' parpadeando, DST y brillo remoto.");
  } else {
    Serial.println("Sin WiFi: el reloj usará solo el RTC interno hasta que se recupere la conexión.");
  }
}

// ----------------------------------------------------
// loop()
// ----------------------------------------------------
void loop() {
  // Reintentar WiFi si se pierde
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    if (WiFi.status() == WL_CONNECTED) {
      initTime();
      fetchBrightnessConfig();  // actualizar config al recuperar conexión
    }
  }

  // Refrescar config remota periódicamente
  if (millis() - lastConfigFetch >= configFetchInterval) {
    fetchBrightnessConfig();
    lastConfigFetch = millis();
  }

  // Cada 1 s: parpadeo ":" y actualización de hora
  if (millis() - lastTimeUpdate >= timeUpdateInterval) {
    colonState = !colonState;
    printAndShowTime();
    lastTimeUpdate = millis();
  }

  delay(50);
}
