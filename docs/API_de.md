# MTranServer Erweiterte Konfigurationsanleitung

[中文](../API.md) | [English](API_en.md) | [日本語](API_ja.md) | [Français](API_fr.md) | [Deutsch](API_de.md)

### Umgebungsvariablenkonfiguration

| Umgebungsvariable | Beschreibung | Standardwert | Optionen |
| ----------------- | ------------ | ------------ | -------- |
| MT_LOG_LEVEL | Protokollierungsgrad | warn | debug, info, warn, error |
| MT_CONFIG_DIR | Konfigurationsverzeichnis | ~/.config/mtran/server | Beliebiger Pfad |
| MT_MODEL_DIR | Modellverzeichnis | ~/.config/mtran/models | Beliebiger Pfad |
| MT_HOST | Server-Abhöradresse | 0.0.0.0 | Beliebige IP-Adresse |
| MT_PORT | Server-Port | 8989 | 1-65535 |
| MT_ENABLE_UI | Web-UI aktivieren | true | true, false |
| MT_OFFLINE | Vorbereitete Übersetzungsdateien offline verwenden | false | true, false |
| MT_WORKER_IDLE_TIMEOUT | Sekunden bis zur Freigabe einer inaktiven Übersetzungsengine | 60 | 1, 2, 3, ... |
| MT_API_TOKEN | API-Zugriffstoken | Leer | Beliebige Zeichenfolge |
| MT_CACHE_SIZE | Zu speichernde Ergebnisse; 0 deaktiviert den Cache | 1000 | 0, 1, 2, ... |
| MT_MAX_SENTENCE_LENGTH | Grenze für die Aufteilung langer Texte | 512 | 8, 9, 10, ... |
| MT_FULLWIDTH_ZH_PUNCTUATION | Vollbreite Kommas in chinesischen Übersetzungen | true | true, false |
| MT_LOG_DIR | Protokollordner | ~/.config/mtran/logs | Beliebiger Pfad |
| MT_LOG_TO_FILE | Protokolle in Dateien speichern | false | true, false |
| MT_LOG_CONSOLE | Protokolle in der Konsole anzeigen | true | true, false |
| MT_LOG_REQUESTS | Anfragen protokollieren | false | true, false |
| MT_CHECK_UPDATE | Beim Start nach Updates suchen | true | true, false |

Ein vorhandener lokaler Ordner `models` wird standardmäßig verwendet. Bereiten Sie die benötigten Sprachrichtungen online vor und behalten Sie sowohl den Konfigurations- als auch den Modellordner.

Beispiel:

```bash
# Protokollierungsgrad auf debug setzen
export MT_LOG_LEVEL=debug

# Port auf 9000 setzen
export MT_PORT=9000

# Dienst starten
mtranserver
```

### API-Schnittstellenbeschreibung

#### Systemschnittstellen

| Schnittstelle | Methode | Beschreibung | Authentifizierung |
| ------------- | ------- | ------------ | ----------------- |
| `/version` | GET | Dienstversion abrufen | Nein |
| `/health` | GET | Gesundheitscheck | Nein |
| `/__heartbeat__` | GET | Heartbeat-Check | Nein |
| `/__lbheartbeat__` | GET | Load Balancer Heartbeat-Check | Nein |
| `/docs/*` | GET | Swagger API-Dokumentation | Nein |

#### Übersetzungsschnittstellen

| Schnittstelle | Methode | Beschreibung | Authentifizierung |
| ------------- | ------- | ------------ | ----------------- |
| `/languages` | GET | Liste der unterstützten Sprachen abrufen | Ja |
| `/detect` | POST | Textsprache erkennen | Ja |
| `/translate` | POST | Einzeltextübersetzung | Ja |
| `/translate/batch` | POST | Stapelübersetzung | Ja |

**Beispiel für Einzeltextübersetzungsanfrage:**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "text": "Hello, world!",
  "html": false
}
```

**Beispiel für Stapelübersetzungsanfrage:**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "texts": ["Hello, world!", "Good morning!"],
  "html": false
}
```

**Authentifizierungsmethoden:**

- Header: `Authorization: Bearer <token>`
- Query: `?token=<token>`


Weitere Informationen finden Sie in der API-Dokumentation nach dem Start des Servers.
