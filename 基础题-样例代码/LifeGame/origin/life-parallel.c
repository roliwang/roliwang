/*
 * life-parallel.c —— 康威生命游戏多线程并行实现（POSIX 线程，面向海光 7495 / 8 核算力节点）
 *
 * 并行策略
 * ---------
 * 1. 按行分块 + 静态划分。内部细胞区域 [1, H-2] x [1, W-2] 被切成与线程数相等的
 *    连续行块，每个线程只写自己块内的行；块与块之间既无写冲突，也不需要消息传递
 *    （共享内存），仅通过每代一次的 barrier 保证代际顺序。
 * 2. 线程组常驻。线程只创建一次，步数循环在线程函数内部执行，避免每步重复
 *    创建/销毁线程的开销。
 * 3. 双缓冲。cur/nxt 两块全尺寸棋盘交替使用，每代整块顺序写、顺序读，
 *    对硬件预取友好。
 * 4. 水平/竖直分割的内核。朴素 8 邻居统计每格要做 9 次访存；这里先把每一行的
 *    "水平 3 邻居和" 算成一张表（每格 3 次访存、可完全向量化），再对每格做
 *    三次查表相加得到 8 邻居数。由于相邻输出行共享输入行，采用 3 行滚动缓冲，
 *    每代每行只需新算一行水平表，读流量约为朴素实现的 1/3。
 * 5. 无分支定值。new = (n == 3) | (alive & (n == 2))，比较产生 0/1，
 *    整个内层循环可被自动向量化。
 * 6. 集中式 barrier。所有线程对同一个到达计数器做原子加，最后一个到达者负责
 *    交换双缓冲并递增代际号；其余线程只自旋等待这一个共享标志，缓存一致性
 *    流量低于"由线程 0 顺序轮询每个线程的标志"。
 * 7. 绑核。线程启动时按线程编号固定到可用 CPU 列表的对应位置，减少核间迁移
 *    导致的缓存反复预热。
 *
 * 边界约定：世界有边界，第 0 行、第 H-1 行、第 0 列、第 W-1 列恒为死细胞，
 * 只有 x∈[1,W-2], y∈[1,H-2] 的内部细胞参与演化。
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>

#include "life.h"

/* 内部行数低于该值时并行收益不足以抵消线程开销，退化为单线程。 */
#define PARALLEL_MIN_ROWS 32

/* 默认线程数：目标节点单机 8 核算力。 */
#define DEFAULT_THREADS 8

#define MAX_PINNED_CPUS 1024

/* barrier 自旋等待的步数，超过后改用 sched_yield() 让出 CPU。 */
#define SPIN_BEFORE_YIELD 20000

/*
 * 线程绑核：仅在 Linux（glibc 提供 cpu_set_t 与 sched_setaffinity）下启用。
 * 非 Linux 平台自动退化为不绑核，不影响正确性。
 */
#if defined(__linux__)
#define LIFE_PIN_CPU 1
#else
#define LIFE_PIN_CPU 0
#endif

/*
 * 计算一行的水平 3 邻居和：dst[x] = src[x-1] + src[x] + src[x+1]。
 * 循环体内无循环携带依赖，可被自动向量化。
 */
static void hsum_row(const LifeCell *restrict src, int W,
                     unsigned char *restrict dst) {
    for (int x = 1; x < W - 1; ++x) {
        dst[x] = (unsigned char)(src[x - 1] + src[x] + src[x + 1]);
    }
}

/*
 * 计算 [y0, y1) 行的下一代。
 * hbuf 为长度 3*W 的线程私有滚动缓冲，分别保存 y-1 / y / y+1 三行的水平 3 邻居和。
 */
