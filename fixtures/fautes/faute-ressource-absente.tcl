# Fixture FAUTE (P2) : commande visant une ressource qui n'existe pas.
# La ressource 1 existe et est affichee ; le flush vise l'identifiant 42, absent
# de la table des ressources (TOILE_MAX_RESOURCES = 64, section 3).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK            resource_create_2d id=1
#   TOILE_OK            resource_attach_backing id=1
#   TOILE_OK            set_scanout scanout=0 id=1
#   TOILE_RESOURCE_MISSING  resource_flush id=42 (ressource absente)
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 1 (verdict de severite error).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
resource_attach_backing id=1 pages=0x1000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=64 height=48
resource_flush id=42
