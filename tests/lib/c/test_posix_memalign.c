#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static int fails = 0;
static void check(const char*tag, int cond){ if(!cond){ printf("FAIL: %s\n", tag); fails++; } }
int main(void){
  size_t aligns[] = {8,16,32,64,256,1024,4096,65536};
  /* basic: alignment honored + region writable + freeable */
  for(unsigned i=0;i<8;i++){
    void *p=NULL; size_t a=aligns[i];
    int rc=posix_memalign(&p,a,1000);
    check("rc==0", rc==0 && p!=NULL);
    check("aligned", ((uintptr_t)p % a)==0);
    memset(p,0xAB,1000);                 /* write whole region */
    free(p);
  }
  /* interleaved stress: many aligned allocs mixed with malloc, write, free */
  void *keep[64]; size_t ka[64];
  for(int iter=0;iter<200;iter++){
    for(int j=0;j<64;j++){
      size_t a = aligns[j%8];
      size_t sz = 16 + (j*37 % 8000);
      void *p=NULL;
      if(posix_memalign(&p,a,sz)!=0 || !p){ printf("FAIL: alloc iter%d j%d\n",iter,j); fails++; keep[j]=NULL; continue; }
      if(((uintptr_t)p % a)!=0){ printf("FAIL: align iter%d j%d a=%zu\n",iter,j,a); fails++; }
      memset(p, j, sz);                  /* fill */
      keep[j]=p; ka[j]=sz;
      void *m = malloc(50+j); if(m){ memset(m,1,50+j); free(m); }  /* interleave */
    }
    /* verify fills survived (no overlap/corruption) then free */
    for(int j=0;j<64;j++){
      if(!keep[j]) continue;
      unsigned char *q=keep[j];
      for(size_t k=0;k<ka[j];k++) if(q[k]!=(unsigned char)j){ printf("FAIL: corrupt iter%d j%d off%zu\n",iter,j,k); fails++; break; }
      free(keep[j]);
    }
  }
  printf("posix_memalign test: %s (%d failures)\n", fails?"FAIL":"PASS", fails);
  return fails?1:0;
}
