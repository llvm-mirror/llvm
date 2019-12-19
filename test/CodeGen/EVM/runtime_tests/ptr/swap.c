void swap(long *xp, long *yp);

long test(long a, long b) {
  long aa =a;
  long bb =b;
  swap(&a, &b);
  if (aa == b && bb ==a) {
    return 1;
  } else {
    return 0;
  }
}


void swap(long *xp, long *yp)  
{  
    long temp = *xp;  
    *xp = *yp;  
    *yp = temp;  
}  
