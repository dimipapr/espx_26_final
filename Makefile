CC := aarch64-linux-gnu-gcc

CFLAGS := -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread

LDLIBS := -lwebsockets -pthread

BUILD_DIR := build
TARGET_NAME := rtes
TARGET := $(BUILD_DIR)/$(TARGET_NAME)

OBJ := $(BUILD_DIR)/main.o $(BUILD_DIR)/queue.o $(BUILD_DIR)/event_classifier.o 

PI_USER := jim
PI_HOST := 10.19.90.64
PI_PATH := /home/jim/rtes
SERVICE_FILE := rtes.service
SERVICE_NAME := rtes.service

.PHONY: all clean clean-local clean-remote upload deploy inspect \
	service-configure service-start service-stop service-status service-logs

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDLIBS)

$(BUILD_DIR)/queue.o: src/queue.c src/queue.h src/message.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/queue.c -o $(BUILD_DIR)/queue.o

$(BUILD_DIR)/event_classifier.o: src/event_classifier.c src/event_classifier.h src/jsmn.h src/message.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/event_classifier.c -o $(BUILD_DIR)/event_classifier.o

$(BUILD_DIR)/main.o: src/main.c src/queue.h src/message.h src/event_classifier.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/main.c -o $(BUILD_DIR)/main.o

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

inspect: $(TARGET)
	file $(TARGET)

upload: $(TARGET)
	ssh $(PI_USER)@$(PI_HOST) "mkdir -p $(PI_PATH)"
	scp $(TARGET) $(PI_USER)@$(PI_HOST):$(PI_PATH)/
	scp $(SERVICE_FILE) $(PI_USER)@$(PI_HOST):/tmp/$(SERVICE_FILE)
	ssh -t $(PI_USER)@$(PI_HOST) \
		"sudo mv /tmp/$(SERVICE_FILE) /etc/systemd/system/$(SERVICE_FILE) && \
		 sudo systemctl daemon-reload"

service-configure: upload
	ssh -t $(PI_USER)@$(PI_HOST) \
		"sudo systemctl enable --now $(SERVICE_NAME)"

service-start:
	ssh -t $(PI_USER)@$(PI_HOST) \
		"sudo systemctl start $(SERVICE_NAME)"

service-stop:
	ssh -t $(PI_USER)@$(PI_HOST) \
		"sudo systemctl stop $(SERVICE_NAME)"

service-status:
	ssh -t $(PI_USER)@$(PI_HOST) \
		"sudo systemctl status $(SERVICE_NAME)"

service-logs:
	ssh -t $(PI_USER)@$(PI_HOST) \
		"journalctl -u $(SERVICE_NAME) -f"

deploy: upload
	ssh -t $(PI_USER)@$(PI_HOST) \
		"sudo systemctl restart $(SERVICE_NAME)"

remote-run:
	ssh -t $(PI_USER)@$(PI_HOST) "$(PI_PATH)/rtes"

deploy: upload

clean-local:
	rm -rf $(BUILD_DIR)

clean-remote:
	ssh -t $(PI_USER)@$(PI_HOST) \
		"sudo systemctl disable --now $(SERVICE_NAME) 2>/dev/null || true; \
		 sudo rm -f /etc/systemd/system/$(SERVICE_FILE); \
		 sudo systemctl daemon-reload; \
		 rm -f $(PI_PATH)/$(TARGET_NAME)"

clean: clean-local