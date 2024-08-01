BUILD_DIR?=build/
ASM?=nasm

.PHONY: all clean

all: stage1

stage1: $(BUILD_DIR)/stage1.bin

$(BUILD_DIR)/stage1.bin:
	$(ASM) boot.asm -f bin -o $(BUILD_DIR)/stage1.bin
	echo "--> Compiled:  boot.asm"
	echo "--> Created:   stage1.bin\n"

clean:
	rm -f $(BUILD_DIR)/stage1.bin