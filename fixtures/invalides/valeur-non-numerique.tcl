# Fixture volontairement NON conforme au DSL (contrat section 4) : valeur non
# numerique. La valeur de "width" n'est ni un entier decimal ni un hexadecimal
# : le parseur doit refuser la ligne. Ce n'est PAS un verdict du modele :
# aucun verdict n'est produit et la sequence n'est pas executee.
# Code de sortie attendu : 2 (usage ou fixture invalide, section 11).
# Message attendu sur stderr : fixtures/invalides/valeur-non-numerique.tcl:7: bad_value
resource_create_2d id=1 format=B8G8R8A8_UNORM width=large height=48
