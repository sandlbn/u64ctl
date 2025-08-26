# Makefile for Ultimate64 Library and Tools
# For Amiga OS 3.x cross-compilation

# Detect host OS
OS := $(shell uname)

# CPU configuration (can be overridden)
CPU ?= 68000
CPU_FLAGS =

# Directories
LIBDIR = /opt/amiga/m68k-amigaos/lib
SDKDIR = /opt/amiga/m68k-amigaos/sys-include
NDKDIR = /opt/amiga/m68k-amigaos/ndk-include
INCDIR = /opt/amiga/m68k-amigaos/include
SRCDIR = src
LIBSRCDIR = lib
BUILDDIR = build
OBJDIR = $(BUILDDIR)/obj
LIBOBJDIR = $(BUILDDIR)/libobj
OUTDIR = out
DISTDIR = dist
DOCDIR = docs

# Program settings
CC = m68k-amigaos-gcc
AR = m68k-amigaos-ar
RANLIB = m68k-amigaos-ranlib
STRIP = m68k-amigaos-strip

# Output names
CLI_PROGRAM = u64ctl
MUI_PROGRAM = u64ctlMUI  
SIDPLAYER_PROGRAM = u64sidplayer
LIBRARY_NAME = libultimate64.a
VERSION = 0.3.0

# Library source files
LIB_SOURCES = \
	$(LIBSRCDIR)/ultimate64_lib.c \
	$(LIBSRCDIR)/ultimate64_petscii.c \
	$(LIBSRCDIR)/ultimate64_network.c \
	$(LIBSRCDIR)/ultimate64_json.c \
	$(LIBSRCDIR)/ultimate64_http.c \
	$(LIBSRCDIR)/ultimate64_config.c \
	$(LIBSRCDIR)/ultimate64_drives.c \
	$(LIBSRCDIR)/ultimate64_utils.c

# CLI program source files
CLI_SOURCES = \
	$(SRCDIR)/main_cli.c

# MUI program source files (original MUI tool)
MUI_SOURCES = \
	$(SRCDIR)/main_mui.c

# SID Player source files (your player application)
SIDPLAYER_SOURCES = \
	$(SRCDIR)/main_player.c

# Test program source files
TEST_SOURCES = \
	$(SRCDIR)/test_ultimate64.c

# Generate object files lists
LIB_OBJECTS = $(patsubst $(LIBSRCDIR)/%.c,$(LIBOBJDIR)/%.o,$(LIB_SOURCES))
CLI_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CLI_SOURCES))
MUI_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(MUI_SOURCES))
SIDPLAYER_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SIDPLAYER_SOURCES))
TEST_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(TEST_SOURCES))

# Include directories
INCLUDES = -I$(INCDIR) -I$(SDKDIR) -I$(NDKDIR) -Iinclude

# Base compiler flags (common to both debug and release)
BASE_CFLAGS = \
	$(CPU_FLAGS) \
	-Wall -Wextra \
	-Wno-unused-function \
	-Wno-discarded-qualifiers \
	-Wno-pointer-sign \
	-Wno-int-conversion \
	-Wno-volatile-register-var \
	-fno-strict-aliasing \
	-fbaserel \
	-noixemul \
	-DUSE_BSDSOCKET \
	-D__AMIGAOS3__ \
	$(INCLUDES)

# Libraries
BASE_LIBS = -lamiga -lm
NETWORK_LIBS = 
JSON_LIBS = -ljson-c
MUI_LIBS = -lmui

ifdef DEBUG
# Debug build: Enable DEBUG macro for DPRINTF output
CFLAGS = $(BASE_CFLAGS) -DDEBUG -DDEBUG_BUILD -O0 -g
BUILD_TYPE = debug
STRIP_CMD = @echo "Debug build - not stripping"
else
# Release build: NO DEBUG macro, so DPRINTF becomes empty
CFLAGS = $(BASE_CFLAGS) -O2 -fomit-frame-pointer -DNDEBUG
BUILD_TYPE = release
STRIP_CMD = $(STRIP)
endif

# Linker flags
LDFLAGS = -noixemul -fbaserel -L$(LIBDIR)

# Optional feature flags
ifdef U64_ASYNC_SUPPORT
CFLAGS += -DU64_ASYNC_SUPPORT
endif

ifdef U64_VICSTREAM_SUPPORT
CFLAGS += -DU64_VICSTREAM_SUPPORT
endif

# Dependency generation
DEPFLAGS = -MP -MMD

