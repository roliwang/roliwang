import pygame
import random
import sys
from pygame.locals import *

# 初始化 Pygame
pygame.init()


# 定义难度级别
difficulties = {
    '简单': {
        'game_time': 120,  # 游戏时间，单位：秒
        'tiles_per_position': 2,  # 每个位置的图案数量
        'bomb_limit': 2,  # 炸弹使用次数限制
    },
    '困难': {
        'game_time': 90,
        'tiles_per_position': 3,
        'bomb_limit': 1,
    },
    '地狱': {
        'game_time': 60,
        'tiles_per_position': 4,
        'bomb_limit': 0,  # 地狱模式可能不允许使用炸弹
    }
}
# 默认难度
current_difficulty = '简单'

# 定义常量
WIDTH, HEIGHT = 600, 600
TILE_SIZE = 100
ROWS, COLS = 6, 6
FPS = 30
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
BG_COLOR = (200, 200, 200)
FONT = pygame.font.SysFont('Arial', 36)
GAME_TIME = 60  # 游戏时间限制，单位：秒

# 创建窗口
screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("消了个消")

# 加载图案图片
pattern_images = [pygame.image.load(f"D:/软件工程/pattern_{i}.png") for i in range(1, 7)]
pattern_images = [pygame.transform.scale(img, (TILE_SIZE, TILE_SIZE)) for img in pattern_images]

# 创建特殊图案
try:
    bomb_image = pygame.image.load(f"D:/软件工程/bomb.png")  # 炸弹图案
    bomb_image = pygame.transform.scale(bomb_image, (TILE_SIZE, TILE_SIZE))
except pygame.error as e:
    print(f"无法加载炸弹图案：{e}")
    bomb_image = None

# 加载炸弹图标
try:
    bomb_icon = pygame.image.load(f"D:/软件工程/bomb.png")  # 炸弹图标
    bomb_icon = pygame.transform.scale(bomb_icon, (50, 50))
except pygame.error as e:
    print(f"无法加载炸弹图标：{e}")
    bomb_icon = None

# 创建游戏板
board = [[[None] for _ in range(COLS)] for _ in range(ROWS)]
selected = []
score = 0
bomb_used = False  # 标记炸弹是否已经使用

# 创建一个 Clock 对象
timer = pygame.time.Clock()


# 初始化游戏
def initialize_game(difficulty):
    global GAME_TIME, FPS, TILE_SIZE, ROWS, COLS, bomb_limit, bomb_used
    settings = difficulties[difficulty]
    GAME_TIME = settings['game_time']  # 设置游戏时间
    bomb_limit = settings['bomb_limit']  # 设置炸弹使用次数
    bomb_used = 0  # 重置炸弹使用次数

# 显示开始界面
def show_start_screen():
    global current_difficulty
    screen.fill(BG_COLOR)  # 先填充背景色
    try:
        # 加载背景图片
        start_screen_image = pygame.image.load("D:/软件工程/background.jpg")  # 确保路径正确
        start_screen_image = pygame.transform.scale(start_screen_image, (WIDTH, HEIGHT))
        screen.blit(start_screen_image, (0, 0))  # 绘制背景图片
    except pygame.error as e:
        print(f"无法加载开始界面背景图片：{e}")

    # 定义按钮文本
    easy_text = FONT.render("Easy", True, WHITE)
    hard_text = FONT.render("Hard", True, WHITE)
    veryhard_text = FONT.render("Very Hard", True, WHITE)
    start_text = FONT.render("Click to Start", True, WHITE)

    # 定义按钮位置
    easy_rect = easy_text.get_rect(center=(WIDTH / 2, 100))
    hard_rect = hard_text.get_rect(center=(WIDTH / 2, 200))
    veryhard_rect = veryhard_text.get_rect(center=(WIDTH / 2, 300))
    start_rect = start_text.get_rect(center=(WIDTH / 2, HEIGHT - 100))

    # 绘制按钮文本
    screen.blit(easy_text, easy_rect)
    screen.blit(hard_text, hard_rect)
    screen.blit(veryhard_text, veryhard_rect)
    screen.blit(start_text, start_rect)

    pygame.display.update()

    while True:
        for event in pygame.event.get():
            if event.type == QUIT:
                pygame.quit()
                sys.exit()
            elif event.type == MOUSEBUTTONDOWN:
                x, y = event.pos
                if start_rect.collidepoint(x, y):
                    initialize_game(current_difficulty)
                    return  # 点击开始游戏
                elif easy_rect.collidepoint(x, y):
                    current_difficulty = '简单'
                    initialize_game(current_difficulty)
                    print(f"当前难度已切换到: {current_difficulty}")
                    break  # 切换到简单难度
                elif hard_rect.collidepoint(x, y):
                    current_difficulty = '困难'
                    initialize_game(current_difficulty)
                    print(f"当前难度已切换到: {current_difficulty}")
                    break  # 切换到困难难度
                elif veryhard_rect.collidepoint(x, y):
                    current_difficulty = '地狱'
                    initialize_game(current_difficulty)
                    print(f"当前难度已切换到: {current_difficulty}")
                    break  # 切换到地狱难度
