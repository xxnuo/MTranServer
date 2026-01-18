# MTranServer

[中文](../README.md) | [English](README_en.md) | [日本語](README_ja.md) | [Français](README_fr.md) | [Deutsch](README_de.md)

Ein kostenloser Offline-Übersetzungsdienst für Ihren eigenen Computer, ohne Grafikkarte. Unterstützt mehrere Sprachen sowie Browser- und Editor-Erweiterungen. Geschwindigkeit und Speicherbedarf hängen vom Computer, den Sprachen und der Textlänge ab.

Geeignet für das alltägliche Lesen und das Verständnis der Kernaussage. Übersetzungen können Fehler enthalten; prüfen Sie wichtige Inhalte anhand des Originals.

> v5 verwendet eine neue Übersetzungsengine. Nach dem Update müssen die Übersetzungsdateien beim ersten Einsatz einer Sprachrichtung möglicherweise erneut vorbereitet werden. Bitte warten Sie, bis dies abgeschlossen ist.

<img src="../images/preview.png" width="auto" height="460">

## Online Demo

| Website                                                                      | TOKEN                     | Andere Schnittstelle                                                                    | Anbieter                             |
| ---------------------------------------------------------------------------- | ------------------------- | --------------------------------------------------------------------------------------- | ------------------------------------ |
| [ipacel.cc](https://MTranServer.ipacel.cc/ui/?token=__IpacEL_MT_API_TOKEN__) | `__IpacEL_MT_API_TOKEN__` | Immersive Translate: `https://MTranServer.ipacel.cc/imme?token=__IpacEL_MT_API_TOKEN__` | [@ApliNi](https://github.com/ApliNi) |

Vielen Dank an die Mitwirkenden der Community für die Bereitstellung von Testdiensten für Benutzer!

## Gebrauchsanweisung

Unterstützt jetzt Desktop-Ein-Klick-Start! Unterstützt Windows, Mac und Linux.

### Desktop-App

#### Manueller Download

Laden Sie die neueste Desktop-Version für Ihre Plattform von [Releases](https://github.com/xxnuo/MTranServer/releases) herunter, installieren und starten Sie direkt.

Nach dem Start der Desktop-App wird ein Tray-Menü erstellt, über das Sie den Dienst bequem verwalten können.

Das Programm enthält eine einfache Benutzeroberfläche und Online-Debug-Dokumentation.

Detaillierte Gebrauchsanweisungen finden Sie unter [Ökosystem-Projekte](#Ökosystem-Projekte)

Vorschau (in der neuesten Version aktualisiert):

![UI](../images/ui.png)

![Dokumentation](../images/swagger.png)

### Server

Für den alltäglichen Gebrauch installieren und starten Sie die Desktop-App. Auf einem Server können Sie Docker oder eine Installation über die Kommandozeile nutzen. v5 bietet derzeit keine eigenständige Server-Programmdatei an.

#### Schnellstart

Programmierer können den Server direkt über die Befehlszeile starten:

```bash
npx mtranserver@latest
```

> `npx` kann durch einen beliebigen Paketmanager ersetzt werden, z. B. `bunx`, `pnpx` usw.

> Bei der ersten Übersetzung in jeder Sprachrichtung werden die benötigten Dateien heruntergeladen und vorbereitet. Bleiben Sie online und warten Sie, bis dies abgeschlossen ist. Vorbereitete Dateien werden wiederverwendet. Testen Sie zunächst einen kurzen Text. Die Vorbereitung beim ersten Einsatz oder nach einem Update dauert länger als eine normale Übersetzung.

#### Schnellinstallation

```bash
npm i -g mtranserver@latest
```

> `npm` kann durch einen beliebigen Paketmanager ersetzt werden, z. B. `bun`, `pnpm` usw.

Starten Sie anschließend `mtranserver`.

#### Docker Compose Bereitstellung

Erstellen Sie ein leeres Verzeichnis und darin eine `compose.yml` Datei mit folgendem Inhalt:

```yml
services:
  mtranserver:
    image: xxnuo/mtranserver:latest
    container_name: mtranserver
    restart: unless-stopped
    ports:
      - "8989:8989"
    environment:
      - MT_HOST=0.0.0.0
      - MT_PORT=8989
      - MT_MODEL_DIR=/app/models
      - MT_CONFIG_DIR=/root/.config/mtran/server
      - MT_OFFLINE=false
      # - MT_API_TOKEN=ihr_geheimer_token_hier
    volumes:
      - ./models:/app/models
      - ./config:/root/.config/mtran
```

```bash
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

### Updates und Offline-Nutzung

1. Behalten Sie beim Update die vorhandenen Konfigurations- und Übersetzungsmodellordner.
2. Bleiben Sie nach dem Update online und testen Sie jede benötigte Sprachrichtung. Warten Sie, bis die Vorbereitung abgeschlossen ist.
3. Aktivieren Sie anschließend den Offline-Modus in den Einstellungen, bevor Sie die Verbindung trennen. Offline sind nur bereits vorbereitete Sprachrichtungen verfügbar.
4. Speichern Sie beim Wechsel des Computers oder einer Docker-Migration beide Ordner. Nur den Modellordner zu kopieren reicht nicht aus.

Das Docker-Beispiel oben speichert diese Ordner als `config` und `models` im aktuellen Verzeichnis. Bereiten Sie die Dateien zunächst online vor, setzen Sie dann `MT_OFFLINE` auf `true` und starten Sie den Dienst neu.

## Ökosystem-Projekte

### IDE-Plugins

#### [MTranCode](https://github.com/xxnuo/MTranCode) Kommentar-Übersetzungs-Plugin

Unterstützt VS Code, Cursor, Augment und andere VS-Code-basierte IDEs.

Suchen Sie im Plugin-Marktplatz nach **`MTranCode`**, um das Kommentar-Übersetzungs-Plugin zu installieren.

Das Plugin ruft standardmäßig den Server unter `http://localhost:8989` für Kommentar- und Code-Übersetzungen auf. Sie können dies in den Einstellungen anpassen.

Dieses Plugin ist ein Fork von [vscode-comment-translate](https://github.com/intellism/vscode-comment-translate).

### Browser-Erweiterung

#### [MTranBrowser](https://github.com/xxnuo/MTranBrowser)

TODO: In aktiver Entwicklung.

> Wenn Sie ein abgeleitetes Projekt entwickeln, können Sie gerne eine PR einreichen. Ich füge Ihr Projekt zur Ökosystem-Liste hinzu.
>
> Übrigens wurde das Projekt auf npm veröffentlicht. Sie können die einfache Bibliotheksschnittstelle direkt in anderen Programmen aufrufen, um Übersetzungsfunktionen zu implementieren. Weitere Informationen finden Sie in den TypeScript-Typdefinitionen.

## Kompatible Schnittstellen

Der Server bietet kompatible Schnittstellen für mehrere Übersetzungs-Plugins:

| Schnittstelle                   | Methode | Beschreibung                                       | Unterstützte Plugins                                                                    |
| ------------------------------- | ------- | -------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `/imme`                         | POST    | Schnittstelle für Immersive Translate Plugin       | [Immersive Translate](https://immersivetranslate.com/)                                  |
| `/kiss`                         | POST    | Schnittstelle für Kiss Translator Plugin           | [Kiss Translator](https://github.com/fishjar/kiss-translator)                           |
| `/deepl`                        | POST    | DeepL API v2 kompatible Schnittstelle              | Clients, die die DeepL API unterstützen                                                 |
| `/deeplx`                       | POST    | DeepLX kompatible Schnittstelle                    | Clients, die die DeepLX API unterstützen                                                |
| `/hcfy`                         | POST    | Selection Translator kompatible Schnittstelle      | [Selection Translator](https://github.com/Selection-Translator/crx-selection-translate) |
| `/google/language/translate/v2` | POST    | Google Translate API v2 kompatible Schnittstelle   | Clients, die die Google Translate API unterstützen                                      |
| `/google/translate_a/single`    | GET     | Google translate_a/single kompatible Schnittstelle | Clients, die Google Web Translate unterstützen                                          |

**Plugin-Konfigurationsanleitung:**

> Tragen Sie die Adressen aus der Tabelle unten in Ihre Erweiterung ein. Beginnen Sie bei der Anfragerate und der Anzahl gleichzeitiger Übersetzungen mit den Standardwerten. Senken Sie diese bei Zeitüberschreitungen oder hoher Computerlast.

| Name                                | URL                                                  | Plugin-Einstellung                                                                      |
| ----------------------------------- | ---------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Immersive Translate (Ohne Passwort) | `http://localhost:8989/imme`                         | `Benutzerdefinierte API-Einstellungen` - `API-URL`                                      |
| Immersive Translate (Mit Passwort)  | `http://localhost:8989/imme?token=your_token`        | Dasselbe wie oben, ändern Sie `your_token` am Ende der URL in Ihren `MT_API_TOKEN` Wert |
| Kiss Translator (Ohne Passwort)     | `http://localhost:8989/kiss`                         | `Schnittstelleneinstellungen` - `Custom` - `URL`                                        |
| Kiss Translator (Mit Passwort)      | `http://localhost:8989/kiss`                         | Dasselbe wie oben, füllen Sie `KEY` mit `your_token` aus                                |
| DeepL Kompatibel                    | `http://localhost:8989/deepl`                        | Verwenden Sie `DeepL-Auth-Key` oder `Bearer` Authentifizierung                          |
| DeepLX Kompatibel                   | `http://localhost:8989/deeplx`                       | Unterstützt `token` Parameter oder `Bearer` Authentifizierung                           |
| Google Kompatibel                   | `http://localhost:8989/google/language/translate/v2` | Verwenden Sie den `key` Parameter oder `Bearer` Authentifizierung                       |
| Selection Translator                | `http://localhost:8989/hcfy`                         | Unterstützt `token` Parameter oder `Bearer` Authentifizierung                           |

**Normale Benutzer können den Dienst nutzen, indem sie die Schnittstellenadresse des Plugins gemäß dem Tabelleninhalt konfigurieren.**

### Befehlszeilenparameter

```bash
mtranserver --help

  --help                              Hilfe anzeigen
  --host <ip>                         Listen-Adresse, Standard 0.0.0.0
  --port <port>                       Port, Standard 8989
  --ui / --no-ui                      Web-Oberfläche aktivieren oder deaktivieren
  --offline                           Bereits vorbereitete Übersetzungsdateien verwenden
  --worker-idle-timeout <seconds>      Inaktive Engine freigeben, Standard 60 Sekunden
  --cache-size <count>                Letzte Ergebnisse speichern, Standard 1000; 0 deaktiviert den Cache
  --download en_zh zh_en              Sprachrichtungen herunterladen und vorbereiten
  --languages [filter]                Verfügbare Sprachrichtungen anzeigen

  mtranserver --host 127.0.0.1 --port 8080
  mtranserver --download en_zh zh_en
  mtranserver --offline
```

## Ähnliche Projekte

Geschwindigkeit, Speicherbedarf und Übersetzungsqualität hängen von Gerät, Sprachen und Text ab. Alte Messwerte gelten nicht für v5, deshalb wurden die Leistungsranglisten entfernt. Sie können auch diese Projekte ansehen:

- [NLLB](https://github.com/facebookresearch/fairseq/tree/nllb)
- [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate)
- [OPUS-MT](https://github.com/Helsinki-NLP/Opus-MT)

# Erweiterte Konfigurationsanleitung

Bitte beachten Sie die Datei [API_de.md](API_de.md) und die API-Dokumentation nach dem Start.

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for the inspiration for local translation.

[ONNX Runtime](https://github.com/microsoft/onnxruntime) for the translation runtime.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
