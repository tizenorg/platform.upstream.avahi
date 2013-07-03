#%{?!WITH_MONO:          %define WITH_MONO 0}
#%{?!WITH_COMPAT_DNSSD:  %define WITH_COMPAT_DNSSD 0}
#%{?!WITH_COMPAT_HOWL:   %define WITH_COMPAT_HOWL  0}
#%ifarch sparc64 s390 %{arm}
#%define WITH_MONO 0
#%endif
%define _unpackaged_files_terminate_build 0
Name:           avahi
Version:        0.6.30
Release:        22
Summary:        Local network service discovery
Group:          System Environment/Base
License:        LGPL-2.0
Requires:       dbus
Requires:       expat
#Requires:       libdaemon
Requires(post): glibc
#Requires:       systemd-units
#Requires(post): initscripts, chkconfig, ldconfig
#Requires(pre):  shadow-utils
Requires:       %{name}-libs = %{version}-%{release}
BuildRequires:  automake libtool
BuildRequires:  dbus-devel >= 0.90
BuildRequires:  dbus-glib-devel >= 0.70
#BuildRequires:  dbus-python
#BuildRequires:  libxml2-python
#BuildRequires:  gtk2-devel
#BuildRequires:  gtk3-devel >= 2.99.0
#BuildRequires:  gobject-introspection-devel
#BuildRequires:  qt3-devel
#BuildRequires:  qt4-devel
#BuildRequires:  libglade2-devel
BuildRequires:  libdaemon-devel
#BuildRequires:  glib2-devel
BuildRequires:  libcap-devel
BuildRequires:  expat-devel
#BuildRequires:  python
#BuildRequires:  gdbm-devel
#BuildRequires:  pygtk2
BuildRequires:  intltool
BuildRequires:  perl-XML-Parser
#%if %{WITH_MONO}
#BuildRequires:  mono-devel >= 1.1.13
#BuildRequires:  monodoc-devel
#%endif
Obsoletes:      howl
#Source0:        http://avahi.org/download/%{name}-%{version}.tar.gz
Source0:        %{name}-%{version}.tar.gz
#Patch1:         01_avahi-daemon.conf.patch

%description
Avahi is a system which facilitates service discovery on
a local network -- this means that you can plug your laptop or
computer into a network and instantly be able to view other people who
you can chat with, find printers to print to or find files being
shared. This kind of technology is already found in MacOS X (branded
'Rendezvous', 'Bonjour' and sometimes 'ZeroConf') and is very
convenient.

%package libs
Summary:  Libraries for avahi run-time use
Group:    System Environment/Libraries
Requires: poppler-tools

%description libs
The avahi-libs package contains the libraries needed
to run programs that use avahi.

%package devel
Summary:  Libraries and header files for avahi development
Group:    Development/Libraries
Requires: %{name}-libs = %{version}-%{release}
Requires: pkgconfig

%description devel
The avahi-devel package contains the header files and libraries
necessary for developing programs using avahi.

%package -n avahi-data
Summary:  Libraries for avahi run-time use
Group:    System Environment/Libraries
Requires: avahi

%description -n avahi-data
The avahi-libs package contains the libraries needed
to run programs that use avahi.

%prep
%setup -q

#%patch1 -p1

%build
%configure --with-distro=fedora --with-avahi-user=app --with-avahi-group=app --with-avahi-priv-access-group=app \
		--disable-compat-libdns_sd \
		--disable-mono \
		--disable-monodoc \
		--disable-qt3 \
		--disable-qt4 \
		--disable-gtk \
		--disable-gtk3\
		--disable-python \
		--disable-pygtk \
		--disable-python-dbus \
		--disable-doxygen-doc\
		--disable-doxygen-dot\
		--disable-doxygen-xml\
		--disable-doxygen-html\
		--disable-doxygen-manpages\
		--disable-doxygen-xmltoman\
		--disable-glib \
		--disable-gobject \
		--disable-gdbm \
        --sysconfdir=/usr/etc  \
		--localstatedir=/opt/var \
		--without-systemdsystemunitdir

