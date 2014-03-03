%define _unpackaged_files_terminate_build 0
Name:           avahi
Version:        0.6.30
Release:        0
Summary:        Local network service discovery
Group:          System Environment/Base
License:        LGPL-2.0
Requires:       dbus
Requires:       expat
Requires(post): glibc
Requires:       %{name}-libs = %{version}-%{release}
BuildRequires:  automake libtool
BuildRequires:  dbus-devel >= 0.90
BuildRequires:  dbus-glib-devel >= 0.70
BuildRequires:  libdaemon-devel
BuildRequires:  libcap-devel
BuildRequires:  expat-devel
BuildRequires:  intltool
BuildRequires:  perl-XML-Parser
Obsoletes:      howl
Source0:        %{name}-%{version}.tar.gz
Source1001:     %{name}.manifest
Source1002:     %{name}-libs.manifest
Source1003:     %{name}-devel.manifest
Source1004:     avahi-data.manifest

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
cp %{SOURCE1001} %{SOURCE1002} %{SOURCE1003} %{SOURCE1004} .

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


# Make /etc/avahi/etc/localtime owned by avahi:
mkdir -p $RPM_BUILD_ROOT/usr/etc/avahi/etc
touch $RPM_BUILD_ROOT/usr/etc/avahi/etc/localtime


%find_lang %{name}

%clean
rm -rf $RPM_BUILD_ROOT


%post
mkdir -p /opt/var/run/avahi-daemon
#Evne eglibc is included in Requires(post),
#Not sure whether it's ok or not during making OBS image.
#That's why if statement is commented out to gurantee chown operation
#if [ ! -z "`getent group app`" ]; then
    chown -R 5000:5000 /opt/var/run/avahi-daemon || true
#fi


%post libs
/sbin/ldconfig
%postun libs
/sbin/ldconfig


%files
%manifest %{name}.manifest
%defattr(0644,root,root,0755)
/usr/share/license/%{name}
%ghost %attr(0755,avahi,avahi) %dir /opt%{_localstatedir}/run/avahi-daemon
%attr(0755,root,root) %{_sbindir}/avahi-daemon

%files devel
%manifest %{name}-devel.manifest
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
%manifest %{name}-libs.manifest
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
%exclude %dir %{_datadir}/avahi
%exclude %{_datadir}/avahi/*.dtd
%exclude %{_datadir}/avahi/service-types
%dir /usr/%{_sysconfdir}/avahi
%dir /usr%{_sysconfdir}/avahi/etc
%ghost /usr%{_sysconfdir}/avahi/etc/localtime
/usr%{_sysconfdir}/avahi/avahi-daemon.conf

%changelog
