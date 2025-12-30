#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include <vector>

// Pines (Regla de Oro: Evitar pins 0, 2, 12, 15)
const int PIN_BOTON = 4;
const int TX_PRINTER = 17;
const int RX_PRINTER = 16;

AsyncWebServer server(80);

// Prototipos
void manejarCargaMasiva(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void realizarSorteo();

void setup() {
    Serial.begin(115200);
    // UART2 para la impresora térmica Goojprt
    Serial2.begin(9600, SERIAL_8N1, 16, 17);

    if(!LittleFS.begin(true)) {
        Serial.println("Error al montar LittleFS");
        return;
    }

    // Configuración Modo Punto de Acceso (Offline)
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.println("Servidor iniciado en: " + WiFi.softAPIP().toString());

    // --- RUTAS DEL SERVIDOR ---

    // Endpoint para "Copia y Pega" del docente
    server.on("/admin/upload", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, manejarCargaMasiva);
    
    // Registro rápido vía QR para alumnos (Modo ráfaga)
    server.on("/registrar", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("nombre")) {
        String alumno = request->getParam("nombre")->value();
        File f = LittleFS.open("/lista.txt", FILE_APPEND);
        if(f) {
          f.println(alumno);
          f.close();
          request->send(200, "text/plain", "Alumno " + alumno + " registrado!");
        } else {
          request->send(500, "text/plain", "Error al abrir lista.txt");
        }
      } else {
        request->send(400, "text/plain", "Falta el nombre");
      }
    });

    // Servir el index.html
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    
    server.begin();
    pinMode(PIN_BOTON, INPUT_PULLUP);
}

void loop() {
    static unsigned long ultimaPresion = 0;
    if (digitalRead(PIN_BOTON) == LOW) {
        if (millis() - ultimaPresion > 500) { // Debounce de 500ms
            Serial.println("¡Boton detectado!"); // Esto DEBE aparecer en el serial
            
            // Verificar si hay alguien en la lista antes de sortear
            if (LittleFS.exists("/lista.txt")) {
                realizarSorteo();
            } else {
                Serial.println("Error: El archivo /lista.txt no existe aun.");
            }
            
            ultimaPresion = millis();
        }
    }
}

void manejarCargaMasiva(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Almacenamos la lista enviada por el docente en la Flash
    File f = LittleFS.open("/lista.txt", index == 0 ? FILE_WRITE : FILE_APPEND);
    if(f) {
        f.write(data, len);
        f.close();
    }
    if (index + len == total) request->send(200, "text/plain", "Lista cargada");
}

void realizarSorteo() {
    if (!LittleFS.exists("/lista.txt")) {
        Serial.println("Error: No existe /lista.txt");
        return;
    }

    File f = LittleFS.open("/lista.txt", "r");
    std::vector<String> alumnos;
    while (f.available()) {
        String nombre = f.readStringUntil('\n');
        nombre.trim();
        if (nombre.length() > 0) alumnos.push_back(nombre);
    }
    f.close();

    if (alumnos.empty()) {
        Serial.println("Lista vacia");
        return;
    }

    int indiceGanador = random(0, alumnos.size());
    String ganador = alumnos[indiceGanador];

    // --- COMANDOS LIMPIOS PARA LA IMPRESORA ---
    Serial2.write(0x1B); Serial2.write(0x40); // Inicializar (ESC @)
    delay(10);
    
    // Centrado
    Serial2.write(0x1B); Serial2.write(0x61); Serial2.write(0x01); 
    Serial2.println("TOMBOLA INTELIGENTE");
    Serial2.println("---------------------------");
    
    // Texto Doble Alto y Doble Ancho para el nombre
    Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x30); 
    Serial2.println(ganador);
    
    // Volver a texto normal
    Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x00);
    Serial2.println("---------------------------");
    Serial2.println("¡Felicidades!");
    
    // Avance de papel para poder cortar
    Serial2.write(0x0A);
    Serial2.write(0x0A);
    Serial2.write(0x0A);

    Serial.println("Impreso con éxito: " + ganador);
}