# Colors for output (optional)
ifdef USE_COLORS
RED = \033[0;31m
GREEN = \033[0;32m
YELLOW = \033[0;33m
BLUE = \033[0;34m
MAGENTA = \033[0;35m
CYAN = \033[0;36m
WHITE = \033[0;37m
RESET = \033[0m
else
RED =
GREEN =
YELLOW =
BLUE =
MAGENTA =
CYAN =
WHITE =
RESET =
endif

# Default target
.PHONY: all
all: library cli

# Help target
.PHONY: help
help:
	@echo "$(CYAN)Ultimate64 Control Library for Amiga OS 3.x$(RESET)"
	@echo "$(WHITE)============================================$(RESET)"
	@echo ""
	@echo "$(YELLOW)Available targets:$(RESET)"
	@echo "  $(GREEN)all$(RESET)       - Build library and CLI tool (default) [RELEASE MODE]"
	@echo "  $(GREEN)debug$(RESET)     - Build everything in debug mode (with DPRINTF output)"
	@echo "  $(GREEN)release$(RESET)   - Build everything in release mode (no debug output)"
	@echo "  $(GREEN)library$(RESET)   - Build static library"
	@echo "  $(GREEN)cli$(RESET)       - Build CLI tool (u64ctl)"
	@echo "  $(GREEN)mui$(RESET)       - Build MUI control tool (u64ctlMUI)"
	@echo "  $(GREEN)sidplayer$(RESET) - Build SID Player (u64sidplayer)"
	@echo "  $(GREEN)test$(RESET)      - Build test program"
	@echo "  $(GREEN)complete$(RESET)  - Build everything (all 3 programs)"
	@echo "  $(GREEN)clean$(RESET)     - Remove all build files"
	@echo "  $(GREEN)dist$(RESET)      - Create distribution archive"
	@echo "  $(GREEN)install$(RESET)   - Install to Amiga (requires UAE or real hardware)"
	@echo ""
	@echo "$(YELLOW)Build modes:$(RESET)"
	@echo "  $(BLUE)make$(RESET)                 - Release build (no debug output)"
	@echo "  $(BLUE)make DEBUG=1$(RESET)         - Debug build (with DPRINTF output)"
	@echo "  $(BLUE)make debug$(RESET)           - Force debug build"
	@echo "  $(BLUE)make release$(RESET)         - Force release build"
	@echo ""
	@echo "$(YELLOW)Build options:$(RESET)"
	@echo "  $(BLUE)CPU=68030$(RESET)            - Target CPU (68000/68020/68030/68040/68060)"
	@echo "  $(BLUE)USE_BSDSOCKET=1$(RESET)      - Enable bsdsocket.library support"
	@echo "  $(BLUE)U64_ASYNC_SUPPORT=1$(RESET)  - Enable async operations"
	@echo "  $(BLUE)U64_VICSTREAM_SUPPORT=1$(RESET) - Enable VIC stream support"
	@echo ""
	@echo "$(YELLOW)Examples:$(RESET)"
	@echo "  make clean release      # Clean release build (all programs, no debug)"
	@echo "  make clean debug        # Clean debug build (all programs, with debug)"
	@echo "  make clean cli          # Build CLI tool only (release)"
	@echo "  make clean mui          # Build MUI control tool only (release)"
	@echo "  make clean sidplayer    # Build SID player only (release)"
	@echo "  make DEBUG=1 sidplayer  # Debug build of SID player"
	@echo "  make CPU=68000 dist     # Release distribution for 68000"

# Explicit debug target
.PHONY: debug
debug:
	@$(MAKE) DEBUG=1 complete

# Explicit release target  
.PHONY: release
release:
	@$(MAKE) complete

# Create directories
.PHONY: dirs
dirs:
	@echo "$(BLUE)[DIR]$(RESET) Creating build directories... ($(BUILD_TYPE) mode)"
	@mkdir -p $(OBJDIR)
	@mkdir -p $(LIBOBJDIR)
	@mkdir -p $(OUTDIR)
	@mkdir -p $(DISTDIR)
	@mkdir -p include

# Build static library
.PHONY: library
library: dirs $(OUTDIR)/$(LIBRARY_NAME)

$(OUTDIR)/$(LIBRARY_NAME): $(LIB_OBJECTS)
	@echo "$(GREEN)[AR]$(RESET) Creating static library $@ ($(BUILD_TYPE))"
	$(AR) rcs $@ $(LIB_OBJECTS)
	$(RANLIB) $@
	@echo "$(GREEN)[OK]$(RESET) Library built: $@"

# Build CLI program
.PHONY: cli
cli: dirs library $(OUTDIR)/$(CLI_PROGRAM)

$(OUTDIR)/$(CLI_PROGRAM): $(CLI_OBJECTS) $(OUTDIR)/$(LIBRARY_NAME)
	@echo "$(GREEN)[LD]$(RESET) Linking CLI program $@ ($(BUILD_TYPE))"
	$(CC) $(CFLAGS) $(LDFLAGS) $(CLI_OBJECTS) -L$(OUTDIR) -lultimate64 \
		$(BASE_LIBS) $(JSON_LIBS) -o $@
	$(STRIP_CMD) $@
	@echo "$(GREEN)[OK]$(RESET) CLI program built: $@ ($(BUILD_TYPE))"

# Build MUI control tool
.PHONY: mui
mui: dirs library $(OUTDIR)/$(MUI_PROGRAM)

$(OUTDIR)/$(MUI_PROGRAM): $(MUI_OBJECTS) $(OUTDIR)/$(LIBRARY_NAME)
	@echo "$(GREEN)[LD]$(RESET) Linking MUI control tool $@ ($(BUILD_TYPE))"
	$(CC) $(CFLAGS) $(LDFLAGS) $(MUI_OBJECTS) -L$(OUTDIR) -lultimate64 \
		$(BASE_LIBS) $(JSON_LIBS) $(MUI_LIBS) -o $@
	$(STRIP_CMD) $@
	@echo "$(GREEN)[OK]$(RESET) MUI control tool built: $@ ($(BUILD_TYPE))"

# Build SID Player
.PHONY: sidplayer
sidplayer: dirs library $(OUTDIR)/$(SIDPLAYER_PROGRAM)

$(OUTDIR)/$(SIDPLAYER_PROGRAM): $(SIDPLAYER_OBJECTS) $(OUTDIR)/$(LIBRARY_NAME)
	@echo "$(GREEN)[LD]$(RESET) Linking SID Player $@ ($(BUILD_TYPE))"
	$(CC) $(CFLAGS) $(LDFLAGS) $(SIDPLAYER_OBJECTS) -L$(OUTDIR) -lultimate64 \
		$(BASE_LIBS) $(JSON_LIBS) $(MUI_LIBS) -o $@
	$(STRIP_CMD) $@
	@echo "$(GREEN)[OK]$(RESET) SID Player built: $@ ($(BUILD_TYPE))"

# Build test program
.PHONY: test
test: dirs library $(OUTDIR)/test_ultimate64

$(OUTDIR)/test_ultimate64: $(TEST_OBJECTS) $(OUTDIR)/$(LIBRARY_NAME)
	@echo "$(GREEN)[LD]$(RESET) Linking test program $@ ($(BUILD_TYPE))"
	$(CC) $(CFLAGS) $(LDFLAGS) $(TEST_OBJECTS) -L$(OUTDIR) -lultimate64 \
		$(BASE_LIBS) $(NETWORK_LIBS) $(JSON_LIBS) -o $@
	@echo "$(GREEN)[OK]$(RESET) Test program built: $@ ($(BUILD_TYPE))"

# Build everything
.PHONY: complete
complete: library cli mui sidplayer test
	@echo "$(GREEN)[OK]$(RESET) Complete build finished ($(BUILD_TYPE) mode)"

# Compile library source files
$(LIBOBJDIR)/%.o: $(LIBSRCDIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(YELLOW)[CC]$(RESET) Compiling library: $< ($(BUILD_TYPE))"
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

# Compile CLI source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(YELLOW)[CC]$(RESET) Compiling: $< ($(BUILD_TYPE))"
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

# Clean build files
.PHONY: clean
clean:
	@echo "$(RED)[CLEAN]$(RESET) Removing build files..."
	@rm -rf $(BUILDDIR)
	@rm -f $(OUTDIR)/$(LIBRARY_NAME)
	@rm -f $(OUTDIR)/$(CLI_PROGRAM)
	@rm -f $(OUTDIR)/$(MUI_PROGRAM)
	@rm -f $(OUTDIR)/$(SIDPLAYER_PROGRAM)
	@rm -f $(OUTDIR)/test_ultimate64
	@rm -f $(OUTDIR)/*.map
	@echo "$(GREEN)[OK]$(RESET) Clean complete"

# Full clean (including distribution)
.PHONY: distclean
distclean: clean
	@echo "$(RED)[CLEAN]$(RESET) Removing distribution files..."
	@rm -rf $(DISTDIR)
	@rm -rf $(OUTDIR)

# Create distribution archive
.PHONY: dist
dist: release
	@echo "$(CYAN)[DIST]$(RESET) Creating distribution (release mode)..."
	@mkdir -p $(DISTDIR)/Ultimate64_$(VERSION)
	@mkdir -p $(DISTDIR)/Ultimate64_$(VERSION)/bin
	@mkdir -p $(DISTDIR)/Ultimate64_$(VERSION)/lib
	@mkdir -p $(DISTDIR)/Ultimate64_$(VERSION)/include
	@mkdir -p $(DISTDIR)/Ultimate64_$(VERSION)/docs
	@mkdir -p $(DISTDIR)/Ultimate64_$(VERSION)/examples
	
	# Copy binaries
	@cp $(OUTDIR)/$(CLI_PROGRAM) $(DISTDIR)/Ultimate64_$(VERSION)/bin/
	@-cp $(OUTDIR)/$(MUI_PROGRAM) $(DISTDIR)/Ultimate64_$(VERSION)/bin/ 2>/dev/null || true
	@-cp $(OUTDIR)/$(SIDPLAYER_PROGRAM) $(DISTDIR)/Ultimate64_$(VERSION)/bin/ 2>/dev/null || true
	
	# Copy library
	@cp $(OUTDIR)/$(LIBRARY_NAME) $(DISTDIR)/Ultimate64_$(VERSION)/lib/
	
	# Copy headers
	@cp include/*.h $(DISTDIR)/Ultimate64_$(VERSION)/include/
	
	# Copy documentation
	@-cp README.md $(DISTDIR)/Ultimate64_$(VERSION)/ 2>/dev/null || true
	@-cp LICENSE $(DISTDIR)/Ultimate64_$(VERSION)/ 2>/dev/null || true
	@-cp -r $(DOCDIR)/* $(DISTDIR)/Ultimate64_$(VERSION)/docs/ 2>/dev/null || true
	
	# Copy examples
	@-cp $(SRCDIR)/main_mui.c $(DISTDIR)/Ultimate64_$(VERSION)/examples/ 2>/dev/null || true
	@-cp $(SRCDIR)/test_ultimate64.c $(DISTDIR)/Ultimate64_$(VERSION)/examples/ 2>/dev/null || true
	
	# Create LHA archive
	@cd $(DISTDIR) && lha -ao5 Ultimate64_$(VERSION).lha Ultimate64_$(VERSION)
	@echo "$(GREEN)[OK]$(RESET) Distribution created: $(DISTDIR)/Ultimate64_$(VERSION).lha"

# Install to Amiga (requires UAE or network share)
.PHONY: install
install: release
	@echo "$(CYAN)[INSTALL]$(RESET) Installing to Amiga..."
	@echo "This target requires configuration for your specific setup"
	@echo "Edit the Makefile to set your installation path"
	# Example: cp $(OUTDIR)/$(CLI_PROGRAM) /path/to/amiga/c/
	# Example: cp $(OUTDIR)/$(LIBRARY_NAME) /path/to/amiga/libs/

# Generate documentation
.PHONY: docs
docs:
	@echo "$(CYAN)[DOC]$(RESET) Generating documentation..."
	@mkdir -p $(DOCDIR)
	@echo "Documentation generation not yet implemented"

# Show configuration
.PHONY: config
config:
	@echo "$(CYAN)Build Configuration:$(RESET)"
	@echo "  CPU:        $(CPU)"
	@echo "  Build type: $(BUILD_TYPE)"
	@echo "  CC:         $(CC)"
	@echo "  CFLAGS:     $(CFLAGS)"
	@echo "  LDFLAGS:    $(LDFLAGS)"
ifdef DEBUG
	@echo "  DEBUG:      ENABLED (U64_DEBUG will output to console)"
else
	@echo "  DEBUG:      DISABLED (U64_DEBUG statements removed)"
endif

# Analyze code size
.PHONY: size
size: all
	@echo "$(CYAN)[SIZE]$(RESET) Binary sizes:"
	@size $(OUTDIR)/$(CLI_PROGRAM) 2>/dev/null || true
	@size $(OUTDIR)/$(MUI_PROGRAM) 2>/dev/null || true
	@size $(OUTDIR)/$(SIDPLAYER_PROGRAM) 2>/dev/null || true
	@echo ""
	@echo "Library object sizes:"
	@size $(LIB_OBJECTS)

# Run tests (in UAE or on real hardware)
.PHONY: run-test
run-test: test
	@echo "$(CYAN)[TEST]$(RESET) Running tests..."
	@echo "Copy $(OUTDIR)/test_ultimate64 to your Amiga and run it"

# Include dependency files
-include $(LIB_OBJECTS:.o=.d)
-include $(CLI_OBJECTS:.o=.d)
-include $(MUI_OBJECTS:.o=.d)
-include $(SIDPLAYER_OBJECTS:.o=.d)
-include $(TEST_OBJECTS:.o=.d)

# Phony targets summary
.PHONY: all help dirs library cli mui sidplayer test complete clean distclean dist install docs config size run-test debug release