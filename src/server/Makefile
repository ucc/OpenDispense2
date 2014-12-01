# OpenDispense 2
#

INSTALLDIR := /usr/local/opendispense2

OBJ := main.o server.o logging.o config.o
OBJ += dispense.o itemdb.o
OBJ += handler_coke.o handler_snack.o handler_door.o
BIN := ../../dispsrv

OBJ := $(OBJ:%=obj/%)
DEPFILES := $(OBJ:%=%.d)

LINKFLAGS := -g ../../cokebank.so -lutil -lident -lmodbus -lsqlite3 -lpthread -Wl,-rpath,. -Wl,-rpath,$(INSTALLDIR)
CPPFLAGS := 
CFLAGS := -Wall -Wextra -Werror -g -std=gnu99

.PHONY: all clean

all: $(BIN)

clean:
	$(RM) $(BIN) $(OBJ) $(DEPFILES)

install: $(BIN)
	mkdir -p $(INSTALLDIR)
	cp $(BIN) $(INSTALLDIR)

$(BIN): $(OBJ)
	$(CC) -o $(BIN) $(OBJ) $(LINKFLAGS)

obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)
	@cpp $< -MM -MF $@.d
	
-include $(DEPFILES)

