# Fixture FAUTE (P2) : flush d'une ressource affichee par aucun scanout.
# La ressource 1 a son backing mais aucune tete ne la pointe : le flush est
# refuse (invariant I2 de la section 5, aucun flush n'est compte).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK            resource_create_2d id=1
#   TOILE_OK            resource_attach_backing id=1
#   TOILE_FLUSH_NO_SCANOUT  resource_flush id=1 (aucun scanout ne montre id=1)
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0. En effet TOILE_FLUSH_NO_SCANOUT est de
# severite "warning" (section 6) et le contrat section 11 ne donne le code 1
# que pour un verdict de severite "error" ; l'etat reste coherent.
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
resource_attach_backing id=1 pages=0x1000:4096
resource_flush id=1
