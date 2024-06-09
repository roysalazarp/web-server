mkdir -p ./build

gcc -std=c89 -g -Wall -Wextra -Werror -pedantic -Wno-unused-variable -Wno-unused-parameter -Wno-declaration-after-statement -Wno-overlength-strings -Wno-format-overflow -o ./build/main main.c -I/usr/include/postgresql -lpq -lpthread
