#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include "secrets.h"
#include <DNSServer.h>

using namespace std;
using namespace ArduinoJson;

// --- PINES ---
const int PIN_BOTON_SORTEO = 4;
const int PIN_BOTON_RESET = 5;
const int PIN_SELECT = 18;
const int LED_R = 13;
const int LED_G = 12;
const int LED_B = 14;
const int TX_PRINTER = 17;
const int RX_PRINTER = 16;
const byte DNS_PORT = 53;

enum ModoJuego { INDIVIDUAL, PREGUNTA_RAPIDA, GRUPOS };
ModoJuego modoActual = INDIVIDUAL;
AsyncWebServer server(80);
DNSServer dnsServer;

// Variable global para redes
String redesEncontradas = "[]"; 

// --- PROTOTIPOS ---
void actualizarLedModo();
void realizarSorteo();
void reiniciarTodo();
String limpiarAcentos(String texto);
void escanearRedesAlInicio();

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
    if (modoActual == INDIVIDUAL) { digitalWrite(LED_G, HIGH); }
    else if (modoActual == PREGUNTA_RAPIDA) { digitalWrite(LED_B, HIGH); }
    else if (modoActual == GRUPOS) { digitalWrite(LED_R, HIGH); }
}

void escanearRedesAlInicio() {
    Serial.println("Escaneando redes WiFi...");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        redesEncontradas = "[]";
    } else {
        redesEncontradas = "[";
        for (int i = 0; i < n; ++i) {
            if (i > 0) redesEncontradas += ",";
            redesEncontradas += "\"" + WiFi.SSID(i) + "\"";
        }
        redesEncontradas += "]";
    }
    Serial.println("Escaneo completado.");
}

void reiniciarTodo() {
    if (LittleFS.exists("/maestro.txt")) {
        // Restaurar Lista Normal
        File maestro = LittleFS.open("/maestro.txt", "r");
        File lista = LittleFS.open("/lista.txt", FILE_WRITE);
        while(maestro.available()) lista.write(maestro.read());
        maestro.close(); lista.close();

        // Restaurar Lista Preguntas (CRUCIAL PARA QUE FUNCIONE EL MODO PREGUNTA)
        maestro = LittleFS.open("/maestro.txt", "r");
        File listaP = LittleFS.open("/lista_preguntas.txt", FILE_WRITE);
        while(maestro.available()) listaP.write(maestro.read());
        maestro.close(); listaP.close();

        digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);
        delay(300);
        actualizarLedModo();
        Serial.println("Reset completado: Archivos restaurados.");
    }
}

