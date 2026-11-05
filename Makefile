# =====================================================================
#  OS Challenge - skavira
#
#  make                          the submitted server (./server) - what the graders run
#  make baseline                 bin/server-baseline: root server.c, no experiment flags
#  make experiments              baseline + every experiment in experiments/
#  make experiments EXP=cache    baseline + only experiments/cache (EXP may list several)
#  make concurrency              same as EXP=concurrency (every experiment is a target)
#  make list                     show experiments and the binaries they build
#  make new-experiment EXP=name  create experiments/name/ with a starting variant
#  make clean                    remove ./server and bin/
#  DEBUG=1                       add to any command to print every request
#                                (slow - never use it for measurements)
#
#  Experiment convention (no Makefile changes needed for a new experiment):
#    experiments/<exp>/server_<variant>.c   one main() per variant
#                                          -> bin/<exp>/server-<variant>
#    experiments/<exp>/*.c (other files)   extra sources linked into every variant
#    experiments/<exp>/*.h                 headers (the folder is on the include path)
#    experiments/<exp>/config.mk           optional, e.g.  CFLAGS_cache = -DUSE_CACHE   (all variants)
#                                          or CFLAGS_cache_pool = -DUSE_CACHE  (only server_pool.c)
#  Common sources are compiled together with each variant (no shared .o files), so an
#  experiment's flags also apply to src/server_helpers.c (e.g. #ifdef USE_CACHE hooks).
# =====================================================================

#'make' always builds submitted server, even with changed config.mk rules
.DEFAULT_GOAL := all

CC ?= gcc
CFLAGS = -Wall -Wextra -O3 -pthread -Iinclude
LDFLAGS = -pthread

# make DEBUG=1 prints every request (is slow)
ifdef DEBUG
CFLAGS += -DDEBUG
endif

TARGET = server
COMMON_SRCS = src/server_helpers.c
HDRS = $(wildcard include/*.h)

EXP_ROOT = experiments
BIN_DIR = bin

# Every folder in experiments/ that contains a server_*.c is an experiment
ALL_EXPS := $(sort $(notdir $(patsubst %/,%,$(dir $(wildcard $(EXP_ROOT)/*/server_*.c)))))
EXP ?= $(ALL_EXPS)

-include $(wildcard $(EXP_ROOT)/*/config.mk)

# ---------------------------
# Submitted server and baseline
# ---------------------------
all: $(TARGET)

$(TARGET): server.c $(COMMON_SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ server.c $(COMMON_SRCS) $(LDFLAGS) $(LDLIBS)

baseline: $(BIN_DIR)/server-baseline

$(BIN_DIR)/server-baseline: server.c $(COMMON_SRCS) $(HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ server.c $(COMMON_SRCS) $(LDFLAGS) $(LDLIBS)

# ----------------------------
# Experiments
# ----------------------------
define EXP_RULES
$(1)_MAINS := $$(wildcard $(EXP_ROOT)/$(1)/server_*.c)
$(1)_EXTRA := $$(filter-out $$($(1)_MAINS),$$(wildcard $(EXP_ROOT)/$(1)/*.c))
$(1)_HDRS  := $$(wildcard $(EXP_ROOT)/$(1)/*.h)
$(1)_BINS  := $$(patsubst $(EXP_ROOT)/$(1)/server_%.c,$(BIN_DIR)/$(1)/server-%,$$($(1)_MAINS))

$(BIN_DIR)/$(1)/server-%: $(EXP_ROOT)/$(1)/server_%.c $$($(1)_EXTRA) $$($(1)_HDRS) $(COMMON_SRCS) $(HDRS) $$(wildcard $(EXP_ROOT)/$(1)/config.mk)
	@mkdir -p $$(@D)
	$$(CC) $$(CFLAGS) $$(CFLAGS_$(1)) $$(CFLAGS_$(1)_$$*) -I$(EXP_ROOT)/$(1) -o $$@ $$< $$($(1)_EXTRA) $(COMMON_SRCS) $$(LDFLAGS) $$(LDLIBS) $$(LDLIBS_$(1))

$(1): $$($(1)_BINS)
.PHONY: $(1)
endef

$(foreach e,$(ALL_EXPS),$(eval $(call EXP_RULES,$(e))))

experiments: check-exp baseline $(EXP)

check-exp:
	@$(if $(filter-out $(ALL_EXPS),$(EXP)),echo "Unknown experiment: $(filter-out $(ALL_EXPS),$(EXP)). Available: $(ALL_EXPS)"; exit 1,true)

list:
	@echo "Experiments in $(EXP_ROOT)/ (build one with: make experiments EXP=<name>)"
	@$(foreach e,$(ALL_EXPS),echo "  $(e):$(if $(CFLAGS_$(e)), [CFLAGS: $(CFLAGS_$(e))],)"; $(foreach b,$($(e)_BINS),echo "    $(b)";))
	@echo "Baseline: $(BIN_DIR)/server-baseline"

# Scaffold a new experiment: folder + a first variant copied from the current server.c
new-experiment:
	@test "$(origin EXP)" = "command line" -a "$(words $(EXP))" = 1 || { echo "Usage: make new-experiment EXP=<new-name>"; exit 1; }
	@test ! -e $(EXP_ROOT)/$(EXP) || { echo "$(EXP_ROOT)/$(EXP) already exists"; exit 1; }
	mkdir -p $(EXP_ROOT)/$(EXP)/results
	cp server.c $(EXP_ROOT)/$(EXP)/server_$(EXP).c
	printf '# Settings for experiment "%s" (all optional)\n# CFLAGS_%s = -DSOMETHING\n# LDLIBS_%s =\n' $(EXP) $(EXP) $(EXP) > $(EXP_ROOT)/$(EXP)/config.mk
	@echo "Created $(EXP_ROOT)/$(EXP)/ - build with: make experiments EXP=$(EXP)"

clean:
	rm -f $(TARGET) *.o
	rm -rf $(BIN_DIR)

.PHONY: all baseline experiments check-exp list new-experiment clean