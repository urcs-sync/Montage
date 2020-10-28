
CXXFLAG:=-I../src -I../src/utils -I../src/rideables -I../src/tests -I../src/persist -I../src/persist/api -I../ext/ralloc/src 
CXXFLAG+=-O0 -g 
CXXFLAG+=-lpthread -lhwloc -lgomp -latomic -std=c++17 -mrtm
CXXFLAG+=-L../ext/ralloc -lralloc -L../lib -lnbMontage 

RECURSEDIRS := ../

all: dcss dcss_visible

dcss: dcss.cpp
	for rec in $(RECURSEDIRS); do $(MAKE) debug -C $$rec; done
	g++ $^ -o $@ $(CXXFLAG)

dcss_visible: dcss.cpp
	for rec in $(RECURSEDIRS); do $(MAKE) debug -C $$rec; done
	g++ $^ -o $@ $(CXXFLAG) -DVISIBLE_READ

clean:
	rm dcss dcss_visible