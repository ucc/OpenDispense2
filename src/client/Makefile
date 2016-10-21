# OpenDispense 2
#
V ?= @

INSTALLDIR := /usr/local/opendispense2
CFLAGS := -Wall -Werror -g -std=gnu99
LDFLAGS := -g -lncurses
# -lssl

BIN := ../../dispense
OBJ := main.o protocol.o menu.o
OBJ += doregex.o config.o

OBJ := $(patsubst %,.obj/%,$(OBJ))


DEPFILES := $(OBJ:%.o=%.d)

.PHONY: all clean

all: $(BIN)

clean:
	$(RM) $(BIN) $(OBJ) $(DEPFILES)

install: $(BIN)
	mkdir -p $(INSTALLDIR)
	cp $(BIN) $(INSTALLDIR)

$(BIN): $(OBJ)
	@echo "[CC] -o $@"
	$V$(CC) -o $(BIN) $(OBJ) $(LDFLAGS)
#	chown root $(BIN) && chmod u+s $(BIN)

.obj/%.o: %.c
	@echo "[CC] -c $<"
	@mkdir -p $(dir $@)
	$V$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS) -MMD -MF $@.d

.obj/%.o: ../common/%.c
	@echo "[CC] -c $<"
	@mkdir -p $(dir $@)
	$V$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS) -MMD -MF $@.d

-include $(DEPFILES)
