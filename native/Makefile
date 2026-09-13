LLVM_CONFIG ?= $(shell command -v llvm-config-21 || command -v llvm-config21 || command -v llvm-config)
CC          ?= cc
AR          ?= ar

CFLAGS := -O2 -fPIC -Wall -Wextra $(shell $(LLVM_CONFIG) --cflags) $(CFLAGS)

OBJ := obsidian_shim.o
LIB := libobsidian_shim.a

.PHONY: all clean check-llvm-config

all: check-llvm-config $(LIB)

check-llvm-config:
	@if [ -z "$(LLVM_CONFIG)" ]; then \
		echo "error: nie znaleziono llvm-config dla LLVM 21."; \
		echo "  zainstaluj pakiet dev LLVM 21 (np. 'apt install llvm-21-dev'"; \
		echo "  albo 'brew install llvm@21') lub podaj LLVM_CONFIG= ręcznie."; \
		exit 1; \
	fi
	@ver="$$($(LLVM_CONFIG) --version)"; \
	case "$$ver" in \
		21.*) ;; \
		*) echo "warning: llvm-config zgłasza wersję $$ver, nie 21.x — Obsidian 0.1.x jest pisany pod LLVM 21. Może się skompilować, ale bez gwarancji." ;; \
	esac

$(LIB): $(OBJ)
	$(AR) rcs $(LIB) $(OBJ)

obsidian_shim.o: obsidian_shim.c obsidian_shim.h
	$(CC) $(CFLAGS) -c obsidian_shim.c -o $(OBJ)

clean:
	rm -f $(OBJ) $(LIB)
