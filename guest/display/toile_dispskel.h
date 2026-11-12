/* SPDX-License-Identifier: MIT */
/*
 * Toile — déclaration du squelette de display driver (E2 non tranché).
 *
 * Le squelette ne fournit AUCUNE API QNX : il documente les points à
 * confirmer.  Voir guest/display/README.md et
 * docs/TRIALS-E1-E4.md §E2 pour les preuves.
 */
#ifndef TOILE_DISPSKEL_H
#define TOILE_DISPSKEL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Affiche l'état du squelette (ce qui est vérifié, ce qui ne l'est pas). */
void toile_dispskel_report(void);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_DISPSKEL_H */
