#include "stm32f10x.h"                  // Device header
#include "Appcfg.h"
#include "timers.h"
#include "stdlib.h"
#include "string.h"

TaskHandle_t            StartTask_Handler;  
TaskHandle_t            Task1Task_Handler; 
TaskHandle_t            Task2Task_Handler;
uint8_t  work_status = 0;
QueueHandle_t message_data_queue;
fkmode_typeDef fkdata_obj;
fkmode_typeDef *fkdata = &fkdata_obj;

// 缩小游戏区域：20行 × 10列
#define GAME_ROWS 20
#define GAME_COLS 10
u8 bg_Data[20][10];

u32 cleared_lines = 0;
TimerHandle_t game_timer = NULL;

// 静态变量声明
static u32 last_cleared_lines = 0xFFFFFFFF;
static u8 game_over = 0;  // 新增：游戏结束标志

// 游戏区域参数
#define CELL_SIZE 12           // 每个格子12像素（原来是8）
#define GAME_AREA_X 50         // 游戏区域起始X（居中）
#define GAME_AREA_Y 20         // 游戏区域起始Y
#define GAME_AREA_WIDTH (GAME_COLS * CELL_SIZE)      // 10 * 12 = 120
#define GAME_AREA_HEIGHT (GAME_ROWS * CELL_SIZE)     // 20 * 12 = 240
#define GAME_AREA_END_X (GAME_AREA_X + GAME_AREA_WIDTH)
#define GAME_AREA_END_Y (GAME_AREA_Y + GAME_AREA_HEIGHT)

// 调试变量
static uint32_t left_key_presses = 0;
static uint32_t left_key_misses = 0;

// 函数前向声明
static void timer_callback_func(TimerHandle_t xTimer);
static void reset_game(void);
static void show_game_over(void);
static void show_score(void);
static void new_fk(void);
static void bg_Data_init(void);
static void timer_start(void);
static void timer_stop(void);
static u8 can_move(u8 new_x, u8 new_y, u8 new_shapeB);
static void lock_piece(void);
static void check_and_clear_lines(void);
static void display(void);
static void welcome(void);
static void fkmove(int xData);
static void draw_grid_lines(void);  // 新增：绘制网格线
static void show_debug_info(void);  // 新增：调试信息

void Delay(uint32_t dlyTicks) 
{ 
    vTaskDelay(pdMS_TO_TICKS(dlyTicks));
}

void freertos_App(void)
{
    xTaskCreate((TaskFunction_t)start_task,
                (const char*)"start_task",
                (uint16_t)START_STK_SIZE,
                (void*)NULL,
                (UBaseType_t)START_TASK_PRIO,
                (TaskHandle_t*)&StartTask_Handler);
    
    vTaskStartScheduler();
}

void start_task(void *pvParameters)
{
    taskENTER_CRITICAL();
    
    // 初始化硬件
    muc_EXIT_Init();
    LCD_Init();
    LCD_Clear(BLACK);  // 先清屏
    
    // 显示欢迎界面（包含图片）
    welcome();
    
    // 创建消息队列（容量增加到50，避免消息丢失）
    message_data_queue = xQueueCreate(50, sizeof(uint8_t));
    
    // 创建任务
    xTaskCreate((TaskFunction_t)task1,
                (const char*)"task1",
                (uint16_t)TASK1_STK_SIZE,
                (void*)NULL,
                (UBaseType_t)TASK1_PRIO,
                (TaskHandle_t*)&Task1Task_Handler);
    
    xTaskCreate((TaskFunction_t)task2,
                (const char*)"task2",
                (uint16_t)TASK2_STK_SIZE,
                (void*)NULL,
                (UBaseType_t)TASK2_PRIO,
                (TaskHandle_t*)&Task2Task_Handler);
    
    vTaskDelete(StartTask_Handler);
    taskEXIT_CRITICAL();
}

