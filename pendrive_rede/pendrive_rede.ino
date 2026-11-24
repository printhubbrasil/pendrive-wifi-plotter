// Pen drive de rede pras máquinas de corte que só leem USB.
// ESP32-S3 DevKitC-1 N16R8: porta "USB" (nativa) vai na máquina; porta "COM" é só pra gravar.
// O arquivo chega pela página web (http://plotter.local), fica no LittleFS e a máquina
// enxerga um pen drive FAT16 com ele na raiz (o que a máquina grava fica só na memória).
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <Update.h>
#include "USB.h"
#include "esp32-hal-tinyusb.h"
#include "tusb.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_dwc_struct.h"
#include "fat_virtual.h"
#include "pagina.h"

#define VERSAO "1.22"

int mscRegistro = -99;
volatile bool mscPresente = false;   // v1.12: camada de pen drive própria (no lugar do USBMSC do núcleo)
WebServer server(80);
DNSServer dns;
Preferences prefs;

String nome, ssid, senha;
bool substituir, replugar;
// v1.20 (teste): depois de entregar um arquivo inteiro à máquina: 0 = nada, 1 = sai e volta, 2 = reinicia a placa
uint8_t aposEntrega = 0;
volatile uint32_t entregueEm = 0;
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
// diagnóstico (v1.3): aparece na página e no /api/estado
uint32_t nBoots = 0, nQuedas = 0, nReplugs = 0, nLeituras = 0, nEscritas = 0;
// v1.14: socorro — a SAGA às vezes "perde" o pen drive (porta em repouso e nenhum comando depois). Sair e voltar faz ela reconhecer de novo.
volatile uint32_t suspensoDesde = 0;   // 0 = porta acordada
uint32_t nDados = 0, nSocorros = 0, ultSocorro = 0, dadosNoSocorro = 0, socorrosSeguidos = 0;
// v1.22: socorro quase na hora, como um pen drive comum que perde a energia e volta
bool voltaRapida = false;            // volta do socorro: não refaz a lista (nada mudou)
uint32_t voltouEm = 0;               // quando a placa se reconectou por conta própria
volatile uint32_t enumEm = 0;        // quando a máquina reconheceu o pen drive pela última vez
uint32_t ultLeitura = 0, ultEscrita = 0, ultQueda = 0, ultReplug = 0;
String motivoBoot, ipNoInfo;
// v1.11: o pen drive fica SEMPRE plugado. Arquivo novo / apagar entram numa fila e são aplicados
// de uma vez só, numa troca rápida, quando a máquina não está lendo.
struct Troca { String tmp, final_; };
std::vector<Troca> pendRenomear;
std::vector<String> pendApagar;
bool pendApagarTudo = false, temPend = false, recebendoAtivo = false;
uint32_t pendDesde = 0, nRx = 0;
String recebendoTmp;
uint32_t espacoLivre = 0, espacoTotal = 0;
void medirEspaco() { espacoTotal = LittleFS.totalBytes(); espacoLivre = espacoTotal - LittleFS.usedBytes(); }
void marcarPend() { temPend = true; pendDesde = millis(); }
// v1.9: várias plaquinhas. Cada uma avisa que existe por UDP (broadcast) e guarda quem ouviu.
WiFiUDP udp;
const uint16_t PORTA_AVISO = 8737;
struct Placa { String nome, ip, mac, versao; int arquivos; uint32_t visto; };
std::vector<Placa> placas;
uint32_t ultAviso = 0;
// "Adicionar plaquinha": fica armado uns minutos; o programa do Mac vê, grava a placa nova e conta o andamento aqui
uint32_t addAte = 0;
String addNome, addProg;
// rastro das últimas leituras/escritas da máquina (v1.4): /api/rastro
struct Rastro { uint32_t t, lba; uint16_t n, ms; uint8_t w; };
const int N_RASTRO = 600;
Rastro rastro[N_RASTRO];
volatile uint32_t rastroPos = 0;
uint32_t maiorMs = 0;
void anotar(uint32_t t0, uint32_t lba, uint32_t bytes, uint8_t w) {
  uint32_t ms = millis() - t0;
  if (ms > maiorMs) maiorMs = ms;
  if (w == 0 && rastroPos) {
    Rastro &a = rastro[(rastroPos - 1) % N_RASTRO];
    if (a.w == 0 && a.lba + a.n == lba && t0 - a.t < 3000 && a.n < 60000) {
      a.n += bytes / 512; a.ms = max(a.ms, (uint16_t)min(ms, (uint32_t)65535)); return;
    }
  }
  rastro[rastroPos % N_RASTRO] = {t0, lba, (uint16_t)(bytes / 512), (uint16_t)min(ms, (uint32_t)65535), w};
  rastroPos++;
}
const char *nomeReset(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "ligou na energia";
    case ESP_RST_BROWNOUT: return "QUEDA DE ENERGIA (tensao baixa)";
    case ESP_RST_SW: return "reinicio pedido pelo programa";
    case ESP_RST_PANIC: return "ERRO do programa (panic)";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "TRAVOU (watchdog)";
    case ESP_RST_EXT: return "botao de reset";
    case ESP_RST_USB: return "reset pelo USB";
    case ESP_RST_DEEPSLEEP: return "voltou do sono";
    default: return "desconhecido";
  }
}
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
  nLeituras++; ultLeitura = millis();
  if (lba > 2048) nDados++;   // leitura de verdade (lista, FAT ou arquivo), não só o reconhecimento
  uint32_t t0 = ultLeitura;
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
  anotar(t0, lba, bufsize, 0);
  return bufsize;
}
// Aceita a escrita da máquina (fica na memória até o próximo arquivo chegar):
// pros arquivos de verdade, só a página manda.
int32_t mscEscrever(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  nEscritas++; ultEscrita = millis();
  anotar(ultEscrita, lba, bufsize, 1);
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
// ---------- camada "pen drive" (SCSI) própria — v1.12 ----------
// Registra cada comando que a máquina manda e responde a mais comandos que o núcleo,
// sem travar o canal (STALL) nos que um pen drive comum aceita.
void anotarCmd(uint8_t op) {
  uint32_t agora = millis();
  if (rastroPos) {
    Rastro &a = rastro[(rastroPos - 1) % N_RASTRO];
    if (a.w == 10 && a.lba == op && a.n < 60000) { a.n++; a.ms = (uint16_t)min((agora - a.t) / 1000, (uint32_t)65535); return; }
  }
  rastro[rastroPos % N_RASTRO] = {agora, op, 1, 0, 10};
  rastroPos++;
}
uint8_t mscEp = 0;   // v1.15: número da porta de dados do pen drive (pra fotografar o chip USB no repouso)
extern "C" uint16_t pd_msc_descritor(uint8_t *dst, uint8_t *itf) {
  uint8_t str_index = tinyusb_add_string_descriptor("PenDrive Rede");
  uint8_t ep_num = tinyusb_get_free_duplex_endpoint();
  TU_VERIFY(ep_num != 0);
  mscEp = ep_num;
  uint8_t d[TUD_MSC_DESC_LEN] = {TUD_MSC_DESCRIPTOR(*itf, str_index, ep_num, (uint8_t)(0x80 | ep_num), CFG_TUD_ENDPOINT_SIZE)};
  *itf += 1;
  memcpy(dst, d, TUD_MSC_DESC_LEN);
  return TUD_MSC_DESC_LEN;
}
extern "C" {
uint8_t tud_msc_get_maxlun_cb(void) { return 1; }
void tud_msc_inquiry_cb(uint8_t, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
  anotarCmd(0x12);
  memcpy(vendor_id, "PrintHub", 8);
  memcpy(product_id, "PenDrive Rede   ", 16);
  memcpy(product_rev, "1.13", 4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  anotarCmd(0x00);
  if (!mscPresente) { tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00); return false; }
  return true;
}
void tud_msc_capacity_cb(uint8_t, uint32_t *block_count, uint16_t *block_size) {
  anotarCmd(0x25);
  *block_count = mscPresente ? vfat::SETORES_DISCO : 0;
  *block_size = mscPresente ? vfat::SETOR : 0;
}
bool tud_msc_start_stop_cb(uint8_t, uint8_t, bool start, bool load_eject) {
  anotar(millis(), start ? 1 : 0, load_eject ? 512 : 0, 5);
  return true;   // nunca "ejeta" de verdade: o disco continua lá
}
bool tud_msc_prevent_allow_medium_removal_cb(uint8_t, uint8_t, uint8_t) { anotarCmd(0x1E); return true; }
int32_t tud_msc_read10_cb(uint8_t, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (!mscPresente) return 0;
  return mscLer(lba, offset, buffer, bufsize);
}
int32_t tud_msc_write10_cb(uint8_t, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  if (!mscPresente) return 0;
  return mscEscrever(lba, offset, buffer, bufsize);
}
bool tud_msc_is_writable_cb(uint8_t) { return true; }
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const cmd[16], void *buffer, uint16_t bufsize) {
  anotarCmd(cmd[0]);
  uint8_t r[32];
  memset(r, 0, sizeof r);
  int32_t n = 0;
  switch (cmd[0]) {
    case 0x1E:            // PREVENT ALLOW MEDIUM REMOVAL
    case 0x35:            // SYNCHRONIZE CACHE (10)
    case 0x91:            // SYNCHRONIZE CACHE (16)
    case 0x2F:            // VERIFY (10)
    case 0x2B:            // SEEK (10)
    case 0x01:            // REZERO UNIT
      return 0;
    case 0x5A:            // MODE SENSE (10): cabeçalho de 8 bytes, sem páginas
      r[1] = 6; n = 8; break;
    case 0x9E:            // SERVICE ACTION IN (16) -> READ CAPACITY (16)
      if ((cmd[1] & 0x1F) != 0x10) goto invalido;
      { uint32_t ult = vfat::SETORES_DISCO - 1;
        r[4] = ult >> 24; r[5] = ult >> 16; r[6] = ult >> 8; r[7] = ult;
        r[10] = vfat::SETOR >> 8; r[11] = vfat::SETOR & 0xFF; n = 32; }
      break;
    case 0xA0:            // REPORT LUNS: uma unidade só
      r[3] = 8; n = 16; break;
    default:
    invalido:
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
      return -1;
  }
  if (n > bufsize) n = bufsize;
  memcpy(buffer, r, n);
  return n;
}
}
// v1.5: eventos do barramento USB no rastro (w: 2 = host suspendeu, 3 = host retomou, 4 = host ligou/enumerou, 6 = host desligou)
// v1.6 (teste): a SAGA põe a porta em repouso e, ao acordar, não consegue mais falar com a placa.
// Quando ela acorda a porta, a placa sai e volta do USB pra máquina reconhecer do zero.
volatile bool acordou = false;
void usbEvento(void *, esp_event_base_t base, int32_t id, void *) {
  if (base != ARDUINO_USB_EVENTS) return;
  uint8_t w = id == ARDUINO_USB_SUSPEND_EVENT ? 2 : id == ARDUINO_USB_RESUME_EVENT ? 3 : id == ARDUINO_USB_STARTED_EVENT ? 4 : id == ARDUINO_USB_STOPPED_EVENT ? 6 : 9;
  // no repouso, guarda o registrador de estado do chip USB: o bit 3 diz se foi "erro de sinal" (e não repouso normal)
  anotar(millis(), w == 2 ? (uint32_t)USB_DWC.dsts_reg.val : 0, 0, w);
  if (w == 2 && mscEp >= 1 && mscEp <= 6) {   // v1.15: foto das portas de dados no instante do repouso
    anotar(millis(), (uint32_t)USB_DWC.out_eps[mscEp - 1].doepctl_reg.val, (uint16_t)USB_DWC.out_eps[mscEp - 1].doeptsiz_reg.val, 12);
    anotar(millis(), (uint32_t)USB_DWC.in_eps[mscEp - 1].diepctl_reg.val, 0, 13);
  }
  if (w == 2) { anotar(millis(), (uint32_t)USB_DWC.gintsts_reg.val, 0, 21); anotar(millis(), (uint32_t)USB_DWC.pcgcctl_reg.val, 0, 22); }
  if (w == 4) { uint32_t m = millis(); enumEm = m ? m : 1; }
  if (w == 3) acordou = true;
  if (w == 2) { uint32_t m = millis(); suspensoDesde = m ? m : 1; } else if (w == 3 || w == 4) suspensoDesde = 0;
}

