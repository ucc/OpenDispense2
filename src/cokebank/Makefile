
BIN := ../../cokebank.so
OBJ := main.o

CPPFLAGS := 
CFLAGS := -Wall -Werror -g -fPIC
LDFLAGS := -shared -Wl,-soname,cokebank.so

.PHONY: all clean

all:	$(BIN)

clean:
	$(RM) $(BIN) $(OBJ)

$(BIN):	$(OBJ)
	$(CC) $(LDFLAGS) -o $(BIN) $(OBJ)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)
