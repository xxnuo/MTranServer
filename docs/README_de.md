# MTranServer

[中文](../README.md) | [English](README_en.md) | [日本語](README_ja.md) | [Français](README_fr.md) | [Deutsch](README_de.md)

Ein kostenloser Offline-Übersetzungsdienst für Ihren eigenen Computer, ohne Grafikkarte. Unterstützt mehrere Sprachen sowie Browser- und Editor-Erweiterungen. Geschwindigkeit und Speicherbedarf hängen vom Computer, den Sprachen und der Textlänge ab.

Geeignet für das alltägliche Lesen und das Verständnis der Kernaussage. Übersetzungen können Fehler enthalten; prüfen Sie wichtige Inhalte anhand des Originals.

> v5 verwendet eine neue Übersetzungsengine mit deutlich verbesserter Leistung. Neue Modelle für Übersetzungen zwischen Chinesisch und Englisch mit höherer Übersetzungsqualität werden derzeit trainiert. Nach dem Update müssen die Übersetzungsdateien beim ersten Einsatz einer Sprachrichtung möglicherweise erneut vorbereitet werden. Bitte warten Sie, bis dies abgeschlossen ist.

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

## Leistungstests (v5 ONNX)

MTranServer **5.1.0**, standardmäßig quantisierte ONNX-Modelle, Übersetzungscache deaktiviert (`MT_CACHE_SIZE=0`). Der Durchsatz ist der Median aus drei Durchläufen; die Richtwerte hängen von Hardware, Text und Systemlast ab.

| Richtung | Eingabe (Zeichen) | Backend / Inferenz-Threads | Gleichzeitige Anfragen | Mittelwert (ms) | P95 (ms) | Durchsatz (Anfragen/s) |
| --- | --- | --- | --- | --- | --- | --- |
| Englisch → Chinesisch | Satz 49–94 | CPU / 6 | 1 | 11.5 | 16.6 | 86.51 |
| Englisch → Chinesisch | Satz 49–94 | CPU / 6 | 4 | 35.6 | 50.8 | 108.33 |
| Englisch → Chinesisch | Absatz 234–344 | CPU / 6 | 1 | 27.7 | 35.0 | 36.31 |
| Chinesisch → Englisch | Satz 14–28 | CPU / 7 | 1 | 20.2 | 28.3 | 48.61 |
| Chinesisch → Englisch | Satz 14–28 | CPU / 7 | 4 | 68.2 | 89.1 | 56.19 |
| Chinesisch → Englisch | Absatz 65–104 | CPU / 7 | 1 | 62.5 | 85.7 | 15.52 |
| Deutsch → Englisch | Satz 50–113 | CPU / 5 | 1 | 12.6 | 17.7 | 83.02 |
| Deutsch → Englisch | Satz 50–113 | CPU / 5 | 4 | 39.7 | 51.5 | 96.51 |
| Deutsch → Englisch | Absatz 232–395 | CPU / 5 | 1 | 33.4 | 43.4 | 30.00 |

| Richtung | Erstes Laden, Backend-Auswahl und Übersetzen (s) | RSS nach Aufwärmen (MiB) | Maximale RSS (MiB) | ONNX-Modell (MiB) |
| --- | --- | --- | --- | --- |
| Englisch → Chinesisch | 33.47 | 364.3 | 391.7 | 118.8 |
| Chinesisch → Englisch | 62.99 | 410.0 | 475.7 | 125.5 |
| Deutsch → Englisch | 26.44 | 319.3 | 345.1 | 71.9 |

### Vergleich mit der veröffentlichten Version 5.0.1

