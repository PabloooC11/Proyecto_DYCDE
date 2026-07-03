#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_NeoPixel.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <SPI.h>
#include <WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// --- CONFIGURACIÓN WI-FI ---
#define WLAN_SSID       
#define WLAN_PASS       

// --- CREDENCIALES ADAFRUIT IO ---
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883

#define NEOPIXEL_PIN    27  
#define NUM_PIXELS       1  
Adafruit_NeoPixel pixel(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// --- PINES HARDWARE ORIGINALES DE TU PLACA ---
#define TFT_CS        5
#define TFT_DC        2
#define TFT_RST       15 
#define ONE_WIRE_BUS  4
#define PH_PIN        34
#define TDS_PIN       35 
#define TDS_VCC_PIN   25 // Pin del transistor de control
#define MQ135_PIN     32 // Mapeado físicamente a tu nuevo MQ-2

#define BUZZER_PIN      13 
#define LED_ALERTA_PIN  12 
#define LED_W_TEMP_PIN  14 // Indicador de Clima
#define LED_AIR_PIN     26 // Indicador de Aire
#define LED_MQ_PIN      33 // Indicador de pH

#define SEALEVELPRESSURE_HPA (1013.25)

// Constantes de Brillo PWM para disipar menos potencia en el regulador
const int BRILLO_TENUE = 15;   // LED encendido de fondo muy tenue (Ahorro de energía)
const int BRILLO_ALERTA = 255; // LED a máxima potencia en caso de error o peligro

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

Adafruit_MQTT_Publish pub_tempAgua = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/h2origin.temperatura-del-agua");
Adafruit_MQTT_Publish pub_ph       = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/h2origin.ph-del-agua");
Adafruit_MQTT_Publish pub_tds      = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/h2origin.tds-agua");
Adafruit_MQTT_Publish pub_tempAire = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/h2origin.temperatura-del-aire");
Adafruit_MQTT_Publish pub_humedad  = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/h2origin.humedad-del-aire");
Adafruit_MQTT_Publish pub_mq135    = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/h2origin.calidad-del-aire");

Adafruit_GC9A01A tft = Adafruit_GC9A01A(TFT_CS, TFT_DC, TFT_RST);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature waterSensors(&oneWire);
Adafruit_BME680 bme; 

float pendiente = 0.186; 
bool bmeHardwareExiste = false;

// Variables globales para datos estables
float phVal = 3.3; 
float tdsPpm = 46.0;
float tempAgua = 26.3;
float tempAire = 25.0;
float humAire = 55.0;
float presionAire = 1013.0;
float altitudAire = 1500.0;
int purezaAirePorcentaje = 28; 
bool bmeError = true;

unsigned long previoMillisIoT = 0;
unsigned long previoMillisDisplay = 0;

void intentarConexionWiFi();
void MQTT_connect();
void dibujarInterfazBase();
void actualizarDatosPantalla();

void setup() {
  delay(1000); 
  Serial.begin(115200);
  
  pinMode(BUZZER_PIN, OUTPUT);     digitalWrite(BUZZER_PIN, LOW);
  pinMode(LED_ALERTA_PIN, OUTPUT); digitalWrite(LED_ALERTA_PIN, LOW);
  
  // Solución Paralelo: Transistor encendido permanentemente
  pinMode(TDS_VCC_PIN, OUTPUT);     
  digitalWrite(TDS_VCC_PIN, HIGH); 

  analogSetPinAttenuation(PH_PIN, ADC_11db);
  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  analogSetPinAttenuation(MQ135_PIN, ADC_11db);
  
  waterSensors.begin();
  
  tft.begin();
  tft.setRotation(3); // Rotación de 90° a la izquierda
  
  dibujarInterfazBase();
  intentarConexionWiFi();

  if (bme.begin(0x76)) {
    bmeHardwareExiste = true;
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150);
  }

  pixel.begin();
  pixel.setBrightness(80); // Subimos un poco el brillo base del NeoPixel para notar los tonos
  pixel.show();
}

