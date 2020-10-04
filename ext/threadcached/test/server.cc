#include <pku_memcached.h>
#include <memcached.h>

extern bool server_flag;
int main(){
  server_flag = true;
  memcached_init();
  memcached_start_server();
}
