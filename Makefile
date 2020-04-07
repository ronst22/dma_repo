make:
	cd page_writer && $(MAKE)
	cd page_reader && $(MAKE)
	cd one_writer && $(MAKE)

clean:
	cd page_writer && $(MAKE) clean
	cd page_reader && $(MAKE) clean
	cd one_writer && $(MAKE) clean

