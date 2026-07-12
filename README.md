# H2Origin - Sistema de Monitoreo Hidrológico y Atmosférico

H2Origin es un sistema embebido y con módulos de monitoreo basado en el microcontrolador **ESP32**. El dispositivo recopila variables críticas tanto de entornos líquidos (pH, sólidos disueltos, temperatura) como de la atmósfera (temperatura, humedad, presión, altitud y gases/pureza del aire), procesando los datos en tiempo real, mostrándolos en una interfaz gráfica circular y transmitiéndolos a la nube mediante IoT.

---

## 📌 1. Pinout General

El siguiente cuadro detalla la interconexión física definitiva entre los periféricos y la placa ESP32 de acuerdo con la arquitectura del firmware:

| Componente / Módulo | Pin del Componente | Pin ESP32 | Tipo de Señal / Rol Electrónico |
| :--- | :--- | :--- | :--- |
| **Pantalla GC9A01A** | CS (Chip Select) | **GPIO 5** | Salida Digital (Control SPI) |
| | DC (Data/Command) | **GPIO 2** | Salida Digital (Control SPI) |
| | RST (Reset) | **GPIO 15** | Salida Digital (Control SPI) |
| | CLK / SCLK | **GPIO 18** | Reloj SPI Nativo |
| | DIN / MOSI | **GPIO 23** | Transmisión de Datos SPI Nativa |
| **Sensor de pH (PH-4502C)** | Po (Señal Analógica) | **GPIO 34** | Entrada Analógica (ADC1_CH6) |
| **Sensor TDS (Agua)** | T (Señal Analógica) | **GPIO 35** | Entrada Analógica (ADC1_CH7) |
| | VCC (Alimentación) | **GPIO 25** | Salida Digital (Control de Transistor) |
| **Sensor MQ-2 (Aire)** | AO (Señal Analógica) | **GPIO 32** | Entrada Analógica (ADC1_CH4) |
| **Termopar (DS18B20)** | DQ (Datos) | **GPIO 4** | Bus Digital Bidireccional (1-Wire) |
| **Sensor BME680** | SDA | **GPIO 21** | Línea de Datos I2C Nativa |
| | SCL | **GPIO 22** | Línea de Reloj I2C Nativa |
| **NeoPixel RGB** | DI (Data In) | **GPIO 27** | Bus Digital / Control de Tira (Bit-banging) |
| **Buzzer Activo** | VCC / Positivo | **GPIO 13** | Salida Digital (Alarma Sonora Fija) |
| **LED Alerta General** | Ánodo (+) | **GPIO 12** | Salida Digital / PWM |
| **LED Clima** | Ánodo (+) | **GPIO 14** | Salida Digital / PWM (Atenuación Estética) |
| **LED Aire** | Ánodo (+) | **GPIO 26** | Salida Digital / PWM (Atenuación Estética) |
| **LED pH** | Ánodo (+) | **GPIO 33** | Salida Digital / PWM (Atenuación Estética) |

---

## 📡 2. Protocolos de Comunicación Implementados

El sistema utilizaa el ancho de banda y la respuesta síncrona del procesador dividiendo las tareas en 4 topologías de comunicación en paralelo:

1. **SPI (Serial Peripheral Interface):** Utilizado exclusivamente por la **Pantalla GC9A01A**. Opera en modo maestro-esclavo a alta velocidad para asegurar un refresco de pantalla fluido de 60 FPS en la interfaz circular sin retrasar las tareas de fondo.
2. **I2C (Inter-Integrated Circuit):** Empleado por el módulo ambiental **BME680**. Requiere solo dos hilos (`SDA` y `SCL`) compartidos, permitiendo direccionamiento por hardware en el bus (Dirección por defecto: `0x76`).
3. **1-Wire (Bus un solo hilo):** Implementado por el termopar **DS18B20**. Protocolo propietario de Dallas Semiconductor que realiza transmisiones de datos y sincronía de reloj sobre una única línea compartida (`GPIO 4`), requiriendo una resistencia externa de Pull-Up de $4.7\text{ k}\Omega$.
4. **Muestreo Analógico Directo (ADC):** Los sensores de **pH, TDS y MQ-2** entregan voltajes continuos que son interpretados por el Convertidor Analógico-Digital interno del ESP32 a una resolución nativa de 12 bits (Valores lógicos digitales entre `0` y `4095`).

