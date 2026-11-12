# Fixture FAUTE (P2) : deplacement d'un curseur qui n'a jamais ete rendu
# visible. Aucun update_cursor n'a precede : le move_cursor est refuse
# (cause figee cursor_not_visible), le curseur reste invisible.
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_CMD_INVALID  move_cursor scanout=0 x=40 y=50 (cause=cursor_not_visible)
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 1 (verdict de severite error).
move_cursor scanout=0 x=40 y=50
