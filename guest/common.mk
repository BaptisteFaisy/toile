# Toile — build du code invité (QNX 8.0, qcc).
#
# ATTENTION : ce Makefile est volontairement HORS du Makefile racine.  Le
# cœur portable (core/) et le jumeau (host/) se construisent avec gcc sur
# le PC de développement et la CI n'a jamais besoin de qcc ; le code invité
# exige un SDP QNX 8.0 installé.  Voir guest/README.md.
#
# Usage :
#   source ~/qnx800/qnxsdp-env.sh
#   make -f common.mk                      # depuis guest/
#   make -f common.mk PASSERELLE_DIR=/chemin/vers/passerelle
#   make -f common.mk clean
#
# Variables :
#   PASSERELLE_DIR  racine du projet frère Passerelle (transport virtio)
#   BUILD           répertoire de sortie          (défaut : guest/build)
#   QNX_TARGET      racine de la cible SDP       (défaut : $QNX_TARGET)
#   CC              compilateur                   (défaut : qcc)
#   TOILE_VARIANT   variante de toolchain        (défaut : gcc_ntox86_64)

TOILE_DIR      := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
TOILE_ROOT     := $(patsubst %/,%,$(dir $(TOILE_DIR)))
PASSERELLE_DIR ?= /home/baptiste/Bureau/passerelle
BUILD          ?= $(TOILE_DIR)/build
# `cc` par défaut de make n'est pas qcc : on ne l'utilise que si l'appelant
# n'a pas fourni CC (origin=default).
ifeq ($(origin CC),default)
CC := qcc
endif
TOILE_VARIANT  ?= gcc_ntox86_64
QNX_TARGET_DIR ?= $(QNX_TARGET)

# --- arborescence de travail -----------------------------------------------
PGW_DIR        := $(BUILD)/pgw
PGW_INC        := $(PGW_DIR)/common/include
PGW_SRC        := $(PGW_DIR)/virtio/src
PGW_COMMON_SRC := $(PGW_DIR)/common/src
PGW_VIRT_INC   := $(PGW_DIR)/virtio/include
PATCH_STAMP    := $(PGW_DIR)/.patched

# --- drapeaux ---------------------------------------------------------------
# -std=gnu11 : en C11 strict, QNX 8.0 masque les extensions UNIX
# (uint_t, utilisé par <pci/pci.h>) et les prototypes POSIX
# (clock_gettime).  gnu11 est le standard par défaut de qcc.  Le cœur
# portable reste C11 strict : la CI racine le compile avec -std=c11
# -Werror (Makefile racine), code inchangé.
WARN_CFLAGS    := -std=gnu11 -O2 -Wall -Wextra -Werror
CPPFLAGS       += -I$(TOILE_ROOT)/core/include \
                   -I$(TOILE_DIR)/common \
                   -I$(TOILE_DIR)/gpu \
                   -I$(TOILE_DIR)/virtio \
                   -I$(PGW_INC) -I$(PGW_VIRT_INC) -I$(PGW_SRC)
CFLAGS         += $(WARN_CFLAGS)
LDFLAGS        += -lpci

# --- sources ----------------------------------------------------------------
# Transport virtio repris de Passerelle (Apache-2.0), non réécrit.
# mmio_qnx.c est volontairement EXCLU : sur QNX 8.0 mmap_device_io() renvoie
# un descripteur opaque (accès par devctl/mdevctl), pas une adresse
# utilisable au pointeur comme le suppose ce fichier — voir
# docs/TRIALS-E1-E4.md §E3.  Aucun appelant ne référence
# pgw_vio_mmio_open_phys(), donc l'exclusion ne casse pas l'édition de liens.
PGW_SRCS       := dev.c virtq.c virtq_alloc.c virtq_dev.c \
                   pci_caps.c pci_modern.c pci_qnx.c \
                   mmio.c qnx_mem.c mock.c
