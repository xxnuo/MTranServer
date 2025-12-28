# Guide de configuration avancée de MTranServer

[中文](../API.md) | [English](API_en.md) | [日本語](API_ja.md) | [Français](API_fr.md) | [Deutsch](API_de.md)

### Configuration des variables d'environnement

| Variable d'environnement | Description | Valeur par défaut | Valeurs possibles |
| ------------------------ | ----------- | ----------------- | ----------------- |
| MT_LOG_LEVEL | Niveau de journalisation | warn | debug, info, warn, error |
| MT_CONFIG_DIR | Répertoire de configuration | ~/.config/mtran/server | Tout chemin |
| MT_MODEL_DIR | Répertoire des modèles | ~/.config/mtran/models | Tout chemin |
| MT_HOST | Adresse d'écoute du serveur | 0.0.0.0 | Toute adresse IP |
| MT_PORT | Port du serveur | 8989 | 1-65535 |
| MT_ENABLE_UI | Activer l'interface Web | true | true, false |
| MT_OFFLINE | Mode hors ligne, ne pas télécharger automatiquement les nouveaux modèles de langue, utiliser uniquement les modèles téléchargés | false | true, false |
| MT_WORKER_IDLE_TIMEOUT | Délai d'inactivité du Worker (secondes) | 300 | Tout entier positif |
| MT_API_TOKEN | Jeton d'accès API | Vide | Toute chaîne de caractères |

Exemple :

```bash
# Définir le niveau de journalisation sur debug
export MT_LOG_LEVEL=debug

# Définir le port sur 9000
export MT_PORT=9000

# Démarrer le service
./mtranserver
```

### Description de l'interface API

#### Interfaces système

| Interface | Méthode | Description | Authentification |
| --------- | ------- | ----------- | ---------------- |
| `/version` | GET | Obtenir la version du service | Non |
| `/health` | GET | Vérification de l'état | Non |
| `/__heartbeat__` | GET | Vérification du rythme cardiaque | Non |
| `/__lbheartbeat__` | GET | Vérification du rythme cardiaque de l'équilibreur de charge | Non |
| `/docs/*` | GET | Documentation API Swagger | Non |

#### Interfaces de traduction

| Interface | Méthode | Description | Authentification |
| --------- | ------- | ----------- | ---------------- |
| `/languages` | GET | Obtenir la liste des langues supportées | Oui |
| `/translate` | POST | Traduction de texte unique | Oui |
| `/translate/batch` | POST | Traduction par lots | Oui |

**Exemple de requête de traduction de texte unique :**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "text": "Hello, world!",
  "html": false
}
```

**Exemple de requête de traduction par lots :**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "texts": ["Hello, world!", "Good morning!"],
  "html": false
}
```

**Méthodes d'authentification :**

- En-tête : `Authorization: Bearer <token>`
- Requête : `?token=<token>`


Pour plus de détails, veuillez vous référer à la documentation API après le démarrage du serveur.