# 生成图案
def generate_board():
    for row in range(ROWS):
        for col in range(COLS):
            stack = []
            num_tiles = random.randint(1, difficulties[current_difficulty]['tiles_per_position'])
            for _ in range(num_tiles):
                pattern_index = random.randint(0, len(pattern_images) - 1)
                stack.append(pattern_index)
            board[row][col] = stack

# 检查胜利条件
def check_win():
    for row in range(ROWS):
        for col in range(COLS):
            if board[row][col]:  # 如果还有图案
                return False
    return True

# 绘制游戏板
def draw_board():
    screen.fill(BG_COLOR)
    for row in range(ROWS):
        for col in range(COLS):
            if board[row][col]:  # 如果该位置有图案堆
                top_tile = board[row][col][0]  # 只取最顶层的图案
                screen.blit(pattern_images[top_tile], (col * TILE_SIZE, row * TILE_SIZE))
    draw_score()
    draw_timer()
    draw_toolbar()

# 绘制分数
def draw_score():
    score_text = FONT.render(f"Score: {score}", True, WHITE)
    screen.blit(score_text, (10, 10))

# 绘制计时器
def draw_timer():
    time_elapsed = pygame.time.get_ticks() - start_ticks
    time_left = max(GAME_TIME * 1000 - time_elapsed, 0)
    timer_text = FONT.render(f"Time: {time_left // 1000:02d}:{(time_left // 10) % 100:02d}", True, WHITE)
    screen.blit(timer_text, (WIDTH - 150, 10))

# 绘制工具栏
def draw_toolbar():
    if bomb_icon:
        screen.blit(bomb_icon, (WIDTH - 60, HEIGHT - 70))
        if bomb_used < bomb_limit:
            pygame.draw.rect(screen, (0, 255, 0), (WIDTH - 60, HEIGHT - 70, 50, 50), 2)  # 绿色边框表示炸弹未使用完
        else:
            pygame.draw.rect(screen, (255, 0, 0), (WIDTH - 60, HEIGHT - 70, 50, 50), 2)  # 红色边框表示炸弹已用完


# 检查是否有无法消除的图案
def can_pairs_be_removed():
    for row in range(ROWS):
        for col in range(COLS):
            stack = board[row][col]
            if stack:
                # 如果堆栈中只有一个图案或图案无法配对，则返回False
                if len(stack) == 1 or (len(stack) > 1 and all(stack[0] != pattern for pattern in stack[1:])):
                    return False
    return True

