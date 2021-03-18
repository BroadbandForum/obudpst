Name:           udpst
Version:        7.1.0
Release:        1%{?dist}
Summary:        Open Broadband-UDP Speed Test (OB-UDPST)
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

%description
Open Broadband-UDP Speed Test (OB-UDPST) is a client/server software utility to demonstrate one approach of doing IP capacity measurements

%prep
%setup -q

%build
cmake3 -DCMAKE_INSTALL_PREFIX=/usr
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}

%files
%defattr(0644,root,root,-)
%attr(0755,root,root) %{_bindir}/%{name}


%changelog
* Thu Mar 18 2021 Michael R. Davis <mrdvt92@yahoo.com> - 7.1.0-1
- Original spec file