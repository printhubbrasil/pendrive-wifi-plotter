// Pen drive de rede pras máquinas de corte que só leem USB.
// ESP32-S3 DevKitC-1 N16R8: porta "USB" (nativa) vai na máquina; porta "COM" é só pra gravar.
// O arquivo chega pela página web (http://plotter.local), fica no LittleFS e a máquina
// enxerga um pen drive FAT16 com ele na raiz (o que a máquina grava fica só na memória).
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <Update.h>
#include "USB.h"
#include "USBMSC.h"
#include "tusb.h"
#include "soc/rtc_cntl_reg.h"
#include "fat_virtual.h"
#include "pagina.h"

#define VERSAO "1.2"

USBMSC msc;
WebServer server(80);
DNSServer dns;
Preferences prefs;

String nome, ssid, senha;
bool substituir, replugar;
bool modoAP = false;
// redes que a placa já conhece (redes.h, fora do git); a escolhida na página (prefs) entra na frente
#if __has_include("redes.h")
#include "redes.h"
#else
const char *REDES[][2] = {{"", ""}};   // nenhuma: configure pela rede PenDrive-<nome>
#endif
uint32_t inserirEm = 0;       // millis() pra "plugar" de novo o USB; 0 = já plugado
uint32_t apDesde = 0;
volatile bool redeMudou = false;
String motivoQueda;
File recebendo;
String recebendoNome, erroEnvio;

// ---------- LED ----------
void led(uint8_t r, uint8_t g, uint8_t b) {
#ifdef RGB_BUILTIN
  rgbLedWrite(RGB_BUILTIN, r, g, b);
#endif
}
void ledEstado() {
  if (modoAP) led(0, 0, 20);
  else if (WiFi.status() == WL_CONNECTED) led(0, 15, 0);
  else led(20, 8, 0);
}

// ---------- USB ----------
int32_t mscLer(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  uint8_t setor[vfat::SETOR];
  uint8_t *out = (uint8_t *)buffer;
  uint32_t pos = lba * vfat::SETOR + offset, feito = 0;
  while (feito < bufsize) {
    uint32_t s = pos / vfat::SETOR, o = pos % vfat::SETOR;
    uint32_t n = min(bufsize - feito, vfat::SETOR - o);
    vfat::lerSetor(s, setor);
    memcpy(out + feito, setor + o, n);
    feito += n; pos += n;
  }
  return bufsize;
}
// Aceita a escrita da máquina (fica na memória até o próximo arquivo chegar):
// pros arquivos de verdade, só a página manda.
int32_t mscEscrever(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  uint8_t setor[vfat::SETOR];
  uint32_t pos = lba * vfat::SETOR + offset, feito = 0;
  while (feito < bufsize) {
    uint32_t s = pos / vfat::SETOR, o = pos % vfat::SETOR;
    uint32_t n = min(bufsize - feito, vfat::SETOR - o);
    if (n < vfat::SETOR) vfat::lerSetor(s, setor);
    memcpy(setor + o, buffer + feito, n);
    if (!vfat::escreverSetor(s, setor)) return -1;
    feito += n; pos += n;
  }
  return bufsize;
}
bool mscStartStop(uint8_t, bool, bool) { return true; }

void usbEjetar() {
  if (inserirEm) return;
  msc.mediaPresent(false);
  if (replugar) tud_disconnect();
  inserirEm = millis() + 600000;  // fica fora até alguém agendar a volta
}
void usbInserirDaquiA(uint32_t ms) { inserirEm = millis() + ms; }
void usbInserirAgora() {
  vfat::reconstruir();
  msc.mediaPresent(true);
  if (replugar) tud_connect();
  inserirEm = 0;
  Serial.printf("USB de volta: %d arquivo(s)\n", (int)vfat::arquivos.size());
}

// ---------- utilidades ----------
String limparNome(String n) {
  int barra = max(n.lastIndexOf('/'), n.lastIndexOf('\\'));
  if (barra >= 0) n = n.substring(barra + 1);
  String r;
  for (char c : n) r += strchr(":*?\"<>|", c) || c < 32 ? '_' : c;
  r.trim();
  while (r.startsWith(".")) r.remove(0, 1);
  if (r.isEmpty()) r = "arquivo.plt";
  return r.substring(0, 120);
}
void apagarTodos(const String &exceto = "") {
  std::vector<String> lixo;
  File d = LittleFS.open(vfat::PASTA);
  for (File f = d.openNextFile(); f; f = d.openNextFile())
    if (exceto != f.name()) lixo.push_back(String(vfat::PASTA) + "/" + f.name());
  d.close();
  for (auto &p : lixo) LittleFS.remove(p);
}
void cors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
}
void json(int cod, const String &s) { cors(); server.send(cod, "application/json", s); }
String esc(const String &s) {
  String r;
  for (char c : s) { if (c == '"' || c == '\\') r += '\\'; r += c; }
  return r;
}

