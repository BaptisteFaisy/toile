# Fixture volontairement NON conforme au DSL (contrat section 4) : cle inconnue.
# La cle "couleur" n'existe pas dans le contrat section 3 : le parseur doit
# refuser la ligne. Ce n'est PAS un verdict du modele : aucun verdict n'est
# produit et la sequence n'est pas executee.
# Code de sortie attendu : 2 (usage ou fixture invalide, section 11).
# Message attendu sur stderr : fixtures/invalides/cle-inconnue.tcl:7: unknown_key
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48 couleur=rouge
