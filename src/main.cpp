// --------------------------------------------------------
//  *** tiny usbKeyboard ***     by NoRi
//  USB keyboard software for Cardputer
//   2025-06-10  v104
// https://github.com/NoRi-230401/tiny-usbKeyboard-Cardputer
//  MIT License
// --------------------------------------------------------
#include <Arduino.h>
#include <SD.h>
#include <M5Cardputer.h>
#include <M5StackUpdater.h>
#include <map>
#include <WiFi.h> // Added for WiFi.mode(WIFI_OFF)
using std::string;

//----- `usbKeyboad` only (no `bleKeyboad`) -----------------
#include <USB.h>
#include <USBHIDKeyboard.h>
// -----------------------------------------------------------

void setup();
void loop();
bool checkInput(m5::Keyboard_Class::KeysState &current_keys_state);
bool specialFnMode(const m5::Keyboard_Class::KeysState &current_keys);
void keySend(const m5::Keyboard_Class::KeysState &current_keys);
void dispLx(uint8_t Lx, const char *msg);
void dispModsKeys(const char *msg);
void dispModsCls();
void dispSendKey(const char *msg);
void dispSendKey2(const char *msg);
void dispFnState();
void dispInit();
void m5stack_begin();
void SDU_lobby();
bool SD_begin();
void fnStateInit();
void powerSave();
void dispBatteryLevel();

// ----- Cardputer Specific disp paramaters -----------
const int32_t N_COLS = 20; // columns
const int32_t N_ROWS = 6;  // rows
//---- caluculated in m5stackc_begin() ----------------
static int32_t X_WIDTH, Y_HEIGHT; // Screen dimensions
static int32_t W_CHR, H_CHR;      // Character dimensions
static int32_t SC_LINES[N_ROWS];  // Array to store Y coordinates of each line

//-------------------------------------
static SPIClass SPI2;
static bool SD_ENABLE;
static bool capsLock; // fn + 1  : Cpas Lock On/Off
static bool cursMode; // fn + 2  : cursor movement mode On/Off

// --- hid key-code define ----
const uint8_t HID_UPARROW = 0x52;
const uint8_t HID_DOWNARROW = 0x51;
const uint8_t HID_LEFTARROW = 0x50;
const uint8_t HID_RIGHTARROW = 0x4F;
const uint8_t HID_ESC = 0x29;
const uint8_t HID_DELETE = 0x4C;
const uint8_t HID_HOME = 0x4A;
const uint8_t HID_END = 0x4D;
const uint8_t HID_PAGEUP = 0x4B;
const uint8_t HID_PAGEDOWN = 0x4E;
const uint8_t HID_INS = 0x49;
const uint8_t HID_PRINTSC = 0x46;
const uint8_t HID_F5 = 0x3E;
const uint8_t HID_F6 = 0x3F;
const uint8_t HID_F7 = 0x40;
const uint8_t HID_F8 = 0x41;
const uint8_t HID_F9 = 0x42;
const uint8_t HID_F10 = 0x43;

// ----- Key Mappings for usbSend() -----
// fnKeyExclusiveMappings: Dedicated mappings evaluated with priority when Fn key is pressed
const std::map<uint8_t, uint8_t> fnKeyExclusiveMappings = {
    {0x35, HID_ESC},    // '`' -> ESC
    {0x2A, HID_DELETE}, // 'BACK' -> DELETE
    {0x22, HID_F5},     // '5' -> F5
    {0x23, HID_F6},     // '6' -> F6
    {0x24, HID_F7},     // '7' -> F7
    {0x25, HID_F8},     // '8' -> F8
    {0x26, HID_F9},     // '9' -> F9
    {0x27, HID_F10},    // '0' -> F10
    {0x31, HID_INS},    // '\' -> Insert
    {0x34, HID_PRINTSC} // ''' -> Print Screen
};

