# OpenDispense 2
#

OBJ := main.o server.o logging.o
OBJ += dispense.o cokebank.o itemdb.o
BIN := ../dispsrv

LINKFLAGS := -g
CPPFLAGS := 
CFLAGS := -Wall -g

.PHONY: all clean

all: $(BIN)

clean:
	$(RM) $(BIN) $(OBJ)

$(BIN): $(OBJ)
	$(CC) -o $(BIN) $(LINKFLAGS) $(OBJ)

