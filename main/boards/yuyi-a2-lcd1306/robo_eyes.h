/*
 * RoboEyes for SSD1306 - Adapted from FluxGarage RoboEyes V 1.1.1
 * Renders animated robot eyes into a 1-bit pixel buffer (128x64).
 * Original: Copyright (C) 2024-2025 Dennis Hoelscher / www.fluxgarage.com
 *
 * This version renders into a uint8_t[1024] buffer suitable for LVGL
 * canvas or direct SSD1306 framebuffer transfer.
 */

#ifndef _ROBO_EYES_H_
#define _ROBO_EYES_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Mood types
#define ROBO_MOOD_DEFAULT 0
#define ROBO_MOOD_TIRED   1
#define ROBO_MOOD_ANGRY   2
#define ROBO_MOOD_HAPPY   3

// Predefined eye positions (9-grid)
#define ROBO_POS_DEFAULT 0
#define ROBO_POS_N  1
#define ROBO_POS_NE 2
#define ROBO_POS_E  3
#define ROBO_POS_SE 4
#define ROBO_POS_S  5
#define ROBO_POS_SW 6
#define ROBO_POS_W  7
#define ROBO_POS_NW 8

// ---------- Pixel Buffer (128x64 monochrome, 1024 bytes) ----------

class PixelBuffer {
public:
    static const int WIDTH = 128;
    static const int HEIGHT = 64;
    static const int SIZE = WIDTH * HEIGHT / 8;

    uint8_t buf[SIZE];

    PixelBuffer() { clear(); }

    void clear() { memset(buf, 0, SIZE); }
    void fill()  { memset(buf, 0xFF, SIZE); }

    inline void setPixel(int x, int y, bool val = true) {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
        if (val) buf[y * (WIDTH / 8) + (x / 8)] |= (1 << (x & 7));
        else     buf[y * (WIDTH / 8) + (x / 8)] &= ~(1 << (x & 7));
    }

    inline bool getPixel(int x, int y) const {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return false;
        return buf[y * (WIDTH / 8) + (x / 8)] & (1 << (x & 7));
    }

    void drawHLine(int x1, int x2, int y, bool val = true) {
        if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
        if (y < 0 || y >= HEIGHT || x2 < 0 || x1 >= WIDTH) return;
        if (x1 < 0) x1 = 0;
        if (x2 >= WIDTH) x2 = WIDTH - 1;
        int byteIdx = y * (WIDTH / 8);
        int startBit = x1 & 7;
        int startByte = byteIdx + (x1 / 8);
        int endBit = x2 & 7;
        int endByte = byteIdx + (x2 / 8);
        uint8_t mask;
        if (startByte == endByte) {
            mask = ((0xFF << startBit) & (0xFF >> (7 - endBit)));
            if (val) buf[startByte] |= mask; else buf[startByte] &= ~mask;
            return;
        }
        if (val) {
            buf[startByte] |= (0xFF << startBit);
            for (int i = startByte + 1; i < endByte; i++) buf[i] = 0xFF;
            buf[endByte] |= (0xFF >> (7 - endBit));
        } else {
            buf[startByte] &= ~(0xFF << startBit);
            for (int i = startByte + 1; i < endByte; i++) buf[i] = 0x00;
            buf[endByte] &= ~(0xFF >> (7 - endBit));
        }
    }

