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
| MT_OFFLINE | Utiliser hors ligne les fichiers de traduction déjà préparés | false | true, false |
| MT_FAST | Mode rapide ; plus rapide mais précision de traduction potentiellement réduite | false | true, false |
| MT_LEXICAL_SHORTLIST | Alias compatible de MT_FAST | false | true, false |
| MT_WORKER_IDLE_TIMEOUT | Secondes avant de libérer un moteur de traduction inactif | 60 | 1, 2, 3, ... |
| MT_API_TOKEN | Jeton d'accès API | Vide | Toute chaîne de caractères |
| MT_CACHE_SIZE | Résultats récents à conserver ; 0 désactive le cache | 1000 | 0, 1, 2, ... |
| MT_MAX_SENTENCE_LENGTH | Limite de découpage des textes longs | 512 | 8, 9, 10, ... |
| MT_FULLWIDTH_ZH_PUNCTUATION | Virgules pleine chasse dans les traductions chinoises | true | true, false |
| MT_LOG_DIR | Dossier des journaux | ~/.config/mtran/logs | Tout chemin |
| MT_LOG_TO_FILE | Écrire les journaux dans des fichiers | false | true, false |
| MT_LOG_CONSOLE | Afficher les journaux dans la console | true | true, false |
| MT_LOG_REQUESTS | Journaliser les requêtes | false | true, false |
| MT_CHECK_UPDATE | Vérifier les mises à jour au démarrage | true | true, false |

Un dossier local `models` existant est utilisé par défaut. Préparez les sens de traduction nécessaires en ligne avant de vous déconnecter et conservez les dossiers de configuration et de modèles.

Exemple :

```bash
# Définir le niveau de journalisation sur debug
export MT_LOG_LEVEL=debug

# Définir le port sur 9000
export MT_PORT=9000

# Démarrer le service
mtranserver
```

### Mode rapide

Par défaut, `fast: false` utilise le moteur K9 d’origine. Activez le mode avec `mtranserver --fast`, `MT_FAST=true`, `"fast": true` dans `~/.config/mtran/server.json`, ou la configuration de bibliothèque `new MTran({ fast: true })`. Utilisez `--no-fast` ou `false` pour le désactiver.

Ce mode réutilise lexical shortlist et peut accélérer la traduction, mais aussi en réduire la précision. Sans fichier lex, le modèle revient au moteur K9 d’origine. Un changement de mode prépare à nouveau le modèle si nécessaire ; la première préparation peut nécessiter une conversion.

Les anciens noms restent compatibles : `--lexical-shortlist` / `--no-lexical-shortlist`, `MT_LEXICAL_SHORTLIST` et le champ de bibliothèque/fichier `lexicalShortlist`. Priorité : CLI > environnement > fichier > valeur par défaut. À niveau égal, `fast` prime sur l’ancien alias. La configuration de bibliothèque et `setConfig` donnent aussi priorité à un `fast` explicite ; les deux champs reflètent le même mode demandé.

`GET /ui/api/settings` expose `config.server.fast` et `config.server.cacheSize`. `fast` indique le mode demandé et reste donc `true` en cas de repli sans lex. Pour le modifier, envoyez `{"server":{"fast":true}}` à `POST /ui/api/settings/apply`, sans ajouter de commande à l’interface.


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
| `/detect` | POST | Détecter la langue du texte | Oui |
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
