# Pre install docker environment

We have deployed the experimental environment on docker. Please pre install docker on your host

# Download

Download docker image:
```sh
$ sudo docker pull apsecpaper/apsecpaper:v4
```

If the image is pulled successfully, please check there is an image named apsecpaper/apsecpaper exists.
```sh
$ sudo docker images
REPOSITORY              TAG       IMAGE ID       CREATED          SIZE
apsecpaper/apsecpaper   v4        1883a4f177d4   9 hours ago      5.56GB
```


Start to run the container in interactive mode.
```sh
$ sudo docker run -it apsecpaper/apsecpaper:v4 bash
```

# Run a example

Enter the directory `/home/apsec/moti` and list all contents.

```sh
$ cd /home/apsec/moti
```

The following code is the example mentioned in our paper.

```c
#include <klee/klee.h>

int main() {
float a,b,c;
klee_make_symbolic(&a, sizeof(a), "a");
klee_make_symbolic(&b, sizeof(b), "b");
klee_make_symbolic(&c, sizeof(c), "c");

if (cos(a) > log(b)){
  if (sin(a) < log(b)){
    c = c -1.0;
    if (c == 1.1)
      printf("Neve reach here !\n");
    }
  }
  return 0;
}

```

We need to confirm the running parameters in the script before analyzing the code:

```sh
$ vi run_solver.c

SEARCH="bfs" # ["bfs","dfs"]
SOLVER_TYPE="smt" #["smt","jfs","dreal-fuzz","gosat","smt-dreal"]
......
```

There are 5 so*__*lving modes (`smt`: BVFP solver),(`jfs`: FUZZ solver),(`dreal-fuzz`: RSO solver),(`gosat`: Search solver),(`smt-dreal`: Our method)

And 2 search modes(`bfs`: BFS heuristic),(`dfs`: DFS heuristic)

If you want to run script with the `bfs + smt-dreal` configuration
```sh
$ (exit vim)
$ ./run_solver.sh
```

Less than 5 seconds, we can see the output at the terminal. The pathes are explored completely.
```sh

...... 
KLEE: WARNING: SMT-DReal: DReal evaluate FAILED ! Using JFS with seeds to solve.
KLEE: WARNING: FUZZ with seed: evaluate SUCCESS !
KLEE: WARNING: SMT-DREAL: Z3 solving UNSAT! Remove this state !

KLEE: done: total instructions = 11863
KLEE: done: completed paths = 3
KLEE: done: partially completed paths = 0
KLEE: done: generated tests = 3
```

If you want to analyze this program using BVFP solver, please modify the `SOLVER_TYPE` configuration as `smt` in `run_solver.sh`. KLEE will take a long time (more than 30min) to finish the program (you can CTRL+C to intrupt this program), because the source code of `sin/cos/log` are not avalible. it can not explore all pathes fastly:

```sh
$ ./run_solver.sh
...... # take a long time

KLEE: done: total instructions = 13198
KLEE: done: completed paths = 0
KLEE: done: partially completed paths = 1
KLEE: done: generated tests = 1
```

# Obtain experimental results

Our experiments were performed on a server with Intel(R) Xeon(R) Platinum 8269CY CPU (2.50GHz) and the operating system is Ubuntu 18.04 LTS. 

To obtain the results, a machine with similar CPUs is required. Moreover, our experiments were run in 100 parallel.

## Analyze a program in Benchmark

Navigate to `/home/apsec/bench_all` and list the contents.

```sh
$ cd /home/apsec/bench_all
```

The basic configuration of the current experiment is as follows.

```sh
$ cat run_solver.sh 

......
EXE_TIME=3600
SOLVER="smt"
HERISTIC="bfs"
......
work_dic=$1
dirv=$2
```

If you want to get the result of ISC solver, please modify `run_fp2int.sh` as the same with `run_soler.sh`

