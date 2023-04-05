#ifndef CONFIG_H
#define CONFIG_H

#define SOFTWARE_VER "@SOFTWARE_VER@"
#cmakedefine HAVE_GETIFADDRS
#cmakedefine HAVE_LIBSSL
#cmakedefine HAVE_LIBZ
#cmakedefine HAVE_MKFIFO
#cmakedefine HAVE_OPENSSL
#cmakedefine HAVE_NET_IF_DL_H
#cmakedefine HAVE_SIOCGIFHWADDR
#cmakedefine HAVE_SYSCTL_H
#cmakedefine HAVE_SYSINFO_H
#cmakedefine HAVE_TIMEGM
#cmakedefine HAVE_WORKING_FORK
#cmakedefine HAVE_SENDMMSG
#cmakedefine HAVE_GSO
#cmakedefine HAVE_RECVMMSG
#cmakedefine DISABLE_INT_TIMER
#cmakedefine RATE_LIMITING

#endif /* CONFIG_H */
