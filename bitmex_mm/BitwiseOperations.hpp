#pragma once


struct Bitwise
{	
	inline int bitwise_non_zero(int n)
    {   //returns 1 if (n > 0 || n < 0)
        return ((n | (~n + 1)) >> 31) & 1;
    }
    
    inline int bitwise_positive_only(int n)
    {   //equivalent to max(0, n)
        return (n & -(0 < n));
    }
    
    inline int bitwise_negative_abs(int n)
    {   //equivalent to -min(0, n)
        return -(n & -(0 > n));
    }
};