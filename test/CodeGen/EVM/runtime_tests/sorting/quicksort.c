void quicksort(long* number,long first,long last);

long sort() {
  long a[5];
  a[0] = 5;
  a[1] = 4;
  a[2] = 3;
  a[3] = 1;
  a[4] = 2;
  quicksort(a, 0, 4);
  if (a[0] == 1 && a[1] == 2 && a[2] == 3 && a[3] == 4 && a[4] == 5) {
    return 1;
  } else {
    return 0;
  }
}

void quicksort(long* number,long first,long last){
   long i, j, pivot, temp;

   if(first<last){
      pivot=first;
      i=first;
      j=last;

      while(i<j){
         while(number[i]<=number[pivot]&&i<last)
            i++;
         while(number[j]>number[pivot])
            j--;
         if(i<j){
            temp=number[i];
            number[i]=number[j];
            number[j]=temp;
         }
      }

      temp=number[pivot];
      number[pivot]=number[j];
      number[j]=temp;
      quicksort(number,first,j-1);
      quicksort(number,j+1,last);

   }
}

