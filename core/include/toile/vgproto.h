/* SPDX-License-Identifier: MIT */
/*
 * Toile — protocole virtio-gpu 2D (couche fil) et types partagés du modèle.
 *
 * Les structures « fil » reproduisent les en-têtes standard virtio-gpu
 * (Linux/QEMU, licence BSD) en petit-boutien explicite. Rien ici ne touche
 * au matériel : encode/decode d'un tampon fourni par l'appelant.
 *
 * Contrat gelé : CONTRACT.md §3, §9, §10.
 */
#ifndef TOILE_VGPROTO_H
#define TOILE_VGPROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- limites du modèle (CONTRACT.md §3) ---------------------------------- */
#define TOILE_MAX_RESOURCES 64u
#define TOILE_MAX_SCANOUTS  16u
#define TOILE_MAX_PAGES     32u
#define TOILE_CAUSE_MAX     96u
#define TOILE_ID_NONE       0xFFFFFFFFu

/* --- types de commande (enum virtio_gpu_ctrl_type) ----------------------- */
#define TOILE_VG_CMD_GET_DISPLAY_INFO        0x0100u
#define TOILE_VG_CMD_RESOURCE_CREATE_2D      0x0101u
#define TOILE_VG_CMD_RESOURCE_UNREF          0x0102u
#define TOILE_VG_CMD_SET_SCANOUT             0x0103u
#define TOILE_VG_CMD_RESOURCE_FLUSH          0x0104u
#define TOILE_VG_CMD_TRANSFER_TO_HOST_2D     0x0105u
#define TOILE_VG_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define TOILE_VG_CMD_RESOURCE_DETACH_BACKING 0x0107u
#define TOILE_VG_CMD_TRANSFER_FROM_HOST_3D   0x0206u
#define TOILE_VG_CMD_UPDATE_CURSOR           0x0300u
#define TOILE_VG_CMD_MOVE_CURSOR             0x0301u

#define TOILE_VG_RESP_OK_NODATA              0x1100u
#define TOILE_VG_RESP_OK_DISPLAY_INFO        0x1101u
#define TOILE_VG_RESP_ERR_UNSPEC             0x1200u
#define TOILE_VG_RESP_ERR_OUT_OF_MEMORY      0x1201u
#define TOILE_VG_RESP_ERR_INVALID_SCANOUT_ID 0x1202u
#define TOILE_VG_RESP_ERR_INVALID_RESOURCE_ID 0x1203u
#define TOILE_VG_RESP_ERR_INVALID_PARAMETER  0x1205u

#define TOILE_VG_FLAG_FENCE 0x1u

/* Longueurs fil : en-tête de contrôle 24 octets, commande au plus longue
 * = attach_backing avec TOILE_MAX_PAGES entrées (8 + 32 * 16). */
#define TOILE_VG_HDR_LEN    24u
#define TOILE_VG_MAX_CMD_LEN (TOILE_VG_HDR_LEN + 8u + TOILE_MAX_PAGES * 16u)

/* --- formats de pixels (CONTRACT.md §3) ---------------------------------- */
typedef enum toile_fmt {
    TOILE_FMT_INVALID        = 0,
    TOILE_FMT_B8G8R8A8_UNORM = 1,
    TOILE_FMT_B8G8R8X8_UNORM = 2,
    TOILE_FMT_A8R8G8B8_UNORM = 3,
    TOILE_FMT_X8R8G8B8_UNORM = 4,
    TOILE_FMT_R8G8B8A8_UNORM = 67,
    TOILE_FMT_X8B8G8R8_UNORM = 68,
    TOILE_FMT_A8B8G8R8_UNORM = 121,
    TOILE_FMT_R8G8B8X8_UNORM = 134
} toile_fmt;

/* --- structures fil (petit-boutien, alignement naturel) ------------------ */
typedef struct toile_vg_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t  ring_idx;
    uint8_t  padding[3];
} toile_vg_ctrl_hdr;

typedef struct toile_vg_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} toile_vg_rect;

typedef struct toile_vg_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} toile_vg_mem_entry;

typedef struct toile_vg_cursor_pos {
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t padding;
} toile_vg_cursor_pos;

/* --- types du modèle (hôtes, indépendants du boutisme) -------------------- */
typedef struct toile_rect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} toile_rect;

typedef struct toile_page {
    uint64_t addr;
    uint32_t length;
} toile_page;

/* Commande décodée : ce que consomme le processeur de commandes. */
typedef struct toile_cmd {
    uint32_t type;       /* TOILE_VG_CMD_* */
    uint32_t flags;
    uint64_t fence_id;
    union {
        struct { uint32_t resource_id, format, width, height; } create_2d;
        struct { uint32_t resource_id; } unref;
        struct { uint32_t resource_id, scanout_id; toile_rect rect; } scanout;
        struct { uint32_t resource_id; toile_rect rect; } flush;
        struct { uint32_t resource_id; toile_rect rect; uint64_t offset; } xfer;
        struct {
            uint32_t resource_id, nr_entries;
            toile_page pages[TOILE_MAX_PAGES];
        } backing;
        struct { uint32_t scanout_id, x, y, resource_id, hot_x, hot_y; } cursor;
    } u;
} toile_cmd;

/* --- API gelée (CONTRACT.md §10) ----------------------------------------- */

/* Octets par pixel d'un format valide ; 0 si le format est inconnu. */
uint32_t toile_fmt_bpp(toile_fmt fmt);

/* Nom textuel d'un format ("B8G8R8A8_UNORM") ; NULL si inconnu. */
const char *toile_fmt_name(toile_fmt fmt);

/* Analyse un nom ou un nombre ("B8G8R8A8_UNORM", "1") ; 0 = OK, sinon
 * TOILE_CMD_INVALID. */
int toile_fmt_parse(const char *name, toile_fmt *out);

/* Nom DSL d'un type de commande ("resource_create_2d") ; NULL si inconnu. */
const char *toile_vg_cmd_name(uint32_t type);

/* Sérialise une commande en octets fil ; *written reçoit la longueur.
 * 0 = OK ; TOILE_CMD_INVALID si le type est inconnu ou le tampon trop petit. */
int toile_vg_encode(const toile_cmd *cmd, uint8_t *buf, size_t cap,
                    size_t *written);

/* Décode une commande fil ; 0 = OK ; TOILE_CMD_INVALID sinon (type inconnu,
 * longueur incohérente, nr_entries > TOILE_MAX_PAGES). */
int toile_vg_decode(const uint8_t *buf, size_t len, toile_cmd *out);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_VGPROTO_H */