---

## ⚙️ 3. Operación, Medición e Interpretación de Componentes

### A. Pantalla Redonda GC9A01A
* **Operación:** Actúa como dispositivo de salida gráfica y panel informativo principal (Dashboard). Recibe primitivas de diseño mediante SPI y actualiza la interfaz de usuario en intervalos exactos de 1 segundo.
* **Medición:** No realiza adquisiciones. 
* **Interpretación:** Divide el display simétricamente en una columna de datos químicos hidrológicos (Izquierda) y datos atmosféricos (Derecha), con una sección central inferior dedicada a la pureza del aire.

### B. Módulo de pH (PH-4502C)
* **Operación:** La sonda galvánica de vidrio genera milivoltajes proporcionales a la concentración de iones $H^+$ en la muestra de agua. El módulo operacional filtra y amplifica dicha señal hacia el pin analógico.
* **Medición:** Potencial de Hidrógeno (pH) en escala de 0 a 14.
* **Interpretación:** * Un voltaje de **$1.10\text{ V}$** en el ADC representa un **pH neutro de 7.0**.
  * Incrementos de voltaje superiores a $1.10\text{ V}$ significan un entorno **Ácido** ($pH < 7$).
  * Voltajes inferiores a $1.10\text{ V}$ representan un entorno **Alcalino** ($pH > 7$).
  * **Umbral de Alerta:** Si el pH se desvía del rango óptimo ($6.5 - 8.5$), el `LED_MQ_PIN` conmuta automáticamente a máxima intensidad.

### C. Analizador de Sólidos Disueltos (TDS)
* **Operación:** Mide la conductividad eléctrica del líquido mediante dos electrodos de titanio. Con el fin de erradicar la degradación de las sondas por electrólisis galvánica, el pin **GPIO 25** actúa como un interruptor de energía que solo energiza el sensor en el instante milimétrico de la lectura.
* **Medición:** Cantidad de Sólidos Totales Disueltos expresados en **ppm** (Partes Por Millón).
* **Interpretación:**
  * **0 - 50 ppm:** Agua pura / Ultra-filtrada.
  * **50 - 300 ppm:** Agua potable óptima de red doméstica.
  * **> 500 ppm:** Agua dura con alta concentración de sales minerales disueltas; no apta para consumo directo sin tratamiento.

### D. Sonda Térmica DS18B20
* **Operación:** Sensor digital sellado en vaina de acero inoxidable. Realiza conversiones térmicas internas de forma autónoma y entrega la trama de datos directamente serializada en grados Celsius.
* **Medición:** Temperatura del agua líquida (°C).
* **Interpretación:** Proporciona el control térmico del fluido dentro de rangos operativos seguros de $-55^\circ\text{C}$ a $+125^\circ\text{C}$ con precisión milimétrica.

### E. Módulo Multivariable BME680
* **Operación:** Unidad ambiental integrada de Bosch de calidad aeroespacial. Utiliza transductores internos micromecanizados expuestos a la presión y flujo del entorno de la placa.
* **Medición:** Temperatura ambiente (°C), Humedad relativa (%), Presión atmosférica (hPa) y Altitud relativa (m).
* **Interpretación:** * La temperatura se renderiza con formato flotante de alta fidelidad. Si supera el umbral crítico de **$30.0^\circ\text{C}$**, el actuador `LED_W_TEMP_PIN` se enciende a su máxima potencia.
  * La presión barométrica toma como constante de cálculo de altitud la base de $1013.25\text{ hPa}$ (nivel del mar).

