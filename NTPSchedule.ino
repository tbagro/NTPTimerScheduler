#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <FS.h>

WiFiManager wifiManager;
WiFiServer server(80);

const byte relayPin = D8;  // Substitua D8 pelo pino correto do seu ESP8266

WiFiUDP udp;                                                  // Cria um objeto "UDP".
int Tz = -4;                                                  // zona UTC
NTPClient timeClient(udp, "a.st1.ntp.br", Tz * 3600, 60000);  // Cria um objeto "NTP" com as configurações.
unsigned long lastActivationTime = 0;
unsigned long highDT = 0;                          // Variáveis para armazenar o tempo da última ativação alta
unsigned long lowDT = 0;                           // Variáveis para armazenar o tempo da última ativação baixa
unsigned long intervalBetweenActivations = 60000;  // Intervalo entre ativações em milissegundos
int utcOffsetInSeconds = 0;                        // Substitua 0 pelo seu deslocamento de fuso horário em segundos

/*******************************************************************************
* SCHEDULE SYSTEM FUNCTIONS
* 10/2018 - Andre Michelon
* Options:
*   - Scheduled at a specific Date/Time
*     On (High): SHyyyy-mm-dd hh:mm
*     Off (Low): SLyyyy-mm-dd hh:mm
*   - Monthly
*     On (High): MHdd hh:mm
*     Off (Low): MLdd hh:mm
*   - Weekly
*     On (High): WHd hh:mm 1-7
*     Off (Low): WLd hh:mm
*   - Daily
*     On (High): DHhh:mm
*     Off (Low): DLhh:mm
*   - Intervaled
*     On (High): IHhh:mm
*     Off (Low): ILhh:mm
* Example Strings:
*   SH2018-10-12 16:30  - set On on Oct 12 2018 16:30
*   MH12 16:30          - set On monthly on day 12 16:30
*   WL6 16:30           - set Off weekly on Fridays 16:30 
*   DH16:30             - set On daily at 16:30
*   IH00:30             - set Off after being On for 30 minutes
*   IL00:10             - set On after being Off for 10 minutes
*/

String hhmmssStr(const unsigned long &t) {
  // Retorna time_t como String "HH:mm:ss"
  String s = "";
  unsigned long hours = (t / 3600) % 24;
  unsigned long minutes = (t / 60) % 60;
  unsigned long seconds = t % 60;

  if (hours < 10) {
    s += '0';
  }
  s += String(hours) + ':';

  if (minutes < 10) {
    s += '0';
  }
  s += String(minutes) + ':';

  if (seconds < 10) {
    s += '0';
  }
  s += String(seconds);

  return s;
}

String hhmmStr(const unsigned long &t) {
  unsigned long minutes = (t / 60) % 60;
  unsigned long hours = (t / 3600) % 24;

  return String(hours) + ':' + String(minutes);
}


