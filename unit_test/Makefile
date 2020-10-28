
CXXFLAG:=-I../src -I../src/utils -I../src/rideables -I../src/tests -I../src/persist -I../src/persist/api -I../ext/ralloc/src 
CXXFLAG+=-O0 -g 
CXXFLAG+=-lpthread -lhwloc -lgomp -latomic -std=c++17 -mrtm
CXXFLAG+=-L../ext/ralloc -lralloc -L../lib -lnbMontage 

RECURSEDIRS := ../

all: dcas dcas_visible

dcas: dcas.cpp
	for rec in $(RECURSEDIRS); do $(MAKE) debug -C $$rec; done
	g++ $^ -o $@ $(CXXFLAG)

dcas_visible: dcas.cpp
	for rec in $(RECURSEDIRS); do $(MAKE) debug -C $$rec; done
	g++ $^ -o $@ $(CXXFLAG) -DVISIBLE_READ

clean:
	rm dcas dcas_visible