void EXTI0_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static uint32_t last_press_time = 0;
    uint32_t current_time = xTaskGetTickCountFromISR();
    uint8_t xData;
    
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        // 简单的按键去抖（100ms）
        if ((current_time - last_press_time) < pdMS_TO_TICKS(100))
        {
            EXTI_ClearITPendingBit(EXTI_Line0);
            return;
        }
        last_press_time = current_time;
        
        if (work_status == 0)  // 游戏未开始
        {
            // 发送开始游戏消息
            xData = 1;
            work_status = 1;
        }
        else if (game_over == 1)  // 游戏结束
        {
            // 发送重新开始消息
            xData = 1;
            work_status = 1;
            game_over = 0;
        }
        else  // 游戏进行中
        {
            // 发送左移消息
            xData = 2;
        }
        
        // 发送消息到队列
        if (xQueueSendToBackFromISR(message_data_queue, &xData, &xHigherPriorityTaskWoken) != pdTRUE)
        {
            left_key_misses++;
            // 如果队列满，尝试覆盖最旧的消息
            xQueueReceiveFromISR(message_data_queue, &xData, &xHigherPriorityTaskWoken);
            xQueueSendToBackFromISR(message_data_queue, &xData, &xHigherPriorityTaskWoken);
        }
        
        EXTI_ClearITPendingBit(EXTI_Line0);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void EXTI9_5_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t xData = 3;  // 旋转
    
    if (EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        // 简单的软件去抖延时
        for(volatile int i = 0; i < 1000; i++);
        
        if (work_status == 1 && game_over == 0)
        {
            xQueueSendToBackFromISR(message_data_queue, &xData, &xHigherPriorityTaskWoken);
        }
        EXTI_ClearITPendingBit(EXTI_Line5);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void EXTI15_10_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t xData = 4;  // 右移
    
    if (EXTI_GetITStatus(EXTI_Line15) != RESET)
    {
        // 简单的软件去抖延时
        for(volatile int i = 0; i < 1000; i++);
        
        if (work_status == 1 && game_over == 0)
        {
            xQueueSendToBackFromISR(message_data_queue, &xData, &xHigherPriorityTaskWoken);
        }
        EXTI_ClearITPendingBit(EXTI_Line15);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// 任务1：游戏初始化任务
void task1(void *pvParameters)
{
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        reset_game();
        new_fk();
        timer_start();
        uint8_t xData = 0;
        xQueueSendToBack(message_data_queue, &xData, 0);
        vTaskSuspend(NULL);
    }
}

// 任务2：游戏主循环任务
void task2(void *pvParameters)
{
    uint8_t xData;
    
    while (1)
    {
        if (xQueueReceive(message_data_queue, &xData, portMAX_DELAY) == pdPASS)
        {
            if (xData == 1)  // 开始/重新开始游戏
            {
                work_status = 1;
                game_over = 0;
                
                // 清除游戏区域
                LCD_Fill(GAME_AREA_X, GAME_AREA_Y, 
                         GAME_AREA_END_X - 1, GAME_AREA_END_Y - 1, 
                         BLACK);
                
                // 初始化游戏
                bg_Data_init();
                new_fk();
                
                // 绘制边框和网格
                POINT_COLOR = WHITE;
                LCD_DrawRectangle(GAME_AREA_X - 1, GAME_AREA_Y - 1, 
                                  GAME_AREA_END_X, GAME_AREA_END_Y);
                draw_grid_lines();
                show_score();
                
                // 启动定时器
                if (game_timer != NULL)
                {
                    xTimerDelete(game_timer, 0);
                }
                game_timer = xTimerCreate("GameTimer", 
                                         pdMS_TO_TICKS(500),
                                         pdTRUE,
                                         (void*)0, 
                                         timer_callback_func);
                if (game_timer != NULL)
                {
                    xTimerStart(game_timer, 0);
                }
                
                // 显示初始状态
                display();
            }
            else if (work_status == 1 && game_over == 0)
            {
                // 处理游戏操作
                fkmove(xData);
                display();
            }
        }
    }
}

// 绘制网格线
static void draw_grid_lines(void)
{
    u16 i;
    POINT_COLOR = GRAY;  // 使用灰色绘制网格线
    
    // 绘制垂直线（列分隔线）
    for (i = 1; i < GAME_COLS; i++)
    {
        u16 x = GAME_AREA_X + i * CELL_SIZE;
        LCD_DrawLine(x, GAME_AREA_Y, x, GAME_AREA_END_Y - 1);
    }
    
    // 绘制水平线（行分隔线）
    for (i = 1; i < GAME_ROWS; i++)
    {
        u16 y = GAME_AREA_Y + i * CELL_SIZE;
        LCD_DrawLine(GAME_AREA_X, y, GAME_AREA_END_X - 1, y);
    }
    
    POINT_COLOR = WHITE;
}

// 显示调试信息
static void show_debug_info(void)
{
    POINT_COLOR = CYAN;
    BACK_COLOR = BLACK;
    
    // 显示在屏幕右上角
    #define DEBUG_X 180
    #define DEBUG_Y 100
    
    // 清除调试区域
    LCD_Fill(DEBUG_X, DEBUG_Y, DEBUG_X + 150, DEBUG_Y + 60, BLACK);
    
    // 显示按键统计
    LCD_ShowString(DEBUG_X, DEBUG_Y, 100, 16, 16, "KeyPress:");
    LCD_ShowNum(DEBUG_X + 80, DEBUG_Y, left_key_presses, 5, 16);
    
    LCD_ShowString(DEBUG_X, DEBUG_Y + 20, 100, 16, 16, "KeyMiss:");
    LCD_ShowNum(DEBUG_X + 80, DEBUG_Y + 20, left_key_misses, 5, 16);
    
    POINT_COLOR = WHITE;
    BACK_COLOR = BLACK;
}

// 欢迎界面
static void welcome(void)
{
    POINT_COLOR = DARKBLUE;
    LCD_DrawRectangle(1, 1, 238, 319);
    LCD_DrawRectangle(3, 3, 236, 316);
    
    LCD_Fill_img(4, 4, 235, 315, (unsigned char*)welcomepic);
    
    POINT_COLOR = YELLOW;
    BACK_COLOR = BLACK;
    LCD_ShowString(60, 280, 150, 24, 24, "TETRIS");
    
    POINT_COLOR = WHITE;
    LCD_ShowString(30, 310, 200, 16, 16, "Press WK_UP to Start");
}

// 生成新方块
static void new_fk(void)
{
    static u32 seed = 0x12345678;
    seed = seed * 1103515245 + 12345;
    
    u8 fk_shape = (seed >> 16) % 28;
    fkdata->shapeA = fk_shape / 4;
    fkdata->shapeB = fk_shape % 4;
    fkdata->color = ((seed >> 8) % 7) + 1;
    
    // 初始位置调整到中间
    fkdata->xxx = (GAME_COLS - 4) / 2;  // 居中
    fkdata->yyy = 0;
}

// 背景数据初始化
static void bg_Data_init(void)
{
    memset(bg_Data, 0, sizeof(bg_Data));
    cleared_lines = 0;
    last_cleared_lines = 0xFFFFFFFF;
    
    // 绘制游戏边框
    POINT_COLOR = WHITE;
    LCD_DrawRectangle(GAME_AREA_X - 1, GAME_AREA_Y - 1, 
                      GAME_AREA_END_X, GAME_AREA_END_Y);
    
    // 绘制网格线
    draw_grid_lines();
    
    // 显示分数
    show_score();
}

// 重置游戏
static void reset_game(void)
{
    if (game_timer != NULL)
    {
        xTimerStop(game_timer, 0);
        xTimerDelete(game_timer, 0);
        game_timer = NULL;
    }
    
    bg_Data_init();
    memset(&fkdata_obj, 0, sizeof(fkdata_obj));
    game_over = 0;
    
    LCD_Fill(GAME_AREA_X, GAME_AREA_Y, 
             GAME_AREA_END_X - 1, GAME_AREA_END_Y - 1, 
             BLACK);
}

// 显示游戏结束
static void show_game_over(void)
{
    POINT_COLOR = RED;
    BACK_COLOR = BLACK;
    
    // 在游戏区域中间显示
    LCD_ShowString(GAME_AREA_X + 20, GAME_AREA_Y + 100, 120, 24, 24, "GAME OVER");
    LCD_ShowString(GAME_AREA_X + 10, GAME_AREA_Y + 140, 150, 16, 16, "Lines:");
    LCD_ShowNum(GAME_AREA_X + 70, GAME_AREA_Y + 140, cleared_lines, 5, 16);
    LCD_ShowString(GAME_AREA_X, GAME_AREA_Y + 170, 180, 16, 16, "Press WK_UP to Restart");
}

// 显示分数 - 修复版
static void show_score(void)
{
    POINT_COLOR = WHITE;
    BACK_COLOR = BLACK;
    
    // 分数显示在游戏区域右侧
    #define SCORE_X (GAME_AREA_END_X + 10)
    #define SCORE_Y 50
    
    // 清除旧的分数区域（清除足够大的区域）
    LCD_Fill(SCORE_X, SCORE_Y, SCORE_X + 100, SCORE_Y + 40, BLACK);
    
    // 显示"Lines:"标签
    LCD_ShowString(SCORE_X, SCORE_Y, 60, 16, 16, "Lines:");
    
    // 显示分数值 - 调整位置，确保不重叠
    LCD_ShowNum(SCORE_X , SCORE_Y+30, cleared_lines, 5, 16);
}

// 定时器回调函数
static void timer_callback_func(TimerHandle_t xTimer)
{
    if (work_status == 1 && game_over == 0)
    {
        uint8_t xData = 5;
        BaseType_t result = xQueueSendToBack(message_data_queue, &xData, 0);
        (void)result;
    }
}

// 启动定时器
static void timer_start(void)
{
    if (game_timer != NULL)
    {
        xTimerDelete(game_timer, 0);
        game_timer = NULL;
    }
    
    game_timer = xTimerCreate("GameTimer", 
                             pdMS_TO_TICKS(500),
                             pdTRUE,
                             (void*)0, 
                             timer_callback_func);
    
    if (game_timer != NULL)
    {
        xTimerStart(game_timer, 0);
    }
}

// 停止定时器
static void timer_stop(void)
{
    if (game_timer != NULL)
    {
        xTimerStop(game_timer, 0);
    }
}

// 检查方块是否可以移动
static u8 can_move(u8 new_x, u8 new_y, u8 new_shapeB)
{
    u8 i, j;
    u8 shape = fkdata->shapeA;
    u8 rot = new_shapeB;
    
    // 检查方块每个点
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (fkmodel[shape][rot][i][j] != 0)
            {
                s8 bg_x = (s8)new_x + (s8)j;  // 使用有符号数
                s8 bg_y = (s8)new_y + (s8)i;
                
                // 检查边界（允许负值检查，但不允许超出）
                if (bg_x < 0 || bg_x >= GAME_COLS || bg_y < 0 || bg_y >= GAME_ROWS)
                    return 0;
                
                // 检查是否与背景重叠
                if (bg_Data[bg_y][bg_x] != 0)
                    return 0;
            }
        }
    }
    
    return 1;
}

