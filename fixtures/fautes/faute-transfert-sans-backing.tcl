# Fixture FAUTE (P2) : transfert invite->hote d'une ressource sans backing.
# La ressource 1 existe mais aucun backing n'est attache : le transfert est
# refuse (invariant I3 de la section 5, aucun octet n'est compte).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK            resource_create_2d id=1
#   TOILE_BACKING_MISSING  transfer_to_host_2d id=1 (aucun backing attache)
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 1 (verdict de severite error).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
transfer_to_host_2d id=1
