Name:           udpst
Version:        7.1.0
Release:        3%{?dist}
Summary:        Open Broadband-UDP Speed Test
Group:          Development/Libraries
License:        BSD 3-Clause
URL:            https://github.com/BroadbandForum/obudpst
#git archive --format=tar.gz -o udpst-7.1.0.tar.gz --prefix=udpst-7.1.0/ main
Source0:        udpst-7.1.0.tar.gz
BuildRequires:  cmake3
BuildRequires:  make
BuildRequires:  gcc
BuildRequires:  glibc-headers
BuildRequires:  openssl-devel
Requires:       systemd
Requires:       firewalld

%package server
Requires:       %{name} = %{version}-%{release}
Summary:        Open Broadband-UDP Speed Test Server

%description
Open Broadband-UDP Speed Test (OB-UDPST) is a client/server software utility to demonstrate one approach of doing IP capacity measurements

%description server
Open Broadband-UDP Speed Test (OB-UDPST) server configuration package

%prep
%setup -q

%build
cmake3 -DCMAKE_INSTALL_PREFIX=/usr .
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}
mkdir -p           $RPM_BUILD_ROOT/%{_unitdir}/
cp %{name}.service $RPM_BUILD_ROOT/%{_unitdir}/

%post server
systemctl daemon-reload
firewall-cmd --permanent --add-port=25000/udp
firewall-cmd --permanent --add-port=32768-60999/udp
firewall-cmd --reload

%files
%defattr(0644,root,root,-)

%files server
%attr(0755,root,root) %{_bindir}/%{name}
%attr(0644,root,root) %{_unitdir}/%{name}.service

%changelog
* Thu Mar 18 2021 Michael R. Davis <mrdvt92@yahoo.com> - 7.1.0-3
- Split out server package and added firewall-cmd to open ports

* Thu Mar 18 2021 Michael R. Davis <mrdvt92@yahoo.com> - 7.1.0-2
- Added service file

* Thu Mar 18 2021 Michael R. Davis <mrdvt92@yahoo.com> - 7.1.0-1
- Original spec file
