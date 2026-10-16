ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO before building)
endif
TARGET := switch-ai-chat
BUILD := build
SOURCES := source
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -O2 -Wall -Wextra -ffunction-sections $(ARCH) -D__SWITCH__ -I$(DEVKITPRO)/libnx/include -I$(DEVKITPRO)/portlibs/switch/include/SDL2 -I$(DEVKITPRO)/portlibs/switch/include
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -L$(DEVKITPRO)/libnx/lib -L$(DEVKITPRO)/portlibs/switch/lib
LIBS := -lcurl -lSDL2_ttf -lSDL2 -lEGL -lglapi -ldrm_nouveau -lharfbuzz -lfreetype -lbz2 -lpng16 -lz -lstdc++ -lm -lnx -lpthread
APP_TITLE := Switch AI Chat
APP_AUTHOR := hero
APP_VERSION := 0.9.3
APP_ICON := $(CURDIR)/icon.jpg
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(CURDIR)/$(SOURCES)
export DEPSDIR := $(CURDIR)/$(BUILD)
export OFILES := $(patsubst %.c,%.o,$(notdir $(wildcard $(SOURCES)/*.c))) $(patsubst %.s,%.o,$(notdir $(wildcard $(SOURCES)/*.s)))
export NROFLAGS := --nacp=$(CURDIR)/$(TARGET).nacp --icon=$(APP_ICON) --romfsdir=$(CURDIR)/romfs
.PHONY: all clean $(BUILD)
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp
else
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)
endif
include $(DEVKITPRO)/libnx/switch_rules
LD = $(CC)
