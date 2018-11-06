void multi(int n, int *a, int *b) {
  #pragma omp taskgroup
  {
    #pragma omp task
    #pragma omp simd simdlen(4)
    for (int i = 0; i < n; ++i) {
      a[i] = a[i] + 1;
    }
    #pragma omp task
    #pragma omp simd simdlen(4)
    for (int i = 0; i < n; ++i) {
      b[i] = a[i] + 1;
    }
  }
}
