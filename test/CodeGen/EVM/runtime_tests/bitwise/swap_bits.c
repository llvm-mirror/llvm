long  swap(long n, long p, long q)
{
	// see if the bits are same. we use XOR operator to do so.
	if (((n & (1 << p)) >> p) ^ ((n & (1 << q)) >> q))
	{
		n ^= (1 << p);
		n ^= (1 << q);
	}
 
	return n;
}
