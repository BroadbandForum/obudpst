#
# Copyright (c) 2020, Broadband Forum
# Copyright (c) 2020, AT&T Communications
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
#
# UDP Speed Test - Makefile
#

#
# Compiler
#
CC = gcc

#
# Compiler flags
#
CFLAGS = -g -Wall -Wstrict-prototypes -O2 -Wextra

#
# Authentication is included if OpenSSL is installed (e.g., 'sudo apt-get
# install libssl-dev') and the expected directory exists, else build without
#
ifneq ("$(wildcard /usr/include/openssl)","")
	AUTHFLAG = -DAUTH_KEY_ENABLE
	LIBS = -lcrypto
endif
#-----------------------------------------------------------------------------
all:			udpst

udpst:			udpst.o udpst_control.o udpst_data.o udpst_srates.o
			$(CC) $(CFLAGS) $(AUTHFLAG) -o $@ udpst.o \
			udpst_control.c udpst_data.c udpst_srates.c $(LIBS)

udpst.o:		udpst_common.h udpst_protocol.h udpst.h \
			udpst_control.h udpst_data.h udpst_srates.h udpst.c
			$(CC) $(CFLAGS) $(AUTHFLAG) -c -o $@ udpst.c

udpst_control.o:	udpst_common.h udpst_protocol.h udpst.h \
			udpst_control.h udpst_data.h udpst_control.c
			$(CC) $(CFLAGS) $(AUTHFLAG) -c -o $@ udpst_control.c

udpst_data.o:		udpst_common.h udpst_protocol.h udpst.h udpst_data.c
			$(CC) $(CFLAGS) $(AUTHFLAG) -c -o $@ udpst_data.c

udpst_srates.o:		udpst_common.h udpst_protocol.h udpst.h udpst_srates.c
			$(CC) $(CFLAGS) $(AUTHFLAG) -c -o $@ udpst_srates.c

clean:
			rm -f udpst *.o		 
#-----------------------------------------------------------------------------
