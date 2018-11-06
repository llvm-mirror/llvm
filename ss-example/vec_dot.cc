int vec_dot(int n, int *a, int *b) {
  int res = 0;
  #pragma ss config
  #pragma ss stream
  #pragma ss dfg dedicated unroll(4)
  for (int i = 0; i < n; i++) {
    res += a[i] * b[i];
  }
  return res;
}