void usbEjetar() {
  if (inserirEm) return;
  mscPresente = false;
  if (replugar) tud_disconnect();
  nReplugs++; ultReplug = millis();
  anotar(millis(), 0, 0, 7);
  suspensoDesde = 0;
  inserirEm = millis() + 600000;  // fica fora até alguém agendar a volta
}
void usbInserirDaquiA(uint32_t ms) { inserirEm = millis() + ms; }
void usbInserirAgora() {
  if (!voltaRapida) vfat::reconstruir();
  voltaRapida = false;
  voltouEm = millis(); suspensoDesde = 0;
  mscPresente = true;
  if (replugar) tud_connect();
  anotar(millis(), 0, 0, 8);
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
    if (exceto != f.name() && strncmp(f.name(), ".rx", 3) != 0) lixo.push_back(String(vfat::PASTA) + "/" + f.name());
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

// ---------- várias plaquinhas ----------
void avisoEnviar() {
  if (WiFi.status() != WL_CONNECTED) return;
  String m = "PDREDE|" + nome + "|" + WiFi.localIP().toString() + "|" + WiFi.macAddress() + "|" VERSAO "|" + String((int)vfat::arquivos.size());
  udp.beginPacket(IPAddress(255, 255, 255, 255), PORTA_AVISO);
  udp.print(m);
  udp.endPacket();
}
void avisoReceber() {
  int n = udp.parsePacket();
  if (n <= 0) return;
  char buf[200];
  int l = udp.read(buf, sizeof(buf) - 1);
  if (l <= 0) return;
  buf[l] = 0;
  String m(buf);
  if (!m.startsWith("PDREDE|")) return;
  String c[6]; int k = 0, ini = 0;
  for (int i = 0; i <= (int)m.length() && k < 6; i++)
    if (i == (int)m.length() || m[i] == '|') { c[k++] = m.substring(ini, i); ini = i + 1; }
  if (k < 6 || c[3] == WiFi.macAddress()) return;
  for (auto &p : placas) if (p.mac == c[3]) { p = {c[1], c[2], c[3], c[4], (int)c[5].toInt(), millis()}; return; }
  if (placas.size() < 30) placas.push_back({c[1], c[2], c[3], c[4], (int)c[5].toInt(), millis()});
}
void rotaPlacas() {
  String s = "{\"eu\":{\"nome\":\"" + esc(nome) + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"mac\":\"" + WiFi.macAddress() + "\",\"versao\":\"" VERSAO "\"},\"placas\":[";
  bool primeiro = true;
  for (auto &p : placas) {
    if (millis() - p.visto > 120000) continue;   // sumiu há mais de 2 min
    s += String(primeiro ? "" : ",") + "{\"nome\":\"" + esc(p.nome) + "\",\"ip\":\"" + esc(p.ip) + "\",\"mac\":\"" + esc(p.mac) + "\",\"versao\":\"" + esc(p.versao) + "\",\"arquivos\":" + String(p.arquivos) + ",\"visto\":" + String((millis() - p.visto) / 1000) + "}";
    primeiro = false;
  }
  int32_t resta = addAte ? (int32_t)(addAte - millis()) / 1000 : 0;
  if (resta <= 0) { resta = 0; addAte = 0; }
  json(200, s + "],\"adicionar\":{\"resta\":" + String(resta) + ",\"nome\":\"" + esc(addNome) + "\",\"progresso\":\"" + esc(addProg) + "\"}}");
}
void rotaAdicionar() {
  if (server.hasArg("cancelar")) { addAte = 0; addProg = ""; json(200, "{\"ok\":true}"); return; }
  String n;
  for (char c : server.arg("nome")) if (isalnum(c) || c == '-') n += c;
  n = n.substring(0, 11);
  if (n.isEmpty()) { json(400, "{\"ok\":false,\"erro\":\"falta o nome\"}"); return; }
  addNome = n;
  addProg = "Esperando a plaquinha nova ser ligada no Mac pelo cabo...";
  addAte = millis() + 600000;
  json(200, "{\"ok\":true}");
}
void rotaAdicionarProgresso() {
  addProg = server.arg("txt").substring(0, 200);
  if (server.hasArg("fim")) addAte = 0;
  else if (addAte) addAte = millis() + 600000;   // enquanto o Mac trabalha, não deixa vencer
  json(200, "{\"ok\":true}");
}

// ---------- rotas ----------
String ondeFica(uint32_t lba) {
  if (lba == 0) return "MBR";
  if (lba < vfat::INICIO_PART) return "antes da particao";
  uint32_t v = lba - vfat::INICIO_PART;
  if (v == 0) return "boot";
  if (v < vfat::FAT1) return "reservado";
  if (v < vfat::FAT2) return "FAT1+" + String(v - vfat::FAT1);
  if (v < vfat::RAIZ) return "FAT2+" + String(v - vfat::FAT2);
  if (v < vfat::DADOS) return "LISTA+" + String(v - vfat::RAIZ);
  uint32_t c = 2 + (v - vfat::DADOS) / vfat::SPC;
  for (auto &a : vfat::arquivos)
    if (a.nClusters && c >= a.cluster && c < (uint32_t)a.cluster + a.nClusters)
      return a.nome + " @" + String(((c - a.cluster) * vfat::SPC + (v - vfat::DADOS) % vfat::SPC) * 512) + "/" + String(a.tamanho);
  return "vazio(c" + String(c) + ")";
}
String nomeCmd(uint32_t op) {
  const char *n = op == 0x00 ? "TESTE DE PRONTO" : op == 0x12 ? "IDENTIFICACAO" : op == 0x25 ? "TAMANHO" : op == 0x1E ? "TRAVA DE REMOCAO" :
                  op == 0x1A ? "MODE SENSE 6" : op == 0x5A ? "MODE SENSE 10" : op == 0x23 ? "FORMATOS" : op == 0x35 ? "SINCRONIZAR" :
                  op == 0x03 ? "PEDIDO DE ERRO" : op == 0x2F ? "VERIFICAR" : op == 0x9E ? "TAMANHO 16" : op == 0xA0 ? "LISTA DE UNIDADES" : "NAO ATENDIDO";
  char h[8]; snprintf(h, sizeof h, "%02X", (unsigned)op);
  return String("cmd ") + h + " " + n;
}
void rotaRastro() {
  uint32_t fim = rastroPos, ini = fim > N_RASTRO ? fim - N_RASTRO : 0, agora = millis();
  String s = "{\"agora\":" + String(agora / 1000) + ",\"total\":" + String(fim) + ",\"maiorMs\":" + String(maiorMs) + ",\"itens\":[";
  for (uint32_t i = ini; i < fim; i++) {
    Rastro &r = rastro[i % N_RASTRO];
    s += String(i > ini ? "," : "") + "[" + String((agora - r.t) / 1000.0, 2) + "," + String(r.lba) + "," + String(r.n) + "," + String(r.ms) + "," + String(r.w) + ",\"" + esc(r.w == 2 ? ((r.lba & 8) ? "USB SUSPENSO por ERRO DE SINAL" : "USB SUSPENSO pelo host") : r.w == 3 ? "USB retomado" : r.w == 21 ? String("chip: avisos pendentes ") + String(r.lba, HEX) : r.w == 22 ? String("chip: relogio da porta ") + String(r.lba, HEX) : r.w == 20 ? (r.lba == 2 ? "APOS A ENTREGA: placa reiniciando" : "APOS A ENTREGA: placa saiu e volta em 2,5 s") : r.w == 4 ? "USB enumerado" : r.w == 6 ? "USB parado" : r.w == 7 ? "PROGRAMA tirou o pen drive" : r.w == 8 ? "PROGRAMA devolveu o pen drive" : r.w == 5 ? "start/stop" : r.w == 9 ? "evento usb" : r.w == 12 ? String("chip: entrada de comando ") + ((r.lba & 0x80000000UL) ? "ARMADA (esperando comando da maquina)" : "NAO ARMADA") + ((r.lba & 0x200000UL) ? " TRAVADA(stall)" : "") + ((r.lba & 0x10000UL) ? " d1" : " d0") : r.w == 13 ? String("chip: saida de dados ") + ((r.lba & 0x80000000UL) ? "COM RESPOSTA PRESA (maquina nao buscou)" : "vazia") + ((r.lba & 0x200000UL) ? " TRAVADA(stall)" : "") + ((r.lba & 0x10000UL) ? " d1" : " d0") : r.w == 11 ? "SOCORRO: porta parada, a placa saiu e voltou sozinha" : r.w == 16 ? String("FALHA AO LER DA MEMORIA da placa (tentou de novo) trecho ") + String(r.lba) : r.w == 17 ? String("arquivo entregue INTEIRO e CONFERIDO: ") + (r.lba < vfat::arquivos.size() ? vfat::arquivos[r.lba].nome : String("?")) : r.w == 18 ? String("ARQUIVO ENTREGUE COM ERRO: ") + (r.lba < vfat::arquivos.size() ? vfat::arquivos[r.lba].nome : String("?")) : r.w == 19 ? String("porta parada: o chip ve ") + ((r.lba & 1) ? "REPOUSO" : "PORTA ATIVA (a placa perdeu a volta!)") + ", quadro " + String((r.lba >> 8) & 0x3FFF) : r.w == 10 ? nomeCmd(r.lba) : ondeFica(r.lba)) + "\"]";
  }
  json(200, s + "]}");
}
// v1.19: lê cada arquivo pelo MESMO caminho que a máquina usa e diz se o que sai confere com o guardado.
void rotaConferir() {
  String s = "{\"arquivos\":[";
  uint8_t setor[vfat::SETOR];
  bool primeiro = true;
  for (size_t i = 0; i < vfat::arquivos.size(); i++) {
    auto &a = vfat::arquivos[i];
    if (a.caminho.isEmpty() || !a.nClusters) continue;
    uint32_t antesOk = vfat::entregasOk, antesRuim = vfat::entregasRuins, antesFalha = vfat::falhasLeitura;
    uint32_t lba0 = vfat::INICIO_PART + vfat::DADOS + (uint32_t)(a.cluster - 2) * vfat::SPC;
    for (uint32_t k = 0; k * vfat::SETOR < a.tamanho; k++) vfat::lerSetor(lba0 + k, setor);
    s += String(primeiro ? "" : ",") + "{\"nome\":\"" + esc(a.nome) + "\",\"tamanho\":" + String(a.tamanho) +
         ",\"confere\":" + (vfat::entregasOk == antesOk + 1 && vfat::entregasRuins == antesRuim ? "true" : "false") +
         ",\"falhas\":" + String(vfat::falhasLeitura - antesFalha) + "}";
    primeiro = false;
  }
  entregueEm = 0;   // conferência pela rede não conta como entrega à máquina
  json(200, s + "]}");
}
void rotaEstado() {
  String s = "{\"nome\":\"" + esc(nome) + "\",\"versao\":\"" VERSAO "\",\"substituir\":" + (substituir ? "true" : "false") +
             ",\"replugar\":" + (replugar ? "true" : "false") + ",\"aposEntrega\":" + String(aposEntrega) + ",\"livre\":" + String(espacoLivre) +
             ",\"total\":" + String(espacoTotal) + ",\"pend\":" + (temPend ? "true" : "false") + ",\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") +
             "\",\"ssid\":\"" + esc(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "") + "\",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) +
             ",\"ligado\":" + String(millis() / 1000) + ",\"boots\":" + String(nBoots) + ",\"motivoBoot\":\"" + esc(motivoBoot) +
             "\",\"quedasWifi\":" + String(nQuedas) + ",\"ultQueda\":" + String(nQuedas ? (int32_t)((millis() - ultQueda) / 1000) : -1) +
             ",\"motivoQueda\":\"" + esc(motivoQueda) + "\",\"replugs\":" + String(nReplugs) + ",\"socorros\":" + String(nSocorros) + ",\"ultSocorro\":" + String(nSocorros ? (int32_t)((millis() - ultSocorro) / 1000) : -1) +
             ",\"ultReplug\":" + String(nReplugs ? (int32_t)((millis() - ultReplug) / 1000) : -1) +
             ",\"falhasLeitura\":" + String(vfat::falhasLeitura) + ",\"entregasOk\":" + String(vfat::entregasOk) + ",\"entregasRuins\":" + String(vfat::entregasRuins) +
             ",\"leituras\":" + String(nLeituras) + ",\"ultLeitura\":" + String(nLeituras ? (int32_t)((millis() - ultLeitura) / 1000) : -1) +
             ",\"escritas\":" + String(nEscritas) + ",\"ultEscrita\":" + String(nEscritas ? (int32_t)((millis() - ultEscrita) / 1000) : -1) +
             ",\"usbMontado\":" + (tud_mounted() ? "true" : "false") + ",\"usbSuspenso\":" + (tud_suspended() ? "true" : "false") +
             ",\"usbFora\":" + (inserirEm ? "true" : "false") + ",\"mscRegistro\":" + String(mscRegistro) + ",\"usbConectado\":" + (tud_connected() ? "true" : "false") + ",\"usbPronto\":" + (tud_ready() ? "true" : "false") +
             ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"heapMin\":" + String(ESP.getMinFreeHeap()) + ",\"psram\":" + String(ESP.getFreePsram()) +
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
    if (subst) {   // apagar os anteriores: vale pro que já estava e pro que ainda esperava na fila
      for (auto &t : pendRenomear) LittleFS.remove(t.tmp);
      pendRenomear.clear(); pendApagar.clear(); pendApagarTudo = true;
    }
    recebendoTmp = String(vfat::PASTA) + "/.rx" + String(++nRx);
    LittleFS.remove(recebendoTmp);
    recebendo = LittleFS.open(recebendoTmp, "w");
    if (!recebendo) erroEnvio = "não consegui abrir o arquivo na memória";
    recebendoAtivo = true;
    marcarPend();
    Serial.printf("Recebendo %s\n", recebendoNome.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (recebendo && erroEnvio.isEmpty() && recebendo.write(up.buf, up.currentSize) != up.currentSize)
      erroEnvio = "memória cheia — apague arquivos antigos";
    pendDesde = millis();
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (recebendo) recebendo.close();
    recebendoAtivo = false;
    if (up.status == UPLOAD_FILE_ABORTED && erroEnvio.isEmpty()) erroEnvio = "envio interrompido";
    if (!erroEnvio.isEmpty()) { LittleFS.remove(recebendoTmp); marcarPend(); return; }
    pendRenomear.push_back({recebendoTmp, String(vfat::PASTA) + "/" + recebendoNome});
    marcarPend();   // entra no pen drive na próxima troca (a máquina vê ele "sair e voltar" uma vez só)
    Serial.printf("Recebido: %s (%u bytes)\n", recebendoNome.c_str(), (unsigned)up.totalSize);
  }
}
void rotaEnvioFim() {
  if (erroEnvio.isEmpty() && !recebendoNome.isEmpty()) json(200, "{\"ok\":true,\"nome\":\"" + esc(recebendoNome) + "\"}");
  else json(500, "{\"ok\":false,\"erro\":\"" + esc(erroEnvio.isEmpty() ? "nenhum arquivo recebido" : erroEnvio) + "\"}");
  recebendoNome = "";
}