// generalNavigationMappings: Mappings used in combination with Fn key (if not in exclusive) or in cursor mode
const std::map<uint8_t, uint8_t> generalNavigationMappings = {
    {0x33, HID_UPARROW},    // ';' -> Up Arrow
    {0x37, HID_DOWNARROW},  // '.' -> Down Arrow
    {0x36, HID_LEFTARROW},  // ',' -> Left Arrow
    {0x38, HID_RIGHTARROW}, // '/' -> Right Arrow
    {0x2d, HID_HOME},       // '-' -> Home
    {0x2f, HID_END},        // '[' -> End
    {0x2e, HID_PAGEUP},     // '=' -> Page Up
    {0x30, HID_PAGEDOWN}    // ']' -> Page Down
};
// --------------------------------------

unsigned long lastKeyInput = 0;   // last key input time
const uint8_t BRIGHT_NORMAL = 70; // LCD normal bright level
const uint8_t BRIGHT_LOW = 20;    // LCD low bright level

USBHIDKeyboard txKey;
KeyReport txKeyReport;

void setup()
{
    m5stack_begin();

    if (SD_ENABLE)
    { // M5stack-SD-Updater lobby
        SDU_lobby();
        SD.end();
    }

    USB.begin();
    txKey.begin();
    fnStateInit(); // function state initialize
    dispInit();    // display initialize
    lastKeyInput = millis();
}

void loop()
{
    M5Cardputer.update(); // update Cardputer key input

    m5::Keyboard_Class::KeysState current_keys_state;
    if (checkInput(current_keys_state))         // check Cardputer key input and get state
    {                                           // if keys input
        if (!specialFnMode(current_keys_state)) // special function mode check
            keySend(current_keys_state);        // send data via USB
    }

    powerSave(); // bat.lvl disp and dimming
    delay(5);
}

bool checkInput(m5::Keyboard_Class::KeysState &current_keys_state)
{ // check Cardputer key input
    if (M5Cardputer.Keyboard.isChange())
    {
        lastKeyInput = millis();
        if (M5Cardputer.Keyboard.isPressed())
        {
            current_keys_state = M5Cardputer.Keyboard.keysState();
            return true;
        }
        else // All keys are physically released
        {
            txKey.releaseAll();
            dispModsCls();
        }
    }
    if (M5Cardputer.BtnA.wasPressed())
    { // BtnG0 : instead name of Cardputer BtnA
        lastKeyInput = millis();
    }
    return false;
}

bool specialFnMode(const m5::Keyboard_Class::KeysState &current_keys)
{ // **** [fn] special function mode (High Priority) **********
    // return true: special function executed , false: not exectuted

    //  --- input keys status setup  -------------------------
    bool existWord = !current_keys.word.empty();
    uint8_t keyWord = 0;
    if (existWord)
        keyWord = current_keys.word[0];

    if (current_keys.fn && existWord)
    {
        bool specialFnProcessed = false;
        switch (keyWord)
        {
        case '1':
        case '!': // Caps Lock Toggle (fn + '1')
            specialFnProcessed = true;
            capsLock = !capsLock;
            txKey.write(KEY_CAPS_LOCK);
            break;
        case '2':
        case '@': // Cursor movement Mode Toggle (fn + '2')
            specialFnProcessed = true;
            cursMode = !cursMode;
            break;
        }

        if (specialFnProcessed)
        {
            dispFnState();
            txKey.releaseAll(); // Release all keys/modifiers on the host side
            dispModsCls();      // Clear modifier key display on the Cardputer side
            return true;
        }
    }
    return false;
}

