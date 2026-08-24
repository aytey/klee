//
// Created by liukunlin on 2021/8/29.
//
#include "klee/klee.h"
#include "gsl/gsl_cdf.h"
int main()
{
    double x,nu;
    klee_make_symbolic(&x, sizeof(x),"x");
    klee_make_symbolic(&nu,sizeof(nu),"nu");
    gsl_cdf_chisq_Qinv (x,nu);
}
