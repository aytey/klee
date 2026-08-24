#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b,sn,cn,dn;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  gsl_sf_result result;
  int status = gsl_sf_elljac_e(a,b,&sn,&cn,&dn);
  return 0;
}



