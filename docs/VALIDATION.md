# VALIDATION — la méthode de preuve de Toile

Règle du projet : **on ne dit « vérifié » que si une commande de ce dépôt le
prouve.** Le vocabulaire de statut est celui de [`GAP.md`](GAP.md) — vérifié /
partiellement vérifié / non vérifié. Les moyens de preuve sont au nombre de six :
P1, P2, P3 (sans cible), le différentiel C↔Python, les fixtures (saines et
fautives), et la validation contre QEMU réel (C8, avec cible). S'y ajoutent deux
gardes transverses : la garde de taille et le budget CI.

---

## 1. P1 — cohérence du modèle (invariants I1..I6)

Après **chaque** commande appliquée ou refusée, `toile_model_check_invariants`
relit l'état complet (`CONTRACT.md` §5) :

| # | Invariant |
|---|---|
| I1 | tout scanout actif référence une ressource existante |
| I2 | `resource_flush` ne réussit que sur une ressource attachée à un scanout |
| I3 | `transfer_to_host_2d` exige un backing attaché |
| I4 | aucune ressource active avec `width==0`, `height==0` ou format inconnu |
| I5 | les tables ne débordent jamais leurs limites (64 ressources, 16 scanouts, 32 pages) |
| I6 | `bytes_transferred` = somme des aires × bpp des transferts réussis |

Sortie attendue : **0** (modèle cohérent). Sinon `TOILE_MODEL_COUNTEREXAMPLE`
avec la cause `invariant_i1` … `invariant_i6`. En fin de séquence propre, le
processeur émet `TOILE_MODEL_CONSISTENT` — c'est ce verdict qui atteste P1 dans
un rapport `text`/`json`/`md`.

## 2. P2 — rejet sans application partielle

Toute commande invalide doit être refusée **entièrement** : soit l'état change
complètement, soit il ne change pas du tout (`CONTRACT.md` §1). Moyen de preuve :
pour chaque fixture fautive, hachage de l'état **avant** la commande fautive et
**après**, plus le détail des tables ; seuls `cmds_rejected` (et le verdict
émis) peuvent différer. Les causes figées vérifiées sont celles de
`CONTRACT.md` §6 : `unknown_command_type`, `bad_format`, `bad_geometry`,
`resource_table_full`, `scanout_table_full`, `backing_entry_count`,
`resource_referenced_by_scanout`, `cursor_not_visible`,
`scanout_id_out_of_range`, `invariant_i1`…`invariant_i6`.

## 3. P3 — déterminisme (hachage FNV-1a 64 bits)

Même séquence ⇒ même état final, mêmes verdicts, **même hachage**. Le hachage
est FNV-1a 64 bits (base `0xcbf29ce484222325`, premier `0x100000001b3`) sur la
sérialisation canonique ASCII de `CONTRACT.md` §8 (`toile-state-v1`, lignes
`R`/`P`/`S`/`C`/`K`, seules les entrées actives, ids croissants, affiché en 16
hexadécimaux minuscules). Preuve : deux exécutions successives du même binaire
sur la même fixture, et l'exécution du jumeau Python, donnent le même `%016llx`.
Aucune horloge n'entre dans l'état — c'est la raison de l'interdiction `time`/
`clock` dans `core/` (D1).

## 4. Différentiel C↔Python, octet pour octet

`tests/differential.py` (cible `make diff`) exécute, pour chaque fixture :

1. `build/toile-run run <fixture> --format text|json|md` ;
2. `python3 -m toile.cli run <fixture> --format text|json|md` ;
3. comparaison **octet pour octet** des trois sorties, du hachage d'état et du
   code de sortie (0/1/2/3, `CONTRACT.md` §11).

Une divergence est un bug, jamais un écart toléré : soit le C, soit le Python,
soit le contrat est faux. Le différentiel est la seule preuve que les deux
implémentations respectent le même texte — d'où l'exigence que les trois formats
soient figés (`CONTRACT.md` §7) et le hachage canonique (§8). Même harnais pour
les fixtures « fil » : `toile-run wire fixtures/wire/<nom>.hex` des deux côtés,
ce qui éprouve `toile_vg_encode` / `toile_vg_decode` en plus du processeur.

## 5. Fixtures de référence et fixtures fautives

- **Référence** (P1, P3) : boot graphique (`fixtures/boot-graphique.tcl` —
  création, backing, scanout 0, flush), resize (désactivation du scanout puis
  nouvelle ressource, ordre imposé par D11), multi-ressource, multi-scanout.
- **Fautives** (P2) : une faute **par** fixture — `resource_unref` d'une
  ressource affichée, `resource_flush` sans scanout, `transfer_to_host_2d` sans
  backing, `set_scanout` sur ressource inexistante, `scanout=16`, format
  inconnu, géométrie nulle, plus de 32 entrées de backing, `move_cursor` sans
  curseur visible. Attendu : le verdict exact, la cause figée exacte, l'état
  inchangé (P2), et — si la faute viole un invariant — l'arrêt immédiat de la
  séquence (fail fast, D16).
- **Goldens** : `make golden` régénère `fixtures/golden/<nom>.<fmt>` avec le
  binaire **C** (référence) ; `toile-run run --expect <golden>` renvoie le code
  de sortie **3** en cas de divergence. Les goldens sont versionnés : une PR qui
  change le contrat doit les régénérer explicitement, ce qui rend la régression
  visible en revue.
- **Erreurs de parse** : une ligne invalide n'est pas un verdict du modèle —
  code de sortie **2** et message `fixtures/x.tcl:<ligne>: <raison>`
  (`CONTRACT.md` §4).

