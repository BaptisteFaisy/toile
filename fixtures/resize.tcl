# Fixture "resize" : le guest change la taille de son framebuffer.
# Ordre impose par DECISIONS.md D11 : desactiver le scanout (set_scanout id=0)
# AVANT resource_unref, sinon la ressource encore affichee est refusee.
# Prouve P1 (coherence), P2 (ordre des refus), P3 (determinisme).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK            resource_create_2d id=1 (64x48)
#   TOILE_OK            resource_attach_backing id=1
#   TOILE_OK            set_scanout scanout=0 id=1
#   TOILE_TRANSFER_OK   transfer_to_host_2d id=1 (rectangle omis -> 0,0,64,48)
#   TOILE_OK            resource_flush id=1
#   TOILE_OK            set_scanout scanout=0 id=0 (desactivation)
#   TOILE_OK            resource_unref id=1 (plus aucune reference active)
#   TOILE_OK            resource_create_2d id=2 (128x96, nouvelle taille)
#   TOILE_OK            resource_attach_backing id=2
#   TOILE_OK            set_scanout scanout=0 id=2
#   TOILE_TRANSFER_OK   transfer_to_host_2d id=2 (rectangle omis -> 0,0,128,96)
#   TOILE_OK            resource_flush id=2
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0 (aucun verdict d'erreur).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
resource_attach_backing id=1 pages=0x1000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=64 height=48
transfer_to_host_2d id=1
resource_flush id=1
set_scanout scanout=0 id=0 x=0 y=0 width=0 height=0
resource_unref id=1
resource_create_2d id=2 format=B8G8R8A8_UNORM width=128 height=96
resource_attach_backing id=2 pages=0x2000:4096
set_scanout scanout=0 id=2 x=0 y=0 width=128 height=96
transfer_to_host_2d id=2
resource_flush id=2
