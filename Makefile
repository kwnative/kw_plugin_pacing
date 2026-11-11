# SPDX-FileCopyrightText: 2026 N.Leiten
# SPDX-License-Identifier: LicenseRef-KW-Game-Plugin
REPO_ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KWMOD_SDK_ROOT ?= $(abspath $(REPO_ROOT)/../kw_modloader)
include $(KWMOD_SDK_ROOT)/mk/plugin.mk
.DEFAULT_GOAL := all
CFLAGS := $(filter-out -Wpedantic,$(CFLAGS))
PACING_SOURCES := src/pacing.c
PACING_OBJECTS := $(call objects,$(PACING_SOURCES))
ALL_OBJECTS := $(sort $(PACING_OBJECTS))
BIN_TARGETS := $(BIN_DIR)/kw_pacing.dll
CONFIG_INPUTS := $(wildcard $(REPO_ROOT)/config/*)
CONFIG_TARGETS := $(patsubst $(REPO_ROOT)/config/%,$(CONFIG_DIR)/%,$(CONFIG_INPUTS))
.PHONY: all config developer clean
all: $(BIN_DIR)/kw_pacing.dll config
developer: $(BIN_TARGETS) config
config: $(CONFIG_TARGETS)
$(CONFIG_DIR)/%: $(REPO_ROOT)/config/%
	@mkdir -p "$(@D)"
	cp "$<" "$@"
ENGINE_ARTIFACTS := $(KWMOD_ENGINE_DLL)
include $(KWMOD_SDK_ROOT)/mk/sdk_dependencies.mk
$(ENGINE_ARTIFACTS): | $(SDK_ARTIFACT_STAMP)
	@test -f "$@"
$(BIN_DIR)/kw_pacing.dll: $(PACING_OBJECTS) $(KWMOD_ENGINE_DLL)
	@mkdir -p "$(@D)"
	$(WINDOWS_CC) $(DLL_FLAGS) $(PACING_OBJECTS) -lwinmm -o "$@"
clean:
	$(RM) $(BIN_TARGETS) $(ALL_OBJECTS) $(ALL_OBJECTS:.o=.d) $(CONFIG_TARGETS)
-include $(ALL_OBJECTS:.o=.d)

BUNDLE_FILES := $(BIN_DIR)/kw_pacing.dll=plugins/kw_pacing.dll \
    $(REPO_ROOT)/config/kw_pacing.kwplugin=plugins/kw_pacing.kwplugin \
    $(REPO_ROOT)/config/kw_pacing.ini=plugins/kw_pacing.ini
include $(KWMOD_SDK_ROOT)/mk/bundle.mk
