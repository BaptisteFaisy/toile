# Fixture FAUTE (P2) : format de pixel absent de la table du contrat section 3.
# La ligne est refusee AVANT le modele : le DSL resout le nom de format au
# parse (section 4), donc un format hors table est une ERREUR DE PARSE, pas un
# verdict du modele.
# Verdict attendu : aucun (erreur de parse, section 4).
# Code de sortie attendu : 2 (usage ou fixture invalide, section 11).
# Message attendu sur stderr : fixtures/fautes/faute-format-inconnu.tcl:12: bad_value
# Note : la cause figee bad_format (section 6) reste le refus du MODELE
# (resource.c / resource.py, verification core/tests/test_resource.c) ; elle
# n'est pas atteignable depuis une fixture .tcl, le parseur rejettant le
# format inconnu en amont.
resource_create_2d id=1 format=RGB565_UNORM width=64 height=48
