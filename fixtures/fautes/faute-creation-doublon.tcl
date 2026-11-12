# Fixture FAUTE (P2) : creation d'un identifiant de ressource deja existant.
# La ressource 1 existe deja : la creation en double est refusee et l'etat
# reste inchange (section 1 : application tout ou rien).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK              resource_create_2d id=1
#   TOILE_RESOURCE_EXISTS resource_create_2d id=1 (identifiant deja pris)
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0. TOILE_RESOURCE_EXISTS est de severite
# "warning" (section 6) et le contrat section 11 ne donne le code 1 que pour
# un verdict de severite "error".
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