## 6. Validation contre QEMU réel (exigence C8, avec cible)

Le modèle prouve sa cohérence, pas sa fidélité au périphérique réel (D6).
Méthode :

1. lancer l'invité QNX sous QEMU avec `-device virtio-gpu-pci` (volet A) ;
2. **capturer** les commandes virtio-gpu émises par le pilote (instrumentation du
   pilote, ou trace côté QEMU) ;
3. les **rejouer** dans le modèle C — `toile-run wire` pour les octets capturés,
   `toile-run run` pour leur forme DSL ;
4. comparer l'état final : mêmes ressources, mêmes scanouts, mêmes compteurs.

Un écart est un bug **du modèle ou du pilote** — le harnais, lui, est déterministe.
Cette étape exige SDP QNX + QEMU + un invité qui énumère le périphérique (E1) :
elle est **hors CI de PR** et `make qemu-demo` n'est jamais appelé en CI
(`CONTRACT.md` §12).

## 7. Gardes transverses

| Garde | Règle | Fichier |
|---|---|---|
| Taille | tout fichier versionné < 500 lignes ; toute fonction C/Python < 50 lignes | `.github/checks/taille.py` |
| API gelée | chaque signature de `CONTRACT.md` §10 est présente dans `core/include/toile/` | `.github/checks/api-frozen.py` |
| Budget CI | chaque job déclare `timeout-minutes` ≤ 5 | `.github/ci-budget/check_timeouts.py` |
| Build strict | `gcc -std=c11 -O2 -Werror -Wall -Wextra` | `make build` |

`make guards` exécute les trois scripts Python (D14 : pas de Node). Le budget CI
global est **5 minutes par job**, avec `concurrency` + `cancel-in-progress` et
`fetch-depth: 1` ; build natif long et E2E QEMU hors PR (`CONTRACT.md` §13).

## 8. Exigences → moyen de preuve → statut

Statut au **2026-11-12**. Chaque ligne renvoie à une **commande du dépôt** :
ce qui est marqué « vérifié » se rejoue en local ou en CI, ce qui ne l'est pas
est dit comme tel.

| Exigence | Moyen de preuve | Statut |
|---|---|---|
| S1 `CONTRACT.md` gelé | fichier présent, §1–§13, symboles publics figés | **vérifié** |
| S2 cœur C11 : machine à commandes + invariants | `make test-c` (P1, P2 sur toutes les fixtures) | **vérifié** — 4 binaires de test verts |
| S3 jumeau Python + différentiel vert | `make diff` (§4) | **vérifié** — 21 fixtures, 63 comparaisons, 0 écart |
| S4 fixtures de référence | `toile-run run` + goldens (`make golden`, `--expect`) | **vérifié** — 21 fixtures + 63 goldens |
| S5 transport Passerelle adapté, non réécrit | compilation `qcc` de `guest/virtio/` + patch appliqué à une copie de build (E3) | partiellement vérifié — adaptation écrite, non compilée sur cible |
| S6 driver invité minimal (PCI, controlq, scanout unique, flush) | boot QEMU + trace des commandes (E1, puis C8) | en cours — énumération PCI **vérifiée** (E1), pilote en cours de démarrage dans l'invité |
| S7 display driver Screen minimal | chargement du driver par Screen dans l'invité (E2) | **bloqué** — interface réelle identifiée (OpenWF Display + Wfdcfg), DDK hors licence (E2) |
| S8 démo QEMU (image invitée qui affiche) | `make qemu-demo`, capture vidéo | non vérifié — exige SDP + QEMU |
| S9 `make test` vert sans QNX/QEMU/GPU | `make test` = `build test-c test-py diff guards` | **vérifié** — vert, sans QNX/QEMU/GPU |
| S10 CI `-Werror`, tests, différentiel, gardes, ≤ 5 min | `.github/workflows/ci.yml` + les trois gardes | **vérifié** — CI verte sur le runner r630, budget ≤ 5 min |
| C1 multi-scanout | fixtures multi-scanout, P1 (I1, I5), différentiel | non vérifié |
| C2 curseur matériel (`cursorq`) | fixtures `update_cursor`/`move_cursor`, P2 (`cursor_not_visible`) | non vérifié — commandes déjà dans le DSL S |
| C3 suivi de dommages | fixtures à rectangles partiels, I6 (`bytes_transferred`) | non vérifié |
| C4 double tampon + vsync | mesures sur cible (E5) | non vérifié — E5 : débit du modèle **mesuré**, débit réel non mesuré (`docs/TRIALS-E5.md`) |
| C5 virtio-input clavier/souris | essai en invité QEMU | non vérifié |
| C6 intégration Screen complète | application Screen réelle lancée en invité | non vérifié — dépend de E2 |
| C7 resource manager `/dev/gpu0` (ioctl/devctl) | client de test en invité | non vérifié |
| C8 validation contre QEMU réel | capture puis rejeu dans le modèle (§6) | non vérifié |
| C9 codelab 4 chapitres | relecture + exécution des commandes citées | **écrit** — 4 chapitres ; chapitres 1–2 exécutables partout, 3–4 dépendent de la cible |
| C10 image `mkifs` reproductible + scripts QEMU | `mkifs` puis boot, deux fois de suite, même empreinte | non vérifié |

Les niveaux X (X1–X5) ne sont pas des exigences prouvables au 15/12 : X1 est
bloqué par E4, X4 est explicitement hors socle (D5), X5 est la contribution
amont du patch `guest/virtio/toile-virtio-gpu.patch`.