void keySend(const m5::Keyboard_Class::KeysState &current_keys)
{
    txKeyReport = {0};
    uint8_t modifier = 0;
    string modsStr;    // Buffer for modifier keys string
    string hidCodeStr; // Buffer for HID codes string, stores " 0xYY 0xZZ..."

    // ** modifiers keys(ctrl,shift,alt) and fn **
    //    these keys are used with other key
    if (current_keys.ctrl)
    {
        modifier |= 0x01;
        modsStr += "Ctrl ";
    }
    if (current_keys.shift)
    {
        modifier |= 0x02;
        modsStr += "Shift ";
    }
    if (current_keys.alt)
    {
        modifier |= 0x04;
        modsStr += "Alt ";
    }
    if (current_keys.opt)
    {
        modifier |= 0x08;
        modsStr += "Opt ";
    }
    if (current_keys.fn)
    {
        modsStr += "Fn ";
    }

    txKeyReport.modifiers = modifier;
    dispModsKeys(modsStr.c_str());

    // *****  Regular Character Keys *****
    int count = 0;
    const uint8_t MAX_SIMULTANEOUS_KEYS = 6; // Max number of keys in HID report

    for (auto hidCode : current_keys.hid_keys)
    {
        if (count < MAX_SIMULTANEOUS_KEYS)
        {
            char tempBuf[8]; // For " 0xYY" format, e.g., " 0x1A"
            uint8_t finalHidCode = hidCode;

            if (current_keys.fn)
            {
                auto it_exclusive = fnKeyExclusiveMappings.find(hidCode);
                if (it_exclusive != fnKeyExclusiveMappings.end())
                {
                    finalHidCode = it_exclusive->second; // Apply Fn-specific mapping
                }
                else
                {
                    // If Fn key is pressed but no dedicated mapping is found, try general navigation mapping
                    auto it_general = generalNavigationMappings.find(hidCode);
                    if (it_general != generalNavigationMappings.end())
                    {
                        finalHidCode = it_general->second;
                    }
                }
            }
            else if (cursMode) // If Fn key is not pressed and cursor mode is enabled, try general navigation mapping
            {
                auto it_general = generalNavigationMappings.find(hidCode);
                if (it_general != generalNavigationMappings.end())
                {
                    finalHidCode = it_general->second;
                }
            }
            txKeyReport.keys[count] = finalHidCode;
            int written = snprintf(tempBuf, sizeof(tempBuf), " 0x%02X", finalHidCode);
            if (written > 0) // For std::string, just check if snprintf was successful
            {
                hidCodeStr += tempBuf;
            }
            count++;
        }
    }

    // Send keyReport to host device
    txKey.sendReport(&txKeyReport);

    string keysDisplayString;
    if (!current_keys.word.empty())
    {
        keysDisplayString = " " + string(1, current_keys.word[0]) + " :hid";
    }
    else if (!hidCodeStr.empty())
    {
        keysDisplayString = " hid";
    }

    // Append hidCodeStr (HID codes) if there's content and space
    if (!hidCodeStr.empty())
    {
        keysDisplayString += hidCodeStr;
    }
    if (!keysDisplayString.empty())
    {
        dispSendKey(keysDisplayString.c_str());
    }
}

void dispLx(uint8_t Lx, const char *msg)
{
    //*********** Lx is (0 to N_ROWS - 1) *************************
    // -----01234567890123456789--------------------------
    // L0  "- tiny usbKeyborad -" : title
    // L1               bat.100%  : battery Status
    // L2   fn1:Caps  fn2:CursMd  :
    // L3    unlock       off     : fn status
    // L4   Shift Ctrl Alt Opt    : modifiers keys
    // L5    X :hid 0x00          : sendKey and hidCode
    // -----01234567890123456789---------------------------
    // ****************************************************
    if (Lx >= N_ROWS)
        return;

    M5Cardputer.Display.fillRect(0, SC_LINES[Lx], X_WIDTH, H_CHR, TFT_BLACK);
    M5Cardputer.Display.setCursor(0, SC_LINES[Lx]);
    M5Cardputer.Display.print(msg);
}

void dispModsKeys(const char *msg)
{ // line4 : modifiers keys(Shift/Ctrl/Alt/Opt/Fn)
    dispLx(4, msg);
}

void dispModsCls()
{ // line4 : modifiers keys disp clear
    M5Cardputer.Display.fillRect(0, SC_LINES[4], X_WIDTH, H_CHR, TFT_BLACK);
}

void dispSendKey(const char *msg)
{ // line5 : reguler character send info
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    dispLx(5, msg);
#ifdef DEBUG
    Serial.println(msg);
#endif
}

void dispSendKey2(const char *msg)
{ // line5 : other keys (enter,tab,backspace, etc ...) send info
    M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    string temp_str = " ";
    temp_str += msg;
    dispLx(5, temp_str.c_str());
#ifdef DEBUG
    Serial.println(msg); // msg is already const char*
#endif
}

