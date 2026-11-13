# CI E2E de l'invité QNX

La CI PR ne compile pas l'invité : le SDP QNX 8.0 n'est installé que sur le
poste de travail. Elle utilise une image IFS publiée dans une release GitHub.
Le job `guest-e2e` la télécharge sur le runner `toile-worker`, démarre QEMU avec
KVM et rejoue `qemu/verify-hash.sh`.

## Publier une image

Sur le poste où le SDP est installé :

```bash
cd ~/Bureau/toile
git checkout main
git tag guest-ifs-latest                 # une première fois
qemu/publish-ifs.sh --tag guest-ifs-latest --allow-branch
```

Pour créer un tag qui n'existe pas encore :

```bash
qemu/publish-ifs.sh --tag guest-ifs-20261113 \
  --allow-new-tag --allow-branch
```

Le script compile `anim`, `driver`, `probe` et `input`, puis construit **deux**
images :

- `image/toile-vitrine.build` → asset `toile-guest.ifs` (la vitrine : trois
  clients qui peignent, à lancer à la main) ;
- `image/toile-verify.build` → asset `toile-verify.ifs` (la **preuve** : son
  client publie l'empreinte FNV-1a de son framebuffer et tient l'image).

C'est `toile-verify.ifs` que la CI télécharge : l'image de vitrine ne publie
aucune empreinte, il n'y aurait rien à comparer.

`--dry-run` construit l'image sans publier :

```bash
qemu/publish-ifs.sh --tag guest-ifs-latest --allow-branch --dry-run
```

Le tag utilisé par défaut par la CI est `guest-ifs-latest`. Il peut être
remplacé par la variable de dépôt `GUEST_IFS_TAG`.

## Ce que le job prouve

- l'image IFS publiée démarre dans un vrai invité QNX 8.0 ;
- le pilote voit le virtio-gpu et le client peint le framebuffer ;
- l'empreinte FNV-1a publiée sur la console série est identique à celle
  recalculée à partir de la capture produite par QEMU.

Le job ne prouve pas la compilation QNX (elle est faite avant publication), le
chemin Screen `mmap` ni les performances d'autres résolutions. Il ne démarre
pas le display driver.

## Budget et absence d'image

Le job a `timeout-minutes: 5`, `concurrency` et `fetch-depth: 1`. Il exige
`/dev/kvm` : l'émulation logicielle n'est pas compatible avec ce budget. Tant
qu'aucune release ne contient `toile-guest.ifs`, le workflow publie un warning
et sort proprement (la CI core reste la référence pour le code).
