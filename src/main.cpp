#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include <vector>

// Pines (Regla de Oro: Evitar pins 0, 2, 12, 15)
const int PIN_BOTON_SORTEO = 4;
const int TX_PRINTER = 17;
const int RX_PRINTER = 16;
const int PIN_BOTON_RESET = 5;

AsyncWebServer server(80);

// Prototipos
void manejarCargaMasiva(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void realizarSorteo();
void reiniciarTombola();

void setup() {
    Serial.begin(115200);
    pinMode(PIN_BOTON_SORTEO, INPUT_PULLUP);
    pinMode(PIN_BOTON_RESET, INPUT_PULLUP);
    Serial2.write(0x1B); Serial2.write(0x74); Serial2.write(0x01);
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
    
    // 1. Recibir lista masiva desde el textarea
server.on("/upload_nombres", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("lista", true)) {
        String contenido = request->getParam("lista", true)->value();
        // Guardamos en el Maestro (permanente)
        File f1 = LittleFS.open("/maestro.txt", FILE_WRITE);
        if (f1) { f1.print(contenido); f1.close(); }
        
        // Guardamos en la Lista de Juego (la que se va vaciando)
        File f2 = LittleFS.open("/lista.txt", FILE_WRITE);
        if (f2) { f2.print(contenido); f2.close(); }
        
        request->send(200, "text/plain", "Lista cargada en Maestro y Juego.");
    }
});
    
// 2. Resetear la lista (Borrar archivo)
server.on("/reset_lista", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.remove("/lista.txt")) {
        request->send(200, "text/plain", "Lista borrada. El archivo esta vacio.");
    } else {
        request->send(500, "text/plain", "Error al borrar o archivo no existe.");
    }
});

// 3. (Opcional) Ver cuántos hay registrados
server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    File f = LittleFS.open("/lista.txt", "r");
    int contador = 0;
    while(f.available()) {
        if(f.read() == '\n') contador++;
    }
    f.close();
    request->send(200, "text/plain", "Alumnos en tombola: " + String(contador));
});

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
    pinMode(PIN_BOTON_SORTEO, INPUT_PULLUP);
}

void loop() {

    // Botón Sorteo
    if (digitalRead(PIN_BOTON_SORTEO) == LOW) {
        delay(50); // debounce
        realizarSorteo();
        while(digitalRead(PIN_BOTON_SORTEO) == LOW);
    }
    
    // Botón Reset
    if (digitalRead(PIN_BOTON_RESET) == LOW) {
        delay(50);
        reiniciarTombola();
        while(digitalRead(PIN_BOTON_RESET) == LOW);
    }

    static unsigned long ultimaPresion = 0;
    if (digitalRead(PIN_BOTON_SORTEO) == LOW) {
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

// Función para limpiar la ortografía para la impresora
String limpiarAcentos(String texto) {
    // Reemplazos manuales de secuencias de bytes UTF-8 comunes
    texto.replace("á", "a"); texto.replace("é", "e"); texto.replace("í", "i");
    texto.replace("ó", "o"); texto.replace("ú", "u"); texto.replace("ñ", "n");
    texto.replace("Á", "A"); texto.replace("É", "E"); texto.replace("Í", "I");
    texto.replace("Ó", "O"); texto.replace("Ú", "U"); texto.replace("Ñ", "N");
    return texto;
}

// Función para reiniciar la tómbola (copiar maestro a trabajo)
void reiniciarTombola() {
    if (!LittleFS.exists("/maestro.txt")) return;
    
    File maestro = LittleFS.open("/maestro.txt", "r");
    File trabajo = LittleFS.open("/lista.txt", FILE_WRITE);
    
    while (maestro.available()) {
        trabajo.write(maestro.read());
    }
    maestro.close();
    trabajo.close();
    Serial.println("Tómbola reiniciada con la lista maestra.");
}

void realizarSorteo() {
    if (!LittleFS.exists("/lista.txt")) {
        Serial.println("Error: No hay lista de juego.");
        return;
    }

    // 1. Leer alumnos actuales
    File f = LittleFS.open("/lista.txt", "r");
    std::vector<String> alumnos;
    while (f.available()) {
        String n = f.readStringUntil('\n');
        n.trim();
        if (n.length() > 0) alumnos.push_back(n);
    }
    f.close();

    if (alumnos.empty()) {
        Serial2.println("TOMBOLA VACIA");
        Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05);
        return;
    }

    // 2. Seleccionar ganador
    int indice = random(0, alumnos.size());
    String ganadorOriginal = alumnos[indice];
    String ganadorParaImprimir = limpiarAcentos(ganadorOriginal);

    // 3. IMPRESIÓN CON FORMATO ANTERIOR (EL MEJORADO)
    Serial2.write(0x1B); Serial2.write(0x40); // Reset
    Serial2.write(0x1B); Serial2.write(0x61); Serial2.write(0x01); // Centrado

    Serial2.println("***************************");
    Serial2.println("TOMBOLA INTELIGENTE");
    Serial2.println("***************************\n");

    // Nombre en GRANDE
    Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x30); 
    Serial2.println(ganadorParaImprimir);
    
    // Pie de página en normal
    Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x00);
    Serial2.println("\n---------------------------");
    Serial2.println("¡Suerte en la clase!");

    // Margen para corte (5 líneas)
    Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05); 

    // 4. ACTUALIZAR ARCHIVO (Eliminar al ganador)
    File nf = LittleFS.open("/lista.txt", FILE_WRITE);
    for (int i = 0; i < alumnos.size(); i++) {
        if (i != indice) nf.println(alumnos[i]);
    }
    nf.close();

    Serial.println("Ganador impreso y eliminado: " + ganadorOriginal);
}