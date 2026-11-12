# Fixture "desactivation du scanout" : set_scanout id=0 libere la tete, ce qui
# permet ensuite de detruire la ressource (ordre impose par DECISIONS.md D11).
# Prouve P1 (I1 tient : plus aucun scanout ne reference la ressource apres
# desactivation) et P2 (l'unref est refuse tant que le scanout est actif).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK            resource_create_2d id=1
#   TOILE_OK            resource_attach_backing id=1
#   TOILE_OK            set_scanout scanout=0 id=1
#   TOILE_TRANSFER_OK   transfer_to_host_2d id=1
#   TOILE_OK            resource_flush id=1
#   TOILE_OK            set_scanout scanout=0 id=0 (desactivation, rectangle ignore)
#   TOILE_OK            resource_unref id=1 (plus de scanout actif : accepte)
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0 (aucun verdict d'erreur).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=640 height=480
resource_attach_backing id=1 pages=0x1000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=640 height=480
transfer_to_host_2d id=1
resource_flush id=1
set_scanout scanout=0 id=0 x=0 y=0 width=0 height=0
resource_unref id=1
