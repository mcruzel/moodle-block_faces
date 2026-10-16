# Le Cervin et son reflet dans le Riffelsee — image 100 % vectorielle

Ce dossier est indépendant du plugin Moodle : il contient une image vectorielle (SVG) de paysage, voulue
la plus réaliste possible, ainsi que tout le code qui l'a produite.

| Fichier | Contenu |
|---|---|
| `cervin-riffelsee.svg` | version haute fidélité : 1 399 456 triangles, 25,3 Mo |
| `cervin-riffelsee-leger.svg` | version allégée : 659 587 triangles, 13,5 Mo, quasi identique à taille d'écran |
| `src/` | générateur de relief, moteur de rendu, vectoriseur, scripts de contrôle |

Format : 2400 × 1600 (rapport 3:2). Fidélité au rendu source, mesurée dans Chromium (PSNR) :
34,1 dB à l'échelle 1:1 et 36,8 dB à mi-échelle pour la version haute fidélité ; 32,8 et 36,4 dB pour
la version allégée.

Les SVG ne contiennent **aucun élément matriciel** : ni `<image>`, ni URI `data:`, ni filtre (`<filter>`,
`fe*`), ni motif. Uniquement des chemins (`<path>`, triangles), des dégradés linéaires, un rectangle de
fond et deux `<use>`. Vérification : `python3 src/check_vector.py cervin-riffelsee.svg`.

## Scène

Le Cervin (4478 m) vu depuis la rive est du Riffelsee (2757 m), au-dessus de Zermatt, un matin de
mi-octobre vers 11 h : soleil à 26° de hauteur, arrivant de derrière l'observateur, à gauche, d'où
une face est éclairée de biais et une face nord dans l'ombre. Neige d'automne au-dessus de 2900 m,
pelouses sèches et myrtilliers rougis, cumulus de beau temps, voile de cirrus.

## Chaîne de production

1. **Relief réel.** Modèle numérique de terrain Copernicus GLO-30 (maille 30 m) rééchantillonné en
   une grille locale de 5 m orientée selon l'axe de visée (`dem2field.py`). Le lac est sculpté à
   l'emplacement de la zone plate du MNT (`riffel.py`). Les sommets très raides étant écrêtés par le
   radar et la maille (Cervin à 4330 m au lieu de 4478 m), les 450 m supérieurs sont étirés
   verticalement, de façon non linéaire, en conservant les courbes de niveau réelles.
2. **Amplification** (`terrain.cpp`, étape `amp`). Ajout des échelles absentes du MNT : couloirs en V
   alignés sur la ligne de plus grande pente, vires (strates) irrégulières, rugosité rocheuse.
3. **Rendu physique** (`render.cpp`, `atmo.h`, `clouds.h`). Lancer de rayons sur carte de hauteurs
   accéléré par « max-mipmap » ; atmosphère de Hillaire (2020) : diffusions de Rayleigh et de Mie,
   ozone, diffusion multiple ; nuages volumétriques (bruit Perlin-Worley, diffusion multiple
   approchée) et leurs ombres portées ; eau avec réflexion et réfraction de Fresnel, absorption et
   rétrodiffusion ; blocs rocheux polyédriques, pelouse, lichens, occlusion ambiante ;
   anticrénelage adaptatif (4 à 13 échantillons par pixel) ; courbe de tonalité ACES.
4. **Vectorisation** (`vectorize.py`). Triangulation de Delaunay adaptative raffinée par insertion
   au pixel d'erreur maximale, avec une tolérance perceptive qui tient compte du masquage par la
   texture. Chaque triangle est rempli d'une couleur unie ou d'un dégradé linéaire ajusté par
   moindres carrés, avec des couleurs intégrées sur l'aire réelle (2×2 sous-échantillons par
   pixel). Le maillage est défini une fois et dessiné en deux passes (`<use>`) : la première, avec
   un trait de la couleur de chaque triangle, bouche les fentes d'anticrénelage entre triangles
   adjacents.

## Limites (honnêtement)

- La géométrie du relief est réelle, mais à 30 m de maille : les détails plus fins (couloirs,
  rochers, enneigement) sont synthétiques et plausibles, pas cartographiquement exacts.
- Une image vectorielle « photoréaliste » est forcément une approximation par facettes : à très
  fort grossissement, on finit par voir des triangles, comme on verrait des pixels sur une photo.
- Chrome assombrit très légèrement (≈ 1/255) les zones de triangles minuscules : c'est un effet
  d'arrondi du moteur d'affichage, pas du fichier (rastérisation exacte : biais nul).

## Reproduire

`terrain.cpp` contient aussi les étapes d'une première approche entièrement procédurale (soulèvement
et érosion fluviale, glaciers, érosion par gouttes), abandonnée au profit du relief réel : seule
l'étape `amp` sert à cette image.

```
cd src
python3 fetch_dem.py                                 # télécharge GLO-30 N45E007 + N46E007 -> mosaic.npy
python3 dem2field.py mosaic.npy heightR.bin 5.0
python3 riffel.py heightR.bin heightR2.bin
g++ -O3 -march=native -fopenmp -o terrain terrain.cpp && ./terrain amp heightR2.bin heightR3.bin 1.0
g++ -O3 -march=native -fopenmp -o render render.cpp
./render height=heightR3.bin lakemask=lakemask.bin w=2400 h=1600 spp=2 extra=3 aathr=0.05 \
  cx=15 cy=1.9 cz=84 pitch=3.5 hfov=50 sunaz=-118 sunel=26 waveamp=0.0004 \
  clouds=1 cres=0.02 cden=0.12 cero=0.65 cbright=1.6 hazebg=4 out=final
python3 vectorize.py final --out cervin-riffelsee.svg --maxpts 700000 --tau0 1.0 --kappa 0.15 --taumax 6 --stroke 1.0 --gradmin 20 --flips 0 --iters 22
```

## Attribution des données

Relief produit à partir de Copernicus WorldDEM-30 © DLR e.V. 2010-2014 et © Airbus Defence and
Space GmbH 2014-2018, fourni dans le cadre du programme COPERNICUS par l'Union européenne et l'ESA ;
tous droits réservés.
