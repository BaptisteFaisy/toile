# Reference "boot graphique" : creation, backing, scanout 0, transfert invite->hote,
# flush (affichage). Prouve P1 (modele coherent) et P3 (determinisme).
# Ressource 1, B8G8R8A8_UNORM, 1280x800, une page invite 0x1000:4096.
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK            resource_create_2d id=1
#   TOILE_OK            resource_attach_backing id=1
#   TOILE_OK            set_scanout scanout=0 id=1
#   TOILE_TRANSFER_OK   transfer_to_host_2d id=1 (rectangle omis -> 0,0,1280,800)
#   TOILE_OK            resource_flush id=1
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0 (aucun verdict de severite error).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=1280 height=800
resource_attach_backing id=1 pages=0x1000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=1280 height=800
transfer_to_host_2d id=1
resource_flush id=1
