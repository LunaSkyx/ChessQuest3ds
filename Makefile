# Lichess3DS - build with devkitARM (run through tools/build.ps1 or an msys2 shell with DEVKITPRO set)
.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM not set")
endif
include $(DEVKITARM)/3ds_rules

TARGET   := Lichess3DS
BUILD    := build
SOURCES  := source
ROMFS    := romfs
APP_TITLE  := Chess Quest 3DS
APP_DESC   := Chess: Lichess, FICS, bots, puzzles
APP_AUTHOR := Chess Quest
ICON     := tools/icon.png

ARCH   := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -g -Wall -Wno-unused-function -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) \
          -std=gnu11 -D__3DS__ -I$(PORTLIBS)/include -I$(CTRULIB)/include -Isource $(EXTRA_CFLAGS)
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map
LIBS   := -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -ljansson -lz -lctru -lm
LIBPATHS := -L$(PORTLIBS)/lib -L$(CTRULIB)/lib

CFILES := $(wildcard $(SOURCES)/*.c)
OFILES := $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))

.PHONY: all clean
all: $(TARGET).3dsx

$(BUILD)/%.o: $(SOURCES)/%.c | $(BUILD)
	@echo CC $<
	@$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $@

$(TARGET).elf: $(OFILES)
	@echo LD $@
	@$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

$(TARGET).smdh: $(ICON)
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $(ICON) $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh $(shell find $(ROMFS) -type f 2>/dev/null)
	@3dsxtool $< $@ --smdh=$(TARGET).smdh --romfs=$(ROMFS)
	@echo built $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh

-include $(OFILES:.o=.d)
