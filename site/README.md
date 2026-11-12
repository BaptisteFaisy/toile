# Site du projet Toile

Site statique du projet, publié sur GitHub Pages :
https://baptistefaisy.github.io/toile/

Contraintes (exprès) :

- **Anglais** (jury international du QNX Global Hackathon) ; le dépôt reste en français.
- **Vanille et hors ligne** : HTML/CSS/JS sans framework, sans CDN, sans police externe,
  aucune requête réseau. Tout fonctionne en ouvrant `index.html` ou via un serveur local.
- **Aucun nom de marque tierce de GPU** (règle du concours) : le standard « virtio »
  (OASIS), « QEMU » et « QNX » sont cités, rien d'autre.
- **Factuel** : chaque affirmation est vérifiable dans le dépôt ; ce qui n'est pas mesuré
  est dit « not measured », jamais enjolivé.

## Servir en local

```bash
python3 -m http.server 8000 --directory site
# puis ouvrir http://localhost:8000/
```

## Pages

| Fichier | Contenu |
|---|---|
| `index.html` | accroche, problème, promesse, trois idées, statut, démarrage rapide |
| `architecture.html` | 3 couches, chaîne de traitement, double implémentation, preuve P1/P2/P3 |
| `trials.html` | E1–E5 avec les verdicts exacts et les extraits de preuve |
| `contract.html` | contrat gelé : commandes, verdicts, formats, hash, API protégée |
| `codelab.html` | les 4 chapitres « Giving a QNX VM a display » |

## Déploiement

`.github/workflows/pages.yml` (push sur `main`, filtre `paths`, budget 5 min).
Le JS est un bonus (boutons « copy ») : chaque page fonctionne sans JavaScript.