
CXXFLAG:=-I../src -I../src/utils -I../src/rideables -I../src/tests -I../src/persist -I../src/persist/api -I../ext/ralloc/src 
CXXFLAG+=-O0 -g
CXXFLAG+=-lpthread -lhwloc -lgomp -latomic -std=c++17
CXXFLAG+=-L../ext/ralloc -lralloc -L../lib -lnbMontage 

RECURSEDIRS := ../

llsc: llsc.cpp
	for rec in $(RECURSEDIRS); do $(MAKE) -C $$rec; done
	g++ $^ -o $@ $(CXXFLAG)
	
clean:
	rm llsc