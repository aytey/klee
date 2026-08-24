#include <gsl/gsl_sf.h>
#include <klee/klee.h>

double fc_array[100],fcp_array[100],gc_array[100],gcp_array[100];
int main() {
  double a,c,d,f_exp,g_exp;
  int b;
  
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");
  klee_make_symbolic(&d, sizeof(d), "d");

  int status = gsl_sf_coulomb_wave_FGp_array(a,b,c,d,
                    fc_array,fcp_array,gc_array,gcp_array,&f_exp,&g_exp);
  return 0;
}



