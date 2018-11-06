void vec_add(int n, int *a, int *b, int *c, int *d) {
  #pragma ss config
  #pragma ss stream
  #pragma ss dfg dedicated unroll(4)
  for (int i = 0; i < n; i++) {
    d[i] += a[i] * b[i] + c[i];
  }
}