We mainly focus on the following parameters:
* `EXE_TIME`: Maximum symbolic execution time
* `SOLVER`: This option is used to specify the solution method.
* `HERISTIC`: Search heuristic.

Now, let's run this script:
```sh
$ ./run_solver.sh  gegenbauer gsl_sf_gegenpoly_1_e.c
     ......
     Running ==== > gsl_sf_gegenpoly_1_e
     ...... # please wait 2 min, you can CTRL+C to interrupt it.
```

After running, log and test cases are generated in the corresponding directory, you can read the log as follow:

```sh
$ vi gegenbauer/gsl_sf_gegenpoly_1_e.runlog
......
KLEE: WARNING: Z3: Z3 evalueate SUCCESS !
KLEE: ERROR: gegenbauer.c:38: FloatPointCheck: Common Overflow found !
KLEE: NOTE: now ignoring this error at this location
KLEE: WARNING: Z3: Z3 evalueate SUCCESS !
KLEE: ERROR: gegenbauer.c:38: FloatPointCheck: Common Underflow found !
KLEE: NOTE: now ignoring this error at this location
KLEE: WARNING: Z3: Z3 evalueate SUCCESS !
KLEE: WARNING: Z3: Z3 evalueate SUCCESS !
......
```

We can get the coverage information by running the script:

```sh
$ cd /home/apsec/bench_all
$ ./repaly.sh

......# some info
     Running ==== > gsl_sf_gegenpoly_1_e
====  Replay Ktest ====
CHECK:  KTests have been generated !
/home/apsec/gsl/specfunc/.libs/gegenbauer.c
KTest : /home/apsec/bench_all/gegenbauer/gsl_sf_gegenpoly_1_e_output/test000001.ktest
KLEE-REPLAY: klee_assume(0)!
KLEE-REPLAY: NOTE: Test file: /home/apsec/bench_all/gegenbauer/gsl_sf_gegenpoly_1_e_output/test000001.ktest
KLEE-REPLAY: NOTE: Arguments: "./gsl_sf_gegenpoly_1_e" 
KLEE-REPLAY: NOTE: Storing KLEE replay files in /tmp/klee-replay-zAJZXr
KLEE-REPLAY: NOTE: EXIT STATUS: NORMAL (8 seconds)
KLEE-REPLAY: NOTE: removing /tmp/klee-replay-zAJZXr
gegenbauer.c: No such file or directory
57.14285714285714 , 5
...... # some info

```

All coverage information can be found in `res_all.txt`. The four columns are the name of benchmark, the coverage of analyzed function, total covered lines of code, and the execution time, respectively.

```sh
$ cat res_all.txt

...... 
gsl_sf_taylorcoeff_e , 0,
gsl_sf_gegenpoly_1_e , 100.0 , 9,
gsl_sf_gegenpoly_2_e , 0,
......
```

After running, you can use `clean_all.sh` to clear all outputs (remember to save results if necessary):

```sh
$ ./clean_all.sh
```

## Run in 100 parallel

Before running the script, you must ensure that the machine used has more than 104 cores and 192GB of memory. Similarly, you should configure your running options.

Suppose we use `DFS + BVFP(smt)` configuration for running an hour. Then, `run_solver.sh` must be set as:

```sh
$ vi run_solver.sh

......
EXE_TIME=3600
SOLVER="smt"
HERISTIC="dfs"
......
```

You can also modify the parallel quantity in the script:

```sh
$ vi multi_process.sh

......
def runOneTestCase(workDic, testName):
  # if you want to get the ISC method, change run_solver to run_fp2int
  cmdStr = "./run_solver.sh %s %s" %(workDic, testName)
  print(cmdStr)
  os.system(cmdStr)
......
pool = multiprocessing.Pool(processes=100) # parallel of 100
......
```

Then you can use `nohup python multi_process.py &` to execute all benchmarks in parallel.

```sh
$ nohup python multi_process.py &
```

If there are 100 programs in parallel, it may take 3 hours to get the results.





