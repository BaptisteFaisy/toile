# Fixture "lecture hote" : transfer_from_host (code virtio 0x0206, lecture
# hote -> invite ; DECISIONS.md D12). La lecture ne touche pas I6 : aucun
# octet n'est compte dans bytes_transferred.
# Verdicts attendus (noms exacts du contrat section 6, dans l'ordre) :
#   TOILE_OK  resource_create_2d id=1
#   TOILE_OK  resource_attach_backing id=1
#   TOILE_OK  set_scanout scanout=0 id=1
#   TOILE_OK  transfer_from_host id=1 (lecture hote -> invite)
#   TOILE_OK  resource_flush id=1
#   TOILE_MODEL_CONSISTENT
# Code de sortie attendu : 0 (aucun verdict d'erreur).
resource_create_2d id=1 format=B8G8R8A8_UNORM width=1280 height=800
resource_attach_backing id=1 pages=0x1000:4096,0x2000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=1280 height=800
transfer_from_host id=1
resource_flush id=1
