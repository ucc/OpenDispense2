
BIN := ../../cokebank.so
OBJ := main.o bank.o

CPPFLAGS := 
CFLAGS := -Wall -Werror -g -fPIC
LDFLAGS := -shared -Wl,-soname,cokebank.so

ifneq ($(USE_LDAP),)
	CFLAGS += -DUSE_LDAP
	LDFLAGS += -lldap
endif

.PHONY: all clean

all:	$(BIN)

clean:
	$(RM) $(BIN) $(OBJ)

$(BIN):	$(OBJ)
	$(CC) $(LDFLAGS) -o $(BIN) $(OBJ)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)