static void compute_range(const LifeCell *restrict cur, LifeCell *restrict nxt,
                          int W, int y0, int y1,
                          unsigned char *restrict hbuf) {
    unsigned char *h0 = hbuf;
    unsigned char *h1 = hbuf + W;
    unsigned char *h2 = hbuf + 2 * (size_t)W;

    const LifeCell *row = cur + (size_t)y0 * W;
    LifeCell *out = nxt + (size_t)y0 * W;

    hsum_row(cur + (size_t)(y0 - 1) * W, W, h0);   /* 第 y-1 行（ghost halo） */
    hsum_row(row, W, h1);                          /* 第 y   行             */

    for (int y = y0; y < y1; ++y) {
        hsum_row(cur + (size_t)(y + 1) * W, W, h2);   /* 第 y+1 行 */

        for (int x = 1; x < W - 1; ++x) {
            /* 3x3 块内 9 格之和减去自身，即 8 邻居数。 */
            int n = (int)h0[x] + (int)h1[x] + (int)h2[x] - (int)row[x];
            /* 存活：邻居 2 或 3；新生：邻居恰为 3。 */
            out[x] = (LifeCell)((n == 3) | ((int)row[x] & (n == 2)));
        }

        /* 滚动：h0<-h1<-h2<-h0，h2 下一轮覆写为第 y+2 行。 */
        unsigned char *t = h0; h0 = h1; h1 = h2; h2 = t;
        row += W;
        out += W;
    }
}

/* 单线程演化整个内部区域（小网格或线程数为 1 时使用）。 */
static void evolve_serial(LifeBoard *state, int steps) {
    int W = state->width, H = state->height;
    LifeCell *cur = state->cells;
    /*
     * nxt 必须零初始化：compute_range() 只写内部区域 [1,W-2]x[1,H-2]，
     * 第 0 行、第 H-1 行、第 0 列、第 W-1 列永远不被写入，靠这里的清零
     * 保证它们恒为死细胞（与题目的有边界世界一致）。初始棋盘的 cells
     * 同样由 read_life_board() 内的 calloc 清零，因此两块棋盘的边界
     * 始终为 0。
     */
    LifeCell *nxt = (LifeCell *)calloc((size_t)W * (size_t)H, sizeof(LifeCell));
    unsigned char *hbuf = (unsigned char *)malloc((size_t)3 * W);
    if (nxt == NULL || hbuf == NULL) {
        free(nxt);
        free(hbuf);
        fprintf(stderr, "Failed to allocate memory for next state.\n");
        return;
    }
    for (int s = 0; s < steps; ++s) {
        compute_range(cur, nxt, W, 1, H - 1, hbuf);
        LifeCell *t = cur; cur = nxt; nxt = t;
    }
    state->cells = cur;
    free(nxt);
    free(hbuf);
}

/* ---------------- 并行执行体 ---------------- */

typedef struct {
    /* 只读输入 */
    int W;
    int steps;
    int nchunks;                 /* 参与 barrier 的线程数 */
    unsigned char *hbufs;        /* 所有线程的滚动缓冲（线程 tid 用 [tid]） */
    size_t hbuf_stride;

    /* 线程私有 */
    int tid;
    int y0, y1;                  /* 本线程负责的内部行区间 */
    pthread_t thread;

    /*
     * 共享状态（只由 workers[0] 的字段承载，所有线程通过它通信）：
     *   arrived      —— 本代已完成计算的线程数（原子加）
     *   release_gen  —— 双缓冲交换完成的代际号
     *   abort_flag   —— 初始化失败时通知所有线程退出
     *   cur / nxt    —— 当前代与下一代的棋盘指针
     */
    volatile int arrived;
    volatile int release_gen;
    volatile int abort_flag;
    LifeCell *cur;
    LifeCell *nxt;
} Worker;

static inline void set_flag(volatile int *flag, int value) {
    __atomic_store_n(flag, value, __ATOMIC_RELEASE);
}

static inline int load_flag(volatile int *flag) {
    return __atomic_load_n(flag, __ATOMIC_ACQUIRE);
}

/*
 * 等待 release_gen 达到 want；若 abort_flag 被置位则立即返回（调用方据此退出）。
 * 先自旋（pause 指令），超过阈值后让出 CPU，避免在超配时霸占核。
 */
