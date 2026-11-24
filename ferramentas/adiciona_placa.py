#!/usr/bin/env python3
"""Grava sozinho uma plaquinha nova (ESP32-S3) quando ela é ligada no Mac pelo cabo.

Só age se alguma plaquinha da rede estiver com o "Adicionar plaquinha" ligado (botão da página).
Fica rodando em segundo plano (LaunchAgent br.com.printhub.pendrive-placas).

Passos: acha a porta nova -> confere se é ESP32-S3 -> apaga e grava o programa (pasta build) ->
espera ela entrar no Wi-Fi -> dá o nome escolhido na página -> avisa a página que terminou.
"""
import glob, json, os, re, socket, subprocess, sys, threading, time, urllib.parse, urllib.request

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(RAIZ, "pendrive_rede", "build")
ARDUINO = os.path.expanduser("~/Library/Arduino15/packages/esp32")
CONHECIDAS = os.path.join(RAIZ, "ferramentas", "placas_conhecidas.json")
LOG = os.path.join(RAIZ, "ferramentas", "adiciona_placa.log")
PORTA_AVISO = 8737
SO_TESTE = "--teste" in sys.argv     # não grava nada: só mostra o que faria

placas = {}          # mac -> {"nome", "ip", "visto"}
trava = threading.Lock()


def log(*a):
    linha = time.strftime("%d/%m %H:%M:%S ") + " ".join(str(x) for x in a)
    print(linha, flush=True)
    try:
        with open(LOG, "a") as f:
            f.write(linha + "\n")
    except OSError:
        pass


def achar(padrao):
    r = sorted(glob.glob(padrao))
    return r[-1] if r else None


def ouvir_avisos():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    s.bind(("", PORTA_AVISO))
    while True:
        try:
            d, _ = s.recvfrom(400)
            c = d.decode("utf-8", "ignore").split("|")
            if len(c) >= 6 and c[0] == "PDREDE":
                with trava:
                    nova = c[3] not in placas
                    placas[c[3]] = {"nome": c[1], "ip": c[2], "visto": time.time()}
                if nova:
                    guardar_conhecidas()
        except OSError:
            time.sleep(1)


def guardar_conhecidas():
    with trava:
        ips = sorted({p["ip"] for p in placas.values()})
    try:
        antigas = json.load(open(CONHECIDAS))
    except (OSError, ValueError):
        antigas = []
    todas = sorted(set(antigas) | set(ips))
    try:
        json.dump(todas, open(CONHECIDAS, "w"))
    except OSError:
        pass


def ips_das_placas():
    with trava:
        vivos = [p["ip"] for p in placas.values() if time.time() - p["visto"] < 180]
    try:
        antigas = json.load(open(CONHECIDAS))
    except (OSError, ValueError):
        antigas = []
    return vivos + [i for i in antigas if i not in vivos]


def pedir(url, post=False, espera=3):
    req = urllib.request.Request(url, data=b"" if post else None, method="POST" if post else "GET")
    with urllib.request.urlopen(req, timeout=espera) as r:
        return json.loads(r.read().decode("utf-8", "ignore") or "{}")


def placa_armada():
    """Devolve (ip, nome escolhido) da plaquinha que está com o Adicionar ligado, ou None."""
    for ip in ips_das_placas():
        try:
            a = pedir(f"http://{ip}/api/placas")["adicionar"]
            if a["resta"] > 0:
                return ip, a["nome"]
        except Exception:
            continue
    return None


def progresso(ip, txt, fim=False):
    log("->", txt)
    try:
        pedir(f"http://{ip}/api/adicionar-progresso?txt=" + urllib.parse.quote(txt) + ("&fim=1" if fim else ""), post=True)
    except Exception as e:
        log("   (não consegui avisar a página:", e, ")")


def esptool():
    return achar(os.path.join(ARDUINO, "tools", "esptool_py", "*", "esptool"))


def identificar(porta):
    """Roda o esptool só para ler o chip. Devolve (é S3?, mac ou None, texto)."""
    try:
        r = subprocess.run([esptool(), "--port", porta, "--chip", "auto", "read-mac"],
                           capture_output=True, text=True, timeout=40)
    except subprocess.TimeoutExpired:
        return False, None, "sem resposta"
    txt = r.stdout + r.stderr
    mac = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", txt)
    return ("ESP32-S3" in txt and r.returncode == 0), (mac.group(1).upper() if mac else None), txt[-400:]


