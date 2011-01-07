# OpenDispense 2
#

OBJ := main.o server.o logging.o
OBJ += dispense.o itemdb.o
OBJ += handler_coke.o handler_snack.o handler_door.o
BIN := ../../dispsrv

LINKFLAGS := -g ../../cokebank.so -lldap
CPPFLAGS := 
CFLAGS := -Wall -g

.PHONY: all clean

all: $(BIN)

clean:
	$(RM) $(BIN) $(OBJ)

$(BIN): $(OBJ)
	$(CC) -o $(BIN) $(LINKFLAGS) $(OBJ)