void rotaApagar() {
  String n = limparNome(server.arg("nome"));
  String cam = String(vfat::PASTA) + "/" + n;
  bool ok = false;
  for (size_t k = 0; k < pendRenomear.size(); k++)       // ainda estava na fila de chegada
    if (pendRenomear[k].final_ == cam) { LittleFS.remove(pendRenomear[k].tmp); pendRenomear.erase(pendRenomear.begin() + k); k--; ok = true; }
  if (LittleFS.exists(cam)) { pendApagar.push_back(cam); ok = true; }
  if (ok) marcarPend();
  json(ok ? 200 : 404, ok ? "{\"ok\":true}" : "{\"ok\":false,\"erro\":\"não achei\"}");
}
void rotaApagarTudo() {
  for (auto &t : pendRenomear) LittleFS.remove(t.tmp);
  pendRenomear.clear(); pendApagar.clear();
  pendApagarTudo = true;
  marcarPend();
  json(200, "{\"ok\":true}");
}
// Aplica a fila numa troca só. Só roda com a máquina parada (sem leitura há alguns segundos).
void aplicarPendencias() {
  usbEjetar();
  if (pendApagarTudo) apagarTodos();
  for (auto &c : pendApagar) LittleFS.remove(c);
  for (auto &t : pendRenomear) { LittleFS.remove(t.final_); LittleFS.rename(t.tmp, t.final_); }
  pendApagarTudo = false; pendApagar.clear(); pendRenomear.clear(); temPend = false;
  medirEspaco();
  usbInserirDaquiA(800);
}

