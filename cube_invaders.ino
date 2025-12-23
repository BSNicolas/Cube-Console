/*
 * Cube Invaders OLED
 * -------------------
 * This program implements a simplified Space Invaders-style game using an Wemos mini microcontroller and an OLED display.
 *
 * The player controls a ship via a joystick, and can shoot both normal and special bullets to destroy enemies.
 * The objective is to eliminate all enemies before they reach the player's level.
 * 
 * Features:
 * - Basic joystick control for lateral movement
 * - Normal and special bullets with a cooldown for special attacks
 * - OLED display rendering for the player, enemies, bullets, and score
 * - Win and game over conditions
 * - WiFiManager for easy WiFi setup
 * - Score posting via HTTP POST to a remote server upon game win
 * - Reset button to restart the game
 */


#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiManager.h> // WiFiManager library

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// WiFi configuration
#define WIFI_SSID      "something"  // The Wi-fi network name of University
#define WIFI_PASSWORD  "something"  // The Wi-fi network password of University
#define WIFI_HOME     "something"  // The Wi-fi network name of Home
#define HOME_PASSWORD  "something"  // The Wi-fi network password of Home


// I/O pins
#define JOYSTICK_X A0
#define BUTTON_SHOOT 12
#define BUTTON_SPECIAL 13
#define BUTTON_RESET 15
#define LED_PIN 2
#define BUZZER_PIN 0

// Screen dimensions
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1

// Player dimensions
#define PLAYER_WIDTH    10
#define PLAYER_HEIGHT   5

// Bullet properties
#define BULLET_WIDTH        2
#define BULLET_HEIGHT       4
#define BULLET_SPEED        5
#define SPECIAL_BULLET_SPEED 7
#define SPECIAL_COOLDOWN    10000 // in ms

// Enemy properties
#define ENEMY_WIDTH         8
#define ENEMY_HEIGHT        5
#define ENEMY_SPACING_X     10
#define ENEMY_SPACING_Y     5
#define ENEMY_ROWS          3
#define ENEMY_COLS          5
#define ENEMY_INITIAL_Y     10
#define ENEMY_MOVE_INTERVAL 20
#define ENEMY_MOVE_STEP     2

// Display
#define TEXT_SIZE           1
#define TEXT_COLOR          SSD1306_WHITE
#define GAME_DELAY_MS       50

// Score
#define SCORE_MAX           1000

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

WiFiManager wifiManager;

String url1 = "ip_address"; // tunnel url at university
String url2 = "ip_address"; // tunnel url at home
String url3 = "ip_address"; // tunnel url in France
int player_id = 1;
bool is_posted = false;

int playerX = SCREEN_WIDTH / 2;
bool bulletActive = false;
bool specialBulletActive = false;
unsigned long lastSpecialTime = 0;
int bulletX, bulletY;
bool enemies[ENEMY_COLS][ENEMY_ROWS];
int enemyY = ENEMY_INITIAL_Y;
int score = 0;
unsigned long startTime;
bool gameOver = false;
bool win = false;


/**
 * Updates the player's horizontal position based on joystick input.
 * Also checks if a bullet or special bullet should be fired.
 *
 * No parameters.
 * No return value.
 */
void updatePlayer() {
    int joystickValue = analogRead(JOYSTICK_X);
    playerX = map(joystickValue, 0, 1023, 0, SCREEN_WIDTH - PLAYER_WIDTH);
    if (digitalRead(BUTTON_SHOOT) == HIGH && !bulletActive) {
        bulletActive = true;
        bulletX = playerX + PLAYER_WIDTH / 2;
        bulletY = SCREEN_HEIGHT - PLAYER_HEIGHT - BULLET_HEIGHT;
    }
    if (digitalRead(BUTTON_SPECIAL) == HIGH && !specialBulletActive && millis() - lastSpecialTime > SPECIAL_COOLDOWN) {
        specialBulletActive = true;
        bulletX = playerX + PLAYER_WIDTH / 2;
        bulletY = SCREEN_HEIGHT - PLAYER_HEIGHT - BULLET_HEIGHT;
        lastSpecialTime = millis();
        tone(BUZZER_PIN, 2000, 150);
    }
}