    // Filled rounded rectangle (symmetric top/bottom)
    void fillRoundRect(int x, int y, int w, int h, int r, bool val = true) {
        if (w <= 0 || h <= 0) return;
        if (r <= 0) { fillRect(x, y, w, h, val); return; }
        if (r > w / 2) r = w / 2;
        if (r > h / 2) r = h / 2;

        // Precompute corner x-offsets for one quadrant (rounded)
        int corner[32];
        for (int i = 0; i < r; i++) {
            float dy = (float)(r - i);          // distance from outer edge: r .. 1
            float dx = sqrtf((float)(r * r - dy * dy));
            corner[i] = r - (int)(dx + 0.5f);    // round to nearest pixel
            if (corner[i] < 0) corner[i] = 0;
            if (corner[i] > r) corner[i] = r;
        }

        // Top half (outer→inner)
        for (int i = 0; i < r; i++) {
            int left = x + corner[i];
            int right = x + w - 1 - corner[i];
            if (left < x) left = x;
            if (right >= x + w) right = x + w - 1;
            drawHLine(left, right, y + i, val);
        }

        // Middle rectangle
        for (int j = y + r; j < y + h - r; j++) {
            drawHLine(x, x + w - 1, j, val);
        }

        // Bottom half (mirror of top: inner→outer)
        for (int i = 0; i < r; i++) {
            int left = x + corner[r - 1 - i];
            int right = x + w - 1 - corner[r - 1 - i];
            if (left < x) left = x;
            if (right >= x + w) right = x + w - 1;
            drawHLine(left, right, y + h - r + i, val);
        }
    }

    void fillRect(int x, int y, int w, int h, bool val = true) {
        for (int j = y; j < y + h && j < HEIGHT; j++) {
            if (j >= 0) drawHLine(x, x + w - 1, j, val);
        }
    }

    // Filled triangle using barycentric coordinates
    void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, bool val = true) {
        int minX = (x1 < x2) ? ((x1 < x3) ? x1 : x3) : ((x2 < x3) ? x2 : x3);
        int maxX = (x1 > x2) ? ((x1 > x3) ? x1 : x3) : ((x2 > x3) ? x2 : x3);
        int minY = (y1 < y2) ? ((y1 < y3) ? y1 : y3) : ((y2 < y3) ? y2 : y3);
        int maxY = (y1 > y2) ? ((y1 > y3) ? y1 : y3) : ((y2 > y3) ? y2 : y3);

        if (minX < 0) minX = 0;
        if (maxX >= WIDTH) maxX = WIDTH - 1;
        if (minY < 0) minY = 0;
        if (maxY >= HEIGHT) maxY = HEIGHT - 1;

        int area = (x2 - x1) * (y3 - y1) - (x3 - x1) * (y2 - y1);
        bool invert = (area < 0);
        if (invert) area = -area;

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                int a1 = (x2 - x1) * (y - y1) - (x - x1) * (y2 - y1);
                int a2 = (x3 - x2) * (y - y2) - (x - x2) * (y3 - y2);
                int a3 = (x1 - x3) * (y - y3) - (x - x3) * (y1 - y3);
                bool inside;
                if (invert) inside = (a1 <= 0 && a2 <= 0 && a3 <= 0);
                else        inside = (a1 >= 0 && a2 >= 0 && a3 >= 0);
                if (inside) setPixel(x, y, val);
            }
        }
    }
};

// ---------- RoboEyes Class ----------

class RoboEyes {
public:
    PixelBuffer buffer;

    int screenWidth = 128;
    int screenHeight = 64;
    int frameInterval = 20; // ms, 50fps default

    // Mood
    bool tired = false;
    bool angry = false;
    bool happy = false;
    bool curious = false;
    bool cyclops = false;
    bool eyeL_open = false;
    bool eyeR_open = false;

    // Left eye geometry
    int eyeLwidthDefault = 36, eyeLheightDefault = 36;
    int eyeLwidthCurrent = 36, eyeLheightCurrent = 1;
    int eyeLwidthNext = 36, eyeLheightNext = 36;
    int eyeLheightOffset = 0;
    int eyeLborderRadiusDefault = 8;
    int eyeLborderRadiusCurrent = 8, eyeLborderRadiusNext = 8;

    // Right eye geometry
    int eyeRwidthDefault = 36, eyeRheightDefault = 36;
    int eyeRwidthCurrent = 36, eyeRheightCurrent = 1;
    int eyeRwidthNext = 36, eyeRheightNext = 36;
    int eyeRheightOffset = 0;
    int eyeRborderRadiusDefault = 8;
    int eyeRborderRadiusCurrent = 8, eyeRborderRadiusNext = 8;

