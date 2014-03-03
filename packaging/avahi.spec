%define _unpackaged_files_terminate_build 0
Name:           avahi
Version:        0.6.30
Release:        0
Summary:        Local network service discovery
Group:          System/Network
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
Source1004:     %{name}-data.manifest

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
Group:    System/Libraries
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

%package -n %{name}-data
Summary:  Libraries for avahi run-time use
Group:    System/Libraries
Requires: %{name}

%description -n %{name}-data
The avahi-libs package contains the libraries needed
to run programs that use avahi.

%prep
%setup -q
cp %{SOURCE1001} %{SOURCE1002} %{SOURCE1003} %{SOURCE1004} .

%build
%configure --with-distro=fedora --with-%{name}-user=%{name} --with-%{name}-group=%{name} --with-%{name}-priv-access-group=%{name} \
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
        --sysconfdir=%{_sysconfdir}  \
		--localstatedir=/opt/var \
		--without-systemdsystemunitdir

make %{?_smp_mflags}

%install

rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

rm -f $RPM_BUILD_ROOT%{_libdir}/*.la
rm -f $RPM_BUILD_ROOT%{_libdir}/*.a

# remove example
rm -f $RPM_BUILD_ROOT%{_sysconfdir}/%{name}/services/sftp-ssh.service

# create /var/run/avahi-daemon to ensure correct selinux policy for it:
mkdir -p $RPM_BUILD_ROOT/opt%{_localstatedir}/run/%{name}-daemon


#mkdir -p $RPM_BUILD_ROOT%{_localstatedir}/lib/%{name}-autoipd


# Make /etc/avahi/etc/localtime owned by avahi:
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/%{name}/etc
touch $RPM_BUILD_ROOT%{_sysconfdir}/%{name}/etc/localtime


%find_lang %{name}

%clean
rm -rf $RPM_BUILD_ROOT


%post
mkdir -p /opt/var/run/%{name}-daemon
#Evne eglibc is included in Requires(post),
#Not sure whether it's ok or not during making OBS image.
#That's why if statement is commented out to gurantee chown operation
#if [ ! -z "`getent group %{name}`" ]; then
    chown -R %{name}:%{name} /opt/var/run/%{name}-daemon || true
#fi


%post libs
/sbin/ldconfig
%postun libs
/sbin/ldconfig


%files
%manifest %{name}.manifest
%defattr(0644,root,root,0755)
%ghost %attr(0755,%{name},%{name}) %dir /opt%{_localstatedir}/run/%{name}-daemon
%attr(0755,root,root) %{_sbindir}/%{name}-daemon
%license LICENSE

%files devel
%manifest %{name}-devel.manifest
%defattr(0644, root, root, 0755)
%attr(755,root,root) %{_libdir}/lib%{name}-common.so
%attr(755,root,root) %{_libdir}/lib%{name}-core.so
%attr(755,root,root) %{_libdir}/lib%{name}-client.so
%{_includedir}/%{name}-client
%{_includedir}/%{name}-common
%{_includedir}/%{name}-core
%{_libdir}/pkgconfig/%{name}-core.pc
%{_libdir}/pkgconfig/%{name}-client.pc
%attr(755,root,root) %{_bindir}/%{name}-browse
%attr(755,root,root) %{_bindir}/%{name}-browse-domains
%attr(755,root,root) %{_bindir}/%{name}-publish
%attr(755,root,root) %{_bindir}/%{name}-publish-address
%attr(755,root,root) %{_bindir}/%{name}-publish-service
%attr(755,root,root) %{_bindir}/%{name}-resolve
%attr(755,root,root) %{_bindir}/%{name}-resolve-address
%attr(755,root,root) %{_bindir}/%{name}-resolve-host-name
%attr(755,root,root) %{_bindir}/%{name}-set-host-name
%attr(755,root,root) %{_sbindir}/%{name}-autoipd

%files libs
%manifest %{name}-libs.manifest
%defattr(0644, root, root, 0755)
%{_libdir}/%{name}
%exclude %{_libdir}/%{name}/service-types.db
%attr(0755,root,root) %{_libdir}/lib%{name}-common.so.*
%attr(0755,root,root) %{_libdir}/lib%{name}-client.so.*
%attr(0755,root,root) %{_libdir}/lib%{name}-core.so.*
%license LICENSE

%files -n %{name}-data
%manifest %{name}-data.manifest
%defattr(0644,root,root,0755)
%exclude %dir %{_datadir}/%{name}
%exclude %{_datadir}/%{name}/*.dtd
%exclude %{_datadir}/%{name}/service-types
%dir %{_sysconfdir}/%{name}
%dir %{_sysconfdir}/%{name}/etc
%ghost %{_sysconfdir}/%{name}/etc/localtime
%config %{_sysconfdir}/%{name}/%{name}-daemon.conf
%license LICENSE
