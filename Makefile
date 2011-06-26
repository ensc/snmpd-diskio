VERSION =	0.2

prefix =		/usr/local
bin_dir =		$(prefix)/bin
sbin_dir =		$(prefix)/sbin
libexec_dir =		$(prefix)/libexec
localstate_dir =	$(prefix)/run
sysconf_dir =		$(prefix)/etc

PKG_CONFIG =		pkg-config
INSTALL =		install
INSTALL_BIN =		$(INSTALL) -p -m 0755
MKDIR_P =		$(INSTALL) -d

CFLAGS = \
	-Wall -W -Wno-unused-parameter \
	-D_FORTIFY_SOURCE=2 -fstack-protector \
	-O2 -g3
AM_CFLAGS =		-std=gnu99

AM_CPPFLAGS = \
	-DVERSION=\"$(VERSION)\" \
	-DPREFIX=\"$(prefix)\" \
	-DSYSCONFDIR=\"$(sysconf_dir)\" \
	-DLIBEXECDIR=\"$(libexec_dir)\" \
	-DLOCALSTATEDIR=\"$(localstate_dir)\" \

sbin_PROGRAMS =		snmpd-diskio
libexec_PROGRAMS =	snmpd-diskio-cache

BLKID_CFLAGS =		$(shell $(PKG_CONFIG) --cflags blkid)
BLKID_LIBS =		$(shell $(PKG_CONFIG) --libs blkid)

all:	$(sbin_PROGRAMS) $(libexec_PROGRAMS)

install:	$(sbin_PROGRAMS) $(libexec_PROGRAMS)
	$(MKDIR_P) $(DESTDIR)$(sbin_dir)/ $(DESTDIR)$(libexec_dir)/
	$(INSTALL_BIN) $(sbin_PROGRAMS) $(DESTDIR)$(sbin_dir)/
	$(INSTALL_BIN) $(libexec_PROGRAMS) $(DESTDIR)$(libexec_dir)/

clean:
	rm -f $(sbin_PROGRAMS) $(libexec_PROGRAMS)
	rm -f snmpd-diskio-*.tar*

dist:	$(addprefix snmpd-diskio-$(VERSION).tar.,xz bz2)

%.xz:	%
	@rm -f $@.tmp $@
	xz -z < $< > $@.tmp
	mv $@.tmp $@

%.bz2:	%
	@rm -f $@.tmp $@
	bzip2 -z < $< > $@.tmp
	mv $@.tmp $@

snmpd-diskio-$(VERSION).tar:	.git/objects
	git archive --format=tar --prefix=snmpd-diskio-$(VERSION)/ -o $@ HEAD

snmpd-diskio:		src/snmpd-diskio.c src/config.h
snmpd-diskio-cache:	src/snmpd-diskio-cache.c src/config.h


snmpd-diskio-cache:	AM_CFLAGS += $(BLKID_CFLAGS)
snmpd-diskio-cache:	LIBS += $(BLKID_LIBS)

$(sbin_PROGRAMS) $(libexec_PROGRAMS):
	$(CC) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) -o $@ $(LIBS)
