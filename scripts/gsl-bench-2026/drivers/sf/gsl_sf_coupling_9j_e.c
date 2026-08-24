#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  int a,b,c,d,e,f,g,h,i;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");
  klee_make_symbolic(&d, sizeof(d), "d");
  klee_make_symbolic(&e, sizeof(e), "e");
  klee_make_symbolic(&f, sizeof(f), "f");
  klee_make_symbolic(&g, sizeof(g), "g");
  klee_make_symbolic(&h, sizeof(h), "h");
  klee_make_symbolic(&i, sizeof(i), "i");

  gsl_sf_result result;
  int status = gsl_sf_coupling_9j_e(a,b,c,d,e,f,g,h,i,&result);
  return 0;
}