// 检测并消除满行
static void check_and_clear_lines(void)
{
    u8 i, j, k;
    u8 full_line;
    u32 lines_cleared = 0;
    
    for (i = GAME_ROWS - 1; i > 0; i--)
    {
        full_line = 1;
        
        for (j = 0; j < GAME_COLS; j++)
        {
            if (bg_Data[i][j] == 0)
            {
                full_line = 0;
                break;
            }
        }
        
        if (full_line)
        {
            lines_cleared++;
            
            for (k = i; k > 0; k--)
            {
                for (j = 0; j < GAME_COLS; j++)
                {
                    bg_Data[k][j] = bg_Data[k-1][j];
                }
            }
            
            for (j = 0; j < GAME_COLS; j++)
            {
                bg_Data[0][j] = 0;
            }
            
            i++;
        }
    }
    
    if (lines_cleared > 0)
    {
        cleared_lines += lines_cleared;
    }
}

// 将方块固定到背景
static void lock_piece(void)
{
    u8 i, j;
    u8 shape = fkdata->shapeA;
    u8 rot = fkdata->shapeB;
    u8 x = fkdata->xxx;
    u8 y = fkdata->yyy;
    u8 color = fkdata->color;
    
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (fkmodel[shape][rot][i][j] != 0)
            {
                u8 bg_x = x + j;
                u8 bg_y = y + i;
                
                if (bg_x < GAME_COLS && bg_y < GAME_ROWS)
                {
                    bg_Data[bg_y][bg_x] = color;
                }
            }
        }
    }
    
    check_and_clear_lines();
    new_fk();
    
    if (!can_move(fkdata->xxx, fkdata->yyy, fkdata->shapeB))
    {
        work_status = 0;
        game_over = 1;
        timer_stop();
        show_game_over();
    }
}

