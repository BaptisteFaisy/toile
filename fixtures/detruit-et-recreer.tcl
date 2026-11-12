# Fixture "detruit et recree" : unref puis re-creation du MEME identifiant.
# Ordre impose par DECISIONS.md D11 : le scanout est desactive (id=0) avant le
# unref, sinon la ressource encore affichee est refusee (voir
# fixtures/fautes/faute-unref-affichee.tcl).
# Prouve P1 (la table des ressources libere bien l'identifiant) et P3.
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK            resource_create_2d id=1
#   TOILE_OK            resource_attach_backing id=1
#   TOILE_OK            set_scanout scanout=0 id=1
#   TOILE_TRANSFER_OK   transfer_to_host_2d id=1
#   TOILE_OK            resource_flush id=1
#   TOILE_OK            set_scanout scanout=0 id=0 (desactivation)
#   TOILE_OK            resource_unref id=1 (detruite)
#   TOILE_OK            resource_create_2d id=1 (meme identifiant, recree)
#   TOILE_OK            resource_attach_backing id=1
#   TOILE_OK            set_scanout scanout=0 id=1
#   TOILE_TRANSFER_OK   transfer_to_host_2d id=1
#   TOILE_OK            resource_flush id=1
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0 (aucun verdict d'erreur).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
resource_attach_backing id=1 pages=0x1000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=64 height=48
transfer_to_host_2d id=1
resource_flush id=1
set_scanout scanout=0 id=0 x=0 y=0 width=0 height=0
resource_unref id=1
resource_create_2d id=1 format=B8G8R8A8_UNORM width=32 height=32
resource_attach_backing id=1 pages=0x2000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=32 height=32
transfer_to_host_2d id=1
resource_flush id=1
