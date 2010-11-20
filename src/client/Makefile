
CFLAGS := -Wall -Werror -g
LDFLAGS := -g

BIN := ../../dispense
OBJ := main.o

DEPFILES := $(OBJ:%.o=%.d)

.PHONY: all clean

all: $(BIN)

clean:
	$(RM) $(BIN) $(OBJ)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)
	$(CC) -M -MT $@ -o $*.d $< $(CPPFLAGS)

-include $(DEPFILES)