/**
 * Updates the vertical position of both normal and special bullets.
 * Disables bullets if they leave the screen.
 * Also checks for enemy collisions.
 *
 * No parameters.
 * No return value.
 */
void updateBullet() {
    if (bulletActive) {
        bulletY -= BULLET_SPEED;
        if (bulletY < 0) bulletActive = false;
        checkCollision(false);
    }
    if (specialBulletActive) {
        bulletY -= SPECIAL_BULLET_SPEED;
        if (bulletY < 0) specialBulletActive = false;
        checkCollision(true);
    }
}

/**
 * Moves the enemies downward at fixed intervals.
 * Checks if any enemy has reached the player's level, triggering game over.
 *
 * No parameters.
 * No return value.
 */
void updateEnemies() {
    static int moveCounter = 0;
    moveCounter++;
    if (moveCounter > ENEMY_MOVE_INTERVAL) {
        enemyY += ENEMY_MOVE_STEP;
        moveCounter = 0;
    }

    for (int i = 0; i < ENEMY_COLS; i++) {
        for (int j = 0; j < ENEMY_ROWS; j++) {
            if (enemies[i][j]) {
                int yPos = enemyY + j * (ENEMY_HEIGHT + ENEMY_SPACING_Y);
                if (yPos + ENEMY_HEIGHT >= SCREEN_HEIGHT - PLAYER_HEIGHT) {
                    gameOver = true;
                    tone(BUZZER_PIN, 150, 500);
                    return;
                }
            }
        }
    }
}

/**
 * Checks whether the active bullet (normal or special) collides with any enemy.
 * If a collision is detected, the enemy is removed and bullet deactivated.
 *
 * @param special - True if the bullet is a special one; false otherwise.
 * No return value.
 */
void checkCollision(bool special) {
    if (!(special ? specialBulletActive : bulletActive)) return;

    for (int i = 0; i < ENEMY_COLS; i++) {
        for (int j = 0; j < ENEMY_ROWS; j++) {
            if (enemies[i][j]) {
                int enemyX = (i + 1) * (ENEMY_WIDTH + ENEMY_SPACING_X);
                int enemyYPos = enemyY + j * (ENEMY_HEIGHT + ENEMY_SPACING_Y);
                if (bulletX >= enemyX && bulletX <= enemyX + ENEMY_WIDTH &&
                    bulletY >= enemyYPos && bulletY <= enemyYPos + ENEMY_HEIGHT) {
                    enemies[i][j] = false;
                    if (!special) {
                        bulletActive = false;
                        tone(BUZZER_PIN, 1000, 100);
                    } else {
                        tone(BUZZER_PIN, 800, 50);
                    }
                    checkGameWin();
                }
            }
        }
    }
}

/**
 * Checks if all enemies have been destroyed.
 * If so, sets the win state and calculates the final score.
 *
 * No parameters.
 * No return value.
 */
void checkGameWin() {
    bool allDefeated = true;
    for (int i = 0; i < ENEMY_COLS; i++) {
        for (int j = 0; j < ENEMY_ROWS; j++) {
            if (enemies[i][j]) {
                allDefeated = false;
                break;
            }
        }
    }
    if (allDefeated) {
        unsigned long endTime = millis();
        score = SCORE_MAX - (endTime - startTime) / 1000;
        win = true;
        tone(BUZZER_PIN, 500, 500);
    }
}

/**
 * Displays the win or game over screen with the player's final score.
 *
 * @param s - Integer specifying screen type: 1 for "YOU WIN", 2 for "GAME OVER".
 * No return value.
 */
void drawScreen(int s) {
    display.setTextSize(TEXT_SIZE);
    display.setTextColor(TEXT_COLOR);
    display.setCursor(25, 20);
    switch (s) {
        case 1:
            display.print("YOU WIN");
            break;
        case 2:
            display.print("GAME OVER");
            break;
    }
    display.setCursor(15, 35);
    display.print("Score : ");
    display.print(score);
    display.setCursor(10, 50);
    display.print("Press RESET to play");
    display.display();
}

