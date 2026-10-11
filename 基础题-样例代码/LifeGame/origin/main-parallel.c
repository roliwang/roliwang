/**
 * @file main-parallel.c
 * @brief 生命游戏程序入口（多线程并行版）
 *
 * 用法：./life STEPS FILENAME
 *   STEPS    - 模拟步数
 *   FILENAME - 输入文件路径
 *
 * 除调用 simulate_life_parallel() 外，文件读取、资源释放逻辑与 main.c 一致。
 * 输出格式与 print_life_board() 完全相同（首行 "W H"，随后 H 行、每行 W 个
 * '*'/'.' 字符），这里用整行 fwrite 代替逐字符 printf，减少大棋盘的输出开销。
 *
 * 可选环境变量 LIFE_THREADS：覆盖默认线程数（默认 8，即目标节点单机核数）。
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

    /* 并行模拟演算 */
    simulate_life_parallel(board, steps);

    /* 输出最终棋盘状态到 stdout（格式与 print_life_board 一致，整行写入）。 */
    int W = board->width, H = board->height;
    char *line = (char *)malloc((size_t)W + 2);
    if (line == NULL) {
        /* 分配失败时退回框架自带的输出函数，保证结果一定被写出。 */
        print_life_board(board);
    } else {
        printf("%d %d\n", W, H);
        for (int y = 0; y < H; ++y) {
            const LifeCell *row = board->cells + (size_t)y * W;
            for (int x = 0; x < W; ++x) {
                line[x] = row[x] ? '*' : '.';
            }
            line[W] = '\n';
            fwrite(line, 1, (size_t)W + 1, stdout);
        }
        free(line);
    }

    /* 释放资源 */
    destroy_life_board(board);

    return 0;
}
