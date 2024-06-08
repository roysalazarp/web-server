mkdir -p ./build

gcc -std=c89 -g -Wall -Wextra -Werror -pedantic -Wno-unused-variable -Wno-unused-parameter -Wno-declaration-after-statement -o ./build/main main.c -lpthread