# 显示游戏胜利界面
def show_win_screen():
    try:
        # 加载胜利界面背景图片
        win_screen_image = pygame.image.load(f"D:/软件工程/win.jpg")  # 替换为你的胜利界面背景图片文件路径
        win_screen_image = pygame.transform.scale(win_screen_image, (WIDTH, HEIGHT))
    except pygame.error as e:
        print(f"无法加载胜利界面背景图片：{e}")
        win_screen_image = None

    if win_screen_image:
        screen.blit(win_screen_image, (0, 0))  # 绘制背景图片

    win_text = FONT.render("You Win!", True, WHITE)
    screen.blit(win_text, (WIDTH // 2 - 100, HEIGHT // 2))
    pygame.display.flip()
    pygame.time.wait(5000)  # 显示5秒

# 显示游戏失败界面
def show_lose_screen():
    try:
        # 加载失败界面背景图片
        lose_screen_image = pygame.image.load(f"D:/软件工程/lose.jpg")  # 替换为你的失败界面背景图片文件路径
        lose_screen_image = pygame.transform.scale(lose_screen_image, (WIDTH, HEIGHT))
    except pygame.error as e:
        print(f"无法加载失败界面背景图片：{e}")
        lose_screen_image = None

    if lose_screen_image:
        screen.blit(lose_screen_image, (0, 0))  # 绘制背景图片

    lose_text = FONT.render("You Lose!", True, WHITE)
    screen.blit(lose_text, (WIDTH // 2 - 100, HEIGHT // 2))
    pygame.display.flip()
    pygame.time.wait(8000)  # 显示8秒

# 显示“看广告通关”界面
def show_watch_ad_screen():
    try:
        # 加载广告界面背景图片
        ad_screen_image = pygame.image.load(f"D:/软件工程/ad.jpg")  # 替换为你的广告界面背景图片文件路径
        ad_screen_image = pygame.transform.scale(ad_screen_image, (WIDTH, HEIGHT))
    except pygame.error as e:
        print(f"无法加载广告界面背景图片：{e}")
        ad_screen_image = None

    if ad_screen_image:
        screen.blit(ad_screen_image, (0, 0))  # 绘制背景图片

    ad_text = FONT.render("Watch Ad to Pass!", True, WHITE)
    screen.blit(ad_text, (WIDTH // 2 - 100, HEIGHT // 2))
    pygame.display.flip()
    pygame.time.wait(10000)  # 显示10秒

# 检查所有图案是否都无法配对
def no_pairs_left():
    for row in range(ROWS):
        for col in range(COLS):
            stack = board[row][col]
            if stack:
                # 检查当前图案是否与任何其他图案可以配对
                for other_row in range(ROWS):
                    for other_col in range(COLS):
                        if (other_row, other_col) != (row, col):
                            other_stack = board[other_row][other_col]
                            if other_stack and stack[0] == other_stack[0]:
                                return False
    return True

# 特殊图案效果
def effect_bomb():
    global selected, bomb_used, score
    if not bomb_used >= bomb_limit and len(selected) == 2:
        # 获取玩家选中的两张图片的位置
        r1, c1 = selected[0]
        r2, c2 = selected[1]
        # 消除这两张图片
        board[r1][c1] = None
        board[r2][c2] = None
        score += 20  # 增加分数，可以根据需要调整
        bomb_used += 1  # 更新炸弹使用次数
        selected.clear()  # 清空选中的图案
        draw_toolbar()  # 重新绘制工具栏以更新炸弹图标状态
# 主游戏循环
def main():
    global score, start_ticks, bomb_used
    show_start_screen()  # 显示开始界面
    initialize_game(current_difficulty)  # 初始化游戏设置
    running = True
    start_ticks = pygame.time.get_ticks()  # 获取游戏开始时的时间
    generate_board()  # 生成图案
    bomb_used = 0  # 初始时炸弹未被使用

    while running:
        timer.tick(FPS)
        current_ticks = pygame.time.get_ticks()
        time_elapsed = current_ticks - start_ticks
        time_left = max(GAME_TIME * 1000 - time_elapsed, 0)

        for event in pygame.event.get():
            if event.type == QUIT:
                running = False
            elif event.type == MOUSEBUTTONDOWN:
                x, y = event.pos
                # 检查是否点击了工具栏的炸弹图标
                if x > WIDTH - 60 and y > HEIGHT - 70 and x < WIDTH and y < HEIGHT - 20:
                    if not bomb_used >= bomb_limit:  # 如果炸弹还有剩余次数
                        bomb_used = True
                        effect_bomb()  # 触发炸弹效果
                        continue
                col, row = x // TILE_SIZE, y // TILE_SIZE
                if 0 <= col < COLS and 0 <= row < ROWS:  # 确保点击在游戏板内
                    if board[row][col]:  # 检查是否有图案
                        if not selected or (selected and (row, col) != selected[0]):
                            selected.append((row, col))
                            if len(selected) == 2:
                                # 检查是否匹配并消除
                                r1, c1 = selected[0]
                                r2, c2 = selected[1]
                                if board[r1][c1] and board[r2][c2] and board[r1][c1][0] == board[r2][c2][0]:
                                    board[r1][c1].pop(0)
                                    board[r2][c2].pop(0)
                                    score += 20  # 增加分数
                                else:
                                    effect_bomb()  # 触发炸弹效果
                                selected.clear()  # 清空选中的图案

        draw_board()
        draw_toolbar()  # 绘制工具栏以显示炸弹状态
        pygame.display.flip()

        if check_win():  # 检查是否胜利
            show_win_screen()  # 显示游戏胜利界面
            running = False
        elif time_left == 0 or (bomb_used and no_pairs_left()):  # 时间结束或炸弹已使用且无配对
            if bomb_used and no_pairs_left():
                show_watch_ad_screen()  # 显示“看广告通关”界面
            else:
                show_lose_screen()  # 显示游戏失败界面
            running = False

    pygame.quit()

if __name__ == "__main__":
    main()