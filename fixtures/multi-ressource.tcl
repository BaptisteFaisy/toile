# Fixture "multi-ressource" : trois ressources 2D, un seul scanout (0) dont la
# cible change de ressource a chaque etape (1 -> 2 -> 3). Prouve P1 (le scanout
# pointe toujours une ressource existante) et P3 (etat final deterministe).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK x3        resource_create_2d id=1, id=2, id=3
#   TOILE_OK x3        resource_attach_backing id=1, id=2, id=3
#   TOILE_OK           set_scanout scanout=0 id=1
#   TOILE_TRANSFER_OK  transfer_to_host_2d id=1
#   TOILE_OK           resource_flush id=1
#   TOILE_OK           set_scanout scanout=0 id=2 (le scanout change de ressource)
#   TOILE_TRANSFER_OK  transfer_to_host_2d id=2
#   TOILE_OK           resource_flush id=2
#   TOILE_OK           set_scanout scanout=0 id=3
#   TOILE_TRANSFER_OK  transfer_to_host_2d id=3
#   TOILE_OK           resource_flush id=3
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0 (aucun verdict d'erreur).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
resource_attach_backing id=1 pages=0x1000:4096
resource_create_2d id=2 format=B8G8R8A8_UNORM width=128 height=96
resource_attach_backing id=2 pages=0x2000:4096
resource_create_2d id=3 format=B8G8R8A8_UNORM width=256 height=192
resource_attach_backing id=3 pages=0x3000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=64 height=48
transfer_to_host_2d id=1
resource_flush id=1
set_scanout scanout=0 id=2 x=0 y=0 width=128 height=96
transfer_to_host_2d id=2
resource_flush id=2
set_scanout scanout=0 id=3 x=0 y=0 width=256 height=192
transfer_to_host_2d id=3
resource_flush id=3
