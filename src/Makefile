
.PHONY:	all clean install

SUBMAKE := make --no-print-directory

all:
	@echo "--- cokebank_sqlite: all" && $(SUBMAKE) -C cokebank_sqlite all
#	@echo "--- cokebank_basic: all" && $(SUBMAKE) -C cokebank_basic all
	@echo "--- server: all" && $(SUBMAKE) -C server all
	@echo "--- client: all" && $(SUBMAKE) -C client all

clean:
	@echo "--- cokebank_sqlite: clean" && $(SUBMAKE) -C cokebank_sqlite clean
#	@echo "--- cokebank_basic: clean" && $(SUBMAKE) -C cokebank_basic clean
	@echo "--- server: clean" && $(SUBMAKE) -C server clean
	@echo "--- client: clean" && $(SUBMAKE) -C client clean

install:
	@echo "--- server: install" && $(SUBMAKE) -C server install
	@echo "--- client: install" && $(SUBMAKE) -C client install

