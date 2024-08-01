TARGET_ASMFLAGS += -f elf
TARGET_CFLAGS += -ffreestanding -nostdlib -I.
TARGET_LIBS += -lgcc
TARGET_LINKFLAGS += -T linker.lnk -nostdlib

SOURCES_C=$(shell find . -type f -name "*.c")
HEADERS_C=$(shell find . -type f -name "*.h")
SOURCES_ASM=$(shell find . -type f -name "*.asm")
HEADERS_ASM=$(shell find . -type f -name "*.inc")
OBJECTS_C=$(patsubst %.c, $(BUILD_DIR)/stage2/c/%.obj, $(SOURCES_C))
OBJECTS_ASM=$(patsubst %.asm, $(BUILD_DIR)/stage2/asm/%.obj, $(SOURCES_ASM))

.PHONY: all stage2 clean always

all: stage2

stage2: $(BUILD_DIR)/stage2.bin

$(BUILD_DIR)/stage2.bin: $(OBJECTS_ASM) $(OBJECTS_C)
	$(TARGET_LD) $(TARGET_LINKFLAGS) -Wl,-Map=$(BUILD_DIR)/stage2.map -o $@ $^ $(TARGET_LIBS)
	echo "--> Created:   stage2.bin\n"

$(BUILD_DIR)/stage2/c/%.obj: %.c $(HEADERS_C) always
	$(TARGET_CC) $(TARGET_CFLAGS) -c -o $@ $<
	echo "--> Compiled: " $<

$(BUILD_DIR)/stage2/asm/%.obj: %.asm $(HEADERS_ASM) always
	$(TARGET_ASM) $(TARGET_ASMFLAGS) -o $@ $<
	echo "--> Compiled: " $<

always:
	mkdir -p $(BUILD_DIR)/stage2/c
	mkdir -p $(BUILD_DIR)/stage2/asm

clean:
	rm -f $(BUILD_DIR)/stage2.bin