static void wait_release(volatile int *gen, int want, volatile int *abort) {
    int spins = 0;
    for (;;) {
        if (load_flag(gen) == want) return;
        if (load_flag(abort) != 0) return;
        if (++spins < SPIN_BEFORE_YIELD) {
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        } else {
            sched_yield();
            spins = SPIN_BEFORE_YIELD;   /* 之后一直 yield，避免重复计数 */
        }
    }
}

/* 绑核与 CPU 检测：Linux 下真实生效，其他平台为空操作（见下方定义）。 */
static void pin_thread_affinity(int slot);
static int detect_cpu_count(void);

static void *worker_main(void *arg) {
    Worker *self = (Worker *)arg;
    Worker *all = self - self->tid;   /* 线程数组基址 */
    int nthreads = self->nchunks;
    unsigned char *hbuf = self->hbufs + (size_t)self->tid * self->hbuf_stride;

    pin_thread_affinity(self->tid);

    for (int s = 0; s < self->steps; ++s) {
        if (load_flag(&all[0].abort_flag) != 0) break;

        compute_range(all[0].cur, all[0].nxt, self->W, self->y0, self->y1, hbuf);

        /*
         * 到达 barrier：最后一个到达的线程负责交换双缓冲并递增代际号。
         * 它对 arrived 的清零发生在 release 存储 release_gen 之前，
         * 而等待方以 acquire 语义读取 release_gen，因此所有线程都能看到
         * 新的 cur/nxt 与被清零的 arrived。
         */
        if (__atomic_fetch_add(&all[0].arrived, 1, __ATOMIC_ACQ_REL)
                == nthreads - 1) {
            LifeCell *t = all[0].cur;
            all[0].cur = all[0].nxt;
            all[0].nxt = t;
            all[0].arrived = 0;
            set_flag(&all[0].release_gen, s + 1);
        } else {
            wait_release(&all[0].release_gen, s + 1, &all[0].abort_flag);
            if (load_flag(&all[0].abort_flag) != 0) break;
        }
    }
    return NULL;
}

#if LIFE_PIN_CPU

/* 本进程被允许运行的 CPU 列表（按编号升序）；g_cpu_count < 0 表示不可用。 */
static int g_cpu_list[MAX_PINNED_CPUS];
static int g_cpu_count = -1;

static void init_cpu_list(void) {
    if (g_cpu_count >= 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0) {
        g_cpu_count = -1;   /* 查询失败则放弃绑核，不影响正确性 */
        return;
    }
    g_cpu_count = 0;
    for (int c = 0; c < CPU_SETSIZE; ++c) {
        if (CPU_ISSET(c, &set)) {
            if (g_cpu_count < MAX_PINNED_CPUS) {
                g_cpu_list[g_cpu_count++] = c;
            } else {
                break;   /* 掩码过大，只固定到已收集的部分 */
            }
        }
    }
    if (g_cpu_count == 0) g_cpu_count = -1;
}

