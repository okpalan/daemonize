
BIN=bin
OBJ=obj
SRC=src
INCLUDE=$(SRC)/include

CFLAGS=-Wall -std=gnu99 -lpthread

SOURCES=$(wildcard $(SRC)/*.c)
OBJECTS=$(patsubst $(SRC)/%.c,$(OBJ)/%.o,$(SOURCES))
BINS=$(patsubst $(SRC)/%.c,$(BIN)/%,$(SOURCES))

$(OBJECTS): $(OBJ)/%.o : $(SRC)/%.c
	@mkdir -p $(OBJ)
	gcc $(CFLAGS) -I$(INCLUDE) -c $< -o $@

$(BINS): $(BIN)/% : $(OBJ)/%.o
	@mkdir -p $(BIN)
	gcc $(CFLAGS) $< -o $@

.PHONY: all

all: $(BINS)

.PHONY: clean
clean:
	@rm -rf $(OBJ) $(BIN)