#include <stdio.h>
#include <ss-intrin/ss_insts.h>
#include "./vecadd.dfg.h"

#define N 128

double a[N];
double b[N];
double c[N];

int main() {

#ifndef AUTO
  SS_DMA_READ(a, 0, 8 * N, 1, P_vecadd_a);
  SS_DMA_READ(b, 0, 8 * N, 1, P_vecadd_b);
  SS_DMA_WRITE(P_vecadd_c, 0, 8 * N, 1, c);
  SS_WAIT_ALL();
#else
  #pragma ss config
  #pragma ss stream
  #pragma ss dfg dedicated
  for (int i = 0; i < N; ++i) {
    c[i] = a[i] + b[i];
  }
#endif

}
