long n_bit_position(long number,long position)
{
    long result = (number>>(position));
    return result & 1;
}
