SUBDIRS = m4

EXTRA_DIST = stdint.h inttypes.h

if NEED_LIBCOMPAT
noinst_LIBRARIES = libcompat.a
libcompat_a_SOURCES = getaddrinfo.h
libcompat_a_LIBADD = $(LIBOBJS) $(ALLOCA)
endif
