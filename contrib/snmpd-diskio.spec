%bcond_without		tmpfiles
%bcond_with		noarch

%{?with_noarch:%global noarch	BuildArch:	noarch}

%global selinux_variants mls strict targeted
%global selinux_policyver %(%__sed -e 's,.*selinux-policy-\\([^/]*\\)/.*,\\1,' /usr/share/selinux/devel/policyhelp || echo "")
%global modulename %name

%{!?release_func:%global release_func() %1%{?dist}}

Name:		snmpd-diskio
Version:	0.2.2
Release:	%release_func 1
Summary:	snmpd diskio plugin

License:	GPLv3
Group:		System Environment/Base
URL:		http://github.org/ensc/%name
Source0:	%name-%version.tar.bz2

BuildRequires:	/usr/include/blkid/blkid.h
BuildRequires:	dietlibc
Requires(post):	coreutils

%package	selinux
Summary:	SELinux policy module supporting %name
Group:		System Environment/Base
Conflicts:	%name < %version-%release
Conflicts:	%name > %version-%release
BuildRequires:  checkpolicy, selinux-policy-devel, hardlink
BuildRequires:	/usr/share/selinux/devel/policyhelp
%if "%selinux_policyver" != ""
Requires:       selinux-policy >= %selinux_policyver
%endif
Source10:	snmpd-diskio.te
Source11:	snmpd-diskio.fc

Requires(post):   /usr/sbin/semodule, /sbin/restorecon
Requires(postun): /usr/sbin/semodule, /sbin/restorecon
%{?noarch}



%description
%description selinux
SELinux policy module supporting %name


%prep
%setup -q

mkdir selinux/
install -p -m 0644 %SOURCE10 %SOURCE11 selinux/


cat <<EOF > tmpfiles
d /var/run/snmpd-diskio 0755 root root
EOF


%global makeflags CC='%__cc' CFLAGS="$RPM_BUILD_OPTS" prefix=%_prefix sysconf_dir=%_sysconfdir localstate_dir=/var/run DIET_CC='diet %__cc' CC='%__cc'

%build
make %{?_smp_mflags} %makeflags

cd selinux
for selinuxvariant in %selinux_variants; do
    make NAME=${selinuxvariant} -f /usr/share/selinux/devel/Makefile
    mv %modulename.pp %modulename.pp.${selinuxvariant}
    make NAME=${selinuxvariant} -f /usr/share/selinux/devel/Makefile clean
done
cd -


%install
rm -rf $RPM_BUILD_ROOT
make %makeflags install DESTDIR=$RPM_BUILD_ROOT

install -D -p -m 0644 tmpfiles $RPM_BUILD_ROOT%_sysconfdir/tmpfiles.d/%name.conf
install -d -m 0755 $RPM_BUILD_ROOT/var/run/snmpd-diskio

cd selinux
for selinuxvariant in %selinux_variants; do
    install -D -p -m 644 %modulename.pp.${selinuxvariant} \
	$RPM_BUILD_ROOT%_datadir/selinux/${selinuxvariant}/%modulename.pp
done
cd -

/usr/sbin/hardlink -cv $RPM_BUILD_ROOT%_datadir/selinux


%{!?with_tmpfiles: rm -rf $RPM_BUILD_ROOT%_sysconfdir/tmpfiles.d}


%post
rm -f /var/run/snmpd-diskio/volumes || :


%post selinux
for selinuxvariant in %selinux_variants; do
    /usr/sbin/semodule -s ${selinuxvariant} -i \
	%_datadir/selinux/${selinuxvariant}/%modulename.pp &> /dev/null || :
done

%postun selinux
if [ $1 -eq 0 ] ; then
   for selinuxvariant in %selinux_variants; do
       /usr/sbin/semodule -s ${selinuxvariant} -r %modulename &> /dev/null || :
   done
fi


%files
%doc
%_sbindir/*
%_libexecdir/*

%if 0%{?with_tmpfiles:1}
  %_sysconfdir/tmpfiles.d/%name.conf
  %ghost %dir %attr(0755,root,root) /var/run/snmpd-diskio
%else
  %dir %attr(0755,root,root) /var/run/snmpd-diskio
%endif


%files selinux
%defattr(-,root,root,0755)
%_datadir/selinux/*/%modulename.pp



%changelog
* Sat Jul  2 2011 Enrico Scholz <enrico.scholz@informatik.tu-chemnitz.de> - 0.2.1-2
- updated to 0.2.1
- build with dietlibc