### F. Sensor de Gases MQ-2 (Calidad del Aire)
* **Operación:** Integra un filamento calefactor de Alúmina y una película química de Dióxido de Estaño ($SnO_2$). En presencia de hidrocarburos, gases inflamables, humo o exhalación humana directa ($CO_2$ y vapor de agua), la resistencia interna se desploma, generando un divisor de tensión con el potenciómetro físico fijado en **$3.3\text{ k}\Omega$** para proteger el ESP32.
* **Medición:** Concentración de gases/Humo e índice de Pureza de Aire.
* **Interpretación:**
  * **Aire Limpio (Base):** Produce una lectura aproximada de **$0.65\text{ V}$**, lo cual la matemática flotante del código calibra exactamente al **90% de pureza**. El `LED_AIR_PIN` opera en modo tenue estético.
  * **Inyección de Gas / Humo / Aliento:** El voltaje incrementa de forma súbita hacia el techo calibrado de **$1.10\text{ V}$** (0% de pureza). El software actualiza de forma decimal el porcentaje en reversa (ej. 79%, 75%, 68%).
  * **Línea de Emergencia (< 65%):** Si la pureza cae de este valor, el `LED_AIR_PIN` se satura a brillo total (255) y el **Buzzer (GPIO 13)** emite un tono acústico fijo, plano y continuo de emergencia sin generar bloqueos en los bucles del microcontrolador.

### G. Sistema de Alertas Visuales PWM y NeoPixel
* **Operación:** El pin **GPIO 27** gobierna mediante *bit-banging* el chip direccionable integrado `WS2812B` del NeoPixel. Los LEDs independientes restantes operan bajo modulación por ancho de pulso (**PWM**).
* **Interpretación:**
  * **NeoPixel:** Cambia cromáticamente replicando la escala universal de reactivos químicos para el pH (Rojo para ácidos críticos, Verde para soluciones neutras óptimas y Púrpura para compuestos fuertemente alcalinos).
  * **Atenuación PWM Eficiente:** En condiciones seguras, todos los LEDs independientes se fijan en `BRILLO_TENUE = 15`. Esto disminuye el consumo eléctrico en un **94%**, manteniendo los reguladores lógicos fríos y extendiendo la vida útil del hardware.

---
## 💻 4. Requisitos de Software

Para compilar este firmware en el entorno Arduino IDE se requieren las siguientes librerías instaladas mediante el gestor oficial:
* `Adafruit_GC9A01A` y `Adafruit_GFX`
* `Adafruit_NeoPixel`
* `OneWire` y `DallasTemperature`
* `Adafruit_BME680` y `Adafruit_Sensor`
* `Adafruit_MQTT` y `Adafruit_MQTT_Client`

## 5. Diseño de la PCB

| Vista Frontal | Vista Posterior |
| :---: | :---: |
| <img src="https://github.com/user-attachments/assets/5789b969-257a-4d6f-8530-94177b0bef66" width="400" height="500" alt="Vista Frontal"> | <img src="https://github.com/user-attachments/assets/0f159de4-c8d5-4006-8357-8bfa8a0212b7" width="400" height="500" alt="Vista Posterior"> |


**Módulos utilizados**

| TDS (Solidos disueltos en el agua) | pH (Acidez agua) |
| :---: | :---: |
| <img src="https://github.com/user-attachments/assets/3a34026f-12d7-4d2e-bcd1-f0feb6dcc1d3" width="400" height="300" alt="Módulo TDS"> | <img src="https://github.com/user-attachments/assets/92446994-9d35-425f-aab5-e14d9434f34a" width="400" height="300" alt="Módulo pH"> |


## 📜 Certificación OSHW
Este proyecto está certificado como hardware libre abierto por la Open Source Hardware Association (OSHWA).

<img width="4000" height="3221" alt="certification-mark-GT000025-stacked" src="https://github.com/user-attachments/assets/063ad08b-89b2-4de4-983f-fff0f8836559" />

ID de certificación: GT000025
