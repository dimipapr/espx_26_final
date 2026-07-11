CC := aarch64-linux-gnu-gcc

CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -O2

LDLIBS := -lwebsockets

BUILD_DIR := build
TARGET_NAME := rtes
TARGET := $(BUILD_DIR)/$(TARGET_NAME)

SRC := src/main.c
OBJ := $(BUILD_DIR)/main.o

PI_USER := jim
PI_HOST := 10.19.90.64
PI_PATH := /home/jim/rtes

.PHONY: all clean clean-local clean-remote upload remote-run deploy inspect

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDLIBS)

$(BUILD_DIR)/main.o: $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $(SRC) -o $(BUILD_DIR)/main.o

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

inspect: $(TARGET)
	file $(TARGET)

upload: $(TARGET) clean-remote
	ssh $(PI_USER)@$(PI_HOST) "mkdir -p $(PI_PATH)"
	scp $(TARGET) $(PI_USER)@$(PI_HOST):$(PI_PATH)/

remote-run:
	ssh $(PI_USER)@$(PI_HOST) "$(PI_PATH)/$(TARGET_NAME)"

deploy: upload remote-run

clean-local:
	rm -rf $(BUILD_DIR)

clean-remote:
	ssh $(PI_USER)@$(PI_HOST) "rm -rf $(PI_PATH)"

clean: clean-local clean-remote