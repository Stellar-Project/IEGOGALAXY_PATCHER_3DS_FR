<div align="center">

# IEGO Galaxy Patcher FR

**Homebrew Nintendo 3DS — Patch de traduction française**  
*Inazuma Eleven GO Galaxy : Big Bang & Supernova*

![Version](https://img.shields.io/github/v/release/Stellar-Project/IEGOGALAXY_PATCHER_FR?style=flat-square&color=1E6FD9)
![Plateforme](https://img.shields.io/badge/plateforme-Nintendo%203DS-red?style=flat-square)
![Licence](https://img.shields.io/badge/licence-MIT-green?style=flat-square)

</div>

---

## À propos

IEGO Galaxy Patcher FR est un homebrew Nintendo 3DS qui installe automatiquement le patch de traduction française d'**Inazuma Eleven GO Galaxy** (Big Bang & Supernova) directement depuis votre console, sans PC.

Le patch utilise le système **LayeredFS de Luma3DS** pour superposer les fichiers traduits par-dessus le jeu original, sans modifier la ROM.

---

## Prérequis

| Élément | Détail |
|---|---|
| Console | Nintendo 3DS / 2DS / New 3DS / New 2DS (toutes variantes) |
| Custom Firmware | [Luma3DS](https://github.com/LumaTeam/Luma3DS) installé |
| LayeredFS | Activé dans les options Luma (`SELECT` au démarrage) |
| Wi-Fi | Connexion active lors du téléchargement |
| Espace SD | ~1.2 Go libres minimum |
| Jeu | Inazuma Eleven GO Galaxy : Big Bang ou Supernova (dump personnel) |

---

## Installation du homebrew

1. Télécharge la dernière version depuis les [Releases](../../releases/latest)
2. Copie `iego_patcher.3dsx` dans `sdmc:/3ds/iego_patcher/`
3. Lance-le via le **Homebrew Launcher**

---

## Utilisation

```
Écran d'accueil
    │
    ├─ [A] Big Bang     → Sélectionne le jeu
    └─ [B] Supernova    → Sélectionne le jeu
            │
            ▼
    Confirmation
            │
            ▼ [A] Installer
    Téléchargement  ──── [B] Annuler
            │
            ▼
    Extraction + Installation
            │
            ▼
    ✓ Patch installé !
```

Le patch est installé dans :
```
sdmc:/luma/titles/<TitleID>/romfs/
```

| Version | Title ID |
|---|---|
| Big Bang | `000400000010BA00` |
| Supernova | `000400000010BB00` |

---

## Compilation

### Prérequis

- [devkitPro](https://devkitpro.org/wiki/Getting_Started) avec le support 3DS
- Git

### Installer les dépendances

```bash
pacman -S 3ds-dev
```

### Cloner et compiler

```bash
git clone https://github.com/Stellar-Project/IEGOGALAXY_PATCHER_FR.git
cd IEGOGALAXY_PATCHER_FR

# Télécharger minizip (requis, non versionné)
curl -o lib/minizip/unzip.c  https://raw.githubusercontent.com/madler/zlib/master/contrib/minizip/unzip.c
curl -o lib/minizip/unzip.h  https://raw.githubusercontent.com/madler/zlib/master/contrib/minizip/unzip.h
curl -o lib/minizip/ioapi.h  https://raw.githubusercontent.com/madler/zlib/master/contrib/minizip/ioapi.h
curl -o lib/minizip/ints.h   https://raw.githubusercontent.com/madler/zlib/master/contrib/minizip/ints.h
curl -o lib/minizip/crypt.h  https://raw.githubusercontent.com/madler/zlib/master/contrib/minizip/crypt.h

make
```

Le fichier `iego_patcher.3dsx` est généré à la racine du projet.

### Versioning

La version est automatiquement déduite des tags Git :

| Situation | Version affichée |
|---|---|
| Tag exact `1.2.0` | `1.2.0` |
| 3 commits après le tag | `1.2.0-3-gabcdef` |
| Modifications non commitées | `1.2.0-dirty` |
| Aucun tag | `dev-gabcdef` |
| Hors dépôt Git | `dev` |

Pour créer une release :
```bash
git tag -a 1.2.0 -m "Release 1.2.0"
git push origin 1.2.0
make clean && make
```

---

## Structure du projet

```
iego_patcher/
├── source/
│   ├── main.c          — Boucle principale + GUI citro2d
│   ├── network.c       — Téléchargement HTTP (httpc natif 3DS)
│   ├── patcher.c       — Extraction ZIP + installation
│   └── gui/
│       ├── theme.c     — Couleurs Big Bang / Supernova
│       ├── stars.c     — Étoiles animées en arrière-plan
│       └── draw.c      — Primitives de rendu (barres, boutons, texte)
├── include/
│   ├── network.h
│   ├── patcher.h
│   └── gui/
│       ├── theme.h
│       ├── stars.h
│       └── draw.h
├── lib/
│   └── minizip/        — unzip + ioapi (patché pour devkitARM)
├── Makefile
├── icon.png
└── README.md
```

---

## Fonctionnalités

- ✅ Interface graphique avec thèmes **Big Bang** (bleu) et **Supernova** (violet)
- ✅ Étoiles animées en arrière-plan
- ✅ Téléchargement avec **reprise automatique** (HTTP Range requests)
- ✅ Barre de progression, débit en temps réel, compteur de reconnexions
- ✅ Annulation du téléchargement avec `[B]`
- ✅ Système de **restauration de sauvegarde** (`[X]` dans le menu)
- ✅ Vérification de l'espace disque disponible (1.2 Go requis)
- ✅ Anti-veille automatique pendant le téléchargement
- ✅ Log de debug avec horodatage sur `sdmc:/3ds/iego_patcher/debug.log`
- ✅ Versioning automatique depuis les tags Git
- ✅ Sélection de version du patch (historique des versions)
- ✅ Auto-update du patcher (détection et alerte visuelle)
- 🔜 Version CIA (installation dans le menu HOME via FBI)

---

## Crédits

| Rôle | Personne / Équipe |
|---|---|
| Patch de traduction FR | Équipe IEGO Galaxy FR |
| Homebrew patcher | Stellar Project |
| SDK homebrew 3DS | [devkitPro](https://devkitpro.org) / [libctru](https://github.com/devkitPro/libctru) |
| Rendu 2D | [citro2d](https://github.com/devkitPro/citro2d) |
| Extraction ZIP | [minizip / madler](https://github.com/madler/zlib) |
| Inspiration réseau | [3hs / hShop](https://github.com/Hettomei/3hs) |

---

## Licence

Ce projet est distribué sous licence **MIT**. Voir [LICENSE](LICENSE).

Le patch de traduction est la propriété de l'équipe IEGO Galaxy FR.  
Inazuma Eleven GO Galaxy est la propriété de **Level-5**.  
Ce projet n'est pas affilié à Level-5.