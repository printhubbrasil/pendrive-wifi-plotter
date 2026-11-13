// Disco FAT16 "de mentira": a máquina vê um pen drive normal, mas cada setor é
// montado na hora a partir dos arquivos guardados no LittleFS. Só leitura.
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include <algorithm>
#include <time.h>
#include <map>

namespace vfat {

const uint32_t SETOR = 512;
const uint32_t INICIO_PART = 2048;                  // MBR no setor 0, partição alinhada
const uint32_t SETORES_VOL = 65536;                 // 32 MB virtuais
const uint32_t SETORES_DISCO = INICIO_PART + SETORES_VOL;
const uint8_t SPC = 4;                              // setores por cluster (2 KB)
const uint32_t TAM_CLUSTER = SPC * SETOR;
const uint32_t RESERVADOS = 1;
const uint32_t SETORES_FAT = 64;
const uint32_t ENTRADAS_RAIZ = 512;
const uint32_t SETORES_RAIZ = ENTRADAS_RAIZ * 32 / SETOR;
const uint32_t FAT1 = RESERVADOS;
const uint32_t FAT2 = FAT1 + SETORES_FAT;
const uint32_t RAIZ = FAT2 + SETORES_FAT;
const uint32_t DADOS = RAIZ + SETORES_RAIZ;
const uint32_t CLUSTERS = (SETORES_VOL - DADOS) / SPC;   // 16343 -> FAT16 de verdade
const char *PASTA = "/f";

struct Arquivo {
  String nome;      // nome longo, como chegou
  String caminho;   // no LittleFS
  uint32_t tamanho;
  uint16_t cluster; // primeiro cluster (0 = vazio)
  uint16_t nClusters;
  time_t quando;
};

static std::vector<Arquivo> arquivos;
static uint8_t raiz[SETORES_RAIZ * SETOR];
static char rotulo[12] = "PLOTTER    ";
static uint32_t serialVol = 0x50480001;
static SemaphoreHandle_t trava = nullptr;
static String info;   // vira o _REDE.TXT oculto: endereço e estado do Wi-Fi
static File aberto;
static int abertoIdx = -1;

// O que a máquina grava fica só na memória (PSRAM) até o próximo replug. Disco
// "só leitura" fazia máquina com Android/Linux (SAGA) reconhecer o pen drive e não
// mostrar arquivo nenhum: o fsck/mount pede escrita e desiste quando não tem.
const size_t MAX_GRAVADOS = 4096;                  // 2 MB de setores gravados
static std::map<uint32_t, uint8_t *> gravados;
static void limparGravados() {
  for (auto &g : gravados) free(g.second);
  gravados.clear();
}

static void w16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void w32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static void dataHora(time_t t, uint16_t &d, uint16_t &h) {
  struct tm tm;
  if (t < 1700000000) t = 1767225600;  // relógio sem hora: 01/01/2026
  localtime_r(&t, &tm);
  d = ((tm.tm_year + 1900 - 1980) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday;
  h = (tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2);
}

static std::vector<uint16_t> utf16(const String &s) {
  std::vector<uint16_t> u;
  const uint8_t *p = (const uint8_t *)s.c_str();
  while (*p) {
    uint32_t c = *p++;
    if (c >= 0xC0 && c < 0xE0 && *p) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
    else if (c >= 0xE0 && c < 0xF0 && p[0] && p[1]) { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
    else if (c >= 0x80) c = '_';
    u.push_back(c);
  }
  return u;
}

static bool valido83(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }

// Nome curto 8.3 (o que máquina antiga enxerga). Devolve true se o nome longo cabe nele sem perda.
static bool nomeCurto(const String &nome, char out[11], std::vector<String> &usados) {
  int ponto = nome.lastIndexOf('.');
  String base = ponto > 0 ? nome.substring(0, ponto) : nome;
  String ext = ponto > 0 ? nome.substring(ponto + 1) : "";
  bool perda = false;
  String b, e;
  for (char c : base) { char u = toupper(c); if (valido83(u)) b += u; else perda = true; if (u != c) perda = true; }
  for (char c : ext) { char u = toupper(c); if (valido83(u)) e += u; else perda = true; if (u != c) perda = true; }
  if (b.length() > 8 || e.length() > 3) perda = true;
  if (b.isEmpty()) { b = "ARQ"; perda = true; }
  e = e.substring(0, 3);
  String curto;
  if (!perda) curto = b;
  for (int n = 1; curto.isEmpty() || std::find(usados.begin(), usados.end(), curto + "." + e) != usados.end(); n++) {
    String suf = "~" + String(n);
    curto = b.substring(0, 8 - suf.length()) + suf;
    perda = true;
  }
  usados.push_back(curto + "." + e);
  memset(out, ' ', 11);
  memcpy(out, curto.c_str(), curto.length());
  memcpy(out + 8, e.c_str(), e.length());
  return !perda;
}

static uint8_t somaLfn(const char *n) {
  uint8_t s = 0;
  for (int i = 0; i < 11; i++) s = ((s & 1) << 7) + (s >> 1) + (uint8_t)n[i];
  return s;
}

static void entradaLfn(uint8_t *e, uint8_t ord, bool ultima, const std::vector<uint16_t> &u, size_t ini, uint8_t soma) {
  static const uint8_t pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
  memset(e, 0, 32);
  e[0] = ord | (ultima ? 0x40 : 0);
  e[11] = 0x0F;
  e[13] = soma;
  for (int i = 0; i < 13; i++) {
    size_t k = ini + i;
    uint16_t v = k < u.size() ? u[k] : (k == u.size() ? 0x0000 : 0xFFFF);
    w16(e + pos[i], v);
  }
}

void iniciar(const String &nomeVolume) {
  if (!trava) trava = xSemaphoreCreateMutex();
  memset(rotulo, ' ', 11);
  String r = nomeVolume; r.toUpperCase();
  for (int i = 0, j = 0; i < (int)r.length() && j < 11; i++) if (valido83(r[i])) rotulo[j++] = r[i];
  LittleFS.mkdir(PASTA);
}

// Relê a pasta e remonta a tabela. Chamar com o USB "desplugado".
void reconstruir() {
  xSemaphoreTake(trava, portMAX_DELAY);
  if (abertoIdx >= 0) { aberto.close(); abertoIdx = -1; }
  limparGravados();
  arquivos.clear();
  File d = LittleFS.open(PASTA);
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (f.isDirectory() || f.name()[0] == '.') continue;
    arquivos.push_back({String(f.name()), String(PASTA) + "/" + f.name(), (uint32_t)f.size(), 0, 0, f.getLastWrite()});
  }
  d.close();
  std::sort(arquivos.begin(), arquivos.end(), [](const Arquivo &a, const Arquivo &b) { return a.quando < b.quando; });
  if (!info.isEmpty()) arquivos.insert(arquivos.begin(), {"_REDE.TXT", "", (uint32_t)info.length(), 0, 0, time(nullptr)});

  memset(raiz, 0, sizeof(raiz));
  memcpy(raiz, rotulo, 11);
  raiz[11] = 0x08;
  uint32_t ent = 1;
  uint32_t prox = 2;
  std::vector<String> usados;
  std::vector<Arquivo> cabem;
  for (auto &a : arquivos) {
    uint32_t nc = (a.tamanho + TAM_CLUSTER - 1) / TAM_CLUSTER;
    char curto[11];
    bool semLfn = nomeCurto(a.nome, curto, usados);
    auto u = utf16(a.nome);
    uint32_t nLfn = semLfn ? 0 : (u.size() + 12) / 13;
    if (ent + nLfn + 1 > ENTRADAS_RAIZ || prox + nc > CLUSTERS + 2) break;
    uint8_t soma = somaLfn(curto);
    for (uint32_t i = nLfn; i >= 1; i--) entradaLfn(raiz + 32 * ent++, i, i == nLfn, u, (i - 1) * 13, soma);
    uint8_t *e = raiz + 32 * ent++;
    memcpy(e, curto, 11);
    e[11] = a.caminho.isEmpty() ? 0x22 : 0x20;  // arquivo comum (+ oculto no _REDE.TXT)
    uint16_t d16, h16;
    dataHora(a.quando, d16, h16);
    w16(e + 14, h16); w16(e + 16, d16); w16(e + 18, d16); w16(e + 22, h16); w16(e + 24, d16);
    a.cluster = nc ? prox : 0;
    a.nClusters = nc;
    w16(e + 26, a.cluster);
    w32(e + 28, a.tamanho);
    prox += nc;
    cabem.push_back(a);
  }
  arquivos = cabem;
  serialVol++;
  xSemaphoreGive(trava);
}

static void setorBoot(uint8_t *b) {
  b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
  memcpy(b + 3, "MSDOS5.0", 8);
  w16(b + 11, SETOR); b[13] = SPC; w16(b + 14, RESERVADOS); b[16] = 2;
  w16(b + 17, ENTRADAS_RAIZ); w16(b + 19, 0); b[21] = 0xF8; w16(b + 22, SETORES_FAT);
  w16(b + 24, 63); w16(b + 26, 255); w32(b + 28, INICIO_PART); w32(b + 32, SETORES_VOL);
  b[36] = 0x80; b[38] = 0x29; w32(b + 39, serialVol);
  memcpy(b + 43, rotulo, 11); memcpy(b + 54, "FAT16   ", 8);
  b[510] = 0x55; b[511] = 0xAA;
}

static void setorMbr(uint8_t *b) {
  w32(b + 440, 0x50524E54);
  uint8_t *p = b + 446;
  p[0] = 0x00;
  p[1] = 0xFE; p[2] = 0xFF; p[3] = 0xFF;       // CHS "use LBA"
  p[4] = 0x06;                                  // FAT16
  p[5] = 0xFE; p[6] = 0xFF; p[7] = 0xFF;
  w32(p + 8, INICIO_PART); w32(p + 12, SETORES_VOL);
  b[510] = 0x55; b[511] = 0xAA;
}

static void setorFat(uint32_t s, uint8_t *b) {
  for (uint32_t i = 0; i < SETOR / 2; i++) {
    uint32_t c = s * (SETOR / 2) + i;
    uint16_t v = 0;
    if (c == 0) v = 0xFFF8;
    else if (c == 1) v = 0xFFFF;
    else for (auto &a : arquivos)
      if (a.nClusters && c >= a.cluster && c < (uint32_t)a.cluster + a.nClusters) {
        v = (c == (uint32_t)a.cluster + a.nClusters - 1) ? 0xFFFF : c + 1;
        break;
      }
    w16(b + 2 * i, v);
  }
}

static void setorDados(uint32_t s, uint8_t *b) {
  uint32_t c = 2 + s / SPC;
  for (int i = 0; i < (int)arquivos.size(); i++) {
    auto &a = arquivos[i];
    if (!a.nClusters || c < a.cluster || c >= (uint32_t)a.cluster + a.nClusters) continue;
    uint32_t off = (c - a.cluster) * TAM_CLUSTER + (s % SPC) * SETOR;
    if (off >= a.tamanho) return;
    if (a.caminho.isEmpty()) { memcpy(b, info.c_str() + off, std::min<uint32_t>(SETOR, a.tamanho - off)); return; }
    if (abertoIdx != i) {
      if (abertoIdx >= 0) aberto.close();
      aberto = LittleFS.open(a.caminho, "r");
      abertoIdx = aberto ? i : -1;
      if (abertoIdx < 0) return;
    }
    aberto.seek(off);
    aberto.read(b, std::min<uint32_t>(SETOR, a.tamanho - off));
    return;
  }
}

void lerSetor(uint32_t lba, uint8_t *b) {
  memset(b, 0, SETOR);
  if (lba == 0) { setorMbr(b); return; }
  if (lba < INICIO_PART || lba >= SETORES_DISCO) return;
  uint32_t v = lba - INICIO_PART;
  xSemaphoreTake(trava, portMAX_DELAY);
  if (v == 0) setorBoot(b);
  else if (v >= FAT1 && v < RAIZ) setorFat((v - FAT1) % SETORES_FAT, b);
  else if (v >= RAIZ && v < DADOS) memcpy(b, raiz + (v - RAIZ) * SETOR, SETOR);
  else if (v >= DADOS) setorDados(v - DADOS, b);
  auto g = gravados.find(lba);
  if (g != gravados.end()) memcpy(b, g->second, SETOR);
  xSemaphoreGive(trava);
}

bool escreverSetor(uint32_t lba, const uint8_t *b) {
  if (lba >= SETORES_DISCO) return false;
  xSemaphoreTake(trava, portMAX_DELAY);
  auto g = gravados.find(lba);
  uint8_t *m = g != gravados.end() ? g->second : nullptr;
  if (!m && gravados.size() < MAX_GRAVADOS && (m = (uint8_t *)ps_malloc(SETOR))) gravados[lba] = m;
  if (m) memcpy(m, b, SETOR);
  xSemaphoreGive(trava);
  return m != nullptr;
}

}  // namespace vfat
