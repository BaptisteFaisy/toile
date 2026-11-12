# Fixture "multi-scanout" : quatre tetes positionnees sur une surface virtuelle
# 1280x960 (deux par ligne, deux par colonne), une ressource 2D par tete.
# Prouve P1 (I1 : chaque scanout actif reference une ressource existante) et
# P3 (positions et etat final deterministes).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK x4        resource_create_2d id=1, id=2, id=3, id=4
#   TOILE_OK x4        resource_attach_backing id=1, id=2, id=3, id=4
#   TOILE_OK x4        set_scanout scanout=0..3 (positions 0,0 / 640,0 / 0,480 / 640,480)
#   TOILE_TRANSFER_OK x4 transfer_to_host_2d id=1, id=2, id=3, id=4
#   TOILE_OK x4        resource_flush id=1, id=2, id=3, id=4
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0 (aucun verdict d'erreur).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=640 height=480
resource_attach_backing id=1 pages=0x1000:4096
resource_create_2d id=2 format=B8G8R8A8_UNORM width=640 height=480
resource_attach_backing id=2 pages=0x2000:4096
resource_create_2d id=3 format=B8G8R8A8_UNORM width=640 height=480
resource_attach_backing id=3 pages=0x3000:4096
resource_create_2d id=4 format=B8G8R8A8_UNORM width=640 height=480
resource_attach_backing id=4 pages=0x4000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=640 height=480
set_scanout scanout=1 id=2 x=640 y=0 width=640 height=480
set_scanout scanout=2 id=3 x=0 y=480 width=640 height=480
set_scanout scanout=3 id=4 x=640 y=480 width=640 height=480
transfer_to_host_2d id=1
transfer_to_host_2d id=2
transfer_to_host_2d id=3
transfer_to_host_2d id=4
resource_flush id=1
resource_flush id=2
resource_flush id=3
resource_flush id=4
