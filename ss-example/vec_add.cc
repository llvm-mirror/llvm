void vec_add(int *a, int *b, int *c) {
  #pragma ss config
  #pragma ss stream
  #pragma ss dfg dedicated unroll(4)
  for (int i = 0; i < 128; i++) {
    c[i] = a[i] + b[i];
  }
}
