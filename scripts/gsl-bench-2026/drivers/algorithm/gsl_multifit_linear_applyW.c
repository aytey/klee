/*=====add by yx =====*/

#include <klee/klee.h>
#include <stdio.h>
#include <gsl/gsl_multifit.h>

int main ()
{
  double w0=1,w1=2,w2=3;

  klee_make_symbolic(&w0, sizeof(w0), "w0");
  klee_make_symbolic(&w1, sizeof(w1), "w1");
  klee_make_symbolic(&w2, sizeof(w2), "w2");

  gsl_matrix *X = gsl_matrix_alloc(3,3);
  gsl_vector *w = gsl_vector_alloc(3);
  gsl_vector *y = gsl_vector_alloc(3);
  gsl_matrix *WX = gsl_matrix_alloc(3,3);
  gsl_vector *Wy = gsl_vector_alloc(3);

  gsl_matrix_set (X, 0, 0, 3);
  gsl_matrix_set (X, 0, 1, 9);
  gsl_matrix_set (X, 0, 2, 27);
  gsl_matrix_set (X, 1, 0, 2);
  gsl_matrix_set (X, 1, 1, 4);
  gsl_matrix_set (X, 1, 2, 8);
  gsl_matrix_set (X, 2, 0, 4);
  gsl_matrix_set (X, 2, 1, 16);
  gsl_matrix_set (X, 2, 2, 64);
  gsl_vector_set(w, 0, w0);
  gsl_vector_set(w, 1, w1);
  gsl_vector_set(w, 2, w2);
  gsl_vector_set(y, 0, 2);
  gsl_vector_set(y, 1, 3);
  gsl_vector_set(y, 2, 4);

  int status = gsl_multifit_linear_applyW(X, w, y, WX, Wy);
//  printf("%d\n",status);

  gsl_matrix_free (X);
  gsl_vector_free (w);
  gsl_vector_free (y);
  gsl_matrix_free (WX);
  gsl_vector_free (Wy);

  return 0;
}