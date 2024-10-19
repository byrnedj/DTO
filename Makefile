# Copyright (C) 2023 Intel Corporation
#
# SPDX-License-Identifier: MIT

all: libdto dto-test-wodto

DML_LIB_CXX=-D_GNU_SOURCE

libdto: dto.c
	gcc -shared -fPIC -Wl,-soname,libdto.so dto.c $(DML_LIB_CXX) -DDTO_STATS_SUPPORT -o libdto.so.1.0 -laccel-config -ldl -lnuma

libdto-dev: dev-dto.c
	gcc -shared -fPIC -Wl,-soname,libdevdto.so dev-dto.c $(DML_LIB_CXX) -DDTO_STATS_SUPPORT -o libdevdto.so.1.0 -laccel-config -ldl -lnuma

libdto-nostats: dto.c
	gcc -shared -fPIC -Wl,-soname,libdtons.so dto.c $(DML_LIB_CXX) -o libdtons.so.1.0 -laccel-config -ldl -lnuma

install:
	cp libdto.so.1.0 /usr/lib64/
	ln -sf /usr/lib64/libdto.so.1.0 /usr/lib64/libdto.so.1
	ln -sf /usr/lib64/libdto.so.1.0 /usr/lib64/libdto.so

install-local:
	ln -sf ./libdto.so.1.0 ./libdto.so.1
	ln -sf ./libdto.so.1.0 ./libdto.so

install-local-dev:
	ln -sf ./libdevdto.so.1.0 ./libdevdto.so.1
	ln -sf ./libdevdto.so.1.0 ./libdevdto.so

install-local-nostats:
	ln -sf ./libdtons.so.1.0 ./libdtons.so.1
	ln -sf ./libdtons.so.1.0 ./libdtons.so

dto-test: dto-test.c
	gcc -g dto-test.c $(DML_LIB_CXX) -o dto-test -ldto -lpthread -L ./

dto-test-dev: dto-test.c
	gcc -g dto-test.c $(DML_LIB_CXX) -o dto-test-dev -ldevdto -lpthread -L ./

dto-test-set: dto-test-settable-size.c
	gcc -g dto-test-settable-size.c $(DML_LIB_CXX) -o dto-test-settable-size -ldtons -lpthread -L ./

dto-test-set-dev: dto-test-settable-size.c
	gcc -g dto-test-settable-size.c $(DML_LIB_CXX) -o dto-test-settable-size-dev -ldevdto -lpthread -L ./

dto-test-distribution: dto-test-distribution.c
	gcc -g dto-test-distribution.c $(DML_LIB_CXX) -o dto-test-distribution -ldtons -lpthread -L ./

dto-test-distribution-dev: dto-test-distribution.c
	gcc -g dto-test-distribution.c $(DML_LIB_CXX) -o dto-test-distribution-dev -ldevdto -lpthread -L ./

dto-test-wodto: dto-test.c
	gcc -g dto-test.c $(DML_LIB_CXX) -o dto-test-wodto -lpthread

clean:
	rm -rf *.o *.so dto-test dto-test-distribution dto-test-settable-size