    // Left eye coordinates
    int eyeLxDefault, eyeLyDefault;
    int eyeLx, eyeLy, eyeLxNext, eyeLyNext;

    // Right eye coordinates
    int eyeRxDefault, eyeRyDefault;
    int eyeRx, eyeRy, eyeRxNext, eyeRyNext;

    // Eyelids
    int eyelidsTiredHeight = 0, eyelidsTiredHeightNext = 0;
    int eyelidsAngryHeight = 0, eyelidsAngryHeightNext = 0;
    int eyelidsHappyBottomOffset = 0, eyelidsHappyBottomOffsetNext = 0;

    // Space between
    int spaceBetweenDefault = 10;
    int spaceBetweenCurrent = 10, spaceBetweenNext = 10;

    // Animations
    bool hFlicker = false, hFlickerAlternate = false;
    int hFlickerAmplitude = 2;
    bool vFlicker = false, vFlickerAlternate = false;
    int vFlickerAmplitude = 10;
    bool autoblinker = false;
    int blinkInterval = 1, blinkIntervalVariation = 4;
    unsigned long blinktimer = 0;
    bool idle = false;
    int idleInterval = 1, idleIntervalVariation = 3;
    unsigned long idleAnimationTimer = 0;
    bool confused = false;
    unsigned long confusedAnimationTimer = 0;
    int confusedAnimationDuration = 500;
    bool confusedToggle = true;
    bool laugh = false;
    unsigned long laughAnimationTimer = 0;
    int laughAnimationDuration = 500;
    bool laughToggle = true;

    // Sweat
    bool sweat = false;
    int sweatBorderradius = 3;
    float sweat1XPos, sweat1YPos = 2, sweat1Width = 1, sweat1Height = 2;
    int sweat1XPosInitial = 2, sweat1YPosMax;
    float sweat2XPos, sweat2YPos = 2, sweat2Width = 1, sweat2Height = 2;
    int sweat2XPosInitial = 2, sweat2YPosMax;
    float sweat3XPos, sweat3YPos = 2, sweat3Width = 1, sweat3Height = 2;
    int sweat3XPosInitial = 2, sweat3YPosMax;

    RoboEyes() { resetDefaults(); }

    // Recalculate default positions after changing eye size
    void recenter() {
        eyeLxDefault = (screenWidth - (eyeLwidthDefault + spaceBetweenDefault + eyeRwidthDefault)) / 2;
        eyeLyDefault = (screenHeight - eyeLheightDefault) / 2;
        eyeLx = eyeLxNext = eyeLxDefault;
        eyeLy = eyeLyNext = eyeLyDefault;
        eyeRxDefault = eyeLxDefault + eyeLwidthDefault + spaceBetweenDefault;
        eyeRyDefault = eyeLyDefault;
        eyeRx = eyeRxNext = eyeRxDefault;
        eyeRy = eyeRyNext = eyeRyDefault;
    }

