#include <stdio.h>
#include <iostream>
#include "Stack.hpp"

using namespace std;

int main(int argc, char const *argv[])
{
    Stack shreif;
    shreif.push(5);
    shreif.push(4);
    shreif.push(3);
    printf("%d \n", shreif.is_empty());
    printf("%d \n", shreif.peek());
    printf("%d \n", shreif.pop());
    printf("%d \n", shreif.pop());
    printf("%d \n", shreif.pop());
    printf("%d \n", shreif.peek());
    printf("%d \n", shreif.is_empty());

    return 0;
}
