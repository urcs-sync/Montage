#include <rpmalloc.hpp>
#include <BaseMeta.hpp>
#include <assert.h>

int main(){
  pm_init();
  unsigned *al = pm_get_root<unsigned>(0);
//  pm_recover();
  for(int i = 0; i < 1000; ++i){
    assert(al[i] == 0xDEADBEEF);
  }
}