void loop() {
  unsigned long currentMillis = millis();

  if (WiFi.status() == WL_CONNECTED) {
    MQTT_connect();
  }

  // --- MUESTREO DE SENSORES Y PANTALLA (CADA 1 SEGUNDO) ---
  if (currentMillis - previoMillisDisplay >= 1000) {
    previoMillisDisplay = currentMillis;

    // 1. Medición de pH (Calibrado con tu agua de grifo real a 1.10V)
    long sumaPH = 0;
    for(int i=0; i<15; i++) { sumaPH += analogRead(PH_PIN); delay(10); }
    float vPH = (sumaPH / 15.0) * (3.3 / 4095.0);
    phVal = 7.0 + ((1.10 - vPH) / 0.18);
    if(phVal < 0) phVal = 0;
    if(phVal > 14) phVal = 14;

    // 2. Medición de TDS
    long sumaTDS = 0;
    for(int i=0; i<15; i++) { sumaTDS += analogRead(TDS_PIN); delayMicroseconds(50); }
    float vTDS = (sumaTDS / 15.0) * (3.3 / 4095.0);
    tdsPpm = (133.42 * pow(vTDS, 3) - 255.86 * pow(vTDS, 2) + 857.39 * vTDS) * 0.5;

    // 3. Temp Agua
    waterSensors.requestTemperatures();
    float tempAguaSurgida = waterSensors.getTempCByIndex(0);
    if (tempAguaSurgida > -50.0 && tempAguaSurgida < 85.0) {
      tempAgua = tempAguaSurgida;
    }

    // 4. BME680 (Clima / Aire)
    if (bmeHardwareExiste) {
      bmeError = !bme.performReading();
      if (!bmeError) {
        tempAire = bme.temperature;
        humAire = bme.humidity;
        presionAire = bme.pressure / 100.0;
        altitudAire = bme.readAltitude(SEALEVELPRESSURE_HPA);
      }
    }

    // 5. Mapeado Seguro para el nuevo MQ-2 (Con Potenciómetro a 3.3K)
    float mqVolts = analogRead(MQ135_PIN) * (3.3 / 4095.0);
    float vAireLimpio = 0.65; // Voltaje ideal para un 100% de pureza
    float vHumoDenso  = 1.10; // Voltaje estimado para 0% de pureza

    // Si el voltaje está en el rango útil, calculamos con decimales exactos
    if (mqVolts < vAireLimpio) {
      purezaAirePorcentaje = 90;
    } else if (mqVolts > vHumoDenso) {
      purezaAirePorcentaje = 0;
    } else {
      // Regla de tres flotante: da transiciones ultra suaves paso a paso
      purezaAirePorcentaje = 90 - ((mqVolts - vAireLimpio) / (vHumoDenso - vAireLimpio)) * 100;
    }
    // =============================================================
    // GESTIÓN DINÁMICA DE LEDS ATENUADOS, BUZZER Y NEOPIXEL
    // =============================================================

    // --- LED INDICADOR DE PH (Pin 33) ---
    if (phVal < 6.5 || phVal > 8.5) {
      analogWrite(LED_MQ_PIN, BRILLO_ALERTA); // Alerta por agua fuera de rango seguro
    } else {
      analogWrite(LED_MQ_PIN, BRILLO_TENUE);  // Estado normal: Siempre encendido pero atenuado
    }

    // --- ESCALA CROMÁTICA DEL NEOPIXEL SEGÚN EL PH ---
    if (phVal >= 0.0 && phVal < 3.0) {
      pixel.setPixelColor(0, pixel.Color(255, 0, 0));       // Rojo (Ácido Fuerte)
    } else if (phVal >= 3.0 && phVal < 5.5) {
      pixel.setPixelColor(0, pixel.Color(255, 128, 0));     // Naranja (Ácido Moderado)
    } else if (phVal >= 5.5 && phVal < 6.8) {
      pixel.setPixelColor(0, pixel.Color(255, 255, 0));     // Amarillo (Ácido Leve)
    } else if (phVal >= 6.8 && phVal <= 7.8) {
      pixel.setPixelColor(0, pixel.Color(0, 255, 0));       // Verde (Neutro / Agua Óptima)
    } else if (phVal > 7.8 && phVal <= 9.5) {
      pixel.setPixelColor(0, pixel.Color(0, 255, 255));     // Turquesa / Azul Claro
    } else if (phVal > 9.5 && phVal <= 11.5) {
      pixel.setPixelColor(0, pixel.Color(0, 0, 255));       // Azul Oscuro
    } else if (phVal > 11.5 && phVal <= 14.0) {
      pixel.setPixelColor(0, pixel.Color(128, 0, 255));     // Púrpura (Alcalino Fuerte)
    }
    pixel.show();

    // --- LED INDICADOR DE AIRE (Pin 26) Y ALERTA SONORA (BUZZER Pin 13) ---
    if(purezaAirePorcentaje < 0)   purezaAirePorcentaje = 0;
    if(purezaAirePorcentaje > 100) purezaAirePorcentaje = 100;


    // =============================================================
    // GESTIÓN DE ALERTAS DEL MQ-2 (BUZZER Y LED CONTINUOS)
    // =============================================================
    
    // Si la calidad del aire cae por debajo del 65%:
    if (purezaAirePorcentaje < 65) {
      analogWrite(LED_AIR_PIN, BRILLO_ALERTA); // LED del Aire a máxima potencia (255)
      digitalWrite(BUZZER_PIN, HIGH);          // Buzzer encendido FIJO, continuo y sin pausas
    } else {
      analogWrite(LED_AIR_PIN, BRILLO_TENUE);  // Estado normal: LED encendido pero muy bajito (15)
      digitalWrite(BUZZER_PIN, LOW);           // Buzzer completamente apagado y en silencio
    }

    // --- LED INDICADOR DE CLIMA (Pin 14) ---
    if (tempAire > 30.0) {
      analogWrite(LED_W_TEMP_PIN, BRILLO_ALERTA); // Habitación muy caliente: Máximo brillo
    } else {
      analogWrite(LED_W_TEMP_PIN, BRILLO_TENUE);  // Temperatura ambiente normal: LED de fondo atenuado
    }

    // Refrescar los gráficos y números en la pantalla redonda
    actualizarDatosPantalla();
  }

  // --- ENVÍO IOT A ADAFRUIT CADA 15 SEGUNDOS ---
  if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
    if (currentMillis - previoMillisIoT >= 15000) {
      previoMillisIoT = currentMillis;
      pub_tempAgua.publish(tempAgua);
      pub_ph.publish(phVal);
      pub_tds.publish(tdsPpm);
      pub_mq135.publish((double)purezaAirePorcentaje);
      if (!bmeError) {
        pub_tempAire.publish(tempAire);
        pub_humedad.publish(humAire);
      }
    }
  }
}

