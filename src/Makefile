
.PHONY:	all clean install

all:
	@make -C cokebank_sqlite all
#	@make -C cokebank_basic all
	@make -C server all
	@make -C client all

clean:
	@make -C cokebank_sqlite clean
#	@make -C cokebank_basic clean
	@make -C server clean
	@make -C client clean

install:
	@make -C server install
	@make -C client install

