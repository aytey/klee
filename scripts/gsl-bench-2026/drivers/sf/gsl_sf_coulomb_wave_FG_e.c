#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a,b,c;
  int d;
  
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");
  klee_make_symbolic(&d, sizeof(d), "d");

  gsl_sf_result F, Fp, G, Gp;
  double Fe,Ge;
  int status = gsl_sf_coulomb_wave_FG_e(a,b,c,d,&F, &Fp, &G, &Gp, &Fe, &Ge);
  return 0;
}



