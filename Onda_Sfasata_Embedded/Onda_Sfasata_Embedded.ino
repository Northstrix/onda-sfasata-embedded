#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <Audio.h>
#include <Bounce2.h>
#include "Levels.h"

// --- Styling (565 Colors) ---
#define COL_BG          0x0000 
#define COL_CARD        0x18c3 
#define COL_OUTLINE     0x39e7 
#define COL_ACCENT      0x145a
#define COL_SEL_OUT     0x353d
#define COL_SUCCESS     0x25c1 
#define COL_SUCCESS_BG  0x1c21 
#define COL_DANGER      0xb864 
#define COL_DANGER_BG   0x8043 
#define COL_MUTED       0x7bef 
#define COL_WHITE       0xffff

// --- Adjustable Variables ---
int GLOBAL_CHAR_LIMIT = 48; // Max characters per line in answer card
int SIZE2_SPACING     = 6;  // 6px spacing for all Size 2 text blocks
int MAX_ANSWER_LINES  = 6;  // Maximum lines allowed in the answer card total

// --- Hardware Pins ---
#define BTN_START  24
#define BTN_ESC    25
#define BTN_UP     26 // Top-Left choice
#define BTN_C2     27 // Top-Right
#define BTN_DOWN   28 // Bottom-Left choice
#define BTN_C4     29 // Bottom-Right
#define BTN_REPEAT 32

const int btnPins[] = {BTN_START, BTN_ESC, BTN_UP, BTN_C2, BTN_DOWN, BTN_C4, BTN_REPEAT};
Bounce btns[7];

// --- Audio ---
AudioPlaySdWav           playWav;
AudioOutputPWM           pwmOutput;
AudioConnection          patchCord(playWav, 0, pwmOutput, 0);

// --- State ---
enum AppState { BOOT, LANDING, QUIZ, RESULT };
AppState currentState = BOOT;
int selLvl = 0, curWord = 0, score = 0, mistakes = 0;
bool isRevealed = false;
int userChoice = -1;
const char* quizOptions[4];
int correctIdx = 0;

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// --- Logic: Word Wrapping ---
int wrapAdvanced(const char* input, int maxChars, String lines[], int maxLines) {
    if (!input || strlen(input) == 0) return 0;
    String str = String(input);
    int lineCount = 0;
    while (str.length() > 0 && lineCount < maxLines) {
        str.trim();
        int breakIdx = -1;
        int forced = str.indexOf("  "); // Manual double-space break
        if (forced != -1 && forced <= maxChars) {
            breakIdx = forced;
        } else if ((int)str.length() <= maxChars) {
            lines[lineCount++] = str;
            break;
        } else {
            breakIdx = str.lastIndexOf(' ', maxChars);
            if (breakIdx == -1) breakIdx = maxChars; // Force break if no space found
        }
        lines[lineCount++] = str.substring(0, breakIdx);
        str = str.substring(breakIdx);
        // Remove ALL leading spaces so the new line starts with a character
        while (str.length() > 0 && str[0] == ' ') str.remove(0, 1);
    }
    return lineCount;
}

void playSfx(const char* folder, const char* file) {
    char path[128];
    if (folder) sprintf(path, "audio/%s/%s", folder, file);
    else sprintf(path, "audio/%s", file);
    if (playWav.isPlaying()) playWav.stop();
    playWav.play(path);
}

void setupQuestion() {
    Word &w = levels[selLvl].words[curWord];
    correctIdx = random(0, 4);
    quizOptions[correctIdx] = w.trans[0];
    for (int i = 0; i < 4; i++) {
        if (i == correctIdx) continue;
        bool unique = false;
        while (!unique) {
            int rL = random(0, numLevels);
            int wordCount = 0; while(levels[rL].words[wordCount].word != NULL) wordCount++;
            int rW = random(0, wordCount);
            const char* candidate = levels[rL].words[rW].trans[0];
            unique = (strcmp(candidate, w.trans[0]) != 0);
            for (int j = 0; j < i; j++) if (quizOptions[j] && strcmp(quizOptions[j], candidate) == 0) unique = false;
            if (unique) quizOptions[i] = candidate;
        }
    }
    playSfx("Italian", w.filename);
}

void drawCard(int x, int y, int w, int h, uint32_t bg, uint32_t out) {
    spr.fillRect(x, y, w, h, bg);
    spr.drawRect(x, y, w, h, out);
}

