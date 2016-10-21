# OpenDispense 2
#
V ?= @

INSTALLDIR := /usr/local/opendispense2

OBJ := main.o server.o logging.o 
OBJ += dispense.o itemdb.o
OBJ += handler_coke.o handler_snack.o handler_door.o
OBJ += config.o doregex.o
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
	@echo "[CC] -o $@"
	@$(CC) -o $(BIN) $(OBJ) $(LINKFLAGS)

obj/%.o: %.c 
	@mkdir -p $(dir $@)
	@echo "[CC] -c $<"
	$V$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS) -MMD -MF $@.d
	

obj/%.o: ../common/%.c 
	@mkdir -p $(dir $@)
	@echo "[CC] -c $<"
	$V$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS) -MMD -MF $@.d

-include $(DEPFILES)

