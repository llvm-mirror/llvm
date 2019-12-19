void swap(long *xp, long *yp);
void bubbleSort(long arr[], long n);

long sort() {
  long a[5];
  a[0] = 5;
  a[1] = 4;
  a[2] = 3;
  a[3] = 1;
  a[4] = 2;
  bubbleSort(a, 5);
  if (a[0] == 1) {
    return 1;
  } else {
    return 0;
  }
}
  
// A function to implement bubble sort  
void bubbleSort(long arr[], long n)  
{  
    long i, j;  
    for (i = 0; i < n-1; i++)      
      
    // Last i elements are already in place  
    for (j = 0; j < n-i-1; j++)  
        if (arr[j] > arr[j+1])  
            swap(&arr[j], &arr[j+1]);  
}  

void swap(long *xp, long *yp)  
{  
    long temp = *xp;  
    *xp = *yp;  
    *yp = temp;  
}  
