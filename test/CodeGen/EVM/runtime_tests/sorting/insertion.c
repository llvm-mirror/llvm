void insertionSort(long arr[], long n);

long test() {
  long a[2]; 
  a[0] = 1;
  a[1] = 0;

  insertionSort(a, 2);
  if (a[0] == 0 && a[1] == 1) {
    return 1;
  } else {
    return 0;
  }
}

void insertionSort(long arr[], long n) 
{ 
    long i, key, j; 
    for (i = 1; i < n; i++) { 
        key = arr[i]; 
        j = i - 1; 
  
        /* Move elements of arr[0..i-1], that are 
          greater than key, to one position ahead 
          of their current position */
        while (j >= 0 && arr[j] > key) { 
            arr[j + 1] = arr[j]; 
            j = j - 1; 
        } 
        arr[j + 1] = key; 
    } 
} 
