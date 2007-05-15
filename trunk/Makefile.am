
AUTOMAKE_OPTIONS = dist-bzip2 dist-zip
SUBDIRS = src doc
EXTRA_DIST = README.* Release_Notes inet_ntop.c
dist-hook:
	rm -rf `find $(distdir) -name .svn`
	sed -e 's|@VERSION\@|$(VERSION)|g' $(PACKAGE).spec.in > $(PACKAGE).spec
	cp $(PACKAGE).spec $(distdir)