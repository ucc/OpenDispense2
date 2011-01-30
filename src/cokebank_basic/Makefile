
BIN := ../../cokebank_basic.so
OBJ := main.o

CPPFLAGS := 
CFLAGS := -Wall -Wextra -Werror -g -fPIC -Wmissing-prototypes -Wstrict-prototypes
LDFLAGS := -shared -Wl,-soname,cokebank.so

ifneq ($(USE_LDAP),)
	CFLAGS += -DUSE_LDAP
	LDFLAGS += -lldap
endif

DEPFILES := $(OBJ:%.o=%.d)

.PHONY: all clean

all:	$(BIN)

clean:
	$(RM) $(BIN) $(OBJ) $(DEPFILES)

$(BIN):	$(OBJ)
	$(CC) $(LDFLAGS) -o $(BIN) $(OBJ)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)
	@cpp $< -MM -MF $*.d
	
-include $(DEPFILES)
