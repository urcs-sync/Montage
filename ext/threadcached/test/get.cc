/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include <pku_memcached.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <string.h>

#define BUFF_LEN 32
int main(){
  memcached_init();
  std::string name = "chris";

  char nbuff[BUFF_LEN];
  size_t len;
  uint32_t flags;
  memcached_return_t err;

  strcpy(nbuff, name.c_str());
  char *str = memcached_get_internal(nbuff, strlen(nbuff), &len, &flags, &err);
  char *cpy = str;
  assert(err == MEMCACHED_SUCCESS);
  printf("len: %lu\n'", len);
  for(unsigned i = 0; i < len; ++i)
    printf("%c", *(str++));
  free(cpy);
  printf("'\n");
  memcached_close();
}
