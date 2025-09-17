#include <stdio.h>

int fib(int n) {
    if (n < 2)
        return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    int n = 10;  // change this if you want different input
    printf("Fibonacci(%d) = %d\n", n, fib(n));
    return 0;
}
