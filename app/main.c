/**
 * @file    main.c
 * @author  Projet DEEP - Console Snake G431 PRO
 * @brief   Logiciel final avec Machine à États, Combos, Obstacles et Sauvegarde Flash.
 * @date    Juin 2026
 */

#include "config.h"
#include "stm32g4_sys.h"
#include "stm32g4_uart.h"
#include "stm32g4_gpio.h"
#include <stdio.h>
#include <stdlib.h>
#include "TFT_ili9341/stm32g4_ili9341.h"

// PARAMÈTRES TECHNIQUES
#define SIZE 10
#define WIDTH 320
#define HEIGHT 240
#define MAX_SNAKE 100
#define MAX_OBS 15
#define FLASH_STORAGE_ADDR 0x0801F800 // Page 63 (Dernière page du STM32G431KB)

//MAPPING DES BOUTONS
#define B_BAS      GPIOA, GPIO_PIN_11
#define B_START    GPIOA, GPIO_PIN_12
#define B_HAUT     GPIOB, GPIO_PIN_4
#define B_DROIT    GPIOB, GPIO_PIN_5
#define B_GAUCHE   GPIOB, GPIO_PIN_6

// --- ÉTATS DU JEU ---
typedef enum { MENU, PLAY, PAUSE, GAMEOVER } GameState;
GameState state = MENU;

// --- VARIABLES GLOBALES ---
int snakeX[MAX_SNAKE], snakeY[MAX_SNAKE], snakeLen;
int obsX[MAX_OBS], obsY[MAX_OBS], numObs;
int dirX, dirY, appleX, appleY, score, highScore = 0;
int combo = 1;
uint32_t lastEatTime = 0;
uint16_t appleColor;

// Difficultés : Délais en ms {Débutant, Moyen, Difficile}
int selectedDiff = 0;
char* diffNames[] = {"DEBUTANT", "MOYEN", "DIFFICILE"};
int diffSpeeds[] = {300, 150, 80};

// ==============================================================================
// FONCTIONS DE GESTION DE LA MÉMOIRE FLASH (Option F)
// ==============================================================================

/**
 * @brief Sauvegarde le record dans la mémoire non-volatile du STM32.
 */
void Save_HighScore_To_Flash(int score_to_save) {
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t PageError;
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.Banks = FLASH_BANK_1;
    EraseInitStruct.Page = 63;
    EraseInitStruct.NbPages = 1;

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &PageError) == HAL_OK) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, FLASH_STORAGE_ADDR, (uint64_t)score_to_save);
    }
    HAL_FLASH_Lock();
    printf("[FLASH] Nouveau record %d enregistre !\r\n", score_to_save);
}

/**
 * @brief Lit le record stocké en Flash au démarrage.
 */
int Read_HighScore_From_Flash() {
    int saved_score = *(__IO uint32_t *)FLASH_STORAGE_ADDR;
    if (saved_score == 0xFFFFFFFF || saved_score < 0) return 0;
    return saved_score;
}


// LOGIQUE DE JEU


void spawn_apple() {
    appleX = (rand() % (WIDTH / SIZE)) * SIZE;
    appleY = (rand() % (HEIGHT / SIZE)) * SIZE;
    appleColor = ((rand() % 5) == 0) ? ILI9341_COLOR_YELLOW : ILI9341_COLOR_RED;
    ILI9341_DrawFilledRectangle(appleX, appleY, appleX + SIZE, appleY + SIZE, appleColor);
}

