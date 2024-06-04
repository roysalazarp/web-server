mkdir -p ./build

gcc -g -Wall -Wextra -Werror -pedantic -o ./build/main main.c -lpthread