    void resetDefaults() {
        screenWidth = 128; screenHeight = 64;
        tired = angry = happy = curious = cyclops = false;
        eyeL_open = eyeR_open = false;

        eyeLwidthDefault = eyeRwidthDefault = 36;
        eyeLheightDefault = eyeRheightDefault = 36;
        eyeLwidthCurrent = eyeRwidthCurrent = 36;
        eyeLheightCurrent = eyeRheightCurrent = 1;
        eyeLwidthNext = eyeRwidthNext = 36;
        eyeLheightNext = eyeRheightNext = 36;
        eyeLheightOffset = eyeRheightOffset = 0;
        eyeLborderRadiusDefault = eyeRborderRadiusDefault = 8;
        eyeLborderRadiusCurrent = eyeRborderRadiusCurrent = 8;
        eyeLborderRadiusNext = eyeRborderRadiusNext = 8;

        spaceBetweenDefault = spaceBetweenCurrent = spaceBetweenNext = 10;

        eyeLxDefault = (screenWidth - (eyeLwidthDefault + spaceBetweenDefault + eyeRwidthDefault)) / 2;
        eyeLyDefault = (screenHeight - eyeLheightDefault) / 2;
        eyeLx = eyeLxDefault; eyeLy = eyeLyDefault;
        eyeLxNext = eyeLx; eyeLyNext = eyeLy;

        eyeRxDefault = eyeLx + eyeLwidthCurrent + spaceBetweenDefault;
        eyeRyDefault = eyeLy;
        eyeRx = eyeRxDefault; eyeRy = eyeRyDefault;
        eyeRxNext = eyeRx; eyeRyNext = eyeRy;

        hFlicker = vFlicker = false; hFlickerAlternate = vFlickerAlternate = false;
        confused = laugh = false; confusedToggle = laughToggle = true;
        sweat = false;

        sweat1XPos = sweat1YPos = 2; sweat1Width = 1; sweat1Height = 2;
        sweat2XPos = sweat2YPos = 2; sweat2Width = 1; sweat2Height = 2;
        sweat3XPos = sweat3YPos = 2; sweat3Width = 1; sweat3Height = 2;
    }

    int getScreenConstraint_X() {
        return screenWidth - eyeLwidthCurrent - spaceBetweenCurrent - eyeRwidthCurrent;
    }
    int getScreenConstraint_Y() {
        return screenHeight - eyeLheightDefault;
    }

    // --- Setters ---
    void setFramerate(int fps) { frameInterval = 1000 / fps; }
    void setWidth(int l, int r) { eyeLwidthNext = eyeLwidthDefault = l; eyeRwidthNext = eyeRwidthDefault = r; }
    void setHeight(int l, int r) { eyeLheightNext = eyeLheightDefault = l; eyeRheightNext = eyeRheightDefault = r; }
    void setBorderradius(int l, int r) { eyeLborderRadiusNext = eyeLborderRadiusDefault = l; eyeRborderRadiusNext = eyeRborderRadiusDefault = r; }
    void setSpacebetween(int s) { spaceBetweenNext = spaceBetweenDefault = s; }

    void setMood(int mood) {
        switch (mood) {
            case ROBO_MOOD_TIRED: tired = true;  angry = false; happy = false; break;
            case ROBO_MOOD_ANGRY: tired = false; angry = true;  happy = false; break;
            case ROBO_MOOD_HAPPY: tired = false; angry = false; happy = true;  break;
            default:              tired = false; angry = false; happy = false; break;
        }
    }

    void setPosition(int pos) {
        int cx = getScreenConstraint_X();
        int cy = getScreenConstraint_Y();
        switch (pos) {
            case ROBO_POS_N:  eyeLxNext = cx/2; eyeLyNext = 0;  break;
            case ROBO_POS_NE: eyeLxNext = cx;   eyeLyNext = 0;  break;
            case ROBO_POS_E:  eyeLxNext = cx;   eyeLyNext = cy/2; break;
            case ROBO_POS_SE: eyeLxNext = cx;   eyeLyNext = cy; break;
            case ROBO_POS_S:  eyeLxNext = cx/2; eyeLyNext = cy; break;
            case ROBO_POS_SW: eyeLxNext = 0;    eyeLyNext = cy; break;
            case ROBO_POS_W:  eyeLxNext = 0;    eyeLyNext = cy/2; break;
            case ROBO_POS_NW: eyeLxNext = 0;    eyeLyNext = 0;  break;
            default:          eyeLxNext = cx/2; eyeLyNext = cy/2; break;
        }
    }

