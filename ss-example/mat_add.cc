void mat_add(int n, int *a, int *b, int *c) {
  #pragma ss config
  #pragma ss stream
  for (int i = 0; i < n; i++) {
    #pragma ss dfg unroll(4) dedicated
    for (int j = 0; j < n; j++) {
      c[i * n + j] = a[i * n + j] + b[i * n + j];
    }
  }
}
