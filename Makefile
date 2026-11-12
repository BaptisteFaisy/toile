# SPDX-License-Identifier: MIT
#
# Toile — Makefile (CONTRACT.md §12). Cibles gelées :
#   help build test-c test-py diff golden guards test qemu-demo bench clean
# Aucune dépendance externe à QNX/QEMU/GPU pour `make test` (S9).
SHELL := /bin/bash
CC := gcc
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Icore/include
BUILD := build
OBJDIR := $(BUILD)/obj

SRC_CORE := $(wildcard core/src/*.c)
SRC_CLI := $(wildcard core/cli/*.c)
HDRS := $(wildcard core/include/toile/*.h) core/cli/cli_internal.h
OBJ_CORE := $(patsubst %.c,$(OBJDIR)/%.o,$(SRC_CORE))
OBJ_CLI := $(patsubst %.c,$(OBJDIR)/%.o,$(SRC_CLI))
SRC_TESTS := $(wildcard core/tests/test_*.c)
TEST_BINS := $(patsubst core/tests/%.c,$(BUILD)/%,$(SRC_TESTS))

.DEFAULT_GOAL := help
.PHONY: help build test-c test-py diff golden guards test qemu-demo bench clean

help: ## liste les cibles disponibles
	@grep -hE '^[a-zA-Z0-9_-]+:.*?## ' $(MAKEFILE_LIST) | sort | \
	  awk 'BEGIN {FS = ":.*?## "} {printf "  %-10s %s\n", $$1, $$2}'

build: $(BUILD)/toile-run $(TEST_BINS) ## compile le CLI et les binaires de test

$(OBJDIR)/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/toile-run: $(OBJ_CLI) $(OBJ_CORE)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/test_%: core/tests/test_%.c $(OBJ_CORE) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(OBJ_CORE)

test-c: build ## tests unitaires C (core/tests/)
	@if [ -z "$(strip $(TEST_BINS))" ]; then \
	  echo "test-c: aucun test C trouve dans core/tests/"; exit 2; \
	fi
	@fail=0; for t in $(TEST_BINS); do \
	  echo "== $$t"; \
	  $$t || fail=1; \
	done; \
	if [ "$$fail" -ne 0 ]; then echo "test-c: echec"; exit 1; fi

test-py: ## tests unitaires Python (host/tests/, stdlib unittest)
	@test -d host/tests || { echo "test-py: host/tests/ manquant"; exit 2; }
	cd host && python3 -m unittest discover -s tests -q

diff: ## differentiel C<->Python sur les fixtures, octet pour octet
	@test -f tests/differential.py || { echo "diff: tests/differential.py manquant"; exit 2; }
	python3 tests/differential.py

golden: ## regenere fixtures/golden/ (reference C)
	@test -f tests/differential.py || { echo "golden: tests/differential.py manquant"; exit 2; }
	python3 tests/differential.py --write-golden

guards: ## gardes CI : taille des fichiers + API gelee
	@test -f .github/checks/taille.py || { echo "guards: .github/checks/taille.py manquant"; exit 2; }
	python3 .github/checks/taille.py
	@test -f .github/checks/api-frozen.py || { echo "guards: .github/checks/api-frozen.py manquant"; exit 2; }
	python3 .github/checks/api-frozen.py

test: build test-c test-py diff guards ## suite complete, verte sans QNX/QEMU/GPU

qemu-demo: ## volet A : exige SDP QNX + QEMU (jamais appele en CI PR)
	@if ! command -v qcc >/dev/null 2>&1; then \
	  echo "qemu-demo: SDP QNX absent (qcc introuvable) - volet A non execute"; \
	  exit 0; \
	fi; \
	if [ -z "$$(ls -A image 2>/dev/null)" ]; then \
	  echo "qemu-demo: image QNX absente (image/ vide) - volet A non execute"; \
	  exit 0; \
	fi; \
	if [ ! -f qemu/run-demo.sh ]; then \
	  echo "qemu-demo: script de lancement absent (qemu/run-demo.sh) - volet A non execute"; \
	  exit 0; \
	fi; \
	echo "qemu-demo: lancement de qemu/run-demo.sh"; \
	qemu/run-demo.sh

bench: $(BUILD)/bench-model ## debit du coeur portable (essai E5, hors CI PR)
	@$(BUILD)/bench-model

$(BUILD)/bench-model: tools/bench-model.c $(OBJ_CORE) $(HDRS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(OBJ_CORE)

clean: ## supprime build/ et les __pycache__
	rm -rf $(BUILD)
	find . -type d -name __pycache__ -prune -exec rm -rf {} +
