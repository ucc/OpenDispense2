# OpenDispense 2
#

OBJ := main.o server.o logging.o
OBJ += dispense.o itemdb.o
OBJ += handler_coke.o handler_snack.o handler_door.o
BIN := ../../dispsrv

DEPFILES := $(OBJ:%.o=%.d)

LINKFLAGS := -g ../../cokebank.so
CPPFLAGS := 
CFLAGS := -Wall -Wextra -Werror -g

.PHONY: all clean

all: $(BIN)

clean:
	$(RM) $(BIN) $(OBJ)

$(BIN): $(OBJ)
	$(CC) -o $(BIN) $(LINKFLAGS) $(OBJ)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)
	@cpp $< -MM -MF $*.d
	
-include $(DEPFILES)

