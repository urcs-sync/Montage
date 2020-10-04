/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include <pku_memcached.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <string.h>

int main(){
  memcached_init();
  memcached_end();
  memcached_close();
  return 0;
}