/**
 * Draws the current game frame: player, bullets, enemies, and score.
 * If win or game over states are active, renders the respective screen.
 *
 * No parameters.
 * No return value.
 */
void drawGame() {
    display.clearDisplay();

    if (gameOver) {
        drawScreen(2);
        return;
    }

    if (win) {
        drawScreen(1);
        return;
    }

    display.fillRect(playerX, SCREEN_HEIGHT - PLAYER_HEIGHT, PLAYER_WIDTH, PLAYER_HEIGHT, TEXT_COLOR);
    if (bulletActive || specialBulletActive) {
        display.fillRect(bulletX, bulletY, BULLET_WIDTH, BULLET_HEIGHT, TEXT_COLOR);
    }
    for (int i = 0; i < ENEMY_COLS; i++) {
        for (int j = 0; j < ENEMY_ROWS; j++) {
            if (enemies[i][j]) {
                display.fillRect((i + 1) * (ENEMY_WIDTH + ENEMY_SPACING_X), enemyY + j * (ENEMY_HEIGHT + ENEMY_SPACING_Y), ENEMY_WIDTH, ENEMY_HEIGHT, TEXT_COLOR);
            }
        }
    }
    display.setTextSize(TEXT_SIZE);
    display.setTextColor(TEXT_COLOR);
    display.setCursor(0, 0);
    display.print("Score: ");
    display.print(score);
    display.display();
}

/**
 * Resets all game variables to their initial state to start a new game.
 *
 * No parameters.
 * No return value.
 */
void resetGame() {
    for (int i = 0; i < ENEMY_COLS; i++) {
        for (int j = 0; j < ENEMY_ROWS; j++) {
            enemies[i][j] = true;
        }
    }
    enemyY = ENEMY_INITIAL_Y;
    bulletActive = false;
    specialBulletActive = false;
    score = 0;
    startTime = millis();
    gameOver = false;
    win = false;
    is_posted = false;
}

/**
 * Sends the player's score to a predefined HTTP server via POST request.
 * Triggered only once per win event.
 *
 * No parameters.
 * No return value.
 */
void post_method() {
    // Initialize a wi-fi client & http client
    WiFiClient wifiClient;
    HTTPClient http;

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected, try to send the request...");
    } else {
        Serial.println("ERROR: WiFi NOT CONNECTED !");
        return;
    }

    http.begin(wifiClient, "http://" + url3 + "/post.php");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String httpRequestData = "player_id=" + String(player_id) + "&score=" + String(score);
    int httpResponseCode = http.POST(httpRequestData);
    
    Serial.println(httpResponseCode);
    Serial.println(http.getString());
    Serial.print("Server response: ");
    Serial.println(http.getString());

    http.end();
    is_posted = true;
    player_id++;
}

/**
 * Initializes all hardware components and connects to WiFi using WiFiManager.
 * Also initializes the OLED display and starts a new game.
 *
 * No parameters.
 * No return value.
 */
void setup() {
    Serial.begin(9600);

    pinMode(JOYSTICK_X, INPUT);
    pinMode(BUTTON_SHOOT, INPUT_PULLUP);
    pinMode(BUTTON_SPECIAL, INPUT_PULLUP);
    pinMode(BUTTON_RESET, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    
    
    wifiManager.autoConnect("name", "password");
    
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        for(;;);
    }
    resetGame();
}

/**
 * Main loop of the game:
 * - Handles reset button press
 * - Sends score to server upon win
 * - Updates game state and renders frame
 *
 * No parameters.
 * No return value.
 */
void loop() {
    if (digitalRead(BUTTON_RESET) == HIGH) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        resetGame();
    }
    
    if (score != 0 && !(is_posted)) {
        post_method();
    }
    
    if (!gameOver) {
        updatePlayer();
        updateBullet();
        updateEnemies();
    }

    drawGame();
    delay(GAME_DELAY_MS);
}