void dibujarInterfazBase() {
  tft.fillScreen(GC9A01A_BLACK);
  // Guía perimetral punteada fina estética circular
  tft.drawCircle(120, 120, 118, 0x39E7);
}

void actualizarDatosPantalla() {
  // -------------------------------------------------------------
  // COLUMNA IZQUIERDA: SISTEMA HIDROLÓGICO
  // -------------------------------------------------------------
  tft.fillRect(25, 30, 95, 95, GC9A01A_BLACK);
  tft.setTextSize(1); tft.setTextColor(GC9A01A_WHITE);
  
  // Temp Agua
  tft.setCursor(45, 33); tft.print("TEMP AGUA:");
  tft.setTextColor(GC9A01A_CYAN); tft.setTextSize(2);
  tft.setCursor(45, 45); tft.print(tempAgua, 1); tft.setTextSize(1); tft.print(" C");
  
  // pH Agua
  tft.setTextColor(GC9A01A_WHITE); tft.setTextSize(1);
  tft.setCursor(43, 68); tft.print("pH AGUA:");
  tft.setTextColor(0xFBE0); // Color Naranja/Rojo original para el pH
  tft.setTextSize(2);
  tft.setCursor(43, 80); tft.print(phVal, 1);
  
  // TDS Agua
  tft.setTextColor(GC9A01A_WHITE); tft.setTextSize(1);
  tft.setCursor(35, 103); tft.print("TDS AGUA:");
  tft.setTextSize(2);
  tft.setCursor(35, 115); tft.print((int)tdsPpm); tft.setTextSize(1); tft.print("ppm");

  // -------------------------------------------------------------
  // COLUMNA DERECHA: MONITOREO ATMOSFÉRICO
  // -------------------------------------------------------------
  tft.fillRect(122, 45, 95, 115, GC9A01A_BLACK);
  tft.setTextSize(1); tft.setTextColor(GC9A01A_WHITE);
  
  // Temp Aire (¡Modificado con 1 decimal exacto!)
  tft.setCursor(125, 48); tft.print("TEMP AIRE:");
  tft.setTextColor(GC9A01A_ORANGE); tft.setTextSize(2);
  tft.setCursor(125, 60); tft.print(tempAire, 1); tft.setTextSize(1); tft.print(" C");
  
  // Humedad Aire
  tft.setTextColor(GC9A01A_WHITE); tft.setTextSize(1);
  tft.setCursor(115, 83); tft.print("HUMEDAD AIRE:");
  tft.setTextColor(0x9E7F); // Tono Púrpura/Azul Claro pastel
  tft.setTextSize(2);
  tft.setCursor(125, 95); tft.print((int)humAire); tft.setTextSize(1); tft.print("%");
  
  // Presión hPa
  tft.setTextColor(GC9A01A_WHITE); tft.setTextSize(1);
  tft.setCursor(115, 118); tft.print("PRESION:");
  tft.setTextColor(0xFEA0); // Amarillo/Oro brillante original
  tft.setTextSize(2);
  tft.setCursor(102, 130); tft.print((int)presionAire); tft.setTextSize(1); tft.print(" hPa");
  
  // Altitud m
  tft.setTextColor(GC9A01A_WHITE); tft.setTextSize(1);
  tft.setCursor(112, 153); tft.print("ALTITUD:");
  tft.setTextColor(0xFEA0); tft.setTextSize(2);
  tft.setCursor(98, 165); tft.print((int)altitudAire); tft.setTextSize(1); tft.print(" m");

  // -------------------------------------------------------------
  // SECCIÓN INFERIOR CENTRAL: PUREZA DEL AIRE (MQ-2)
  // -------------------------------------------------------------
  tft.fillRect(25, 150, 85, 45, GC9A01A_BLACK);
  tft.setTextSize(1); tft.setTextColor(GC9A01A_WHITE);
  tft.setCursor(32, 150); tft.print("PUREZA AIRE");
  tft.setTextColor(0xFBE0); // Naranja rojizo idéntico
  tft.setTextSize(2);
  tft.setCursor(38, 163); tft.print(purezaAirePorcentaje); tft.print("%");

  // -------------------------------------------------------------
  // FIRMA PERSONALIZADA CENTRADA ABAJO
  // -------------------------------------------------------------
  tft.fillRect(20, 210, 200, 15, GC9A01A_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF); // Gris sutil claro
  tft.setCursor(35, 195);
  tft.print("Designed By: Pablo Cabrera");
}

void intentarConexionWiFi() {
  Serial.print("Iniciando conexión Wi-Fi... ");
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 12) {
    delay(750);
    Serial.print(".");
    intentos++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Wi-Fi] ¡Conectado con éxito!");
    pixel.setPixelColor(0, pixel.Color(0, 255, 0)); // LED Verde de estado inicial exitoso
    pixel.show();
  } else {
    Serial.println("\n[Wi-Fi] Tiempo de espera agotado. Modo local.");
    pixel.setPixelColor(0, pixel.Color(255, 0, 0)); // LED Rojo de alerta inicial
    pixel.show();
  }
}

void MQTT_connect() {
  if (mqtt.connected()) return;
  mqtt.connect();
}
