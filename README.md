# Pen Drive Wi-Fi para plotter

Uma placa ESP32-S3 que a máquina enxerga como **pen drive comum** — mas os arquivos chegam **pelo Wi-Fi**, por uma página no navegador. Feito pra plotters de recorte e máquinas que só leem arquivo pelo USB (testado numa SAGA Titanium com os `.plt` de corte por QR code).

Chega de tirar pen drive do computador e levar até a máquina.

## Como funciona

1. A placa fica ligada na porta USB da máquina (é dali que ela tira energia).
2. Você abre o endereço da placa no navegador de qualquer computador ou celular da rede e manda um ou vários arquivos.
3. Os arquivos ficam guardados na memória da placa (~10 MB) e aparecem na raiz do "pen drive".
4. A cada arquivo novo a placa "tira e coloca" o pen drive sozinha, pra máquina reler a lista.

Por dentro, o disco é um FAT16 de 32 MB **montado na hora** a partir dos arquivos guardados (`fat_virtual.h`), com nome longo e acento. O que a máquina gravar no pen drive (algumas checam o disco antes de abrir) fica só na memória e some no próximo arquivo.

## Material

- **ESP32-S3 DevKitC-1 N16R8** (16 MB flash, 8 MB PSRAM), a de **duas portas USB-C**.
- Um cabo USB-C para a porta da máquina.

> Use a porta USB **nativa** (na nossa placa, a da esquerda). Ela leva energia e dados. A outra porta (a da serial) não é usada.

## Gravar o programa

Com o [arduino-cli](https://arduino.github.io/arduino-cli/) e o core `esp32` 3.x:

```sh
cp pendrive_rede/redes.exemplo.h pendrive_rede/redes.h   # opcional: suas redes Wi-Fi
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=default,CDCOnBoot=default,PartitionScheme=custom" --output-dir build pendrive_rede
arduino-cli upload  --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=default,CDCOnBoot=default,PartitionScheme=custom" --input-dir build -p <porta> pendrive_rede
```

O `partitions.csv` deixa duas áreas de programa de 3 MB (pra atualizar pelo Wi-Fi) e ~10 MB para os arquivos.

Depois da primeira gravação, as atualizações vão **pelo Wi-Fi**: na página da placa, *Configurações → Atualizar programa* com o `.bin`, ou:

```sh
curl -F fw=@build/pendrive_rede.ino.bin http://<ip-da-placa>/atualizar
```

Se precisar gravar pelo cabo de novo: tire o cabo, segure **BOOT**, plugue, solte — ou use o botão *Modo gravação pelo cabo* na página.

## Primeiro uso

- **LED azul**: a placa não achou rede conhecida e abriu a dela, `PenDrive-plotter` (senha `12345678`). Conecte nela, abra `http://192.168.4.1` e escolha o seu Wi-Fi em *Configurações*.
- **LED verde**: conectada. O endereço aparece no arquivo oculto `_REDE.TXT` do pen drive (plugue num computador pra ver) e também em `http://plotter.local` nas redes que aceitam mDNS.

Dica: reserve um IP fixo pra placa no roteador, assim o endereço nunca muda.

## Página da placa

- Arraste ou escolha **vários arquivos de uma vez**.
- Lista do que está no pen drive, com botão pra apagar.
- *Apagar os arquivos anteriores ao enviar*: ligado, cada envio substitui o que tinha; desligado, os arquivos se acumulam.
- Nome da placa (vira `<nome>.local` e o nome do pen drive), modo de troca do disco, Wi-Fi e atualização.

## Licença

MIT — use, modifique e compartilhe à vontade.