// 方块移动函数 - 修复版
static void fkmove(int xData)
{
    if (work_status == 0 || game_over == 1)
        return;
        
    u8 new_x, new_y, new_rot;
    
    switch(xData)
    {
        case 0:  // 仅显示
            break;
            
        case 1:  // 开始游戏
            break;
            
        case 2:  // 左移
        {
            // 检查是否可以左移
            for (s8 offset = 1; offset <= 2; offset++)  // 尝试1-2个单位的偏移
            {
                s8 new_x = (s8)fkdata->xxx - offset;
                if (new_x < 0) break;  // 已经到最左边
                
                if (can_move((u8)new_x, fkdata->yyy, fkdata->shapeB))
                {
                    fkdata->xxx = (u8)new_x;
                    break;
                }
            }
            break;
        }
            
        case 3:  // 旋转
            new_x = fkdata->xxx;
            new_y = fkdata->yyy;
            new_rot = (fkdata->shapeB + 1) % 4;
            if (can_move(new_x, new_y, new_rot))
            {
                fkdata->shapeB = new_rot;
            }
            break;
            
        case 4:  // 右移
            new_x = fkdata->xxx + 1;
            new_y = fkdata->yyy;
            new_rot = fkdata->shapeB;
            if (can_move(new_x, new_y, new_rot))
            {
                fkdata->xxx = new_x;
            }
            break;
            
        case 5:  // 向下移动
            new_x = fkdata->xxx;
            new_y = fkdata->yyy + 1;
            new_rot = fkdata->shapeB;
            if (can_move(new_x, new_y, new_rot))
            {
                fkdata->yyy = new_y;
            }
            else
            {
                lock_piece();
            }
            break;
            
        default:
            break;
    }
}

