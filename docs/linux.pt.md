# Kingdom Recomp no Linux

Guia para **compilar e executar** o Kingdom Recomp (baseado no [TotK Recomp](https://github.com/valentderah/totk-pc-recomp)) nativamente no Linux (x86-64, Vulkan, SDL3), sem Wine nem camadas de compatibilidade Windows.

Para a arquitetura do projeto, veja [architecture.md](architecture.md). O fluxo geral de extração e versões do jogo segue [building.md](building.md); aqui entram apenas os passos e detalhes específicos do Linux.

> [!IMPORTANT]
> **Nenhum arquivo do jogo vem com o repositório.** É necessária uma cópia legal do *Tears of the Kingdom* (base + atualização 1.2.1 ou 1.4.3), extraída do **seu** console, além do arquivo de chaves (`prod.keys`) do **seu** Switch. Não fornecemos instruções para obter ROMs ou chaves de terceiros.

## Requisitos

| Item | Detalhe |
|------|---------|
| SO | Linux recente (testado em Fedora; outras distros com os pacotes equivalentes) |
| CPU | x86-64 com **AVX2** |
| GPU | Vulkan **1.3** e extensão **`VK_EXT_external_memory_host`** |
| Espaço | Cerca de **60 GB** na pasta de trabalho (`build/`) durante extração e compilação |
| Ferramentas | **clang**, **lld**, **ninja**, **git**, **pkg-config** |
| Bibliotecas de dev | SDL3, Vulkan, libvpx, opus, libzstd (via `-devel` / `-dev`) |
| Python | **3.12** (`.venv`) para lifter e build; **3.14** (`.venv-py314`) para extração (`compression.zstd`) |
| .NET | **SDK 10** para publicar o tradutor de shaders (`totk_xlate`) em `linux-x64` |

Opcional: [uv](https://docs.astral.sh/uv/) para criar os ambientes Python (como no [building.md](building.md)).

## 1. Clonar o repositório

```bash
git clone https://github.com/aismor/kingdom-recomp.git
cd kingdom-recomp
```

Use o fork ou branch que contém o suporte Linux.

## 2. Pacotes (exemplo Fedora)

```bash
sudo dnf install clang lld ninja-build git python3 python3.14 \
  SDL3-devel vulkan-devel libicu-devel zlib-devel \
  libvpx-devel opus-devel libzstd-devel \
  dotnet-sdk-10.0
```

Vulkan Memory Allocator: o build espera o header `vk_mem_alloc.h` no include path (por exemplo em `/tmp/vma` ou pacote `vulkan-memory-allocator-devel`, se existir na sua distro).

Em **Debian/Ubuntu**, instale os equivalentes (`libsdl3-dev`, `libvulkan-dev`, `libvpx-dev`, `libopus-dev`, `libzstd-dev`, etc.).

## 3. Ambientes Python

Na raiz do repositório:

```bash
uv venv --python 3.12 .venv
uv pip install --python .venv/bin/python -r tools/requirements.txt

uv venv --python 3.14 .venv-py314
uv pip install --python .venv-py314/bin/python -r tools/requirements-py314.txt
```

Sem `uv`:

```bash
python3.12 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt
python3.14 -m venv .venv-py314 && .venv-py314/bin/pip install -r tools/requirements-py314.txt
```

## 4. Configurar caminhos do jogo

Copie o exemplo e edite com **caminhos absolutos** no seu disco:

```bash
cp config.example.toml config.local.toml
```

Exemplo (ajuste para o seu caso):

```toml
[input]
base = '/caminho/para/base.nsp'
updates = ['/caminho/para/update-1.2.1.nsp']
keys = '/caminho/para/prod.keys'

[work]
dir = 'build'
```

- `config.local.toml` **não entra no git**; não compartilhe chaves nem ROMs.
- Versões suportadas: **v121** (1.2.1) e **v143** (1.4.3). O padrão dos scripts é `v121` se omitir `--version`.
- Se você tiver um **XCI** único e a extração falhar, separe base e update em dois NSP (como exige o pipeline de extração) antes de apontar `base` e `updates`.

## 5. hactool (extração)

O script `tools/extract.py` chama `toolchain/hactool/hactool` (sem extensão `.exe` no Linux).

`tools/build_hactool.py` **só funciona no Windows** (MSYS2). No Linux, compile manualmente:

```bash
COMMIT=1d64a83450e025622f3468c28fc4164dad2c5ef6
SRC=toolchain/src/hactool
OUT=toolchain/hactool
mkdir -p "$SRC" "$OUT"
git -C "$SRC" init -q 2>/dev/null || true
git -C "$SRC" remote add origin https://github.com/SciresM/hactool.git 2>/dev/null || true
git -C "$SRC" fetch -q --depth 1 origin "$COMMIT"
git -C "$SRC" checkout -q FETCH_HEAD
git -C "$SRC" apply --whitespace=nowarn tools/hactool/portable.patch
make -C "$SRC" CC=gcc EXEEXT= LDFLAGS='-lmbedtls -lmbedx509 -lmbedcrypto'
install -m755 "$SRC/hactool" "$OUT/hactool"
cp "$SRC/LICENSE" "$OUT/"
```

Passe `-k` com o caminho do seu `prod.keys` via `config.local.toml`; os valores das chaves não devem ir para logs ou commits.

## 6. Extrair e verificar o jogo

```bash
.venv-py314/bin/python tools/extract.py --hactool toolchain/hactool/hactool
```

Isso gera `build/extracted/v0` e `build/extracted/<versão>` conforme os updates configurados.

## 7. Tradutor de shaders (uma vez)

```bash
.venv/bin/python tools/build_xlate.py
```

Saída: `build/xlate/totk_xlate` (no Linux o runtime ainda procura o nome `totk_xlate.exe` ao lado do build).

Requer .NET SDK 10 publicando para **linux-x64**.

## 8. Gerar C, patches RomFS e linkar

Substitua `<ver>` por `v121` ou `v143`:

```bash
.venv/bin/python recomp/gen.py \
  --exefs build/extracted/<ver>/exefs \
  --out generated/<ver> \
  --patches recomp/patches-<ver>.txt

.venv-py314/bin/python tools/make_romfs_patches.py --version <ver>

.venv/bin/python tools/build_game.py --version <ver>
```

Executável: **`build/<ver>/totk`** (não `totk.exe`).

Teste rápido sem dados do jogo (só layout/runtime):

```bash
build/<ver>/totk --selftest
```

## 9. Preparar shaders (recomendado, uma vez por pasta de build)

Reduz stutter na primeira sessão. Precisa de `refs/MacNativeGames` (`sh tools/fetch_refs.sh`) ou de `tools/data/shader_list.tsv`:

```bash
.venv-py314/bin/python tools/prepare_shaders.py build/<ver> --version <ver>
```

No Linux o passo final chama `build/<ver>/totk --prepare-shaders`. Em alguns drivers NVIDIA a pré-compilação em lote pode falhar no fim do processo; o cache de tradução ainda costuma ser útil.

## 10. Executar o jogo

Sempre a partir da pasta do executável (RomFS index, patches e tradutor são resolvidos em relação a ela):

```bash
cd build/<ver>
TOTK_ASYNC_PREEMPT=0 ./totk
```

| Variável / arquivo | Uso |
|--------------------|-----|
| `TOTK_ASYNC_PREEMPT=0` | **Obrigatório no Linux** por enquanto (preempção assíncrona dos núcleos virtuais ainda não implementada). |
| `build/<ver>/totk.ini` | Configurações gravadas pelo menu (F1) e por opções; modelo em [totk.ini.example](../totk.ini.example). |
| `build/profile/` | Saves (v121); outras versões usam `build/profile-<ver>/`. |
| `build/<ver>/logs/totk.log` | Log do host. |
| Controles | [controls.md](controls.md); teclado, mouse e primeiro gamepad SDL3. |

Overlay de FPS do port: `TOTK_PERF_OVERLAY=fps` ou `full`, ou **F10** in-game.

Com [MangoHud](https://github.com/flightlessmango/MangoHud) (se instalado):

```bash
cd build/<ver>
TOTK_ASYNC_PREEMPT=0 mangohud ./totk
```

Mantenha a janela em foco quando possível; perda de foco afeta captura do mouse.

## Limitações conhecidas no Linux

- Teclado virtual do Switch (`nn::swkbd`): **stub** — digitar nomes in-game pode não funcionar.
- **Write-watch** de memória guest em software (correto, porém pode impactar desempenho).
- Alguns probes de teste (`--probe-services` relacionados ao swkbd) podem falhar; isso não impede jogar.
- Driver NVIDIA: raro crash em `--prepare-shaders` em um shader específico; reporte com log se ocorrer in-game.

## Onde fica o quê

```
kingdom-recomp/
  config.local.toml          # seus caminhos (gitignore)
  build/
    extracted/<ver>/         # exefs, romfs extraídos
    generated/<ver>/         # C gerado (gitignore)
    v121/totk                # executável
    v121/totk.ini            # configurações
    profile/save/...         # save commitado
    xlate/                   # tradutor .NET
  generated/<ver>/           # saída do gen.py
```

## Problemas comuns

| Sintoma | O que verificar |
|---------|------------------|
| `host memory import failed` | Driver Vulkan antigo ou sem `VK_EXT_external_memory_host`. |
| Janela não abre / superfície Vulkan | Wayland: extensões de superfície habilitadas no build recente; drivers atualizados. |
| Gamepad não responde | Conecte antes ou reinicie o jogo; teste `evtest` / `/dev/input/event*` no sistema; layout em `totk.ini` (`pad_layout`). |
| Config “estragou” o jogo | Apague ou restaure `totk.ini`; remova linhas experimentais (`replay_async`, `free_cores`) se existirem. |
| Save “perdeu” última sessão | Mensagem `host save recovery: deleted the last run's working copy` = queda antes de commit; save commitado anterior permanece em `build/profile/save/`. |

## Créditos (port Linux)

Implementação e documentação do host Linux: [Bruno Morais (aismor)](https://github.com/aismor) — repositório [github.com/aismor/kingdom-recomp](https://github.com/aismor/kingdom-recomp).

## Contribuir

Melhorias Linux devem manter o build **Windows** intacto. Antes de commit: `python tools/check_content.py --staged`.
