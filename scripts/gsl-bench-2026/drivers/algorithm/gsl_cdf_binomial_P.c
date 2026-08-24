//
// add by yx
//
#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
#include <stdio.h>

int main()
{
    double p;
    unsigned int k;
    unsigned int n;
    klee_make_symbolic(&p,sizeof(p),"p");
    klee_make_symbolic(&k,sizeof(k),"k");
    klee_make_symbolic(&n,sizeof(n),"n");
    double result = gsl_cdf_binomial_P (k,p,n);
//    printf("%lf\n", result);
}
