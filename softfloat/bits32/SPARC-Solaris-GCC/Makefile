
PROCESSOR_H = ../../../processors/SPARC-GCC.h
SOFTFLOAT_MACROS = ../softfloat-macros

OBJ = .o
EXE =
INCLUDES = -I. -I..
COMPILE_C = clang -emit-llvm -c -o $@ $(INCLUDES) -iquote -O2
LINK = llvm-link -o $@

ALL: softfloat$(OBJ) timesoftfloat$(EXE)

milieu.h: $(PROCESSOR_H)
	touch milieu.h

softfloat$(OBJ): milieu.h softfloat.h softfloat-specialize $(SOFTFLOAT_MACROS) ../softfloat.c
	$(COMPILE_C) ../softfloat.c

timesoftfloat$(OBJ): milieu.h softfloat.h ../timesoftfloat.c
	$(COMPILE_C) ../timesoftfloat.c

timesoftfloat$(EXE): softfloat$(OBJ) timesoftfloat$(OBJ)
	$(LINK) softfloat$(OBJ) timesoftfloat$(OBJ)

