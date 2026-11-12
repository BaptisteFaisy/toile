# Fixture FAUTE (P2) : destruction d'une ressource encore affichee.
# Le scanout 0 pointe la ressource 1 : le unref est refuse (DECISIONS.md D11,
# invariant I1), l'etat reste inchange et la sequence se poursuit.
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK            resource_create_2d id=1
#   TOILE_OK            resource_attach_backing id=1
#   TOILE_OK            set_scanout scanout=0 id=1
#   TOILE_SCANOUT_INVALID  resource_unref id=1 (ressource encore affichee)
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 1 (verdict de severite error).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
resource_attach_backing id=1 pages=0x1000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=64 height=48
resource_unref id=1
