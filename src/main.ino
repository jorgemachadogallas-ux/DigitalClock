#include <WiFi.h>
#include "time.h"
#include <TM1637Display.h>
#include <Arduino.h>

const char *ssid = "SSID";
const char *ssid_password = "PASSWORD";

// Servidor NTP
const char* ntpServer = "pool.ntp.org";

// Temporizador para actualizar display y parpadeo
unsigned long lastTimeUpdate = 0;
const unsigned long timeUpdateInterval = 1000; // 1 segundo

// Pines TM1637 (ajusta a tu ESP32-C3 mini; 3 y 4 son RTC/JTAG pero usables si no usas JTAG)
#define CLK 3
#define DIO 4

TM1637Display display(CLK, DIO);

// Estado del “:”
bool colonState = false;

// Brillo actual (para no llamar setBrightness si no cambia)
byte currentBrightness = 1;

// Control de resincro NTP diaria
bool resyncDoneToday = false;
int lastResyncDay = -1;

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

// Inicializar tiempo con NTP y zona horaria España (CET/CEST con DST automático)
void initTime() {
  // Sin offset aquí, solo NTP
  configTime(0, 0, ntpServer);

  Serial.println("Sincronizando reloj con NTP...");
  struct tm timeinfo;

  const int maxRetries = 10;          // 10 intentos
  int retries = 0;

  // Espera a que haya primera sincronización (máx ~10 s)
  while (!getLocalTime(&timeinfo) && retries < maxRetries) {
    Serial.print(".");
    retries++;
    delay(1000);
  }
  Serial.println();

  if (!getLocalTime(&timeinfo)) {
    Serial.println("ERROR: No se pudo obtener hora NTP tras varios intentos. Reiniciando ESP32...");
    delay(2000);          // pequeño margen para ver el mensaje
    ESP.restart();        // reinicio por software [web:200][web:204]
    return;               // por claridad, aunque tras restart no sigue
  }

  // Zona horaria Europa/Madrid (CET/CEST con DST automático)
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  // Actualizar de nuevo tras aplicar TZ
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


// Ajuste de brillo automático, evitando llamadas redundantes
void setAutoBrightness(int hora) {
  byte newBrightness = (hora >= 8 && hora <= 20) ? 7 : 1;  // día / noche [web:62][web:71]
  if (newBrightness != currentBrightness) {
    currentBrightness = newBrightness;
    display.setBrightness(currentBrightness);
  }
}

// Resincro NTP una vez al día a las 03:00:00
void handleDailyResync(struct tm &timeinfo) {
  int day = timeinfo.tm_mday;

  // Si ha cambiado de día, resetea flag
  if (day != lastResyncDay) {
    resyncDoneToday = false;
    lastResyncDay = day;
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

// Función que imprime en Serial y muestra en el display HH:MM
void printAndShowTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Fallo obteniendo hora local");
    return;
  }

  // Log en Serial
  Serial.printf("%02d/%02d/%04d %02d:%02d:%02d (local)\n",
    timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  int hora   = timeinfo.tm_hour;
  int minuto = timeinfo.tm_min;
  int valorDisplay = hora * 100 + minuto; // HHMM

  // Brillo automático día/noche
  setAutoBrightness(hora);

  // Resincro NTP controlada
  handleDailyResync(timeinfo);

  // Máscara del “:”
  uint8_t dotsMask = colonState ? 0b01000000 : 0b00000000;

  // Mostrar HH:MM con o sin “:” (4 dígitos, desde la posición 0)
  display.showNumberDecEx(valorDisplay, dotsMask, true, 4, 0); // [web:56][web:62]
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  display.setBrightness(currentBrightness);
  display.showNumberDec(0, true);  // 0000 al inicio

  connectWifi();

  if (WiFi.status() == WL_CONNECTED) {
    initTime();          // NTP + zona horaria con DST automático [web:124]
    printAndShowTime();  // primera actualización
    Serial.println("Reloj listo: HH:MM en TM1637 con ':' parpadeando y DST automático");
  } else {
    Serial.println("Sin WiFi: el reloj usará solo el RTC interno hasta que se recupere la conexión.");
  }
}

void loop() {
  // Reintentar WiFi si se pierde
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    if (WiFi.status() == WL_CONNECTED) {
      initTime();  // resincronizar al recuperar WiFi [web:182]
    }
  }

  // Cada 1 segundo: cambiar estado del “:” y actualizar hora
  if (millis() - lastTimeUpdate >= timeUpdateInterval) {
    colonState = !colonState;      // alternar ON/OFF del “:”
    printAndShowTime();            // refrescar Serial + display
    lastTimeUpdate = millis();
  }

  delay(50);
}