void rotaConfig() {
  bool reiniciar = false;
  if (server.hasArg("substituir")) { substituir = server.arg("substituir") == "1"; prefs.putBool("subst", substituir); }
  if (server.hasArg("replugar")) { replugar = server.arg("replugar") == "1"; prefs.putBool("replugar", replugar); }
  if (server.hasArg("aposEntrega")) { aposEntrega = (uint8_t)constrain(server.arg("aposEntrega").toInt(), 0, 2); prefs.putUChar("aposEnt", aposEntrega); entregueEm = 0; }
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
  ipNoInfo = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  vfat::definirInfo(t);
}

void iniciarWifi() {
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t i) { redeMudou = true; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t i) {
    nQuedas++; ultQueda = millis();
    motivoQueda = String(ssid) + ": " + String(i.wifi_sta_disconnected.reason) + " " + WiFi.disconnectReasonName((wifi_err_reason_t)i.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(nome.c_str());
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // v1.7 (teste): rádio mais fraco = menos pico de corrente e ruído na porta USB da máquina
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
  udp.begin(PORTA_AVISO);
  MDNS.begin(nome.c_str());
  MDNS.addService("http", "tcp", 80);
  atualizarInfo();   // o _REDE.TXT tem tamanho fixo: muda sem tirar o pen drive
  redeMudou = false;
}

void setup() {
  Serial.begin(115200);
  prefs.begin("pendrive", false);
  // v2: esquece a rede gravada no teste (ela isolava os aparelhos entre si)
  if (prefs.getUInt("cfgver", 1) < 2) { prefs.remove("ssid"); prefs.remove("senha"); prefs.putUInt("cfgver", 2); }
  nBoots = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", nBoots);
  motivoBoot = nomeReset(esp_reset_reason());
  nome = prefs.getString("nome", "plotter");
  ssid = prefs.getString("ssid", "");
  senha = prefs.getString("senha", "");
  substituir = prefs.getBool("subst", true);
  replugar = prefs.getBool("replugar", true);
  aposEntrega = prefs.getUChar("aposEnt", 0);
  led(20, 8, 0);

  if (!LittleFS.begin(true)) Serial.println("LittleFS falhou!");
  LittleFS.remove(String(vfat::PASTA) + "/.recebendo");
  {
    std::vector<String> lixo;
    File d = LittleFS.open(vfat::PASTA);
    for (File f = d.openNextFile(); f; f = d.openNextFile()) if (strncmp(f.name(), ".rx", 3) == 0) lixo.push_back(String(vfat::PASTA) + "/" + f.name());
    d.close();
    for (auto &c : lixo) LittleFS.remove(c);
  }
  medirEspaco();
  vfat::iniciar(nome);
  atualizarInfo();
  vfat::reconstruir();

  mscPresente = true;
  mscRegistro = (int)tinyusb_enable_interface(USB_INTERFACE_MSC, TUD_MSC_DESC_LEN, pd_msc_descritor);
  USB.manufacturerName("PrintHub");
  USB.productName("PenDrive Rede");
  USB.onEvent(usbEvento);
  USB.begin();

  iniciarWifi();

  server.on("/", [] { server.send_P(200, "text/html; charset=utf-8", PAGINA); });
  server.on("/api/estado", rotaEstado);
  server.on("/api/conferir", rotaConferir);
  vfat::aviso = [](uint8_t tipo, uint32_t a, uint32_t b) { anotar(millis(), a, 0, tipo); if (tipo == 17) { uint32_t m = millis(); entregueEm = m ? m : 1; } };
  server.on("/api/rastro", rotaRastro);
  // v1.22: ensaio do socorro (faz de conta que a porta parou agora)
  server.on("/api/teste-socorro", HTTP_POST, [] { uint32_t m = millis(); suspensoDesde = m ? m : 1; json(200, "{\"ok\":true}"); });
  server.on("/api/placas", rotaPlacas);
  server.on("/api/adicionar", HTTP_POST, rotaAdicionar);
  server.on("/api/adicionar-progresso", HTTP_POST, rotaAdicionarProgresso);
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
  // v1.3: Wi-Fi caiu e voltou com o MESMO endereço = não mexe no USB (tirar o pen drive no meio
  // de uma leitura travava a máquina). Só replugga se o endereço mudou (o _REDE.TXT ficou velho).
  if (redeMudou && !inserirEm) {
    redeMudou = false;
    if (WiFi.localIP().toString() != ipNoInfo) atualizarInfo();   // só reescreve o _REDE.TXT, sem mexer no USB
  }
  acordou = false;   // v1.10: NÃO replugga mais ao acordar — a máquina acorda a porta e lê sozinha; o replug cortava essa leitura
  if (temPend && !inserirEm && !recebendoAtivo && millis() - pendDesde > 1200 &&
      (nLeituras == 0 || millis() - ultLeitura > 2500) && (nEscritas == 0 || millis() - ultEscrita > 2500))
    aplicarPendencias();
  // v1.22: socorro. A máquina às vezes para de falar com a porta e fica esperando. Um pen drive comum some e volta
  // sozinho nessa hora; a placa faz igual: 0,1 s de porta parada = sai 0,25 s e volta, sem refazer a lista.
  // Se não adiantar, tenta de novo em 1,5 s (saindo 1 s), depois 30 s, depois 5 min (máquina desligada: não fica piscando).
  // Depois de voltar, dá 3,5 s pra máquina reconhecer antes de contar a porta como parada de novo.
  if (!suspensoDesde && !inserirEm && socorrosSeguidos && enumEm && (int32_t)(enumEm - ultSocorro) > 0 && millis() - enumEm > 8000) socorrosSeguidos = 0;   // a máquina reconheceu e ficou: zera a contagem
  if (suspensoDesde && !inserirEm && !temPend && !recebendoAtivo) {
    if (nDados != dadosNoSocorro) socorrosSeguidos = 0;
    uint32_t espera = socorrosSeguidos == 0 ? 100 : socorrosSeguidos == 1 ? 1500 : socorrosSeguidos == 2 ? 30000 : 300000;
    uint32_t desde = suspensoDesde;
    if (voltouEm && (int32_t)(voltouEm + 3500 - desde) > 0) desde = voltouEm + 3500;
    if ((int32_t)(millis() - desde) > (int32_t)espera) {
      uint32_t fora = socorrosSeguidos == 0 ? 250 : 1000;
      anotar(millis(), fora, 0, 11);
      nSocorros++; ultSocorro = millis(); socorrosSeguidos++; dadosNoSocorro = nDados;
      usbEjetar();
      voltaRapida = true;
      usbInserirDaquiA(fora);
    }
  }
  // v1.20 (teste): 1,5 s depois de a máquina receber um arquivo inteiro (e sem leitura nova), a placa sai e volta ou reinicia.
  if (entregueEm && millis() - entregueEm > 1500 && millis() - ultLeitura > 1000) {
    entregueEm = 0;
    if (aposEntrega && !inserirEm && !recebendoAtivo) {
      anotar(millis(), aposEntrega, 0, 20);
      if (aposEntrega == 2) { delay(150); ESP.restart(); }
      usbEjetar();
      usbInserirDaquiA(2500);
    }
  }
  // v1.19: com a porta parada, anota a cada 5 s (no 1º minuto) o que o chip USB está vendo. Só leitura.
  static uint32_t monEm = 0;
  if (suspensoDesde && !inserirEm && millis() - suspensoDesde < 60000 && millis() - monEm > 5000) {
    monEm = millis();
    anotar(millis(), (uint32_t)USB_DWC.dsts_reg.val, 0, 19);
  }
  if (inserirEm && (int32_t)(millis() - inserirEm) >= 0) usbInserirAgora();
  avisoReceber();
  if (millis() - ultAviso > 15000) { ultAviso = millis(); avisoEnviar(); }
  static uint32_t ultLed = 0;
  if (millis() - ultLed > 2000 && !inserirEm) { ultLed = millis(); ledEstado(); }
  delay(2);
}