// 显示函数（添加网格线绘制）
static void display(void)
{
    if (work_status == 0 || game_over == 1)
        return;
        
    u16 i, j;
    u8 shape, rot, x, y, color;
    u16 screen_x, screen_y;
    
    static const u16 color_map[8] = {
        BLACK,    // 0 - 背景
        RED,      // 1 - 红色
        GREEN,    // 2 - 绿色
        BLUE,     // 3 - 蓝色
        YELLOW,   // 4 - 黄色
        MAGENTA,  // 5 - 洋红
        CYAN,     // 6 - 青色
        WHITE     // 7 - 白色
    };
    
    // 绘制背景区域
    for (i = 0; i < GAME_ROWS; i++)
    {
        for (j = 0; j < GAME_COLS; j++)
        {
            screen_x = GAME_AREA_X + j * CELL_SIZE;
            screen_y = GAME_AREA_Y + i * CELL_SIZE;
            
            u8 cell_color = bg_Data[i][j];
            u16 color = (cell_color == 0) ? BLACK : color_map[cell_color & 0x07];
            
            // 填充格子（留出1像素边框给网格线）
            LCD_Fill(screen_x + 1, screen_y + 1, 
                    screen_x + CELL_SIZE - 2, screen_y + CELL_SIZE - 2, 
                    color);
        }
    }
    
    // 绘制当前方块
    shape = fkdata->shapeA;
    rot = fkdata->shapeB;
    x = fkdata->xxx;
    y = fkdata->yyy;
    color = fkdata->color;
    
    u16 block_color = color_map[color & 0x07];
    
    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (fkmodel[shape][rot][i][j] != 0)
            {
                screen_x = GAME_AREA_X + (x + j) * CELL_SIZE;
                screen_y = GAME_AREA_Y + (y + i) * CELL_SIZE;
                
                if (screen_x < GAME_AREA_END_X && screen_y < GAME_AREA_END_Y)
                {
                    // 填充方块（留出1像素边框）
                    LCD_Fill(screen_x + 1, screen_y + 1,
                            screen_x + CELL_SIZE - 2, screen_y + CELL_SIZE - 2,
                            block_color);
                }
            }
        }
    }
    
    // 重新绘制网格线（确保在最上层）
    draw_grid_lines();
    
    // 更新分数显示
    if (last_cleared_lines != cleared_lines)
    {
        show_score();
        last_cleared_lines = cleared_lines;
        
        // 如果需要调试信息，可以在这里显示
        // show_debug_info();
    }
}
