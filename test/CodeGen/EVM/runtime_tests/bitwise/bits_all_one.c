long bits(long num)
{
	long count = 0, n = 0, i = 0;
 
	n = num;
	if (num == 0)
	{
      return 0;
	}
	while (n)
	{
		count ++;
		n = n >> 1;
	}
	for (i = 0; i < count; i++)
	{
		if (((num >> i) & 1) == 1)
		{
			continue;
		}
		else
		{
          return 0;
		}
	}
	return 1;
}