/* 把当前线程固定到第 slot 个可用 CPU 上；失败时静默继续。 */
static void pin_thread_affinity(int slot) {
    if (g_cpu_count <= 0) return;
    int cpu = g_cpu_list[slot % g_cpu_count];
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

static int detect_cpu_count(void) {
    init_cpu_list();
    return (g_cpu_count > 0) ? g_cpu_count : 0;
}

#else  /* !LIFE_PIN_CPU */

/* 非 Linux 平台：不绑核，CPU 数由 sysconf 提供。 */
static void pin_thread_affinity(int slot) {
    (void)slot;
}

static int detect_cpu_count(void) {
#if defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 0;
#else
    return 0;   /* 无法探测时保持默认线程数 */
#endif
}

#endif /* LIFE_PIN_CPU */

/* 决定线程数：默认 8，尊重 LIFE_THREADS，且不超过实际可用核数。 */
static int choose_threads(void) {
    int want = DEFAULT_THREADS;
    const char *env = getenv("LIFE_THREADS");
    if (env != NULL) {
        int v = atoi(env);
        if (v > 0) want = v;
    }
    int cpus = detect_cpu_count();
    if (cpus > 0 && want > cpus) want = cpus;
    if (want < 1) want = 1;
    return want;
}

void simulate_life_parallel(LifeBoard *state, int steps) {
    if (state == NULL || state->cells == NULL) return;
    if (steps <= 0) return;

    int W = state->width, H = state->height;

    /* 内部细胞区域为空（含 W/H 为 1 或 2 的退化输入）时无需演化。 */
    if (W - 2 < 1 || H - 2 < 1) return;

    int inner_h = H - 2;
    int nthreads = choose_threads();
    if (nthreads > inner_h) nthreads = inner_h;

    /* 小网格单线程处理，省去线程创建与同步开销。 */
    if (nthreads <= 1 || inner_h < PARALLEL_MIN_ROWS) {
        evolve_serial(state, steps);
        return;
    }

    /*
     * nxt 同样必须零初始化（原因同 evolve_serial）：compute_range() 不写
     * 边界行/列，靠 calloc 保证它们恒为死细胞。
     */
    LifeCell *cur = state->cells;
    LifeCell *nxt = (LifeCell *)calloc((size_t)W * (size_t)H, sizeof(LifeCell));
    if (nxt == NULL) {
        fprintf(stderr, "Failed to allocate memory for next state.\n");
        return;
    }

    /* 每线程一块连续行区间 + 3*W 的滚动缓冲。 */
    int nchunks = nthreads;
    int chunk = (inner_h + nchunks - 1) / nchunks;
    size_t hbuf_stride = (size_t)3 * (size_t)W;
    unsigned char *hbufs =
        (unsigned char *)malloc((size_t)nthreads * hbuf_stride);
    Worker *workers = (Worker *)calloc((size_t)nthreads, sizeof(Worker));
    if (hbufs == NULL || workers == NULL) {
        free(nxt);
        free(hbufs);
        free(workers);
        fprintf(stderr, "Failed to allocate worker state.\n");
        return;
    }

    for (int t = 0; t < nthreads; ++t) {
        Worker *w = &workers[t];
        w->W = W;
        w->steps = steps;
        w->nchunks = nchunks;
        w->hbufs = hbufs;
        w->hbuf_stride = hbuf_stride;
        w->tid = t;
        w->y0 = 1 + t * chunk;
        w->y1 = w->y0 + chunk;
        if (w->y1 > H - 1) w->y1 = H - 1;
        if (w->y0 > w->y1) w->y0 = w->y1;   /* 线程数超过行数时可能为空块 */
        w->cur = cur;
        w->nxt = nxt;
        /* arrived / release_gen / abort_flag 已由 calloc 置 0 */
    }

    /*
     * 主线程作为线程 0 参与计算，另外创建 nthreads-1 个工作线程。
     * 若某个线程创建失败，先置 abort_flag 让已启动的线程从 barrier 中退出，
     * 再回收它们，避免它们永久等待一个永远不会到来的代际号。
     */
    int created = 1;   /* 线程 0 由主线程承担，无需创建 */
    for (int t = 1; t < nthreads; ++t) {
        if (pthread_create(&workers[t].thread, NULL, worker_main,
                           &workers[t]) != 0) {
            fprintf(stderr, "Error: failed to create worker thread %d.\n", t);
            set_flag(&workers[0].abort_flag, 1);
            break;
        }
        created = t + 1;
    }

    if (created == nthreads) {
        worker_main(&workers[0]);
    }

    for (int t = 1; t < created; ++t) {
        pthread_join(workers[t].thread, NULL);
    }

    if (created == nthreads) {
        /* 全部线程正常完成：workers[0].cur 是最终棋盘，nxt 是待释放的缓冲。 */
        state->cells = workers[0].cur;
        free(workers[0].nxt);
    }
    /* 创建失败时保持 state 不变（初始棋盘），调用方仍能得到合法的输出。 */

    free(hbufs);
    free(workers);
}
