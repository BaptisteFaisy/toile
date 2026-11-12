# Fixture FAUTE (P2) : creation d'une ressource de geometrie nulle.
# width=0 : la creation est refusee (invariant I4 de la section 5), aucune
# ressource n'est creee et l'etat reste vide.
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_CMD_INVALID  resource_create_2d id=1 (cause=bad_geometry)
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 1 (verdict de severite error).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=0 height=48