CORE_SRCS      := $(wildcard $(TOILE_ROOT)/core/src/*.c)
GPU_SRCS       := gpu_pci.c gpu_ctrl.c gpu_resource.c gpu_mire.c \
                   gpu_resmgr.c gpu_main.c
COMMON_SRCS    := toile_pattern.c
VIRTIO_SRCS    := toile_transport.c
# Client de preuve /dev/gpu0 (essai « le pilote démarre dans l'invité »).
PROBE_SRCS     := toile_gpu_probe.c

# Couche commune Passerelle : seul log.c est nécessaire ici (pgw_log_write,
# pgw_stat_add).
PGW_COMMON_SRCS := log.c
PGW_OBJS       := $(addprefix $(BUILD)/pgw/,$(PGW_SRCS:.c=.o)) \
                   $(addprefix $(BUILD)/pgw/,$(PGW_COMMON_SRCS:.c=.o))
CORE_OBJS      := $(addprefix $(BUILD)/core/,$(notdir $(CORE_SRCS:.c=.o)))
GPU_OBJS       := $(addprefix $(BUILD)/gpu/,$(GPU_SRCS:.c=.o))
COMMON_OBJS    := $(addprefix $(BUILD)/common/,$(COMMON_SRCS:.c=.o))
VIRTIO_OBJS    := $(addprefix $(BUILD)/virtio/,$(VIRTIO_SRCS:.c=.o))
PROBE_OBJS     := $(addprefix $(BUILD)/common/,$(PROBE_SRCS:.c=.o))

DRIVER_BIN     := $(BUILD)/toile-gpu
DISPLAY_BIN    := $(BUILD)/toile-display
INPUT_BIN      := $(BUILD)/toile-input
PROBE_BIN      := $(BUILD)/toile-gpu-probe

.PHONY: all driver display input probe patch clean dirs
all: driver display input probe

driver: $(DRIVER_BIN)
display: $(DISPLAY_BIN)
input: $(INPUT_BIN)
probe: $(PROBE_BIN)

# --- préparation : copie du transport Passerelle + patch -------------------
patch: $(PATCH_STAMP)

$(PATCH_STAMP): $(TOILE_DIR)/virtio/toile-virtio-gpu.patch
	@mkdir -p $(PGW_INC)/pgw $(PGW_VIRT_INC)/pgw $(PGW_SRC) $(PGW_COMMON_SRC)
	@cp $(PASSERELLE_DIR)/common/include/pgw/*.h $(PGW_INC)/pgw/
	@cp $(PASSERELLE_DIR)/common/src/*.c $(PGW_COMMON_SRC)/
	@cp $(PASSERELLE_DIR)/virtio/include/pgw/*.h $(PGW_VIRT_INC)/pgw/
	@cp $(PASSERELLE_DIR)/virtio/src/*.c $(PASSERELLE_DIR)/virtio/src/*.h $(PGW_SRC)/
	@patch -p1 -d $(PGW_DIR) < $<
	@touch $@

# --- compilation ------------------------------------------------------------
$(BUILD)/pgw/%.o: $(PGW_SRC)/%.c $(PATCH_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/pgw/%.o: $(PGW_COMMON_SRC)/%.c $(PATCH_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $(CFLAGS) $(CPPFLAGS) -I$(PGW_COMMON_SRC) -c $< -o $@

$(BUILD)/core/%.o: $(TOILE_ROOT)/core/src/%.c
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/gpu/%.o: $(TOILE_DIR)/gpu/%.c $(PATCH_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/common/%.o: $(TOILE_DIR)/common/%.c
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/virtio/%.o: $(TOILE_DIR)/virtio/%.c $(PATCH_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# --- liaison ----------------------------------------------------------------
$(DRIVER_BIN): $(GPU_OBJS) $(VIRTIO_OBJS) $(COMMON_OBJS) $(CORE_OBJS) $(PGW_OBJS)
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $^ $(LDFLAGS) -o $@

$(DISPLAY_BIN): $(BUILD)/display/toile_display.o $(BUILD)/display/toile_dispskel.o \
                 $(BUILD)/common/toile_pattern.o $(BUILD)/common/toile_gpu_client.o
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $^ -lscreen -o $@

$(INPUT_BIN): $(BUILD)/input/toile_input.o $(VIRTIO_OBJS) $(PGW_OBJS)
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $^ $(LDFLAGS) -o $@

# Client de preuve /dev/gpu0 : ouvre le noeud et interroge le pilote.
$(PROBE_BIN): $(PROBE_OBJS) $(BUILD)/common/toile_gpu_client.o
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $^ -o $@

$(BUILD)/display/%.o: $(TOILE_DIR)/display/%.c
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/input/%.o: $(TOILE_DIR)/input/%.c $(PATCH_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -V $(TOILE_VARIANT) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