| Richtung | Eingabe | Gleichzeitige Anfragen | 5.0.1 (Anfragen/s) | 5.1.0 (Anfragen/s) | Verhältnis |
| --- | --- | --- | --- | --- | --- |
| Englisch → Chinesisch | Satz | 1 | 46.92 | 83.52 | 1.78× |
| Englisch → Chinesisch | Satz | 4 | 57.25 | 93.65 | 1.64× |
| Englisch → Chinesisch | Absatz | 1 | 17.27 | 32.15 | 1.86× |
| Chinesisch → Englisch | Satz | 1 | 28.76 | 48.42 | 1.68× |
| Chinesisch → Englisch | Satz | 4 | 28.86 | 51.69 | 1.79× |
| Chinesisch → Englisch | Absatz | 1 | 7.40 | 15.58 | 2.10× |
| Deutsch → Englisch | Satz | 1 | 48.45 | 75.11 | 1.55× |
| Deutsch → Englisch | Satz | 4 | 46.20 | 93.65 | 2.03× |
| Deutsch → Englisch | Absatz | 1 | 14.78 | 30.57 | 2.07× |

## Ähnliche Projekte

Die Tabelle bewahrt frühere Vergleichsdaten und ergänzt die aktuelle v5-Messung für Englisch nach Chinesisch. Die Testumgebungen unterscheiden sich; die Angaben dienen als Referenz:

| Projektname                                                        | Speicherverbrauch | Nebenläufigkeitsleistung | Übersetzungsqualität | Geschwindigkeit | Weitere Informationen                                                                                                                                                          |
| ------------------------------------------------------------------ | ----------------- | ------------------------ | -------------------- | --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| [NLLB](https://github.com/facebookresearch/fairseq/tree/nllb)      | Sehr hoch         | Schlecht                 | Durchschnittlich     | Langsam         | Die Android-Portierung [RTranslator](https://github.com/niedev/RTranslator) hat viele Optimierungen, aber der Ressourcenverbrauch ist immer noch hoch und es ist nicht schnell |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | Sehr hoch         | Durchschnittlich         | Durchschnittlich     | Mittel          | Mittelklasse-CPU verarbeitet 3 Sätze/s, High-End-CPU 15-20 Sätze/s. [Details](https://community.libretranslate.com/t/performance-benchmark-data/486)                           |
| [OPUS-MT](https://github.com/Helsinki-NLP/Opus-MT)       | Hoch              | Durchschnittlich         | Eher schlecht        | Schnell         | [Leistungstests](https://github.com/OpenNMT/CTranslate2#benchmarks)                                                                                                            |
| Andere große Modelle                                               | Extrem hoch       | Dynamisch                | Sehr gut             | Sehr langsam    | Hohe Hardwareanforderungen. Wenn Sie eine Übersetzung mit hoher Nebenläufigkeit benötigen, wird empfohlen, das vllm-Framework zu verwenden.                                    |
| Dieses Projekt (frühere Bergamot-Engine)                                                     | Niedrig           | Hoch                     | Durchschnittlich     | Extrem schnell  | Durchschnittliche Antwortzeit von 50 ms pro Anfrage.                                                                                                                           |
| Dieses Projekt (v5 ONNX, automatische Auswahl) | 392MiB (peak, EN-ZH) | Gemessene Parallelitätsgrenze | Nicht bewertet | 86.51/s | Englisch-chinesische Sätze; CPU mit 6 Threads automatisch gewählt; Bedingungen siehe oben |

> Frühere Zeilen behalten die einfachen CPU-Tests für Englisch-Chinesisch und den Vergleich nicht quantisierter Versionen aus der ursprünglichen README bei. Die v5-Zeile verwendet die obigen Messungen mit standardmäßig quantisierten ONNX-Modellen.

> Die Angabe von 50 ms stammt aus einem früheren Test mit der Bergamot-Engine und ist kein Messwert für v5 ONNX Runtime. In der ursprünglichen README wurden Hardware, Textlänge, Stichprobengröße und Cache-Einstellungen nicht dokumentiert. Auch die qualitativen Bewertungen stammen aus dieser Version; dies ist keine aktuelle Rangliste aus einer gemeinsamen Testumgebung.

[Ursprüngliche Fassung](https://github.com/xxnuo/MTranServer/blob/1f05f8536940f825be205e3434fd7fe964c56a26/docs/README_de.md)

# Erweiterte Konfigurationsanleitung

Bitte beachten Sie die Datei [API_de.md](API_de.md) und die API-Dokumentation nach dem Start.

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for the inspiration for local translation.

[ONNX Runtime](https://github.com/microsoft/onnxruntime) for the translation runtime.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
