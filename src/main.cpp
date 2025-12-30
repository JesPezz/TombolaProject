#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <vector>
#include "secrets.h"

// --- PINES ---
const int PIN_BOTON_SORTEO = 4;
const int PIN_BOTON_RESET = 5;
const int PIN_SELECT = 18;
const int LED_R = 13;
const int LED_G = 12;
const int LED_B = 14;
const int TX_PRINTER = 17;
const int RX_PRINTER = 16;

enum ModoJuego { INDIVIDUAL, PREGUNTA_RAPIDA, GRUPOS };
ModoJuego modoActual = INDIVIDUAL;
AsyncWebServer server(80);

// --- PROTOTIPOS ---
void actualizarLedModo();
void realizarSorteo();
void reiniciarTodo();
String limpiarAcentos(String texto);
void enviarImpresora(String texto, bool grande = false);

// --- FUNCIONES AUXILIARES ---

String limpiarAcentos(String texto) {
    texto.replace("á", "a"); texto.replace("é", "e"); texto.replace("í", "i");
    texto.replace("ó", "o"); texto.replace("ú", "u"); texto.replace("ñ", "n");
    texto.replace("Á", "A"); texto.replace("É", "E"); texto.replace("Í", "I");
    texto.replace("Ó", "O"); texto.replace("Ú", "U"); texto.replace("Ñ", "N");
    return texto;
}

void actualizarLedModo() {
    digitalWrite(LED_R, LOW); digitalWrite(LED_G, LOW); digitalWrite(LED_B, LOW);
    Serial.print("\n>>> MODO: ");
    if (modoActual == INDIVIDUAL) { digitalWrite(LED_G, HIGH); Serial.println("INDIVIDUAL (Verde)"); }
    else if (modoActual == PREGUNTA_RAPIDA) { digitalWrite(LED_B, HIGH); Serial.println("PREGUNTA Y ALUMNO (Azul)"); }
    else if (modoActual == GRUPOS) { digitalWrite(LED_R, HIGH); Serial.println("GRUPOS DE 3 (Rojo)"); }
}

void reiniciarTodo() {
    if (LittleFS.exists("/maestro.txt")) {
        File m = LittleFS.open("/maestro.txt", "r");
        File l = LittleFS.open("/lista.txt", FILE_WRITE);
        while(m.available()) l.write(m.read());
        m.close(); l.close();
        Serial.println("Lista de alumnos restaurada.");
    }
    // Nota: Las preguntas no se restauran automáticamente para permitir 
    // que el profe suba un set nuevo, pero podrías clonar un 'preguntas_maestro.txt' si quisieras.
}

