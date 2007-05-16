# what is your philosophy - distribute pre-made .pdf et al in a make dist
# or not?  choose your EXTRA_DIST line accordingly
# EXTRA_DIST = netperf.man netserver.man netperf.txt netperf.html netperf.xml netperf_old.ps netperf.pdf netperf.ps netperf.texi examples

SUBDIRS = examples

EXTRA_DIST = netperf.man netserver.man netperf_old.ps netperf.texi 
man1_MANS = netperf.man netserver.man
info_TEXINFOS = netperf.texi
CLEANFILES = netperf.txt netperf.xml netperf.html

netperf.html: $(info_TEXINFOS)
	$(MAKEINFO) $(AM_MAKEINFOFLAGS) $(MAKEINFOFLAGS) --html --no-split -I $(srcdir) \
         -o $@ `test -f '$<' || echo '$(srcdir)/'`$<

netperf.txt: $(info_TEXINFOS)
	$(MAKEINFO) $(AM_MAKEINFOFLAGS) $(MAKEINFOFLAGS) --plaintext -I $(srcdir) \
         -o $@ `test -f '$<' || echo '$(srcdir)/'`$<

netperf.xml: $(info_TEXINFOS)
	$(MAKEINFO) $(AM_MAKEINFOFLAGS) $(MAKEINFOFLAGS) --xml -I $(srcdir) \
         -o $@ `test -f '$<' || echo '$(srcdir)/'`$<
