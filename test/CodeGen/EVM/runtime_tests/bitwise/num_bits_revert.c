int num_bits(int n, int m)
{
    int i, count = 0, a, b;
 
    for (i = 32 - 1;i >= 0;i--)
    {
        a = (n >> i)& 1;
        b = (m >> i)& 1;
        if (a != b)
            count++;
    }
    return count;
}