void init_game() {
    ILI9341_Fill(ILI9341_COLOR_BLACK);
    snakeLen = 3; score = 0; combo = 1;
    for(int i=0; i<snakeLen; i++) { snakeX[i] = 160-(i*SIZE); snakeY[i] = 120; }
    dirX = 1; dirY = 0;

    numObs = (selectedDiff == 0) ? 0 : (selectedDiff == 1) ? 6 : 12;
    for(int i=0; i<numObs; i++) {
        obsX[i] = (rand() % (WIDTH/SIZE)) * SIZE;
        obsY[i] = (rand() % (HEIGHT/SIZE)) * SIZE;
        if(obsX[i] > 100 && obsX[i] < 220 && obsY[i] == 120) obsY[i] += 40;
        ILI9341_DrawFilledRectangle(obsX[i], obsY[i], obsX[i]+SIZE, obsY[i]+SIZE, ILI9341_COLOR_GRAY);
    }
    spawn_apple();
}

void draw_menu() {
    ILI9341_Fill(ILI9341_COLOR_BLACK);
    ILI9341_Puts(70, 30, "SNAKE G431 PRO", &Font_11x18, ILI9341_COLOR_GREEN, ILI9341_COLOR_BLACK);
    ILI9341_printf(180, 5, &Font_7x10, ILI9341_COLOR_YELLOW, ILI9341_COLOR_BLACK, "RECORD : %d", highScore);

    for(int i=0; i<3; i++) {
        uint16_t color = (i == selectedDiff) ? ILI9341_COLOR_YELLOW : ILI9341_COLOR_WHITE;
        uint16_t bg = (i == selectedDiff) ? ILI9341_COLOR_BLUE : ILI9341_COLOR_BLACK;
        ILI9341_printf(90, 100 + (i*30), &Font_11x18, color, bg, (i == selectedDiff) ? "> %s" : "  %s", diffNames[i]);
    }
}


// BOUCLE PRINCIPALE