// ---------- rotas ----------
void rotaEstado() {
  String s = "{\"nome\":\"" + esc(nome) + "\",\"versao\":\"" VERSAO "\",\"substituir\":" + (substituir ? "true" : "false") +
             ",\"replugar\":" + (replugar ? "true" : "false") + ",\"livre\":" + String(LittleFS.totalBytes() - LittleFS.usedBytes()) +
             ",\"total\":" + String(LittleFS.totalBytes()) + ",\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") +
             "\",\"ssid\":\"" + esc(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "") + "\",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) +
             ",\"arquivos\":[";
  bool primeiro = true;
  for (auto &a : vfat::arquivos) {
    char d[20] = "";
    if (a.quando > 1700000000) { struct tm tm; localtime_r(&a.quando, &tm); strftime(d, sizeof d, "%d/%m %H:%M", &tm); }
    s += String(primeiro ? "" : ",") + "{\"nome\":\"" + esc(a.nome) + "\",\"tamanho\":" + a.tamanho + ",\"data\":\"" + d + "\"}";
    primeiro = false;
  }
  json(200, s + "]}");
}

void rotaEnvioArquivo() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    erroEnvio = "";
    recebendoNome = limparNome(up.filename);
    bool subst = server.hasArg("substituir") ? server.arg("substituir") == "1" : substituir;
    led(20, 0, 20);
    if (subst) { usbEjetar(); apagarTodos(); }
    LittleFS.remove(String(vfat::PASTA) + "/.recebendo");
    recebendo = LittleFS.open(String(vfat::PASTA) + "/.recebendo", "w");
    if (!recebendo) erroEnvio = "não consegui abrir o arquivo na memória";
    Serial.printf("Recebendo %s\n", recebendoNome.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (recebendo && erroEnvio.isEmpty() && recebendo.write(up.buf, up.currentSize) != up.currentSize)
      erroEnvio = "memória cheia — apague arquivos antigos";
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (recebendo) recebendo.close();
    String tmp = String(vfat::PASTA) + "/.recebendo";
    if (up.status == UPLOAD_FILE_ABORTED && erroEnvio.isEmpty()) erroEnvio = "envio interrompido";
    if (!erroEnvio.isEmpty()) { LittleFS.remove(tmp); if (inserirEm) usbInserirDaquiA(1500); return; }
    usbEjetar();
    String final_ = String(vfat::PASTA) + "/" + recebendoNome;
    LittleFS.remove(final_);
    LittleFS.rename(tmp, final_);
    usbInserirDaquiA(1500);  // a máquina vê o pen drive "sair" e "voltar" com o arquivo novo
    Serial.printf("Pronto: %s (%u bytes)\n", recebendoNome.c_str(), (unsigned)up.totalSize);
  }
}
void rotaEnvioFim() {
  if (erroEnvio.isEmpty() && !recebendoNome.isEmpty()) json(200, "{\"ok\":true,\"nome\":\"" + esc(recebendoNome) + "\"}");
  else json(500, "{\"ok\":false,\"erro\":\"" + esc(erroEnvio.isEmpty() ? "nenhum arquivo recebido" : erroEnvio) + "\"}");
  recebendoNome = "";
}

void rotaApagar() {
  String n = limparNome(server.arg("nome"));
  usbEjetar();
  bool ok = LittleFS.remove(String(vfat::PASTA) + "/" + n);
  usbInserirDaquiA(1500);
  json(ok ? 200 : 404, ok ? "{\"ok\":true}" : "{\"ok\":false,\"erro\":\"não achei\"}");
}
void rotaApagarTudo() {
  usbEjetar();
  apagarTodos();
  usbInserirDaquiA(1500);
  json(200, "{\"ok\":true}");
}

