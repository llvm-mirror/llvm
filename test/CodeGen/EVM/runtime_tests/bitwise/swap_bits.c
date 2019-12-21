long  swap(long n, long p, long q)
{
	// see if the bits are same. we use XOR operator to do so.
	if (((n & ((long)1 << p)) >> p) ^ ((n & ((long)1 << q)) >> q))
	{
		n ^= ((long)1 << p);
		n ^= ((long)1 << q);
	}
 
	return n;
}
