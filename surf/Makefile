# Native ARMv7 build on Surf; original ViewMD remains unchanged.
CC ?= cc
PKG_CONFIG ?= pkg-config
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-3.0)
CFLAGS ?= -Os -g0 -Wall -Wextra
ROOT := ..
SRC := $(filter-out $(ROOT)/src/app.c $(ROOT)/src/window.c $(ROOT)/src/main.c,$(wildcard $(ROOT)/src/*.c))
SOURCES := surf_ai.c $(SRC) $(ROOT)/src/md4c/md4c.c
TARGET := surf-ai

.PHONY: all clean check
all: $(TARGET)

check:
	@echo "Arquitectura: $$(uname -m)"
	@$(PKG_CONFIG) --modversion gtk+-3.0
	@echo "GTK3 disponible"

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(SOURCES) -o $(TARGET) $(GTK_LIBS)

clean:
	rm -f $(TARGET)