void realizarSorteo() {
    // 1. Cargar Alumnos
    File fA = LittleFS.open("/lista.txt", "r");
    std::vector<String> alumnos;
    while(fA.available()){ String s = fA.readStringUntil('\n'); s.trim(); if(s.length()>0) alumnos.push_back(s); }
    fA.close();

    if(alumnos.empty()) { Serial.println("No hay alumnos."); return; }

    // --- LOGICA DE IMPRESION INICIAL ---
    Serial2.write(0x1B); Serial2.write(0x40); delay(50);
    Serial2.write(0x1B); Serial2.write(0x61); Serial2.write(0x01); // Centrado

    // --- MODO PREGUNTA RAPIDA (AZUL) ---
    if (modoActual == PREGUNTA_RAPIDA) {
        File fP = LittleFS.open("/preguntas.txt", "r");
        std::vector<String> preguntas;
        while(fP.available()){ String s = fP.readStringUntil('\n'); s.trim(); if(s.length()>0) preguntas.push_back(s); }
        fP.close();

        if(preguntas.empty()) {
            Serial2.println("SIN PREGUNTAS EN ARCHIVO");
            Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05);
            return;
        }

        int idxA = random(0, alumnos.size());
        int idxP = random(0, preguntas.size());
        String nom = limpiarAcentos(alumnos[idxA]);
        String pre = limpiarAcentos(preguntas[idxP]);

        Serial2.println("*** DESAFIO DE CLASE ***");
        Serial2.println("\nALUMNO(A):");
        Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x30); // Grande
        Serial2.println(nom);
        Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x00); // Normal
        Serial2.println("\nPREGUNTA:");
        Serial2.println(pre);

        // ELIMINAR AMBOS
        File nl = LittleFS.open("/lista.txt", FILE_WRITE);
        for(int i=0; i<alumnos.size(); i++) if(i != idxA) nl.println(alumnos[i]);
        nl.close();

        File np = LittleFS.open("/preguntas.txt", FILE_WRITE);
        for(int i=0; i<preguntas.size(); i++) if(i != idxP) np.println(preguntas[i]);
        np.close();
    } 
    // --- MODOS INDIVIDUAL / GRUPOS ---
    else {
        int cant = (modoActual == GRUPOS) ? 3 : 1;
        if(alumnos.size() < cant) cant = alumnos.size();
        std::vector<int> elegidos;
        
        Serial2.println(modoActual == GRUPOS ? "--- EQUIPO ---" : "--- GANADOR ---");
        Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x30);

        for(int i=0; i<cant; i++) {
            int r; bool rep;
            do { r = random(0, alumnos.size()); rep = false; for(int e:elegidos) if(e==r) rep=true; } while(rep);
            elegidos.push_back(r);
            Serial2.println(limpiarAcentos(alumnos[r]));
        }

        Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x00);
        
        // ACTUALIZAR LISTA
        File nl = LittleFS.open("/lista.txt", FILE_WRITE);
        for(int i=0; i<alumnos.size(); i++) {
            bool fueE = false; for(int e:elegidos) if(e==i) fueE=true;
            if(!fueE) nl.println(alumnos[i]);
        }
        nl.close();
    }

    Serial2.println("\n---------------------------");
    Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05); 
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, RX_PRINTER, TX_PRINTER);
    LittleFS.begin(true);

    pinMode(PIN_BOTON_SORTEO, INPUT_PULLUP);
    pinMode(PIN_BOTON_RESET, INPUT_PULLUP);
    pinMode(PIN_SELECT, INPUT_PULLUP);
    pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);

    WiFi.softAP(AP_SSID, AP_PASS);
    actualizarLedModo();

    // --- RUTAS DEL SERVIDOR ACTUALIZADAS ---

    // 1. Cargar Alumnos (Restaura la funcionalidad de carga masiva)
    server.on("/upload_nombres", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("lista", true)) {
            String contenido = request->getParam("lista", true)->value();
            
            // Guardamos en Maestro (permanente)
            File f1 = LittleFS.open("/maestro.txt", FILE_WRITE);
            if (f1) { f1.print(contenido); f1.close(); }
            
            // Guardamos en Lista de Juego (se va vaciando)
            File f2 = LittleFS.open("/lista.txt", FILE_WRITE);
            if (f2) { f2.print(contenido); f2.close(); }
            
            request->send(200, "text/plain", "Lista de alumnos cargada con exito.");
            Serial.println("Alumnos actualizados.");
        } else {
            request->send(400, "text/plain", "Error: No se recibio la lista.");
        }
    });

    // 2. Cargar Preguntas
    server.on("/upload_preguntas", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("preguntas", true)) {
            String p = request->getParam("preguntas", true)->value();
            File f = LittleFS.open("/preguntas.txt", FILE_WRITE);
            if (f) {
                f.print(p);
                f.close();
                request->send(200, "text/plain", "Banco de preguntas guardado.");
                Serial.println("Preguntas actualizadas.");
            } else {
                request->send(500, "text/plain", "Error al escribir preguntas.txt");
            }
        }
    });

    // 3. Registro individual (QR)
    server.on("/registrar", HTTP_GET, [](AsyncWebServerRequest *request) {
        if(request->hasParam("nombre")) {
            String alumno = request->getParam("nombre")->value();
            File f = LittleFS.open("/lista.txt", FILE_APPEND);
            if(f) {
                f.println(alumno);
                f.close();
                request->send(200, "text/plain", "Registrado correctamente.");
            }
        }
    });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

void loop() {
    if (digitalRead(PIN_BOTON_SORTEO) == LOW) { delay(50); realizarSorteo(); while(digitalRead(PIN_BOTON_SORTEO) == LOW); }
    if (digitalRead(PIN_BOTON_RESET) == LOW) { delay(50); reiniciarTodo(); while(digitalRead(PIN_BOTON_RESET) == LOW); }
    if (digitalRead(PIN_SELECT) == LOW) {
        delay(250);
        modoActual = (ModoJuego)((modoActual + 1) % 3);
        actualizarLedModo();
        while(digitalRead(PIN_SELECT) == LOW);
    }
}