void renderBlock(int x, int y, int w, int h, const char* txt, int maxC, int maxL, int sz, int spacing) {
    String lines[maxL];
    int count = wrapAdvanced(txt, maxC, lines, maxL);
    int fontH = (sz == 1) ? 8 : (sz == 2 ? 16 : 24);
    int totalH = (count * fontH) + ((count - 1) * spacing);
    int startY = y + (h - totalH) / 2;
    spr.setTextSize(sz);
    for (int i = 0; i < count; i++) {
        spr.drawString(lines[i], x + (w / 2), startY + (i * (fontH + spacing)) + (fontH / 2));
    }
}

// Logic: Text flows immediately after label, subsequent lines return to margin. 
// Respects the shared lineCounter to enforce the 6-line limit.
int renderAnswerLine(int x, int y, const char* label, const char* text, int &lineCounter) {
    if (!text || strlen(text) == 0 || lineCounter >= MAX_ANSWER_LINES) return 0;
    
    spr.setTextSize(1);
    spr.setTextDatum(ML_DATUM);
    
    // 1. Draw Label
    spr.setTextColor(COL_MUTED);
    spr.drawString(label, x, y + 4);
    int labelWidth = spr.textWidth(label);
    
    // 2. Wrap text. First line is shorter.
    int labelCharLen = strlen(label);
    int firstLineLimit = GLOBAL_CHAR_LIMIT - labelCharLen;
    
    String strText = String(text);
    strText.trim();
    
    int breakIdx = strText.lastIndexOf(' ', firstLineLimit);
    if ((int)strText.length() <= firstLineLimit) breakIdx = strText.length();
    if (breakIdx <= 0) breakIdx = 0; 

    String line1 = strText.substring(0, breakIdx);
    String remaining = strText.substring(breakIdx);
    remaining.trim();

    // Draw Line 1
    spr.setTextColor(COL_WHITE);
    spr.drawString(line1, x + labelWidth, y + 4);
    lineCounter++;
    int linesAdded = 1;

    // Draw subsequent lines starting from margin x
    if (remaining.length() > 0 && lineCounter < MAX_ANSWER_LINES) {
        String subLines[6];
        int wrapped = wrapAdvanced(remaining.c_str(), GLOBAL_CHAR_LIMIT, subLines, MAX_ANSWER_LINES - lineCounter);
        for (int i = 0; i < wrapped; i++) {
            spr.drawString(subLines[i], x, y + 4 + (linesAdded * 10));
            linesAdded++;
            lineCounter++;
        }
    }
    return linesAdded * 10; 
}