void realizarSorteo() {
    // --- MODO PREGUNTA ---
    if (modoActual == PREGUNTA_RAPIDA) {
        if (!LittleFS.exists("/preguntas.txt") || !LittleFS.exists("/lista_preguntas.txt")) {
            Serial.println("ERROR: Faltan archivos lista_preguntas.txt o preguntas.txt");
            return;
        }

        File fAP = LittleFS.open("/lista_preguntas.txt", "r");
        std::vector<String> alumnosP;
        while(fAP.available()){ String s = fAP.readStringUntil('\n'); s.trim(); if(s.length()>0) alumnosP.push_back(s); }
        fAP.close();

        File fP = LittleFS.open("/preguntas.txt", "r");
        std::vector<String> preguntas;
        while(fP.available()){ String s = fP.readStringUntil('\n'); s.trim(); if(s.length() > 0) preguntas.push_back(s); }
        fP.close();

        if(alumnosP.empty() || preguntas.empty()) {
            Serial2.write(0x1B); Serial2.write(0x40);
            Serial2.println("MODO PREGUNTAS AGOTADO");
            Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05);
            return;
        }

        int idxA = random(0, alumnosP.size());
        int idxP = random(0, preguntas.size());

        Serial2.write(0x1B); Serial2.write(0x40); delay(50);
        Serial2.write(0x1B); Serial2.write(0x61); Serial2.write(0x01); // Centrado
        Serial2.println("DESAFIO PREGUNTA");
        Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x30); // Grande
        Serial2.println(limpiarAcentos(alumnosP[idxA]));
        Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x00); // Normal
        Serial2.println("\n" + limpiarAcentos(preguntas[idxP]));
        Serial2.println("\n---------------------------");
        Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05);

        // Vaciado independiente
        File nfA = LittleFS.open("/lista_preguntas.txt", FILE_WRITE);
        for(int i=0; i < (int)alumnosP.size(); i++) if(i != idxA) nfA.println(alumnosP[i]);
        nfA.close();

        File nfP = LittleFS.open("/preguntas.txt", FILE_WRITE);
        for(int i=0; i < (int)preguntas.size(); i++) if(i != idxP) nfP.println(preguntas[i]);
        nfP.close();
    } 
    // --- OTROS MODOS ---
    else {
        if (!LittleFS.exists("/lista.txt")) return;
        File fA = LittleFS.open("/lista.txt", "r");
        std::vector<String> alumnos;
        while(fA.available()){ String s = fA.readStringUntil('\n'); s.trim(); if(s.length()>0) alumnos.push_back(s); }
        fA.close();

        if(alumnos.empty()) {
            Serial2.println("LISTA VACIA - RESETEAR");
            Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05);
            return;
        }

        int cantidad = (modoActual == GRUPOS) ? 3 : 1;
        if((int)alumnos.size() < cantidad) cantidad = alumnos.size();

        std::vector<int> elegidos;
        Serial2.write(0x1B); Serial2.write(0x40); delay(50);
        Serial2.write(0x1B); Serial2.write(0x61); Serial2.write(0x01); // Centrado
        Serial2.println(modoActual == GRUPOS ? "EQUIPO:" : "TURNO:");
        Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x30); // Grande

        for(int i=0; i<cantidad; i++){
            int r; bool rep;
            do { r = random(0, alumnos.size()); rep = false; for(int e:elegidos) if(e==r) rep=true; } while(rep);
            elegidos.push_back(r);
            Serial2.println(limpiarAcentos(alumnos[r]));
        }
        
        Serial2.write(0x1B); Serial2.write(0x21); Serial2.write(0x00); // Normal
        Serial2.println("\n---------------------------");
        Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05);

        File nfA = LittleFS.open("/lista.txt", FILE_WRITE);
        for(int i=0; i < (int)alumnos.size(); i++){
            bool fueE = false; for(int e:elegidos) if(e==i) fueE=true;
            if(!fueE) nfA.println(alumnos[i]);
        }
        nfA.close();
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, RX_PRINTER, TX_PRINTER);
    
    if(!LittleFS.begin(true)){
        Serial.println("LittleFS Error");
        return;
    }

    pinMode(PIN_BOTON_SORTEO, INPUT_PULLUP);
    pinMode(PIN_BOTON_RESET, INPUT_PULLUP);
    pinMode(PIN_SELECT, INPUT_PULLUP);
    pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);

    // 1. Intentar conectar WiFi
    bool conectado = false;
    if (LittleFS.exists("/wifi.json")) {
        File f = LittleFS.open("/wifi.json", "r");
        if(f) {
            JsonDocument doc; 
            deserializeJson(doc, f);
            f.close();
            const char* ssid = doc["ssid"];
            const char* pass = doc["pass"];
            if(ssid && strlen(ssid) > 0) {
                WiFi.begin(ssid, pass);
                Serial.print("Conectando a: "); Serial.println(ssid);
                int i = 0;
                while (WiFi.status() != WL_CONNECTED && i < 15) {
                    digitalWrite(LED_B, HIGH); delay(250);
                    digitalWrite(LED_B, LOW); delay(250);
                    i++;
                }
                conectado = (WiFi.status() == WL_CONNECTED);
            }
        }
    }

    // 2. MODO AP + PORTAL CAUTIVO
    if (!conectado) {
        Serial.println("Iniciando AP Configuración...");
        WiFi.mode(WIFI_AP_STA); 
        escanearRedesAlInicio();
        
        WiFi.softAP("Configurar_Tombola");
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        
        // --- IMPRESIÓN DEL TICKET DE AYUDA (RESTAURADO) ---
        Serial2.write(0x1B); Serial2.write(0x40);
        Serial2.println("MODO CONFIGURACION");
        Serial2.println("1. Conectese al WiFi:");
        Serial2.println("   Configurar_Tombola");
        Serial2.println("2. Acceda a:");
        Serial2.println("   192.168.4.1/config");
        Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05);
        // --------------------------------------------------

        Serial.println("Portal Cautivo en 192.168.4.1");
    } else {
        Serial.println("Conectado! IP: " + WiFi.localIP().toString());
        
        // Ticket de éxito con IP
        Serial2.write(0x1B); Serial2.write(0x40);
        Serial2.println("CONEXION EXITOSA");
        Serial2.println("IP para QR:");
        Serial2.println(WiFi.localIP().toString());
        Serial2.write(0x1B); Serial2.write(0x64); Serial2.write(0x05);

        digitalWrite(LED_G, HIGH); delay(1000); 
        actualizarLedModo();
    }

    // --- RUTAS DEL SERVIDOR ---

    // 1. Escaneo de redes (para el frontend)
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", redesEncontradas);
    });

    // 2. Guardar WiFi
    server.on("/save_wifi", HTTP_POST, [](AsyncWebServerRequest *request){
        if(request->hasParam("ssid", true) && request->hasParam("pass", true)){
            JsonDocument doc; 
            doc["ssid"] = request->getParam("ssid", true)->value();
            doc["pass"] = request->getParam("pass", true)->value();
            
            File f = LittleFS.open("/wifi.json", FILE_WRITE);
            serializeJson(doc, f); f.close();
            
            request->send(200, "text/plain", "Guardado. Reiniciando...");
            delay(1000); ESP.restart();
        } else {
            request->send(400, "text/plain", "Faltan datos");
        }
    });

    // 3. Subida de listas (Corregido para crear los 3 archivos)
    server.on("/upload_nombres", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("lista", true)) {
            String contenido = request->getParam("lista", true)->value();
            File f1 = LittleFS.open("/maestro.txt", FILE_WRITE); if(f1){f1.print(contenido); f1.close();}
            File f2 = LittleFS.open("/lista.txt", FILE_WRITE); if(f2){f2.print(contenido); f2.close();}
            File f3 = LittleFS.open("/lista_preguntas.txt", FILE_WRITE); if(f3){f3.print(contenido); f3.close();} // CRUCIAL
            request->send(200, "text/plain", "Listas actualizadas.");
        } else { request->send(400, "text/plain", "Error datos."); }
    });

    // 4. Archivos estáticos
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(LittleFS, "/config.html", "text/html"); });
    
    // Evitar error 404 del favicon
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(404); });

    // 5. Guardar preguntas
    server.on("/upload_preguntas", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("preguntas", true)) {
            String p = request->getParam("preguntas", true)->value();
            File f = LittleFS.open("/preguntas.txt", FILE_WRITE);
            if (f) { f.print(p); f.close(); }
            request->send(200, "text/plain", "Preguntas guardadas.");
        }
    });

    // 6. Registrar alumno
    server.on("/registrar", HTTP_GET, [](AsyncWebServerRequest *request) {
        if(request->hasParam("nombre")) {
            String alum = request->getParam("nombre")->value();
            alum.trim();
            if(alum.length() > 0) {
                const char* rutas[] = {"/maestro.txt", "/lista.txt", "/lista_preguntas.txt"};
                for(const char* r : rutas) {
                    File f = LittleFS.open(r, FILE_APPEND);
                    if(f) { f.println(alum); f.close(); }
                }
                request->send(200, "text/plain", "Registro OK");
            }
        }
    });

   // --- LÓGICA DE PORTAL CAUTIVO AGRESIVO ---
    
    // 7. Rutas trampa para Android y Windows
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){ request->redirect("http://192.168.4.1/config"); });
    server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request){ request->redirect("http://192.168.4.1/config"); });

    // Catch-All: Si no encuentra la ruta, decide qué hacer según el modo
    server.onNotFound([](AsyncWebServerRequest *request){
        // Si estamos en modo AP (Configuración), forzamos ir a /config
        if (WiFi.status() != WL_CONNECTED) {
            request->redirect("http://192.168.4.1/config");
        } else {
            // Si ya estamos conectados al WiFi escolar, simplemente mandamos al index o damos 404
            request->send(LittleFS, "/index.html", "text/html");
        }
    });

    server.begin();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        dnsServer.processNextRequest();
    }

    static unsigned long ultimaAccion = 0;
    unsigned long tiempoActual = millis();
    const int DEBOUNCE = 500;

    if (digitalRead(PIN_BOTON_SORTEO) == LOW && (tiempoActual - ultimaAccion > DEBOUNCE)) {
        realizarSorteo(); ultimaAccion = tiempoActual;
    }
    if (digitalRead(PIN_BOTON_RESET) == LOW && (tiempoActual - ultimaAccion > DEBOUNCE)) {
        reiniciarTodo(); ultimaAccion = tiempoActual;
    }
    if (digitalRead(PIN_SELECT) == LOW && (tiempoActual - ultimaAccion > DEBOUNCE)) {
        modoActual = (ModoJuego)((modoActual + 1) % 3);
        actualizarLedModo(); ultimaAccion = tiempoActual;
    }
    delay(2);
}