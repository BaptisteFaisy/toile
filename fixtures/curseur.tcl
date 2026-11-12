# Fixture "curseur materiel" : update_cursor (rend le curseur visible, image
# ressource 2, point chaud 0,0) puis move_cursor (deplace un curseur deja
# visible). Prouve P1 (etat coherent) et P2 (un move_cursor sans curseur
# visible serait refuse : voir fixtures/fautes/faute-curseur-invisible.tcl).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK  resource_create_2d id=1
#   TOILE_OK  resource_attach_backing id=1
#   TOILE_OK  set_scanout scanout=0 id=1
#   TOILE_OK  resource_create_2d id=2
#   TOILE_OK  resource_attach_backing id=2
#   TOILE_OK  update_cursor scanout=0 x=10 y=20 id=2 hot_x=0 hot_y=0
#   TOILE_OK  move_cursor scanout=0 x=40 y=50
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0 (aucun verdict d'erreur).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=640 height=480
resource_attach_backing id=1 pages=0x1000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=640 height=480
resource_create_2d id=2 format=B8G8R8A8_UNORM width=64 height=64
resource_attach_backing id=2 pages=0x2000:4096
update_cursor scanout=0 x=10 y=20 id=2 hot_x=0 hot_y=0
move_cursor scanout=0 x=40 y=50