void render() {
    spr.fillSprite(COL_BG);
    spr.setTextDatum(MC_DATUM);

    if (currentState == BOOT) {
        spr.setTextColor(COL_ACCENT); spr.setTextSize(3);
        spr.drawString("ONDA SFASATA", 160, 80);
        spr.drawString("EMBEDDED", 160, 120);
        spr.setTextColor(COL_WHITE); spr.setTextSize(1);
        spr.drawString("PRESS ANY BUTTON", 160, 200);
    } 
    else if (currentState == LANDING) {
        spr.setTextSize(2); spr.setTextColor(COL_WHITE);
        spr.drawString("SELECT LEVEL", 160, 18);
        int page = selLvl / 4;
        for (int i = 0; i < 4; i++) {
            int idx = page * 4 + i;
            if (idx >= numLevels) break;
            int cy = 35 + (i * 51);
            uint32_t bg = (idx == selLvl) ? COL_ACCENT : COL_CARD;
            uint32_t out = (idx == selLvl) ? COL_SEL_OUT : COL_OUTLINE;
            drawCard(20, cy, 280, 48, bg, out);
            spr.setTextColor(COL_WHITE);
            renderBlock(20, cy, 280, 48, levels[idx].title, 22, 2, 2, SIZE2_SPACING);
        }
    } 
    else if (currentState == QUIZ) {
        Word &w = levels[selLvl].words[curWord];
        spr.setTextColor(COL_ACCENT);
        int sz = strlen(w.word) > 17 ? 2 : 3;
        renderBlock(10, 5, 300, 50, w.word, 26, 3, sz, (sz == 2 ? SIZE2_SPACING : 0));

        for (int i = 0; i < 4; i++) {
            int ox = (i % 2 == 0) ? 10 : 165;
            int oy = (i < 2) ? 61 : 111; 
            uint32_t bg = COL_CARD;
            uint32_t out = COL_OUTLINE;
            if (isRevealed) {
                if (i == correctIdx) { bg = COL_SUCCESS_BG; out = COL_SUCCESS; }
                else if (i == userChoice) { bg = COL_DANGER_BG; out = COL_DANGER; }
            }
            drawCard(ox, oy, 145, 44, bg, out);
            spr.setTextColor(COL_WHITE);
            renderBlock(ox, oy, 145, 44, quizOptions[i], 21, 4, 1, 2);
        }

        if (isRevealed) {
            int ay = 165;
            drawCard(10, ay, 300, 240 - ay - 5, COL_CARD, COL_OUTLINE);
            
            int cursorY = ay + 6;
            int xPadding = 16;
            int lineCounter = 0;
            
            int tCnt = 0; while(w.trans[tCnt] != NULL && tCnt < 3) tCnt++;
            String ts = ""; for(int j=0; j<tCnt; j++){ ts += w.trans[j]; if(j < tCnt-1) ts += ", "; }
            
            // Apply xPadding to the function calls
            cursorY += renderAnswerLine(xPadding, cursorY, tCnt > 1 ? "Translations: " : "Translation: ", ts.c_str(), lineCounter);
            cursorY += renderAnswerLine(xPadding, cursorY, "Definition: ", w.definition, lineCounter);
            if (w.info) renderAnswerLine(xPadding, cursorY, "Info: ", w.info, lineCounter);
        }
    }
    else if (currentState == RESULT) {
        spr.setTextColor(COL_SUCCESS); spr.setTextSize(2);
        spr.drawString("LEVEL COMPLETE!", 160, 100);
        spr.setTextColor(COL_WHITE); spr.setTextSize(1);
        char b[64]; sprintf(b, "Score: %d | Mistakes: %d", score, mistakes);
        spr.drawString(b, 160, 140);
    }
    spr.pushSprite(0, 0);
}

void setup() {
    tft.init(); tft.setRotation(1);
    spr.createSprite(320, 240);
    SD.begin(BUILTIN_SDCARD);
    AudioMemory(15);
    for (int i = 0; i < 7; i++) {
        btns[i].attach(btnPins[i], INPUT_PULLUP);
        btns[i].interval(25);
    }
    render();
}

void loop() {
    bool update = false;
    for (int i = 0; i < 7; i++) btns[i].update();

    if (currentState == BOOT) {
        for(int i=0; i<7; i++) if(btns[i].fell()) { currentState = LANDING; update = true; break; }
    } 
    else if (currentState == LANDING) {
        if (btns[2].fell()) { selLvl = (selLvl > 0) ? selLvl - 1 : numLevels - 1; update = true; } 
        if (btns[4].fell()) { selLvl = (selLvl < numLevels - 1) ? selLvl + 1 : 0; update = true; } 
        if (btns[0].fell()) { 
            currentState = QUIZ; curWord = 0; score = 0; mistakes = 0; 
            isRevealed = false; setupQuestion(); update = true; 
        }
    } 
    else if (currentState == QUIZ) {
        if (btns[6].fell()) playSfx("Italian", levels[selLvl].words[curWord].filename);
        if (!isRevealed) {
            int choice = -1;
            if (btns[2].fell()) choice = 0; // Top-Left (UP)
            if (btns[3].fell()) choice = 1; // Top-Right
            if (btns[4].fell()) choice = 2; // Bottom-Left (DOWN)
            if (btns[5].fell()) choice = 3; // Bottom-Right
            if (choice != -1) {
                userChoice = choice; isRevealed = true;
                if (choice == correctIdx) { score++; playSfx(NULL, "success.wav"); } 
                else { mistakes++; playSfx(NULL, "error.wav"); }
                update = true;
            }
        } else if (btns[0].fell()) {
            isRevealed = false; curWord++;
            if (levels[selLvl].words[curWord].word == NULL) {
                currentState = RESULT; 
                playSfx(NULL, "completed.wav"); // Played when level complete card shows
            } else { setupQuestion(); }
            update = true;
        }
        if (btns[1].fell()) { currentState = LANDING; update = true; }
    }
    else if (currentState == RESULT) {
        if (btns[0].fell()) { currentState = LANDING; update = true; }
    }
    if (update) render();
}
