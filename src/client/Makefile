# OpenDispense 2
#

INSTALLDIR := /usr/local/opendispense2
CFLAGS := -Wall -Werror -g -std=gnu99
LDFLAGS := -g -lncurses
# -lssl

BIN := ../../dispense
OBJ := main.o protocol.o menu.o
OBJ += doregex.o config.o

DEPFILES := $(OBJ:%.o=%.d)

.PHONY: all clean

all: $(BIN)

clean:
	$(RM) $(BIN) $(OBJ) $(DEPFILES)

install: $(BIN)
	mkdir -p $(INSTALLDIR)
	cp $(BIN) $(INSTALLDIR)

$(BIN): $(OBJ)
	$(CC) -o $(BIN) $(OBJ) $(LDFLAGS)
#	chown root $(BIN)
#	chmod u+s $(BIN)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)
	$(CC) -M -MT $@ -o $*.d $< $(CPPFLAGS)

%.o: ../common/%.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)
	$(CC) -M -MT $@ -o $*.d $< $(CPPFLAGS)

-include $(DEPFILES)