String scheduleChk(const String &schedule, const byte &pin) {
  timeClient.update();
  unsigned long lastCheck = timeClient.getEpochTime() + utcOffsetInSeconds;  // Aplica a diferença horária
  String dt = timeClient.getFormattedTime();                                 // Obtém a data e hora formatadas

  String event = "";
  byte relay = digitalRead(pin);

  auto checkAndProcessEvent = [&](const String &str, byte relayState) {
    if (schedule.indexOf(str) != -1) {
      event = str;
      relay = relayState;
      return true;
    }
    return false;
  };

  // Verifica Agendamentos
  if (checkAndProcessEvent("SH" + dt, HIGH) || checkAndProcessEvent("SL" + dt, LOW) || checkAndProcessEvent("MH" + dt.substring(8), HIGH) || checkAndProcessEvent("ML" + dt.substring(8), LOW) || checkAndProcessEvent("WH" + dt.substring(0, 8), HIGH) || checkAndProcessEvent("WL" + dt.substring(0, 8), LOW) || checkAndProcessEvent("DH" + dt.substring(11), HIGH) || checkAndProcessEvent("DL" + dt.substring(11), LOW)) {
    // Agendamento encontrado, processa o evento
  }
  // Verifica Alta Intervalada - IHhh:mm
  else if (checkAndProcessEvent("IH" + hhmmStr(lastCheck - highDT), HIGH) && digitalRead(pin)) {
    // Agendamento encontrado, processa o evento
  }
  // Verifica Baixa Intervalada - ILhh:mm
  else if (checkAndProcessEvent("IL" + hhmmStr(lastCheck - lowDT), LOW) && !digitalRead(pin)) {
    // Agendamento encontrado, processa o evento
  }
  // Verifica Alta Intervalada - IHhh:mm:ss
  else if (checkAndProcessEvent("IH" + hhmmssStr(lastCheck - highDT), HIGH) && digitalRead(relayPin)) {
    // Agendamento encontrado, processa o evento
    Serial.println("Evento de Alta Intervalada detectado!");
  }
  // Verifica Baixa Intervalada - ILhh:mm:ss
  else if (checkAndProcessEvent("IL" + hhmmssStr(lastCheck - lowDT), LOW) && !digitalRead(relayPin)) {
    // Agendamento encontrado, processa o evento
    Serial.println("Evento de Baixa Intervalada detectado!");
  }
  // Verifica se é hora de acionar o pino novamente
  else if (millis() - lastActivationTime > intervalBetweenActivations) {
    // Aciona o pino
    event = "IL00:00:00";  // IL para desligar indefinidamente usando segundos
    relay = LOW;

    // Adiciona a contagem regressiva para desligar o pino
    // Adiciona a contagem regressiva para desligar o pino
    unsigned long timeRemaining = intervalBetweenActivations - (millis() - lastActivationTime);
    unsigned long segundos = (timeRemaining % 60000) / 1000;  // Obtém os segundos restantes
    unsigned long minutos = segundos / 60;                    // Converte para minutos

    Serial.print(minutos);
    Serial.print(" minutos e ");
    Serial.print(segundos);
    Serial.println(" segundos.");
  }


  // Processa o evento
  if (event != "" && relay != digitalRead(relayPin)) {
    digitalWrite(relayPin, relay);
    if (relay) {
      // Armazena DateTime Alta
      highDT = lastCheck;
    } else {
      // Armazena DateTime Baixa
      lowDT = lastCheck;
      // Armazena o tempo do último acionamento
      lastActivationTime = millis();
    }
    return event;
  }

  return "";
}


boolean scheduleSet(const String &schedule) {
  // Salva as entradas de agendamento
  File file = SPIFFS.open("/Schedule.txt", "w+");
  if (!file) {
    Serial.println("Falha ao abrir o arquivo de agendamento");
    return false;
  }

  file.print(schedule);
  file.close();
  scheduleChk("", 0);
  return true;
}


void setup() {
  // Inicialização do Serial e outros componentes
  Serial.begin(115200);
  pinMode(D8, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  // Inicialização do sistema de arquivos SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("Falha ao montar o sistema de arquivos SPIFFS");
    return;
  }
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.autoConnect("ESPWebServer");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(F("Conectado"));
  server.begin();

  // Inicialização do cliente NTP
  timeClient.begin();

  // Restante do código setup
}

void loop() {
  // Seu código loop
  timeClient.update();
  unsigned long currentTime = timeClient.getEpochTime();


  // Exemplo de string de agendamento
  String schedule = "IH00:00:40";
  schedule += "IL00:01:00";

  String result = scheduleChk(schedule, relayPin);

  if (result != "") {
    Serial.print("Evento acionado: ");
    Serial.println(result);
  }

  // Verifica o estado do pino
  int estadoPino = digitalRead(relayPin);

  if (estadoPino == HIGH) {
    Serial.println("O pino está ligado.");
  } else {
    Serial.println("O pino está desligado.");

    // Calcula o tempo restante até o próximo acionamento
    unsigned long timeRemaining = intervalBetweenActivations - (millis() - lastActivationTime);
    Serial.print("Desligando o pino em ");
    
    unsigned long segundos = (timeRemaining % 60000) / 1000;  // Obtém os segundos restantes
    unsigned long minutos = segundos / 60;                    // Converte para minutos

    Serial.print(minutos);
    Serial.print(" minutos e ");
    Serial.print(segundos);
    Serial.println(" segundos.");
    // Adicione um pequeno atraso para evitar leituras muito frequentes
    }
    delay(1000);
  
}