void rotaConfig() {
  bool reiniciar = false;
  if (server.hasArg("substituir")) { substituir = server.arg("substituir") == "1"; prefs.putBool("subst", substituir); }
  if (server.hasArg("replugar")) { replugar = server.arg("replugar") == "1"; prefs.putBool("replugar", replugar); }
  if (server.hasArg("nome")) {
    String n;
    for (char c : server.arg("nome")) if (isalnum(c) || c == '-') n += c;
    n = n.substring(0, 11);
    if (!n.isEmpty() && n != nome) { prefs.putString("nome", n); reiniciar = true; }
  }
  json(200, "{\"ok\":true}");
  if (reiniciar) { delay(500); ESP.restart(); }
}

void rotaWifi() {
  prefs.putString("ssid", server.arg("ssid"));
  prefs.putString("senha", server.arg("senha"));
  json(200, "{\"ok\":true}");
  delay(800);
  ESP.restart();
}

void rotaRedes() {
  int n = WiFi.scanNetworks();
  String s = "[";
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i).isEmpty() || s.indexOf("\"" + esc(WiFi.SSID(i)) + "\"") >= 0) continue;
    s += String(s.length() > 1 ? "," : "") + "\"" + esc(WiFi.SSID(i)) + "\"";
  }
  WiFi.scanDelete();
  json(200, s + "]");
}

void rotaAtualizarArquivo() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (up.status == UPLOAD_FILE_WRITE) Update.write(up.buf, up.currentSize);
  else if (up.status == UPLOAD_FILE_END) Update.end(true);
}
void rotaAtualizarFim() {
  bool ok = !Update.hasError();
  cors();
  server.send(200, "text/plain", ok ? "Atualizado. Reiniciando…" : "Falhou: " + String(Update.errorString()));
  if (ok) { delay(800); ESP.restart(); }
}

// Reinicia direto no modo de gravação pelo cabo (substitui segurar o BOOT).
void rotaModoGravacao() {
  json(200, "{\"ok\":true}");
  delay(500);
  REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  esp_restart();
}

// ---------- Wi-Fi ----------
// Procura as redes conhecidas; achando uma, deixa ssid/senha apontando pra ela.
bool redeConhecidaNoAr() {
  String salva = prefs.getString("ssid", ""), salvaSenha = prefs.getString("senha", "");
  int n = WiFi.scanNetworks();
  bool achou = false;
  for (int k = -1; k < (int)(sizeof(REDES) / sizeof(REDES[0])) && !achou; k++) {
    String s = k < 0 ? salva : REDES[k][0];
    if (s.isEmpty()) continue;
    for (int i = 0; i < n; i++)
      if (WiFi.SSID(i) == s) { ssid = s; senha = k < 0 ? salvaSenha : REDES[k][1]; achou = true; break; }
  }
  WiFi.scanDelete();
  return achou;
}

// Escreve o _REDE.TXT do pen drive: dá pra descobrir o endereço plugando no computador.
void atualizarInfo() {
  String t = "Pen Drive de Rede " VERSAO " - " + nome + "\r\n";
  if (WiFi.status() == WL_CONNECTED)
    t += "Wi-Fi: " + WiFi.SSID() + " (" + WiFi.RSSI() + " dBm)\r\nEndereco: http://" + WiFi.localIP().toString() + "  ou  http://" + nome + ".local\r\n";
  else if (modoAP)
    t += "Sem Wi-Fi. Conecte na rede PenDrive-" + nome + " (senha 12345678) e abra http://192.168.4.1\r\n";
  else t += "Conectando em " + ssid + "...\r\n";
  if (!motivoQueda.isEmpty()) t += "Ultima falha: " + motivoQueda + "\r\n";
  vfat::info = t;
}

