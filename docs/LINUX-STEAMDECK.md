# Dragon Quest IX Modern sur Linux et Steam Deck

*English below.*

Comme sur Windows, le jeu est fabriqué sur votre machine à partir de la ROM
(`.nds`) de **votre propre** cartouche (européenne ou américaine). Il faut
environ 3 Go libres et Internet la première fois.

Le script `BUILD.sh` vérifie ce qu'il manque pour compiler : un compilateur C,
`make`, les fichiers de SDL2 et PyYAML. Il télécharge ce qui manque, une seule
fois, dans `build/linux-tools`, soit environ 90 Mo sur un Steam Deck. Rien n'est
installé dans le système et aucun mot de passe n'est demandé. Il faut seulement
Python 3 et SDL2, que SteamOS et la plupart des distributions ont déjà.

## Steam Deck

### 1. Installer le jeu (mode Bureau)

1. Passez en mode Bureau : bouton **Steam** > **Alimentation** >
   **Passer au bureau**.
2. Copiez votre ROM sur le Deck, par exemple dans `Téléchargements`, avec une
   clé USB, une carte microSD ou le réseau.
3. **Avec RecompLauncher** : installez RecompLauncher pour Linux
   (https://recomplauncher.com), cherchez **Dragon Quest IX Modern**, cliquez
   sur Installer et choisissez votre ROM. La première fois, la préparation
   télécharge les outils puis compile le jeu, ce qui prend 10 à 20 minutes.
4. **Sans RecompLauncher** :
   1. Téléchargez `Dragon-Quest-IX-Modern-…-Linux-x64.zip` sur la
      [page des versions](https://github.com/romusan495/Dragon-Quest-IX-Modern/releases)
      et décompressez-le.
   2. Faites un clic droit dans le dossier, puis **Ouvrir un terminal ici**
      (Konsole).
   3. Tapez la commande suivante, en remplaçant le chemin par celui de votre ROM :
      ```
      sh BUILD.sh ~/Downloads/ma-rom.nds
      ```
   4. Une fois la compilation finie, le jeu est dans le dossier `game`.
      Lancez-le avec `game/play.sh`.

### 2. Jouer en mode Jeu

1. Toujours en mode Bureau, ouvrez **Steam** puis **Jeux** >
   **Ajouter un jeu non-Steam à ma bibliothèque**.
2. Cliquez sur **Parcourir**, prenez **Type de fichier : Tous les fichiers**
   et choisissez `play.sh` dans le dossier `game` du jeu. Ce dossier se trouve
   à l'endroit où RecompLauncher a installé le jeu, ou dans le dossier
   décompressé.
3. Revenez en mode Jeu : l'icône **Retour au mode Jeu** est sur le bureau.
   Le jeu apparaît dans la bibliothèque, onglet **Non-Steam**.
4. Dans le jeu, mettez le plein écran avec le réglage **Plein écran** de
   RecompLauncher ou la touche F11. En mode Jeu, Steam l'affiche de toute façon
   en plein écran, à 1280×800.

### Commandes sur le Deck

- La manette du Deck est reconnue comme une manette Xbox : A, B, X, Y, les
  gâchettes et les sticks marchent comme sur PC.
- L'écran tactile du Deck sert d'écran tactile de la DS, comme la souris sur PC.
- Le **menu du jeu** (réglages, trophées, quitter) s'ouvre sur PC avec la touche
  Échap ou le bouton PS/Xbox. Sur le Deck, le bouton Steam est pris par Steam.
  Associez donc une touche arrière à Échap : sur la page du jeu, choisissez
  **Paramètres de la manette** > **Modifier la disposition** >
  **Boutons arrière** > **L4** > **Clavier** > **Échap**.

## Linux (PC de bureau)

1. Installez Python 3 et SDL2 s'ils ne sont pas déjà là, puis, si vous le
   souhaitez, gcc et make. Exemples :
   - Debian / Ubuntu : `sudo apt install python3 python3-yaml libsdl2-dev gcc make`
   - Fedora : `sudo dnf install python3 python3-pyyaml SDL2-devel gcc make`
   - Arch : `sudo pacman -S python python-yaml sdl2 gcc make`

   Sans ces paquets, `BUILD.sh` télécharge lui-même ce qui manque.
2. Mettez votre ROM dans le dossier `rom`, puis lancez `sh BUILD.sh` (ou
   `sh BUILD.sh /chemin/vers/la-rom.nds`).
3. Jouez avec `game/play.sh`.

La sauvegarde (`dqix.sav`) et les réglages (`settings.ini`) sont dans `game`.
Sous Linux, il n'y a pas de barre de menus en haut de la fenêtre : tous les
réglages sont dans le menu du jeu (Échap). Les mises à jour se font par
RecompLauncher, ou en décompressant la nouvelle version puis en relançant
`BUILD.sh`. Le Wi-Fi et les sessions en ligne ne sont pas testés sous Linux.

**Ne partagez jamais les dossiers `game` et `build`** : ils contiennent le jeu.

---

# Dragon Quest IX Modern on Linux and the Steam Deck

As on Windows, the game is built on your machine from the ROM (`.nds`) of
**your own** cartridge (European or USA). You need about 3 GB free and the
Internet the first time.

`BUILD.sh` fetches what this computer lacks to build the game (a C compiler,
make, SDL2's headers, PyYAML) once into `build/linux-tools`, about 90 MB on a
Steam Deck. Nothing is installed in the system and no password is asked. It
only needs Python 3 and SDL2, which SteamOS and most distributions already have.

## Steam Deck

**Install (Desktop Mode)**: Steam button > Power > Switch to Desktop. Copy your
ROM to the Deck. Then either:
- **With RecompLauncher**: install RecompLauncher for Linux, find
  **Dragon Quest IX Modern**, Install, and choose your ROM. The first time it
  downloads the tools and builds the game, which takes 10-20 minutes.
- **Without it**: unzip `Dragon-Quest-IX-Modern-…-Linux-x64.zip` from the
  [releases page](https://github.com/romusan495/Dragon-Quest-IX-Modern/releases),
  right-click in its folder > Open Terminal Here, run
  `sh BUILD.sh ~/Downloads/your-rom.nds`, then play with `game/play.sh`.

**Play in Gaming Mode**: in Desktop Mode, Steam > Games > Add a Non-Steam Game
to My Library > Browse, set the file type to *All Files*, and pick `game/play.sh`
in the game's folder. Back in Gaming Mode, the game is in the Non-Steam tab.
Use full screen (RecompLauncher's *Full screen* setting, or F11). Gaming Mode
shows it at 1280×800 anyway.

**Controls**: the Deck's controller works as an Xbox pad, and its touchscreen
is the DS touch screen. The game menu (settings, trophies, quit) opens with Esc
or the PS/Xbox button on PC. The Steam button belongs to Steam, so map a back
button to Esc: Controller Settings > Edit Layout > Back Grip Buttons > L4 >
Keyboard > Esc.

## Linux desktop

Install Python 3 and SDL2 if missing, and optionally gcc, make, PyYAML and
SDL2's development package (examples above). Without them `BUILD.sh` fetches
what it needs. Put your ROM in `rom` and run `sh BUILD.sh` (or
`sh BUILD.sh /path/to/rom.nds`), then play with `game/play.sh`.

There is no menu bar on Linux: every setting is in the game menu (Esc).
Updates come through RecompLauncher, or by unzipping the new version and
running `BUILD.sh` again. Wi-Fi and online sessions are untested on Linux.
**Never share the `game` and `build` folders**: they hold the game.
