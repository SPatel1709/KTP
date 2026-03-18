all:
	$(MAKE) -f Makefiles/Makefile.lib
	$(MAKE) -f Makefiles/Makefile.init
	$(MAKE) -f Makefiles/Makefile.users

clean:
	$(MAKE) -f Makefiles/Makefile.lib clean
	$(MAKE) -f Makefiles/Makefile.init clean
	$(MAKE) -f Makefiles/Makefile.users clean