#include <stdio.h>
#include <stdint.h>
#include <string.h>
extern uintptr_t __stack_chk_guard;
/* a stack-protected function (array forces a canary) to prove SSP works */
__attribute__((noinline)) static int work(const char *s){ char buf[64]; strncpy(buf,s,sizeof buf-1); buf[63]=0; return (int)strlen(buf); }
int main(void){
  printf("guard=%08lx work=%d\n", (unsigned long)__stack_chk_guard, work("hello stack protector"));
  return 0;
}