void iniciarWifi() {
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t i) { redeMudou = true; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t i) {
    if (i.wifi_sta_disconnected.reason == WIFI_REASON_ASSOC_LEAVE) return;  // fomos nós que saímos
    motivoQueda = String(ssid) + ": " + String(i.wifi_sta_disconnected.reason) + " " + WiFi.disconnectReasonName((wifi_err_reason_t)i.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(nome.c_str());
  WiFi.setSleep(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  for (int t = 0; t < 2 && WiFi.status() != WL_CONNECTED; t++) {
    if (!redeConhecidaNoAr()) { delay(1000); continue; }
    WiFi.begin(ssid.c_str(), senha.c_str());
    Serial.printf("Conectando em %s", ssid.c_str());
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print('.'); }
    Serial.println();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("IP %s  ->  http://%s.local\n", WiFi.localIP().toString().c_str(), nome.c_str());
    configTzTime("<-03>3", "pool.ntp.org", "a.st1.ntp.br");
  } else {
    // sem rede: abre a própria rede pra configurar. A rede conhecida é procurada de
    // tempos em tempos no loop — tentar sem parar faz o rádio pular de canal e derruba o AP.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(("PenDrive-" + nome).c_str(), "12345678");
    dns.start(53, "*", WiFi.softAPIP());
    modoAP = true;
    apDesde = millis();
    Serial.printf("Rede de configuração: PenDrive-%s (senha 12345678) -> http://192.168.4.1\n", nome.c_str());
  }
  MDNS.begin(nome.c_str());
  MDNS.addService("http", "tcp", 80);
  atualizarInfo();
  redeMudou = false;
  usbEjetar();
  usbInserirDaquiA(1000);
}

void setup() {
  Serial.begin(115200);
  prefs.begin("pendrive", false);
  // v2: esquece a rede gravada no teste (ela isolava os aparelhos entre si)
  if (prefs.getUInt("cfgver", 1) < 2) { prefs.remove("ssid"); prefs.remove("senha"); prefs.putUInt("cfgver", 2); }
  nome = prefs.getString("nome", "plotter");
  ssid = prefs.getString("ssid", "");
  senha = prefs.getString("senha", "");
  substituir = prefs.getBool("subst", true);
  replugar = prefs.getBool("replugar", true);
  led(20, 8, 0);

  if (!LittleFS.begin(true)) Serial.println("LittleFS falhou!");
  LittleFS.remove(String(vfat::PASTA) + "/.recebendo");
  vfat::iniciar(nome);
  vfat::reconstruir();

  msc.vendorID("PrintHub");
  msc.productID("PenDrive Rede");
  msc.productRevision(VERSAO);
  msc.onRead(mscLer);
  msc.onWrite(mscEscrever);
  msc.onStartStop(mscStartStop);
  msc.isWritable(true);
  msc.mediaPresent(true);
  msc.begin(vfat::SETORES_DISCO, vfat::SETOR);
  USB.manufacturerName("PrintHub");
  USB.productName("PenDrive Rede");
  USB.begin();

  iniciarWifi();

  server.on("/", [] { server.send_P(200, "text/html; charset=utf-8", PAGINA); });
  server.on("/api/estado", rotaEstado);
  server.on("/enviar", HTTP_POST, rotaEnvioFim, rotaEnvioArquivo);
  server.on("/api/apagar", HTTP_POST, rotaApagar);
  server.on("/api/apagar-tudo", HTTP_POST, rotaApagarTudo);
  server.on("/api/config", HTTP_POST, rotaConfig);
  server.on("/api/wifi", HTTP_POST, rotaWifi);
  server.on("/api/redes", rotaRedes);
  server.on("/api/modo-gravacao", HTTP_POST, rotaModoGravacao);
  server.on("/atualizar", HTTP_POST, rotaAtualizarFim, rotaAtualizarArquivo);
  server.onNotFound([] {
    if (server.method() == HTTP_OPTIONS) { cors(); server.send(204); return; }
    server.sendHeader("Location", "http://" + (modoAP ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "/");
    server.send(302);
  });
  server.begin();
  ledEstado();
}

void loop() {
  server.handleClient();
  if (modoAP) {
    dns.processNextRequest();
    // ninguém configurando: a cada 2 min vê se a rede conhecida está no ar
    static uint32_t ultBusca = 0;
    if (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0 && millis() - ultBusca > 120000) {
      ultBusca = millis();
      if (redeConhecidaNoAr()) { WiFi.begin(ssid.c_str(), senha.c_str()); apDesde = millis(); }
    }
    // a rede conhecida voltou e ninguém está na rede de configuração: fecha ela
    if (WiFi.status() == WL_CONNECTED && WiFi.softAPgetStationNum() == 0 && millis() - apDesde > 60000) {
      dns.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      modoAP = false;
      configTzTime("<-03>3", "pool.ntp.org", "a.st1.ntp.br");
      Serial.printf("Conectado: %s\n", WiFi.localIP().toString().c_str());
    }
  }
  if (redeMudou && !inserirEm) { redeMudou = false; atualizarInfo(); usbEjetar(); usbInserirDaquiA(1500); }
  if (inserirEm && (int32_t)(millis() - inserirEm) >= 0) usbInserirAgora();
  static uint32_t ultLed = 0;
  if (millis() - ultLed > 2000 && !inserirEm) { ultLed = millis(); ledEstado(); }
  delay(2);
}
