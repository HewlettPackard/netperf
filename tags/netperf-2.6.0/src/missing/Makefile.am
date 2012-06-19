SUBDIRS = m4

if NEED_LIBCOMPAT 
noinst_LIBRARIES = libcompat.a
libcompat_a_SOURCES = getaddrinfo.h
libcompat_a_LIBADD = $(LIBOBJS) $(ALLOCA)
endif
