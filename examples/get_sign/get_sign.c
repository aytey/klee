/*
 * First KLEE tutorial: testing a small function
 */

//#include "klee/klee.h"
// klee -seed-file=getsign_seed.ktest get_sign.bc

//

int complex_function(int z){
  if (z == 0) return 0;
  if (z < 0) return -1;
  else{
    if (z == 10) return 2;
    if (z == 5) return 3;
    return 1;
  }
}

int get_sign(int x,int y) {
  if (x == 0){
     return 0;
  }
  
  if (x < 0){
     return -1;
  }else{ 
     if (x == 10)
         return complex_function(x + y);
     if (y == 5)
         return complex_function(x - y);
     return complex_function(2 * x + y);
  }
} 

int get_sign2(int x,int y) {
  if (x == 0){
     return 0;
  }
  
  if (x < 0){
     return -1;
  }else{ 
     if (x == 10)
         return 1;
     if (y == 5)
         return 2;
     return 3;
  }
} 

int main() {
  int a,b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  return get_sign2(a,b);
} 