make %{?_smp_mflags}

%install

rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

mkdir -p %{buildroot}/usr/share/license
cp %{_builddir}/%{buildsubdir}/LICENSE %{buildroot}/usr/share/license/avahi
cp %{_builddir}/%{buildsubdir}/LICENSE %{buildroot}/usr/share/license/avahi-libs
cp %{_builddir}/%{buildsubdir}/LICENSE %{buildroot}/usr/share/license/avahi-data

rm -f $RPM_BUILD_ROOT%{_libdir}/*.la
rm -f $RPM_BUILD_ROOT%{_libdir}/*.a

# remove example
rm -f $RPM_BUILD_ROOT/usr%{_sysconfdir}/avahi/services/sftp-ssh.service

# create /var/run/avahi-daemon to ensure correct selinux policy for it:
mkdir -p $RPM_BUILD_ROOT/opt%{_localstatedir}/run/avahi-daemon


#mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/lib/avahi-autoipd

# remove the documentation directory - let % doc handle it:
#rm -rf $RPM_BUILD_ROOT%{_datadir}/%{name}-%{version}

# Make /etc/avahi/etc/localtime owned by avahi:
mkdir -p $RPM_BUILD_ROOT/usr/etc/avahi/etc
touch $RPM_BUILD_ROOT/usr/etc/avahi/etc/localtime

# fix bug 197414 - add missing symlinks for avahi-compat-howl and avahi-compat-dns-sd
#%if %{WITH_COMPAT_HOWL}
#ln -s avahi-compat-howl.pc  $RPM_BUILD_ROOT/%{_libdir}/pkgconfig/howl.pc
#%endif
#%if %{WITH_COMPAT_DNSSD}
#ln -s avahi-compat-libdns_sd.pc $RPM_BUILD_ROOT/%{_libdir}/pkgconfig/libdns_sd.pc
#ln -s avahi-compat-libdns_sd/dns_sd.h $RPM_BUILD_ROOT/%{_includedir}/
#%endif
#

%find_lang %{name}

%clean
rm -rf $RPM_BUILD_ROOT

#%pre
#getent group avahi >/dev/null 2>&1 || groupadd \
#        -r \
#        -g 70 \
#        avahi
#getent passwd avahi >/dev/null 2>&1 || useradd \
#        -r -l \
#        -u 70 \
#        -g avahi \
#        -d /opt%{_localstatedir}/run/avahi-daemon \
#        -s /sbin/nologin \
#        -c "Avahi mDNS/DNS-SD Stack" \
#        avahi
#:;

%post
mkdir -p /opt/var/run/avahi-daemon
#Evne eglibc is included in Requires(post),
#Not sure whether it's ok or not during making OBS image.
#That's why if statement is commented out to gurantee chown operation
#if [ ! -z "`getent group app`" ]; then
    chown -R 5000:5000 /opt/var/run/avahi-daemon || true
#fi


#/sbin/ldconfig
#dbus-send --system --type=method_call --dest=org.freedesktop.DBus / org.freedesktop.DBus.ReloadConfig >/dev/null 2>&1 || :
#/sbin/chkconfig --add avahi-daemon >/dev/null 2>&1 || :
#if [ "$1" -eq 1 ]; then
#        /bin/systemctl enable avahi-daemon.service >/dev/null 2>&1 || :
#        if [ -s /opt/etc/localtime ]; then
#                cp -cfp /opt/etc/localtime /opt/etc/avahi/etc/localtime || :
#        fi
#fi

#%triggerun -- avahi < 0.6.28-1
#if /sbin/chkconfig --level 5 avahi-daemon ; then
#        /bin/systemctl --no-reload enable avahi-daemon.service >/dev/null 2>&1 || :
#fi

#%preun
#if [ "$1" -eq 0 ]; then
#        /bin/systemctl --no-reload disable avahi-daemon.service >/dev/null 2>&1 || :
#        /bin/systemctl stop avahi-daemon.service >/dev/null 2>&1 || :
#        /sbin/chkconfig --del avahi-daemon >/dev/null 2>&1 || :
#fi

%postun
#/bin/systemctl daemon-reload >/dev/null 2>&1 || :
#/sbin/ldconfig

#%pre autoipd
#getent group avahi-autoipd >/dev/null 2>&1 || groupadd \
#        -r \
#        -g 170 \
#        avahi-autoipd
#getent passwd avahi-autoipd >/dev/null 2>&1 || useradd \
#        -r -l \
#        -u 170 \
#        -g avahi-autoipd \
#        -d %{_localstatedir}/lib/avahi-autoipd \
#        -s /sbin/nologin \
#        -c "Avahi IPv4LL Stack" \
#        avahi-autoipd
#:;

#%post dnsconfd
#/sbin/chkconfig --add avahi-dnsconfd >/dev/null 2>&1 || :
#if [ "$1" -eq 1 ]; then
#        /bin/systemctl daemon-reload >/dev/null 2>&1 || :
#fi

#%triggerun dnsconfd -- avahi-dnsconfd < 0.6.28-1
#if /sbin/chkconfig --level 5 avahi-dnsconfd ; then
#        /bin/systemctl --no-reload enable avahi-dnsconfd.service >/dev/null 2>&1 || :
#fi

#%preun dnsconfd
#if [ "$1" -eq 0 ]; then
#        /bin/systemctl --no-reload disable avahi-dnsconfd.service >/dev/null 2>&1 || :
#        /bin/systemctl stop avahi-dnsconfd.service >/dev/null 2>&1 || :
#        /sbin/chkconfig --del avahi-dnsconfd >/dev/null 2>&1 || :
#fi

#%postun dnsconfd
#/bin/systemctl daemon-reload >/dev/null 2>&1 || :

#%post glib -p /sbin/ldconfig
#%postun glib -p /sbin/ldconfig

#%post compat-howl -p /sbin/ldconfig
#%postun compat-howl -p /sbin/ldconfig

#%post compat-libdns_sd -p /sbin/ldconfig
#%postun compat-libdns_sd -p /sbin/ldconfig

%post libs
/sbin/ldconfig
%postun libs
/sbin/ldconfig

#%post qt3 -p /sbin/ldconfig
#%postun qt3 -p /sbin/ldconfig

#%post qt4 -p /sbin/ldconfig
#%postun qt4 -p /sbin/ldconfig

#%post ui -p /sbin/ldconfig
#%postun ui -p /sbin/ldconfig

#%post ui-gtk3 -p /sbin/ldconfig
#%postun ui-gtk3 -p /sbin/ldconfig

#%post gobject -p /sbin/ldconfig
#%postun gobject -p /sbin/ldconfig

%files
%manifest avahi.manifest
%defattr(0644,root,root,0755)
/usr/share/license/%{name}
%ghost %attr(0755,avahi,avahi) %dir /opt%{_localstatedir}/run/avahi-daemon
%attr(0755,root,root) %{_sbindir}/avahi-daemon
#%{_datadir}/dbus-1/interfaces/*.xml
#%{_mandir}/man5/*
#%{_mandir}/man8/avahi-daemon.*
#%{_libdir}/systemd/system/avahi-daemon.service
#%{_libdir}/systemd/system/avahi-daemon.socket
#%{_datadir}/dbus-1/system-services/org.freedesktop.Avahi.service

#%files autoipd
#%defattr(0644,root,root,0755)
#%attr(0755,root,root) %{_sbindir}/avahi-autoipd
#%attr(0755,root,root) %config(noreplace) %{_sysconfdir}/avahi/avahi-autoipd.action
#%{_mandir}/man8/avahi-autoipd.*

#%files dnsconfd
#%defattr(0644,root,root,0755)
#%attr(0755,root,root) %{_sysconfdir}/rc.d/init.d/avahi-dnsconfd
#%attr(0755,root,root) %config(noreplace) %{_sysconfdir}/avahi/avahi-dnsconfd.action
#%attr(0755,root,root) %{_sbindir}/avahi-dnsconfd
#%{_mandir}/man8/avahi-dnsconfd.*
#/lib/systemd/system/avahi-dnsconfd.service

#%files tools
#%defattr(0644, root, root, 0755)
#%attr(0755,root,root) %{_bindir}/*
#%{_mandir}/man1/*
#%exclude %{_bindir}/b*
#%exclude %{_bindir}/avahi-discover*
#%exclude %{_bindir}/avahi-bookmarks
#%exclude %{_mandir}/man1/b*
#%exclude %{_mandir}/man1/avahi-discover*
#%exclude %{_mandir}/man1/avahi-bookmarks*

#%files ui-tools
#%defattr(0644, root, root, 0755)
#%attr(0755,root,root) %{_bindir}/b*
#%attr(0755,root,root) %{_bindir}/avahi-discover
## avahi-bookmarks is not really a UI tool, but I won't create a seperate package for it...
#%attr(0755,root,root) %{_bindir}/avahi-bookmarks
#%{_mandir}/man1/b*
#%{_mandir}/man1/avahi-discover*
#%{_mandir}/man1/avahi-bookmarks*
#%{_datadir}/applications/b*.desktop
#%{_datadir}/applications/avahi-discover.desktop
## These are .py files only, so they don't go in lib64
##%{_prefix}/lib/python?.?/site-packages/*
#%{_datadir}/avahi/interfaces/

%files devel
%defattr(0644, root, root, 0755)
%attr(755,root,root) %{_libdir}/libavahi-common.so
%attr(755,root,root) %{_libdir}/libavahi-core.so
%attr(755,root,root) %{_libdir}/libavahi-client.so
%{_includedir}/avahi-client
%{_includedir}/avahi-common
%{_includedir}/avahi-core
%{_libdir}/pkgconfig/avahi-core.pc
%{_libdir}/pkgconfig/avahi-client.pc
%attr(755,root,root) /usr/bin/avahi-browse
%attr(755,root,root) /usr/bin/avahi-browse-domains
%attr(755,root,root) /usr/bin/avahi-publish
%attr(755,root,root) /usr/bin/avahi-publish-address
%attr(755,root,root) /usr/bin/avahi-publish-service
%attr(755,root,root) /usr/bin/avahi-resolve
%attr(755,root,root) /usr/bin/avahi-resolve-address
%attr(755,root,root) /usr/bin/avahi-resolve-host-name
%attr(755,root,root) /usr/bin/avahi-set-host-name
%attr(755,root,root) /usr/sbin/avahi-autoipd

%files libs
%manifest avahi-libs.manifest
%defattr(0644, root, root, 0755)
/usr/share/license/avahi-libs
%{_libdir}/avahi
%exclude %{_libdir}/avahi/service-types.db
%attr(0755,root,root) %{_libdir}/libavahi-common.so.*
%attr(0755,root,root) %{_libdir}/libavahi-client.so.*
%attr(0755,root,root) %{_libdir}/libavahi-core.so.*

%files -n avahi-data
%manifest avahi-data.manifest
%defattr(0644,root,root,0755)
/usr/share/license/avahi-data
#%doc docs/* avahi-daemon/example.service avahi-daemon/sftp-ssh.service
#%attr(0755,root,root) /usr%{_sysconfdir}/rc.d/init.d/avahi-daemon
%exclude %dir %{_datadir}/avahi
%exclude %{_datadir}/avahi/*.dtd
%exclude %{_datadir}/avahi/service-types
%dir /usr/%{_sysconfdir}/avahi
%dir /usr%{_sysconfdir}/avahi/etc
%ghost /usr%{_sysconfdir}/avahi/etc/localtime
#%config(noreplace) /usr%{_sysconfdir}/avahi/hosts
#%dir /usr%{_sysconfdir}/avahi/services
/usr%{_sysconfdir}/avahi/avahi-daemon.conf
#%config(noreplace) /usr%{_sysconfdir}/avahi/services/ssh.service
#%config(noreplace) /usr%{_sysconfdir}/dbus-1/system.d/avahi-dbus.conf

#%files glib
#%defattr(0755, root, root, 0755)
#%{_libdir}/libavahi-glib.so.*

#%files glib-devel
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libavahi-glib.so
#%{_includedir}/avahi-glib
#%{_libdir}/pkgconfig/avahi-glib.pc

#%files gobject
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libavahi-gobject.so.*
##%{_libdir}/girepository-1.0/Avahi-0.6.typelib
##%{_libdir}/girepository-1.0/AvahiCore-0.6.typelib

#%files gobject-devel
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libavahi-gobject.so
#%{_includedir}/avahi-gobject
#%{_libdir}/pkgconfig/avahi-gobject.pc
##%{_datadir}/gir-1.0/Avahi-0.6.gir
##%{_datadir}/gir-1.0/AvahiCore-0.6.gir

#%files ui
#%defattr(0755, root, root, 0755)
#%{_libdir}/libavahi-ui.so.*

#%files ui-gtk3
#%defattr(0755, root, root, 0755)
#%{_libdir}/libavahi-ui-gtk3.so.*

#%files ui-devel
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libavahi-ui.so
#%attr(755,root,root) %{_libdir}/libavahi-ui-gtk3.so
#%{_includedir}/avahi-ui
#%{_libdir}/pkgconfig/avahi-ui.pc
#%{_libdir}/pkgconfig/avahi-ui-gtk3.pc

#%files qt3
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libavahi-qt3.so.*

#%files qt3-devel
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libavahi-qt3.so
#%{_includedir}/avahi-qt3/
#%{_libdir}/pkgconfig/avahi-qt3.pc

#%files qt4
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libavahi-qt4.so.*

#%files qt4-devel
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libavahi-qt4.so
#%{_includedir}/avahi-qt4/
#%{_libdir}/pkgconfig/avahi-qt4.pc

#%if %{WITH_MONO}
#%files sharp
#%defattr(0644, root, root, 0755)
#%{_libdir}/mono/avahi-sharp
#%{_libdir}/mono/gac/avahi-sharp
#%{_libdir}/pkgconfig/avahi-sharp.pc

#%files ui-sharp
#%defattr(0644, root, root, 0755)
#%{_libdir}/mono/avahi-ui-sharp
#%{_libdir}/mono/gac/avahi-ui-sharp

#%files ui-sharp-devel
#%defattr(-,root,root,-)
#%{_libdir}/pkgconfig/avahi-ui-sharp.pc
#%endif

#%if %{WITH_COMPAT_HOWL}
#%files compat-howl
#%defattr(0755, root, root, 0755)
#%{_libdir}/libhowl.so.*

#%files compat-howl-devel
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libhowl.so
#%{_includedir}/avahi-compat-howl
#%{_libdir}/pkgconfig/avahi-compat-howl.pc
#%{_libdir}/pkgconfig/howl.pc
#%endif

#%if %{WITH_COMPAT_DNSSD}
#%files compat-libdns_sd
#%defattr(0755, root, root, 0755)
#%{_libdir}/libdns_sd.so.*

#%files compat-libdns_sd-devel
#%defattr(0644, root, root, 0755)
#%attr(755,root,root) %{_libdir}/libdns_sd.so
#%{_includedir}/avahi-compat-libdns_sd
#%{_includedir}/dns_sd.h
#%{_libdir}/pkgconfig/avahi-compat-libdns_sd.pc
#%{_libdir}/pkgconfig/libdns_sd.pc
#%endif

%changelog
