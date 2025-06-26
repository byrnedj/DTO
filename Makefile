# Copyright (C) 2023 Intel Corporation
#
# SPDX-License-Identifier: MIT

all: libdto dto-test-wodto

DML_LIB_CXX=-D_GNU_SOURCE

libdto: dto.c
	gcc -shared -fPIC -Wl,-soname,libdto.so dto.c $(DML_LIB_CXX) -DDTO_STATS_SUPPORT -o libdto.so.1.0 -laccel-config -ldl -lnuma

libdtodebug: dto.c
	gcc -g -shared -fPIC -Wl,-soname,libdtodebug.so dto.c $(DML_LIB_CXX) -DDTO_STATS_SUPPORT -o libdtodebug.so.1.0 -laccel-config -ldl -lnuma

libdto-static: dto.c
	gcc -c -o dto.o dto.c -D_GNU_SOURCE
	ar rcs libdto.a dto.o

libmindto: min_dto.c
	gcc -shared -fPIC -Wl,-soname,libmindto.so min_dto.c $(DML_LIB_CXX) -DDTO_STATS_SUPPORT -o libmindto.so.1.0 -laccel-config -ldl -lnuma

libdto-dev: dev-dto.c
	gcc -shared -fPIC -Wl,-soname,libdevdto.so dev-dto.c $(DML_LIB_CXX) -DDTO_STATS_SUPPORT -o libdevdto.so.1.0 -laccel-config -ldl -lnuma

libdto-dev-debug: dev-dto.c
	gcc -g -shared -fPIC -Wl,-soname,libdevdtodebug.so dev-dto.c $(DML_LIB_CXX) -DDTO_STATS_SUPPORT -o libdevdtodebug.so.1.0 -laccel-config -ldl -lnuma

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

dto-test-static: dto-test.c
	gcc -g dto-test.c $(DML_LIB_CXX) -o dto-test -l:libdto.a -lpthread -laccel-config -ldl -lnuma -L ./

dto-test-dev: dto-test.c
	gcc -g dto-test.c $(DML_LIB_CXX) -o dto-test-dev -ldevdto2 -lpthread -L ./

dto-test-set: dto-test-settable-size.c
	gcc -g dto-test-settable-size.c $(DML_LIB_CXX) -o dto-test-settable-size -ldto -lpthread -L ./

dto-test-set-dev: dto-test-settable-size.c
	gcc -g dto-test-settable-size.c $(DML_LIB_CXX) -o dto-test-settable-size-dev -ldevdto -lpthread -L ./

dto-test-set-dev2: dto-test-settable-size2.c
	gcc -g dto-test-settable-size2.c $(DML_LIB_CXX) -o dto-test-settable-size-dev2 -ldevdto -lpthread -lnuma -L ./

dto-test-set-dev3: dto-test-settable-size3.c
	gcc -g dto-test-settable-size3.c $(DML_LIB_CXX) -o dto-test-settable-size-dev3 -ldevdto -lpthread -lnuma -L ./

dto-test-set-wodto3: dto-test-settable-size3.c
	gcc -g dto-test-settable-size3.c $(DML_LIB_CXX) -o dto-test-settable-size-wodto3 -lpthread -lnuma -L ./

dto-test-set-dev4: dto-test-settable-size4.c
	gcc -g dto-test-settable-size4.c $(DML_LIB_CXX) -o dto-test-settable-size-dev4 -ldevdto -lpthread -lnuma -L ./

dto-test-set-dev5: dto-test-settable-size5.c
	gcc -g dto-test-settable-size5.c $(DML_LIB_CXX) -o dto-test-settable-size-dev5 -ldevdto -lpthread -lnuma -L ./

dto-test-set5: dto-test-settable-size5.c
	gcc -g dto-test-settable-size5.c $(DML_LIB_CXX) -o dto-test-settable-size5 -ldto -lpthread -lnuma -L ./

dto-test-set-dev5-nodto: dto-test-settable-size5.c
	gcc -g dto-test-settable-size5.c $(DML_LIB_CXX) -o dto-test-settable-size-dev5-nodto -lpthread -lnuma -L ./

junk: junk.c
	gcc -g junk.c $(DML_LIB_CXX) -o junk -lpthread -lnuma -L ./


dto-test-set-dev6: dto-test-settable-size6.c
	gcc -g dto-test-settable-size6.c $(DML_LIB_CXX) -o dto-test-settable-size-dev6 -ldevdto -lpthread -lnuma -L ./


dto-test-size-steps: dto-test-size-steps.c
	gcc -g dto-test-size-steps.c $(DML_LIB_CXX) -o dto-test-size-steps -ldevdtodebug -lpthread -L ./

dto-test-distribution: dto-test-distribution.c
	gcc -g dto-test-distribution.c $(DML_LIB_CXX) -o dto-test-distribution -ldtons -lpthread -L ./

dto-test-distribution-dev: dto-test-distribution.c
	gcc -g dto-test-distribution.c $(DML_LIB_CXX) -o dto-test-distribution-dev -ldevdto -lpthread -L ./

dto-test-distribution-multithread: dto-test-distribution-multithread.c
	gcc -g dto-test-distribution-multithread.c $(DML_LIB_CXX) -o dto-test-distribution-multithread -ldtons -lpthread -L ./

dto-test-distribution-multithread-dev: dto-test-distribution-multithread.c
	gcc -g dto-test-distribution-multithread.c $(DML_LIB_CXX) -o dto-test-distribution-multithread-dev -ldevdto -lpthread -L ./

dto-test-distribution-multithread-dev2: dto-test-distribution-multithread2.c
	gcc dto-test-distribution-multithread2.c $(DML_LIB_CXX) -o dto-test-distribution-multithread-dev2 -ldevdto -lpthread -L ./

dto-test-distribution-multithread-dev3: dto-test-distribution-multithread3.c
	gcc dto-test-distribution-multithread3.c $(DML_LIB_CXX) -o dto-test-distribution-multithread-dev3 -ldevdto -lpthread -lnuma -L ./

dto-test-distribution-multithread-dev4: dto-test-distribution-multithread4.c
	gcc dto-test-distribution-multithread4.c $(DML_LIB_CXX) -o dto-test-distribution-multithread-dev4 -mwaitpkg -ldevdto -lpthread -lnuma -L ./

dto-test-distribution-multithread-dev5: dto-test-distribution-multithread5.c
	gcc dto-test-distribution-multithread5.c $(DML_LIB_CXX) -o dto-test-distribution-multithread-dev5 -mwaitpkg -ldevdto -lpthread -lnuma -L ./

dto-test-distribution-multithread-dev6: dto-test-distribution-multithread6.c
	gcc dto-test-distribution-multithread6.c $(DML_LIB_CXX) -o dto-test-distribution-multithread-dev6 -mwaitpkg -ldevdto -lpthread -lnuma -L ./

dto-test-wodto: dto-test.c
	gcc -g dto-test.c $(DML_LIB_CXX) -o dto-test-wodto -lpthread

clean:
	rm -rf *.o *.so dto-test dto-test-distribution dto-test-settable-size
