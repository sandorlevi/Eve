static inline int fetch_and_add(u64 * variable, u64 value)
{
    asm volatile("lock; xaddq %0, %1"
                 : "=r" (value), "=m" (*variable)
                 : "0"(value), "m"(*variable)
                 :"memory", "cc");
    return value;
}