    void setAutoblinker(bool on, int interval = 1, int variation = 4) {
        autoblinker = on; blinkInterval = interval; blinkIntervalVariation = variation;
    }
    void setIdleMode(bool on, int interval = 1, int variation = 3) {
        idle = on; idleInterval = interval; idleIntervalVariation = variation;
    }
    void setCuriosity(bool on) { curious = on; }
    void setCyclops(bool on) { cyclops = on; }
    void setHFlicker(bool on, int amp = 2) { hFlicker = on; hFlickerAmplitude = amp; }
    void setVFlicker(bool on, int amp = 10) { vFlicker = on; vFlickerAmplitude = amp; }
    void setSweat(bool on) { sweat = on; }

    // --- Basic animations ---
    void close() {
        eyeLheightNext = 1; eyeRheightNext = 1;
        eyeL_open = false; eyeR_open = false;
    }
    void open() {
        eyeL_open = true; eyeR_open = true;
    }
    void blink() { close(); open(); }
    void close(bool left, bool right) {
        if (left)  { eyeLheightNext = 1; eyeL_open = false; }
        if (right) { eyeRheightNext = 1; eyeR_open = false; }
    }
    void open(bool left, bool right) {
        if (left)  eyeL_open = true;
        if (right) eyeR_open = true;
    }

    // --- Macro animations ---
    void anim_confused() { confused = true; }
    void anim_laugh() { laugh = true; }

