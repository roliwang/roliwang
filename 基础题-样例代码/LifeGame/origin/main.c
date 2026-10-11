/**
 * @file main.c
 * @brief 生命游戏程序入口（串行基准版）
 *
 * 用法：./life STEPS FILENAME
 *   STEPS    - 模拟步数
 *   FILENAME - 输入文件路径
 *
 * 计时由外部脚本完成，程序本身不输出时间信息。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "life.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s STEPS FILENAME\n", argv[0]);
        return 1;
    }

    int steps = atoi(argv[1]);
    char input_file[256];
    strcpy(input_file, argv[2]);

    /* 从文件加载初始棋盘 */
    LifeBoard *board = malloc(sizeof(LifeBoard));
    FILE *in = fopen(input_file, "r");
    if (in == NULL) {
        fprintf(stderr, "Cannot open file: %s\n", input_file);
        return 1;
    }
    read_life_board(in, board);
    fclose(in);

    /* 串行模拟演算 */
    simulate_life_serial(board, steps);

    /* 输出最终棋盘状态到 stdout */
    print_life_board(board);

    /* 释放资源 */
    destroy_life_board(board);

    return 0;
}

