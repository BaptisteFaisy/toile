# Fixture "limites des tables" : les bornes du contrat section 3 sont
# TOILE_MAX_RESOURCES = 64 et TOILE_MAX_SCANOUTS = 16. Ce fixture remplit la
# table des ressources (64 elements acceptes), tente un 65e (refuse), remplit
# la table des scanouts (16 tetes acceptees), puis tente une 17e tete (refusee)
# : jamais de debordement (invariant I5).
# Aucun transfert ici : le backing n'est pas necessaire (seul
# transfer_to_host_2d l'exige, invariant I3).
# Prouve P2 (rejet propre, etat inchange) et P1 (aucune violation d'invariant).
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK x64            resource_create_2d id=1..64 (toutes acceptees)
#   TOILE_CMD_INVALID       resource_create_2d id=65 (cause=resource_table_full)
#   TOILE_OK x16            set_scanout scanout=0..15 (les 16 tetes actives)
#   TOILE_CMD_INVALID       set_scanout scanout=16 (cause=scanout_id_out_of_range)
#   TOILE_MODEL_CONSISTENT
# Codes de sortie attendus : 1 (deux verdicts d'erreur dans la meme sequence).
# Note : la 17e tete est refusee pour scanout_id_out_of_range (scanout >= 16),
# la cause scanout_table_full n'etant pas atteignable par le DSL (les 16
# emplacements de la table sont deja occupes).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=2 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=3 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=4 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=5 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=6 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=7 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=8 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=9 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=10 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=11 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=12 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=13 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=14 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=15 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=16 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=17 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=18 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=19 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=20 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=21 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=22 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=23 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=24 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=25 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=26 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=27 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=28 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=29 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=30 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=31 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=32 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=33 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=34 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=35 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=36 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=37 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=38 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=39 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=40 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=41 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=42 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=43 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=44 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=45 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=46 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=47 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=48 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=49 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=50 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=51 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=52 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=53 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=54 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=55 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=56 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=57 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=58 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=59 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=60 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=61 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=62 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=63 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=64 format=B8G8R8A8_UNORM width=64 height=48
resource_create_2d id=65 format=B8G8R8A8_UNORM width=64 height=48
set_scanout scanout=0 id=1 x=0 y=0 width=64 height=48
set_scanout scanout=1 id=2 x=0 y=0 width=64 height=48
set_scanout scanout=2 id=3 x=0 y=0 width=64 height=48
set_scanout scanout=3 id=4 x=0 y=0 width=64 height=48
set_scanout scanout=4 id=5 x=0 y=0 width=64 height=48
set_scanout scanout=5 id=6 x=0 y=0 width=64 height=48
set_scanout scanout=6 id=7 x=0 y=0 width=64 height=48
set_scanout scanout=7 id=8 x=0 y=0 width=64 height=48
set_scanout scanout=8 id=9 x=0 y=0 width=64 height=48
set_scanout scanout=9 id=10 x=0 y=0 width=64 height=48
set_scanout scanout=10 id=11 x=0 y=0 width=64 height=48
set_scanout scanout=11 id=12 x=0 y=0 width=64 height=48
set_scanout scanout=12 id=13 x=0 y=0 width=64 height=48
set_scanout scanout=13 id=14 x=0 y=0 width=64 height=48
set_scanout scanout=14 id=15 x=0 y=0 width=64 height=48
set_scanout scanout=15 id=16 x=0 y=0 width=64 height=48
set_scanout scanout=16 id=17 x=0 y=0 width=64 height=48