    // --- Main draw ---
    bool drawFrame(unsigned long now_ms) {
        // Pre-calculations: curious mode height offset
        if (curious) {
            if (eyeLxNext <= 10) eyeLheightOffset = 8;
            else if (eyeLxNext >= (getScreenConstraint_X() - 10) && cyclops) eyeLheightOffset = 8;
            else eyeLheightOffset = 0;
            if (eyeRxNext >= screenWidth - eyeRwidthCurrent - 10) eyeRheightOffset = 8;
            else eyeRheightOffset = 0;
        } else {
            eyeLheightOffset = eyeRheightOffset = 0;
        }

        // Smooth transitions
        eyeLheightCurrent = (eyeLheightCurrent + eyeLheightNext + eyeLheightOffset) / 2;
        eyeLy += (eyeLheightDefault - eyeLheightCurrent) / 2;
        eyeLy -= eyeLheightOffset / 2;
        eyeRheightCurrent = (eyeRheightCurrent + eyeRheightNext + eyeRheightOffset) / 2;
        eyeRy += (eyeRheightDefault - eyeRheightCurrent) / 2;
        eyeRy -= eyeRheightOffset / 2;

        if (eyeL_open && eyeLheightCurrent <= 1 + eyeLheightOffset) eyeLheightNext = eyeLheightDefault;
        if (eyeR_open && eyeRheightCurrent <= 1 + eyeRheightOffset) eyeRheightNext = eyeRheightDefault;

        eyeLwidthCurrent = (eyeLwidthCurrent + eyeLwidthNext) / 2;
        eyeRwidthCurrent = (eyeRwidthCurrent + eyeRwidthNext) / 2;
        spaceBetweenCurrent = (spaceBetweenCurrent + spaceBetweenNext) / 2;

        eyeLx = (eyeLx + eyeLxNext) / 2;
        eyeLy = (eyeLy + eyeLyNext) / 2;
        eyeRxNext = eyeLxNext + eyeLwidthCurrent + spaceBetweenCurrent;
        eyeRyNext = eyeLyNext;
        eyeRx = (eyeRx + eyeRxNext) / 2;
        eyeRy = (eyeRy + eyeRyNext) / 2;

        eyeLborderRadiusCurrent = (eyeLborderRadiusCurrent + eyeLborderRadiusNext) / 2;
        eyeRborderRadiusCurrent = (eyeRborderRadiusCurrent + eyeRborderRadiusNext) / 2;

        // Macro animations
        if (autoblinker) {
            if (now_ms >= blinktimer) {
                blink();
                blinktimer = now_ms + blinkInterval * 1000 + (rand() % blinkIntervalVariation) * 1000;
            }
        }

        if (laugh) {
            if (laughToggle) { setVFlicker(true, 5); laughAnimationTimer = now_ms; laughToggle = false; }
            else if (now_ms >= laughAnimationTimer + laughAnimationDuration) { setVFlicker(false, 0); laughToggle = true; laugh = false; }
        }

        if (confused) {
            if (confusedToggle) { setHFlicker(true, 20); confusedAnimationTimer = now_ms; confusedToggle = false; }
            else if (now_ms >= confusedAnimationTimer + confusedAnimationDuration) { setHFlicker(false, 0); confusedToggle = true; confused = false; }
        }

        if (idle) {
            if (now_ms >= idleAnimationTimer) {
                eyeLxNext = rand() % (getScreenConstraint_X() + 1);
                eyeLyNext = rand() % (getScreenConstraint_Y() + 1);
                idleAnimationTimer = now_ms + idleInterval * 1000 + (rand() % idleIntervalVariation) * 1000;
            }
        }

        if (hFlicker) {
            int d = hFlickerAlternate ? hFlickerAmplitude : -hFlickerAmplitude;
            eyeLx += d; eyeRx += d;
            hFlickerAlternate = !hFlickerAlternate;
        }

        if (vFlicker) {
            int d = vFlickerAlternate ? vFlickerAmplitude : -vFlickerAmplitude;
            eyeLy += d; eyeRy += d;
            vFlickerAlternate = !vFlickerAlternate;
        }

        if (cyclops) { eyeRwidthCurrent = 0; eyeRheightCurrent = 0; spaceBetweenCurrent = 0; }

        // --- Draw to buffer ---
        buffer.clear();

        // Main eyes (MAINCOLOR = white = 1)
        buffer.fillRoundRect(eyeLx, eyeLy, eyeLwidthCurrent, eyeLheightCurrent, eyeLborderRadiusCurrent, true);
        if (!cyclops) {
            buffer.fillRoundRect(eyeRx, eyeRy, eyeRwidthCurrent, eyeRheightCurrent, eyeRborderRadiusCurrent, true);
        }

        // Mood eyelids
        if (tired) { eyelidsTiredHeightNext = eyeLheightCurrent / 2; eyelidsAngryHeightNext = 0; }
        else eyelidsTiredHeightNext = 0;
        if (angry) { eyelidsAngryHeightNext = eyeLheightCurrent / 2; eyelidsTiredHeightNext = 0; }
        else eyelidsAngryHeightNext = 0;
        if (happy) eyelidsHappyBottomOffsetNext = eyeLheightCurrent / 2;
        else eyelidsHappyBottomOffsetNext = 0;

        // Tired eyelids (erase with false)
        eyelidsTiredHeight = (eyelidsTiredHeight + eyelidsTiredHeightNext) / 2;
        if (!cyclops) {
            buffer.fillTriangle(eyeLx, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx, eyeLy + eyelidsTiredHeight - 1, false);
            buffer.fillTriangle(eyeRx, eyeRy - 1, eyeRx + eyeRwidthCurrent, eyeRy - 1, eyeRx + eyeRwidthCurrent, eyeRy + eyelidsTiredHeight - 1, false);
        } else {
            buffer.fillTriangle(eyeLx, eyeLy - 1, eyeLx + eyeLwidthCurrent / 2, eyeLy - 1, eyeLx, eyeLy + eyelidsTiredHeight - 1, false);
            buffer.fillTriangle(eyeLx + eyeLwidthCurrent / 2, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy + eyelidsTiredHeight - 1, false);
        }

        // Angry eyelids
        eyelidsAngryHeight = (eyelidsAngryHeight + eyelidsAngryHeightNext) / 2;
        if (!cyclops) {
            buffer.fillTriangle(eyeLx, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy + eyelidsAngryHeight - 1, false);
            buffer.fillTriangle(eyeRx, eyeRy - 1, eyeRx + eyeRwidthCurrent, eyeRy - 1, eyeRx, eyeRy + eyelidsAngryHeight - 1, false);
        } else {
            buffer.fillTriangle(eyeLx, eyeLy - 1, eyeLx + eyeLwidthCurrent / 2, eyeLy - 1, eyeLx + eyeLwidthCurrent / 2, eyeLy + eyelidsAngryHeight - 1, false);
            buffer.fillTriangle(eyeLx + eyeLwidthCurrent / 2, eyeLy - 1, eyeLx + eyeLwidthCurrent, eyeLy - 1, eyeLx + eyeLwidthCurrent / 2, eyeLy + eyelidsAngryHeight - 1, false);
        }

        // Happy bottom eyelids
        eyelidsHappyBottomOffset = (eyelidsHappyBottomOffset + eyelidsHappyBottomOffsetNext) / 2;
        buffer.fillRoundRect(eyeLx - 1, (eyeLy + eyeLheightCurrent) - eyelidsHappyBottomOffset + 1, eyeLwidthCurrent + 2, eyeLheightDefault, eyeLborderRadiusCurrent, false);
        if (!cyclops) {
            buffer.fillRoundRect(eyeRx - 1, (eyeRy + eyeRheightCurrent) - eyelidsHappyBottomOffset + 1, eyeRwidthCurrent + 2, eyeRheightDefault, eyeRborderRadiusCurrent, false);
        }

        // Sweat drops
        if (sweat) {
            if (sweat1YPos <= sweat1YPosMax) sweat1YPos += 0.5f;
            else { sweat1XPosInitial = rand() % 30; sweat1YPos = 2; sweat1YPosMax = (rand() % 10) + 10; sweat1Width = 1; sweat1Height = 2; }
            if (sweat1YPos <= sweat1YPosMax / 2.0f) { sweat1Width += 0.5f; sweat1Height += 0.5f; }
            else { sweat1Width -= 0.1f; sweat1Height -= 0.5f; }
            sweat1XPos = (float)sweat1XPosInitial - (sweat1Width / 2.0f);
            buffer.fillRoundRect((int)sweat1XPos, (int)sweat1YPos, (int)sweat1Width, (int)sweat1Height, sweatBorderradius, true);

            if (sweat2YPos <= sweat2YPosMax) sweat2YPos += 0.5f;
            else { sweat2XPosInitial = (rand() % (screenWidth - 60)) + 30; sweat2YPos = 2; sweat2YPosMax = (rand() % 10) + 10; sweat2Width = 1; sweat2Height = 2; }
            if (sweat2YPos <= sweat2YPosMax / 2.0f) { sweat2Width += 0.5f; sweat2Height += 0.5f; }
            else { sweat2Width -= 0.1f; sweat2Height -= 0.5f; }
            sweat2XPos = (float)sweat2XPosInitial - (sweat2Width / 2.0f);
            buffer.fillRoundRect((int)sweat2XPos, (int)sweat2YPos, (int)sweat2Width, (int)sweat2Height, sweatBorderradius, true);

            if (sweat3YPos <= sweat3YPosMax) sweat3YPos += 0.5f;
            else { sweat3XPosInitial = (screenWidth - 30) + (rand() % 30); sweat3YPos = 2; sweat3YPosMax = (rand() % 10) + 10; sweat3Width = 1; sweat3Height = 2; }
            if (sweat3YPos <= sweat3YPosMax / 2.0f) { sweat3Width += 0.5f; sweat3Height += 0.5f; }
            else { sweat3Width -= 0.1f; sweat3Height -= 0.5f; }
            sweat3XPos = (float)sweat3XPosInitial - (sweat3Width / 2.0f);
            buffer.fillRoundRect((int)sweat3XPos, (int)sweat3YPos, (int)sweat3Width, (int)sweat3Height, sweatBorderradius, true);
        }

        return true;
    }
};

#endif // _ROBO_EYES_H_
