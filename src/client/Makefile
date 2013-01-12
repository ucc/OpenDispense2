
CFLAGS := -Wall -Werror -g -std=gnu99
LDFLAGS := -g -lncurses
# -lssl

BIN := ../../dispense
OBJ := main.o protocol.o menu.o

DEPFILES := $(OBJ:%.o=%.d)

.PHONY: all clean

all: $(BIN)

clean:
	$(RM) $(BIN) $(OBJ) $(DEPFILES)

$(BIN): $(OBJ)
	$(CC) -o $(BIN) $(OBJ) $(LDFLAGS)
#	chown root $(BIN)
#	chmod u+s $(BIN)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)
	$(CC) -M -MT $@ -o $*.d $< $(CPPFLAGS)

-include $(DEPFILES)
