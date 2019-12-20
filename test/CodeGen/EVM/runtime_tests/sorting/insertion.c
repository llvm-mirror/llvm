void insertionSort(long arr[], long n);

long test() {
  long a[5]; 
  a[0] = 5;
  a[1] = 4;
  a[2] = 3;
  a[3] = 1;
  a[4] = 2;

  insertionSort(a, 5);
  if (a[0] == 1 && a[1] == 2 && a[2] == 3 && a[3] == 4 && a[4] == 5) {
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