def gravar(porta, ip_aviso):
    boot0 = achar(os.path.join(ARDUINO, "hardware", "esp32", "*", "tools", "partitions", "boot_app0.bin"))
    partes = [("0x0", os.path.join(BUILD, "pendrive_rede.ino.bootloader.bin")),
              ("0x8000", os.path.join(BUILD, "pendrive_rede.ino.partitions.bin")),
              ("0xe000", boot0),
              ("0x10000", os.path.join(BUILD, "pendrive_rede.ino.bin"))]
    for _, arq in partes:
        if not arq or not os.path.exists(arq):
            return False, f"falta o arquivo do programa ({arq})"
    cmd = [esptool(), "--chip", "esp32s3", "--port", porta, "--baud", "921600",
           "--before", "default-reset", "--after", "hard-reset",
           "write-flash", "--erase-all", "-z", "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep"]
    for end, arq in partes:
        cmd += [end, arq]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    fim_txt, ult = "", 0
    buf = ""
    while True:
        ch = p.stdout.read(1)
        if not ch:
            break
        buf += ch
        if ch in "\r\n":
            linha = buf.strip()
            buf = ""
            if not linha:
                continue
            fim_txt = (fim_txt + "\n" + linha)[-600:]
            m = re.search(r"(\d+(?:\.\d+)?)\s*%", linha)
            if "rasing" in linha and time.time() - ult > 3:
                ult = time.time(); progresso(ip_aviso, "Apagando a memória da plaquinha...")
            elif m and "0x00010000" in fim_txt[-300:] and time.time() - ult > 3:
                ult = time.time(); progresso(ip_aviso, f"Gravando o programa: {float(m.group(1)):.0f}%")
    p.wait()
    return p.returncode == 0, fim_txt[-300:]


def esperar_na_rede(mac, segundos):
    fim = time.time() + segundos
    while time.time() < fim:
        with trava:
            p = placas.get(mac)
        if p and time.time() - p["visto"] < 30:
            return p["ip"]
        time.sleep(1)
    return None


def cuidar_da_porta(porta, ip_aviso, nome):
    progresso(ip_aviso, "Achei uma placa ligada no Mac. Conferindo se é a plaquinha certa...")
    s3, mac, txt = identificar(porta)
    if not s3:
        progresso(ip_aviso, "O que está ligado no Mac não respondeu como plaquinha ESP32-S3. Se for ela: tire o cabo, segure o botão BOOT, ligue de novo na porta da ESQUERDA e solte.")
        log(txt)
        return False
    with trava:
        ja = placas.get(mac)
    log("placa:", mac, "já conhecida" if ja else "nova")
    if SO_TESTE:
        progresso(ip_aviso, f"(teste) gravaria a placa {mac} com o nome {nome}", fim=True)
        return True
    ok, txt = gravar(porta, ip_aviso)
    if not ok:
        progresso(ip_aviso, "Não consegui gravar a plaquinha. Tire e ponha o cabo (porta da ESQUERDA) para tentar de novo. Detalhe: " + txt[-120:])
        log(txt)
        return False
    with trava:
        placas.pop(mac, None)       # o que vier agora já é do programa novo
    progresso(ip_aviso, "Programa gravado. Esperando a plaquinha entrar no Wi-Fi...")
    ip = esperar_na_rede(mac, 75)
    if not ip:
        progresso(ip_aviso, "Programa gravado, mas ela ainda não apareceu na rede. Aperte o botão RST da plaquinha (ou tire e ponha o cabo). Sigo esperando...")
        ip = esperar_na_rede(mac, 240)
    if not ip:
        progresso(ip_aviso, "Programa gravado, mas a plaquinha não entrou no Wi-Fi. Procure a rede PenDrive-plotter (senha 12345678) e configure o Wi-Fi nela.", fim=True)
        return True
    try:
        pedir(f"http://{ip}/api/config?nome=" + urllib.parse.quote(nome), post=True, espera=6)
    except Exception as e:
        log("nome:", e)      # ela reinicia ao trocar o nome; a resposta pode não chegar
    time.sleep(15)                      # ela reinicia para assumir o nome
    ip2 = esperar_na_rede(mac, 60) or ip
    progresso(ip_aviso, f'Pronto! A plaquinha "{nome}" está no ar em http://{ip2} . Pode tirar do Mac e ligar na máquina.', fim=True)
    return True


def main():
    if not esptool():
        log("esptool não encontrado em", ARDUINO); sys.exit(1)
    threading.Thread(target=ouvir_avisos, daemon=True).start()
    log("no ar. Programa a gravar:", BUILD, "(modo teste)" if SO_TESTE else "")
    vistas = {}        # porta -> quando apareceu
    resolvidas = set() # portas já tratadas (até saírem)
    ult_consulta = 0
    while True:
        portas = set(glob.glob("/dev/cu.usbmodem*"))
        for p in list(vistas):
            if p not in portas:
                vistas.pop(p); resolvidas.discard(p)
        for p in portas:
            vistas.setdefault(p, time.time())
        pendentes = [p for p in portas if p not in resolvidas and time.time() - vistas[p] < 900]
        if pendentes and time.time() - ult_consulta > 4:
            ult_consulta = time.time()
            arm = placa_armada()
            if arm:
                ip_aviso, nome = arm
                for p in pendentes:
                    time.sleep(1.5)          # dá tempo da porta assentar
                    try:
                        feito = cuidar_da_porta(p, ip_aviso, nome)
                    except Exception as e:
                        log("erro:", repr(e)); feito = False
                        progresso(ip_aviso, "Deu erro no programa do Mac ao gravar: " + str(e)[:120])
                    resolvidas.add(p)
                    if feito:
                        break
        time.sleep(2)


if __name__ == "__main__":
    main()