void dispFnState()
{ // Line3 : fn1 to fn3 state display
    //----- 01234567890123456789---
    // L2  "fn1:Caps  fn2:CursMd"
    // L3  " unlock       off   ":
    //----- 01234567890123456789---
    const char *StCaps[] = {"unlock", " lock"};
    const char *StEditMode[] = {"off", " on"};
    const int COL_CAPSLOCK = 1;    // "Caps" status start position
    const int COL_CURSORMODE = 14; // "CursMd" status start position

    M5Cardputer.Display.fillRect(0, SC_LINES[3], X_WIDTH, H_CHR, TFT_BLACK);

    // capsLock state
    M5Cardputer.Display.setCursor(W_CHR * COL_CAPSLOCK, SC_LINES[3]);
    if (capsLock)
    {
        M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5Cardputer.Display.print(StCaps[1]);
    }
    else
    {
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.print(StCaps[0]);
    }

    // Cursor Movement mode state
    M5Cardputer.Display.setCursor(W_CHR * COL_CURSORMODE, SC_LINES[3]);
    if (cursMode)
    {
        M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5Cardputer.Display.print(StEditMode[1]);
    }
    else
    {
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.print(StEditMode[0]);
    }
}

void dispInit()
{
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setBrightness(BRIGHT_NORMAL);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setCursor(0, 0);

    //  L0 : software title -----------------
    //--01234567890123456789--
    // "- tiny usbKeyboard -";
    //--01234567890123456789--
    M5Cardputer.Display.fillRect(0, SC_LINES[0], X_WIDTH, H_CHR, TFT_BLACK);
    M5Cardputer.Display.setCursor(0, SC_LINES[0]);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.print("- tiny ");
    M5Cardputer.Display.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    M5Cardputer.Display.print("usb");
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.print("Keyboard -");

    //  L1 :Battery Level ---------------------------------------
    //-------------------"01234567890123456789"------------------;
    const char *L1Str = "            bat.   %";
    //-------------------"01234567890123456789"------------------;
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    dispLx(1, L1Str);
    dispBatteryLevel();

    //  L2 : Fn1 to Fn3 title -----------------------------------
    //--------"01234567890123456789"------------------;
    // L2Str ="fn1:Caps  fn2:CursMd"   : fn
    //--------"01234567890123456789"------------------;
    M5Cardputer.Display.setCursor(0, SC_LINES[2]);
    M5Cardputer.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5Cardputer.Display.print("fn1:");
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.print("Caps  ");

    M5Cardputer.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5Cardputer.Display.print("fn2:");
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.print("CursMd");

    // L3 : Fn1 to Fn3 state disp --------------------------------
    dispFnState();
}

void m5stack_begin()
{
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200; // Serial communication speed
    cfg.internal_imu = false;     // Do not use IMU (accelerometer/gyroscope)
    cfg.internal_mic = false;     // Do not use microphone
    cfg.output_power = false;     // Disable Grove port power output
    cfg.led_brightness = 0;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Speaker.setVolume(0);
    WiFi.mode(WIFI_OFF); // Ensure Wi-Fi is off

#ifdef DEBUG
    // vsCode terminal cannot get serial data
    //  of cardputer before 5 sec ...!
    delay(5000);
    Serial.println("\n\n*** m5stack begin ***");
#endif

    // Reduce power consumption by lowering CPU frequency (e.g., 80MHz)
    if (setCpuFrequencyMhz(80))
    {
#ifdef DEBUG
        Serial.printf("CPU Freq set to: %d MHz\n", getCpuFrequencyMhz());
#endif
    }

    // Calculate Cardputer specific display scale parameters
    X_WIDTH = M5Cardputer.Display.width();
    Y_HEIGHT = M5Cardputer.Display.height();
    W_CHR = X_WIDTH / N_COLS;  // width of 1 character
    H_CHR = Y_HEIGHT / N_ROWS; // height of 1 character
    for (int i = 0; i < N_ROWS; ++i)
    {
        SC_LINES[i] = i * H_CHR;
    }

    // display setup at startup
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setBrightness(BRIGHT_NORMAL);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextDatum(top_left); // character base position
    M5Cardputer.Display.setTextWrap(false);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setCursor(0, 0);

    // SPI setup for using SD card
    SPI2.begin(
        M5.getPin(m5::pin_name_t::sd_spi_sclk),
        M5.getPin(m5::pin_name_t::sd_spi_miso),
        M5.getPin(m5::pin_name_t::sd_spi_mosi),
        M5.getPin(m5::pin_name_t::sd_spi_ss));

    SD_ENABLE = SD_begin();
}

