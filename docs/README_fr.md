# MTranServer

[中文](../README.md) | [English](README_en.md) | [日本語](README_ja.md) | [Français](README_fr.md) | [Deutsch](README_de.md)

Un service de traduction hors ligne gratuit qui fonctionne sur votre ordinateur, sans carte graphique. Il prend en charge plusieurs langues et les extensions de navigateur et d’éditeur. La vitesse et la mémoire utilisée dépendent de votre ordinateur, des langues et de la longueur du texte.

Utile pour la lecture quotidienne et la compréhension du sens général. Les traductions peuvent contenir des erreurs ; vérifiez les passages importants avec le texte original.

> v5 utilise un nouveau moteur de traduction aux performances nettement améliorées. De nouveaux modèles de traduction entre le chinois et l’anglais, offrant une meilleure qualité, sont en cours d’entraînement. Après la mise à jour, la première traduction dans un sens donné peut nécessiter une nouvelle préparation des fichiers. Veuillez attendre la fin de cette étape.

<img src="../images/preview.png" width="auto" height="460">

## Démo en ligne

| Site Web                                                                     | TOKEN                     | Autre interface                                                                          | Fournisseur                          |
| ---------------------------------------------------------------------------- | ------------------------- | ---------------------------------------------------------------------------------------- | ------------------------------------ |
| [ipacel.cc](https://MTranServer.ipacel.cc/ui/?token=__IpacEL_MT_API_TOKEN__) | `__IpacEL_MT_API_TOKEN__` | Immersive Translate : `https://MTranServer.ipacel.cc/imme?token=__IpacEL_MT_API_TOKEN__` | [@ApliNi](https://github.com/ApliNi) |

Merci aux contributeurs de la communauté pour fournir des services d'essai aux utilisateurs !

## Guide d'utilisation

Supporte maintenant le lancement en un clic depuis le bureau ! Compatible avec Windows, Mac et Linux.

### Application de bureau

#### Téléchargement manuel

Téléchargez la dernière version de l'application de bureau pour votre plateforme depuis les [Releases](https://github.com/xxnuo/MTranServer/releases), installez et lancez directement.

Après le lancement de l'application de bureau, un menu dans la barre des tâches sera créé pour gérer facilement le service.

Le programme comprend une interface utilisateur simple et une documentation de débogage en ligne.

Pour les instructions d'utilisation détaillées, consultez [Écosystème](#Écosystème)

Aperçu (mis à jour dans la dernière version) :

![UI](../images/ui.png)

![Documentation](../images/swagger.png)

### Serveur

Pour un usage courant, installez et lancez l’application de bureau. Sur un serveur, utilisez Docker ou une installation en ligne de commande. v5 ne fournit actuellement pas d’exécutable serveur autonome.

#### Démarrage rapide

Les programmeurs peuvent démarrer le serveur directement via la ligne de commande :

```bash
npx mtranserver@latest
```

> `npx` peut être remplacé par le gestionnaire de paquets que vous préférez, comme `bunx`, `pnpx`, etc.

> La première traduction dans chaque sens télécharge et prépare les fichiers nécessaires. Restez connecté et attendez la fin de la préparation. Les fichiers préparés seront réutilisés. Essayez d’abord un court texte pour vérifier le fonctionnement. La préparation au premier lancement ou après une mise à jour prend plus de temps qu’une traduction habituelle.

#### Installation rapide

```bash
npm i -g mtranserver@latest
```

> `npm` peut être remplacé par le gestionnaire de paquets que vous préférez, comme `bun`, `pnpm`, etc.

Puis lancez `mtranserver`.

#### Déploiement Docker Compose

Créez un répertoire vide, puis créez un fichier `compose.yml` avec le contenu suivant :

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
      # - MT_API_TOKEN=votre_jeton_secret_ici
    volumes:
      - ./models:/app/models
      - ./config:/root/.config/mtran
```

```bash
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

### Mise à jour et utilisation hors ligne

1. Conservez les dossiers de configuration et de modèles de traduction lors de la mise à jour.
2. Après la mise à jour, restez connecté et essayez chaque sens de traduction nécessaire. Attendez la fin de la préparation.
3. Activez ensuite le mode hors ligne dans les paramètres avant de vous déconnecter. Seuls les sens de traduction déjà préparés seront disponibles.
4. Pour changer d’ordinateur ou migrer Docker, sauvegardez les deux dossiers. Copier uniquement le dossier des modèles ne suffit pas.

L’exemple Docker ci-dessus sauvegarde ces dossiers dans `config` et `models` du répertoire courant. Préparez les fichiers en ligne, puis définissez `MT_OFFLINE` sur `true` et redémarrez pour une utilisation hors ligne.

## Écosystème

### Plugins IDE

#### [MTranCode](https://github.com/xxnuo/MTranCode) Plugin de traduction de commentaires MTranCode

Prend en charge VS Code, Cursor, Augment et d'autres IDE basés sur VS Code.

Recherchez **`MTranCode`** dans la boutique d'extensions pour installer le plugin de traduction de commentaires.

Par défaut, le plugin appelle le serveur `http://localhost:8989` pour la traduction des commentaires et du code. Vous pouvez l'ajuster dans les paramètres.

Ce plugin est forké de [vscode-comment-translate](https://github.com/intellism/vscode-comment-translate).

### Extension de navigateur

#### [MTranBrowser](https://github.com/xxnuo/MTranBrowser)

TODO: En cours de développement.

> Si vous développez un projet dérivé, n'hésitez pas à soumettre une PR. J'ajouterai votre projet à la liste de l'écosystème.
>
> Au fait, le projet a été publié sur npm. Vous pouvez appeler directement l'interface de bibliothèque simple dans d'autres programmes pour implémenter la fonctionnalité de traduction. Pour plus d'informations, consultez les définitions de types TypeScript.

## Interfaces compatibles

Le serveur fournit plusieurs interfaces compatibles avec les plugins de traduction :

| Interface                       | Méthode | Description                                    | Plugins supportés                                                                       |
| ------------------------------- | ------- | ---------------------------------------------- | --------------------------------------------------------------------------------------- |
| `/imme`                         | POST    | Interface pour le plugin Immersive Translate   | [Immersive Translate](https://immersivetranslate.com/)                                  |
| `/kiss`                         | POST    | Interface pour le plugin Kiss Translator       | [Kiss Translator](https://github.com/fishjar/kiss-translator)                           |
| `/deepl`                        | POST    | Interface compatible DeepL API v2              | Clients supportant l'API DeepL                                                          |
| `/deeplx`                       | POST    | Interface compatible DeepLX                    | Clients supportant l'API DeepLX                                                         |
| `/hcfy`                         | POST    | Interface compatible Selection Translator      | [Selection Translator](https://github.com/Selection-Translator/crx-selection-translate) |
| `/google/language/translate/v2` | POST    | Interface compatible Google Translate API v2   | Clients supportant l'API Google Translate                                               |
| `/google/translate_a/single`    | GET     | Interface compatible Google translate_a/single | Clients supportant la traduction web Google                                             |

**Guide de configuration des plugins :**

> Saisissez les adresses du tableau ci-dessous dans les paramètres de votre extension. Commencez avec les valeurs par défaut pour la fréquence des requêtes et le nombre de traductions simultanées. Réduisez-les si les traductions expirent ou si votre ordinateur est surchargé.

| Nom                                   | URL                                                  | Réglage du plugin                                                                  |
| ------------------------------------- | ---------------------------------------------------- | ---------------------------------------------------------------------------------- |
| Immersive Translate sans mot de passe | `http://localhost:8989/imme`                         | `Paramètres API personnalisés` - `URL de l'API`                                    |
| Immersive Translate avec mot de passe | `http://localhost:8989/imme?token=your_token`        | Idem, changez `your_token` à la fin de l'URL par la valeur de votre `MT_API_TOKEN` |
| Kiss Translator sans mot de passe     | `http://localhost:8989/kiss`                         | `Paramètres d'interface` - `Custom` - `URL`                                        |
| Kiss Translator avec mot de passe     | `http://localhost:8989/kiss`                         | Idem, remplissez `KEY` avec `your_token`                                           |
| Compatible DeepL                      | `http://localhost:8989/deepl`                        | Utilisez l'authentification `DeepL-Auth-Key` ou `Bearer`                           |
| Compatible DeepLX                     | `http://localhost:8989/deeplx`                       | Supporte le paramètre `token` ou l'authentification `Bearer`                       |
| Compatible Google                     | `http://localhost:8989/google/language/translate/v2` | Utilisez le paramètre `key` ou l'authentification `Bearer`                         |
| Selection Translator                  | `http://localhost:8989/hcfy`                         | Supporte le paramètre `token` ou l'authentification `Bearer`                       |

**Les utilisateurs ordinaires peuvent utiliser le service en configurant l'adresse de l'interface du plugin selon le contenu du tableau.**

### Arguments de la ligne de commande

```bash
mtranserver --help

  --help                              Afficher l’aide
  --host <ip>                         Adresse d’écoute, par défaut 0.0.0.0
  --port <port>                       Port, par défaut 8989
  --ui / --no-ui                      Activer ou désactiver l’interface Web
  --offline                           Utiliser les fichiers de traduction déjà préparés
  --worker-idle-timeout <seconds>      Libérer le moteur inactif, par défaut 60 secondes
  --cache-size <count>                Conserver les résultats récents, par défaut 1000 ; 0 désactive le cache
  --download en_zh zh_en              Télécharger et préparer les sens de traduction
  --languages [filter]                Afficher les sens de traduction disponibles

  mtranserver --host 127.0.0.1 --port 8080
  mtranserver --download en_zh zh_en
  mtranserver --offline
```

## Tests de performances (v5 ONNX)

MTranServer **5.1.0**, modèles ONNX quantifiés par défaut, cache de traduction désactivé (`MT_CACHE_SIZE=0`). Le débit est la médiane de trois passages ; ces valeurs indicatives varient selon le matériel, le texte et la charge du système.

| Sens | Entrée (caractères) | Backend / threads d’inférence | Requêtes simultanées | Moyenne (ms) | P95 (ms) | Débit (requêtes/s) |
| --- | --- | --- | --- | --- | --- | --- |
| Anglais → chinois | Phrase 49–94 | CPU / 6 | 1 | 11.5 | 16.6 | 86.51 |
| Anglais → chinois | Phrase 49–94 | CPU / 6 | 4 | 35.6 | 50.8 | 108.33 |
| Anglais → chinois | Paragraphe 234–344 | CPU / 6 | 1 | 27.7 | 35.0 | 36.31 |
| Chinois → anglais | Phrase 14–28 | CPU / 7 | 1 | 20.2 | 28.3 | 48.61 |
| Chinois → anglais | Phrase 14–28 | CPU / 7 | 4 | 68.2 | 89.1 | 56.19 |
| Chinois → anglais | Paragraphe 65–104 | CPU / 7 | 1 | 62.5 | 85.7 | 15.52 |
| Allemand → anglais | Phrase 50–113 | CPU / 5 | 1 | 12.6 | 17.7 | 83.02 |
| Allemand → anglais | Phrase 50–113 | CPU / 5 | 4 | 39.7 | 51.5 | 96.51 |
| Allemand → anglais | Paragraphe 232–395 | CPU / 5 | 1 | 33.4 | 43.4 | 30.00 |

| Sens | Premier chargement, choix du backend et traduction (s) | RSS après préchauffage (MiB) | RSS maximal (MiB) | Modèle ONNX (MiB) |
| --- | --- | --- | --- | --- |
| Anglais → chinois | 33.47 | 364.3 | 391.7 | 118.8 |
| Chinois → anglais | 62.99 | 410.0 | 475.7 | 125.5 |
| Allemand → anglais | 26.44 | 319.3 | 345.1 | 71.9 |

### Comparaison avec la version publiée 5.0.1

| Sens | Entrée | Concurrence | 5.0.1 (requêtes/s) | 5.1.0 (requêtes/s) | Rapport |
| --- | --- | --- | --- | --- | --- |
| Anglais → chinois | Phrase | 1 | 46.92 | 83.52 | 1.78× |
| Anglais → chinois | Phrase | 4 | 57.25 | 93.65 | 1.64× |
| Anglais → chinois | Paragraphe | 1 | 17.27 | 32.15 | 1.86× |
| Chinois → anglais | Phrase | 1 | 28.76 | 48.42 | 1.68× |
| Chinois → anglais | Phrase | 4 | 28.86 | 51.69 | 1.79× |
| Chinois → anglais | Paragraphe | 1 | 7.40 | 15.58 | 2.10× |
| Allemand → anglais | Phrase | 1 | 48.45 | 75.11 | 1.55× |
| Allemand → anglais | Phrase | 4 | 46.20 | 93.65 | 2.03× |
| Allemand → anglais | Paragraphe | 1 | 14.78 | 30.57 | 2.07× |

## Projets similaires

Le tableau conserve les anciennes comparaisons et ajoute la mesure actuelle de v5 en anglais vers le chinois. Les environnements diffèrent ; ces chiffres servent de référence :

| Nom du projet                                                      | Utilisation mémoire | Performance concurrente | Qualité traduction | Vitesse      | Autres informations                                                                                                                                                          |
| ------------------------------------------------------------------ | ------------------- | ----------------------- | ------------------ | ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [NLLB](https://github.com/facebookresearch/fairseq/tree/nllb)      | Très élevée         | Mauvaise                | Moyenne            | Lente        | Le portage Android [RTranslator](https://github.com/niedev/RTranslator) a de nombreuses optimisations, mais l'utilisation des ressources reste élevée et ce n'est pas rapide |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | Très élevée         | Moyenne                 | Moyenne            | Moyenne      | Un CPU milieu de gamme traite 3 phrases/s, un CPU haut de gamme 15-20 phrases/s. [Détails](https://community.libretranslate.com/t/performance-benchmark-data/486)            |
| [OPUS-MT](https://github.com/Helsinki-NLP/Opus-MT)       | Élevée              | Moyenne                 | Assez mauvaise     | Rapide       | [Tests de performance](https://github.com/OpenNMT/CTranslate2#benchmarks)                                                                                                    |
| Autres grands modèles                                              | Ultra élevée        | Dynamique               | Très bonne         | Très lente   | Exigences matérielles élevées. Si vous avez besoin d'une traduction à haute concurrence, il est recommandé d'utiliser le framework vllm.                                     |
| Ce projet (ancien moteur Bergamot)                                                          | Faible              | Élevée                  | Moyenne            | Ultra rapide | Temps de réponse moyen de 50 ms par requête.                                                                                                                                 |
| Ce projet (v5 ONNX, backend automatique) | 392MiB (peak, EN-ZH) | Concurrence réglée par mesure | Non évaluée | 86.51/s | Phrases anglais-chinois ; CPU à 6 threads choisi automatiquement ; conditions ci-dessus |

> Les anciennes lignes conservent les tests informels CPU anglais-chinois et la comparaison non quantifiée du README d’origine. La ligne v5 utilise les mesures ci-dessus et les modèles ONNX quantifiés par défaut.

> Le chiffre de 50 ms est un résultat historique de l’ancien moteur Bergamot, et non une mesure de v5 ONNX Runtime. Le README d’origine ne précisait ni le matériel, ni la longueur des textes, ni le nombre d’échantillons, ni les paramètres du cache. Les appréciations qualitatives proviennent également de cette version ; il ne s’agit pas d’un classement actuel mesuré dans un environnement commun.

[Version d’origine](https://github.com/xxnuo/MTranServer/blob/1f05f8536940f825be205e3434fd7fe964c56a26/docs/README_fr.md)

# Guide de configuration avancée

Veuillez vous référer au fichier [API_fr.md](API_fr.md) et à la documentation API après le démarrage.

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for the inspiration for local translation.

[ONNX Runtime](https://github.com/microsoft/onnxruntime) for the translation runtime.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