int main(void) {
    HAL_Init();
    BSP_GPIO_enable();
    BSP_UART_init(UART2_ID, 115200);
    BSP_SYS_set_std_usart(UART2_ID, UART2_ID, UART2_ID);
    ILI9341_Init();

    BSP_GPIO_pin_config(B_BAS, GPIO_MODE_INPUT, GPIO_PULLUP, 0, 0);
    BSP_GPIO_pin_config(B_HAUT, GPIO_MODE_INPUT, GPIO_PULLUP, 0, 0);
    BSP_GPIO_pin_config(B_GAUCHE, GPIO_MODE_INPUT, GPIO_PULLUP, 0, 0);
    BSP_GPIO_pin_config(B_DROIT, GPIO_MODE_INPUT, GPIO_PULLUP, 0, 0);
    BSP_GPIO_pin_config(B_START, GPIO_MODE_INPUT, GPIO_PULLUP, 0, 0);

    // Charger le record au démarrage
    highScore = Read_HighScore_From_Flash();

    draw_menu();

    while (1) {
        switch (state) {
            case MENU:
                if(HAL_GPIO_ReadPin(B_HAUT) == 0) { selectedDiff = (selectedDiff > 0) ? selectedDiff - 1 : 2; draw_menu(); HAL_Delay(200); }
                if(HAL_GPIO_ReadPin(B_BAS) == 0)  { selectedDiff = (selectedDiff < 2) ? selectedDiff + 1 : 0; draw_menu(); HAL_Delay(200); }
                if(HAL_GPIO_ReadPin(B_START) == 0) { state = PLAY; init_game(); HAL_Delay(300); }
                break;

            case PLAY:
                if(HAL_GPIO_ReadPin(B_HAUT) == 0 && dirY == 0)   { dirX = 0; dirY = -1; }
                if(HAL_GPIO_ReadPin(B_BAS) == 0 && dirY == 0)    { dirX = 0; dirY = 1;  }
                if(HAL_GPIO_ReadPin(B_GAUCHE) == 0 && dirX == 0) { dirX = -1; dirY = 0; }
                if(HAL_GPIO_ReadPin(B_DROIT) == 0 && dirX == 0)  { dirX = 1; dirY = 0;  }
                if(HAL_GPIO_ReadPin(B_START) == 0) { state = PAUSE; HAL_Delay(300); break; }

                // Optimisation : effacer queue
                ILI9341_DrawFilledRectangle(snakeX[snakeLen-1], snakeY[snakeLen-1], snakeX[snakeLen-1]+SIZE, snakeY[snakeLen-1]+SIZE, ILI9341_COLOR_BLACK);

                for(int i=snakeLen-1; i>0; i--) { snakeX[i]=snakeX[i-1]; snakeY[i]=snakeY[i-1]; }
                snakeX[0] += dirX*SIZE; snakeY[0] += dirY*SIZE;

                // Collisions
                if(snakeX[0]<0 || snakeX[0]>=WIDTH || snakeY[0]<0 || snakeY[0]>=HEIGHT) state = GAMEOVER;
                for(int i=0; i<numObs; i++) { if(snakeX[0]==obsX[i] && snakeY[0]==obsY[i]) state = GAMEOVER; }
                for(int i=1; i<snakeLen; i++) { if(snakeX[0]==snakeX[i] && snakeY[0]==snakeY[i]) state = GAMEOVER; }

                if(state == GAMEOVER) {
                    if(score > highScore) {
                        highScore = score;
                        Save_HighScore_To_Flash(highScore); // SAUVEGARDE FLASH
                    }
                    break;
                }

                // Manger pomme + Combo
                if(snakeX[0] == appleX && snakeY[0] == appleY) {
                    uint32_t now = HAL_GetTick();
                    combo = (now - lastEatTime < 3000) ? combo + 1 : 1;
                    lastEatTime = now;
                    score += ((appleColor == ILI9341_COLOR_YELLOW ? 5 : 1) * combo);
                    if(snakeLen < MAX_SNAKE) snakeLen++;
                    spawn_apple();
                }

                // Dessin avec Yeux noirs
                for(int i=0; i<snakeLen; i++) {
                    uint16_t c = (i == 0) ? ILI9341_COLOR_GREEN : ILI9341_COLOR_GREEN2;
                    ILI9341_DrawFilledRectangle(snakeX[i], snakeY[i], snakeX[i]+SIZE, snakeY[i]+SIZE, c);
                    if(i == 0) {
                        ILI9341_DrawPixel(snakeX[i]+2, snakeY[i]+2, ILI9341_COLOR_BLACK);
                        ILI9341_DrawPixel(snakeX[i]+7, snakeY[i]+2, ILI9341_COLOR_BLACK);
                    }
                }

                ILI9341_printf(5, 5, &Font_7x10, ILI9341_COLOR_WHITE, ILI9341_COLOR_BLACK, "Score: %d", score);
                if(combo > 1) ILI9341_printf(240, 5, &Font_7x10, ILI9341_COLOR_MAGENTA, ILI9341_COLOR_BLACK, "COMBO X%d", combo);

                HAL_Delay(diffSpeeds[selectedDiff]);
                break;

            case PAUSE:
                ILI9341_Puts(125, 110, "PAUSE", &Font_11x18, ILI9341_COLOR_YELLOW, ILI9341_COLOR_BLACK);
                if(HAL_GPIO_ReadPin(B_START) == 0) {
                    ILI9341_DrawFilledRectangle(125, 110, 200, 130, ILI9341_COLOR_BLACK);
                    state = PLAY; HAL_Delay(300);
                }
                break;

            case GAMEOVER:
                ILI9341_Fill(ILI9341_COLOR_RED);
                ILI9341_Puts(90, 80, "GAME OVER", &Font_11x18, ILI9341_COLOR_WHITE, ILI9341_COLOR_RED);
                ILI9341_printf(80, 120, &Font_11x18, ILI9341_COLOR_YELLOW, ILI9341_COLOR_RED, "SCORE : %d", score);
                ILI9341_printf(80, 150, &Font_11x18, ILI9341_COLOR_WHITE, ILI9341_COLOR_RED, "RECORD: %d", highScore);
                HAL_Delay(1000);
                while(HAL_GPIO_ReadPin(B_START) != 0);
                state = MENU; draw_menu(); HAL_Delay(300);
                break;
        }
    }
}