// ------------------------------------------------------------------------
// SDU_lobby :  lobby for M5Stack-SD-Updater
// ------------------------------------------------------------------------
// load '/menu.bin' on SD, if key'a' pressed at booting.
// 'menu.bin' for Cardputer is involved in BINS folder at this github site
// https://github.com/NoRi-230401/tiny-usbKeyboard-Cardputer
//
// ------------------------------------------------------------------------
// ** M5Stack-SD-Updater is launcher software for m5stak by tobozo **
// https://github.com/tobozo/M5Stack-SD-Updater/
// ------------------------------------------------------------------------
void SDU_lobby()
{
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isKeyPressed('a'))
    {
        updateFromFS(SD, "/menu.bin");
        ESP.restart();
        // *** NEVER RETURN ***
    }
}

bool SD_begin()
{
    for (int i = 0; i < 10; ++i)
    {
        if (SD.begin(M5.getPin(m5::pin_name_t::sd_spi_ss), SPI2))
        {
            return true; // Success
        }
        delay(500);
    }
    Serial.println("ERR: SD begin fail...");
    SD.end();
    return false; // Failed after retries
}

void fnStateInit()
{
    // capsLock state is always 'false' start
    capsLock = false;
    // cursor movement mode state is always 'false' start
    cursMode = false;
}

enum PowerSaveFSM
{
    PS_NORMAL,
    PS_LOW_BRIGHT,
};
static PowerSaveFSM psState = PS_NORMAL;

static unsigned long prev_btlvl_disp_tm = 0L; // previous battery disp time

void powerSave()
{
    const unsigned long BATLVL_DISP_INTERVAL_MS = 5 * 1000UL;
    const unsigned long LOW_BRIGHT_TIMEOUT_MS = 3 * 60 * 1000UL; // 3 min
    unsigned long currentTime = millis();                        // Get current time once
    unsigned long timeSinceLastInput = currentTime - lastKeyInput;

    // Check if it's time to update battery level display (handles millis() overflow)
    if (currentTime - prev_btlvl_disp_tm >= BATLVL_DISP_INTERVAL_MS)
    {
        prev_btlvl_disp_tm = currentTime;
        dispBatteryLevel();
    }

    if (timeSinceLastInput < LOW_BRIGHT_TIMEOUT_MS)
    { // Still within normal active period
        if (psState != PS_NORMAL)
        {
            // If returning to normal from any other state (low bright, APO warn)
            psState = PS_NORMAL;
            // dispInit(); // Restore full display, normal brightness, and clear any previous warnings
            M5Cardputer.Display.setBrightness(BRIGHT_NORMAL);
        }
        return;
    }

    // LOW_BRIGHT_TIMEOUT_MS has passed.
    // Proceed with dimming
    if (psState == PS_NORMAL)
    {
        // Transition from Normal to Low Bright
        psState = PS_LOW_BRIGHT;
        M5Cardputer.Display.setBrightness(BRIGHT_LOW);
    }
}

void dispBatteryLevel()
{
    // Line1 : battery level display
    //---- 01234567890123456789---
    // L1_"            bat.100%"--
    //---- 01234567890123456789---
    const int COL_BATVAL = 16;      // Battery value display start position
    const int WIDTH_BATVAL_LEN = 3; // Battery value display length

    M5Cardputer.Display.fillRect(W_CHR * COL_BATVAL, SC_LINES[1], W_CHR * WIDTH_BATVAL_LEN, H_CHR, TFT_BLACK);
    M5Cardputer.Display.setCursor(W_CHR * COL_BATVAL, SC_LINES[1]);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    uint8_t batLvl = (uint8_t)M5.Power.getBatteryLevel();  // Get battery level
    char batLvlBuf[4];                                     // Buffer for "XXX" + null terminator
    snprintf(batLvlBuf, sizeof(batLvlBuf), "%3u", batLvl); // %3u pads with spaces if less than 3 digits
    M5Cardputer.Display.print(batLvlBuf);
}
