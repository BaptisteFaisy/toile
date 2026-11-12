# Harnais différentiel C ↔ Python

`tests/differential.py` (+ `tests/diff_lib.py`) exécute les **deux implémentations
du même contrat gelé** (CONTRACT.md §11) sur les mêmes fixtures et compare leurs
sorties **octet pour octet** :

- C : `build/toile-run <run|wire> <fixture> --format <fmt>`
- jumeau Python : `python3 -m toile.cli <run|wire> <fixture> --format <fmt>`
  avec `PYTHONPATH=host` et cwd = racine du dépôt.

Pour chaque fixture découverte — `fixtures/*.tcl`, `fixtures/fautes/*.tcl`,
`fixtures/invalides/*.tcl` (sous-commande `run`) et `fixtures/wire/*.hex`
(sous-commande `wire`) — et pour chaque format `text`, `json`, `md`, le harnais
compare stdout **et code de sortie**. La racine est calculée depuis `__file__`
(parent de `tests/`), jamais écrite en dur.

**Le harnais ne corrige jamais les implémentations : il constate et rapporte.**

## Lancer

```sh
make diff      # = python3 tests/differential.py            (différentiel complet)
make golden    # = python3 tests/differential.py --write-golden
python3 tests/differential.py --verbose --jobs 8 --filter fautes
```

| Option | Effet |
|---|---|
| `--write-golden` | écrit `fixtures/golden/<nom-sans-extension>.<fmt>` **depuis le binaire C** |
| `--verbose` | une ligne par comparaison (`OK`/`ÉCHEC`) ou par golden écrit |
| `--jobs N` | exécutions parallèles, `concurrent.futures` (défaut 4) |
| `--filter MOTIF` | ne garde que les fixtures dont le chemin contient `MOTIF` |
| `--c-bin CHEMIN` | autre binaire C (test du harnais avec un faux binaire) |
| `--py-cmd CMD` | autre jumeau Python, ex. `--py-cmd /tmp/faux_py.sh` |
| `--root CHEMIN` | autre racine de dépôt (défaut : parent de `tests/`) |
| `--timeout S` | timeout par invocation (défaut 120) |

Si `fixtures/golden/` contient des fichiers, le mode normal compare **aussi** les
deux implémentations au golden ; un golden absent pour une fixture est signalé en
avertissement, pas en échec.

## Codes de sortie

| Code | Sens |
|---|---|
| `0` | identique partout (C = Python, et = golden le cas échéant) |
| `1` | au moins une divergence |
| `2` | harnais inutilisable : binaire C ou jumeau Python absent, aucune fixture |

Le code `2` nomme la commande à lancer (`make build`). Tant que `build/toile-run`
n'existe pas, `python3 tests/differential.py` sort `2` : comportement nominal
avant intégration. (`3` = divergence `--expect`, c'est le CLI des implémentations,
pas le harnais.)

## Lire un échec

```
ÉCHEC fixtures/fautes/cle_inconnue.tcl [json]
  C      : build/toile-run run fixtures/fautes/cle_inconnue.tcl --format json
  Python : python3 -m toile.cli run fixtures/fautes/cle_inconnue.tcl --format json
  - codes de sortie différents : C=2 python=0
  - stdout différents : premier octet différent en position 18 (C=94 o, python=91 o)
  diff unifié C -> python :
    --- C fixtures/... [json]
    +++ python fixtures/... [json]
    @@ -1,3 +1,3 @@
```

Lire dans l'ordre : la **fixture et le format** (ligne `ÉCHEC`), les **commandes
exactes** à rejouer à la main, les **puces** (code de sortie, position du premier
octet différent, écart au golden), puis le **diff unifié** (`-` = C, `+` = Python).
Le rapport final synthétise : `rapport : X fixtures, Y comparaisons, Z échecs`.

## Règle des goldens

**Le binaire C est la référence des goldens.** `--write-golden` n'interroge que
`build/toile-run` : le Python ne contribue jamais au contenu de
`fixtures/golden/`. Une divergence C↔golden se règle donc soit en corrigeant le C
(puis `make golden`), soit en régénérant après un changement de contrat assumé —
jamais en éditant un golden à la main.
