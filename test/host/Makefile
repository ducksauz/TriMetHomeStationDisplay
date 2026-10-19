# Host-side unit tests (no ESP-IDF build needed, only its bundled cJSON).
#   make -C test/host

IDF_PATH ?= $(HOME)/esp/esp-idf
CJSON_DIR := $(IDF_PATH)/components/json/cJSON
SRC_DIR   := ../../main/trimet

SAN    := -fsanitize=address,undefined
CFLAGS += -std=gnu17 -g $(SAN) -I$(SRC_DIR) -I$(CJSON_DIR)
STRICT := -Wall -Wextra -Werror

BIN := build/test_trimet
SRCS := $(SRC_DIR)/trimet_parse.c $(SRC_DIR)/trimet_format.c

.PHONY: test clean
test: $(BIN)
	./$(BIN)

# Third-party cJSON: build without our strict warnings.
build/cJSON.o: $(CJSON_DIR)/cJSON.c
	@mkdir -p build
	$(CC) $(CFLAGS) -w -c -o $@ $<

$(BIN): test_trimet.c $(SRCS) $(wildcard $(SRC_DIR)/*.h) build/cJSON.o
	$(CC) $(CFLAGS) $(STRICT) -o $@ test_trimet.c $(SRCS) build/cJSON.o -lm

clean:
	rm -rf build
