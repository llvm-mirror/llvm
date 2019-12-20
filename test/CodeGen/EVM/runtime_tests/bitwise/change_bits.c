long changebits(long num1, long num2, long pos1, long pos2)
{
    long temp1, temp_1, buffer2, bit1 = 0, bit2 = 0, counter = 0, a = 1;
 
    temp1 = num1;
    temp_1 = num1;
    buffer2 = num2;
    for (;pos1 <= pos2;pos1++)        
    {
        a = 1;
        num1 = temp_1;
        num2 = buffer2;
        while (counter <= pos1)
        {
            if (counter  == pos1)        
                bit1 = (num1&1);    //placing the bit of position 1 in bit1
            counter++;
            num1 >>= 1;
        }
        counter = 0;
        while (counter <= pos1)
        {
            if (counter == pos1)
                bit2 = (num2&1);        //placing the bit of position 2 in bit2
            counter++;
            num2 >>= 1;
        }
        counter = 0;
        if (bit1 == bit2);
        else
        {
            while (counter++<pos1)
                a = a << 1;                
            temp1 ^= a;    //placing the repplaced bit longeger longo temp1 variable
        }
        counter = 0;
    }
    return temp1;
}
