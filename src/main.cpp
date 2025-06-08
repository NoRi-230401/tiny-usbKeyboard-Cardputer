// --------------------------------------------------------
//  *** tiny usbKeyboard ***     by NoRi
//  bluetooth keyboard software for Cardputer
//   2025-06-08  v104
// https://github.com/NoRi-230401/tiny-usbKeyboard-Cardputer
//  MIT License
// --------------------------------------------------------
#include <Arduino.h>
#include <SD.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <M5Cardputer.h>
#include <M5StackUpdater.h>
#include <algorithm>
#include <string>
#include <map>

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

// ----- Cardputer Specific disp paramaters -----------
const int32_t N_COLS = 20; // columns
const int32_t N_ROWS = 6;  // rows
//---- caluculated in m5stackc_begin() ----------------
static int32_t X_WIDTH, Y_HEIGHT;
static int32_t W_CHR, H_CHR;
static int32_t LINE0, LINE1, LINE2, LINE3, LINE4, LINE5;

//-------------------------------------
SPIClass SPI2;
static bool SD_ENABLE;
static bool capsLock = false; // fn + 1  : Cpas Lock On/Off
static bool cursMode = false; // fn + 2  : cursor movement mode On/Off

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

const uint8_t BRIGHT_NORMAL = 70;        // LCD normal bright level
const uint8_t MAX_SIMULTANEOUS_KEYS = 6; // Max number of keys in HID report (excluding modifier keys)
const int COL_CAPSLOCK = 1;              // "Cap" display start position
const int COL_CURSORMODE = 10;           // "CurM" display start position

USBHIDKeyboard usbKey;
KeyReport usbKeyReport = {0};

void setup()
{
    m5stack_begin();

    if (SD_ENABLE)
    { // M5stack-SD-Updater lobby
        SDU_lobby();
        SD.end();
    }

    dispInit(); // display initialize
    USB.begin();
    usbKey.begin();
}

void loop()
{
    M5Cardputer.update(); // update Cardputer key input

    m5::Keyboard_Class::KeysState current_keys_state;
    if (checkInput(current_keys_state))         // check Cardputer key input and get state
    {                                           // if keys input
        if (!specialFnMode(current_keys_state)) // special function mode check
            keySend(current_keys_state);        // send data via bluetooth
    }

    delay(5);
}

bool checkInput(m5::Keyboard_Class::KeysState &current_keys_state)
{ // check Cardputer key input
    if (M5Cardputer.Keyboard.isChange())
    {
        if (M5Cardputer.Keyboard.isPressed())
        {
            current_keys_state = M5Cardputer.Keyboard.keysState();
            return true;
        }
        else // All keys are physically released
        {
            usbKey.releaseAll();
            dispModsCls();
        }
    }
    return false;
}

bool specialFnMode(const m5::Keyboard_Class::KeysState &current_keys)
{ // **** [fn] special function mode (High Priority) **********
    // return true: special function executed , false: not exectuted

    //  --- input keys status setup  -------------------------
    uint8_t mods = current_keys.modifiers;
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
            usbKey.write(KEY_CAPS_LOCK);
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
            usbKey.releaseAll(); // Release all keys/modifiers on the host side
            dispModsCls();       // Clear modifier key display on the Cardputer side
            return true;
        }
    }
    return false;
}

void keySend(const m5::Keyboard_Class::KeysState &current_keys)
{
    usbKeyReport = {0};
    String modsStr = "";
    uint8_t modifier = 0;
    String localSendWord = ""; // Use local variable for constructing HID code string

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
        modsStr += "Fn ";

    usbKeyReport.modifiers = modifier;
    dispModsKeys(modsStr.c_str());

    // *****  Regular Character Keys *****
    int count = 0;
    for (auto hidCode : current_keys.hid_keys)
    {
        char tempBuf[8]; // For " 0xYY" format
        if (count < MAX_SIMULTANEOUS_KEYS)
        {
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
            usbKeyReport.keys[count] = finalHidCode;
            snprintf(tempBuf, sizeof(tempBuf), " 0x%02X", finalHidCode);
            localSendWord += tempBuf;
            count++;
        }
    }

    // Send keyReport via USB
    usbKey.sendReport(&usbKeyReport);

    String keysDisplayString = "";
    if (!current_keys.word.empty())
    {
        keysDisplayString += " ";
        keysDisplayString += current_keys.word[0];
        keysDisplayString += " :hid";
    }
    else if (!localSendWord.isEmpty())
    {
        keysDisplayString += " hid";
    }
    keysDisplayString += localSendWord;

    if (!keysDisplayString.isEmpty())
    {
        dispSendKey(keysDisplayString.c_str());
    }
}

void dispLx(uint8_t Lx, const char *msg)
{
    //*********** Lx is (0 to N_ROWS - 1) *************************
    // -----01234567890123456789--------------------------
    // L0  "- tiny usbKeyborad -" : title
    // L1                         :
    // L2   fn1:Cap 2:CurM        : fn
    // L3    unlock   off         :
    // L4   Shift Ctrl Alt Opt    : modifiers keys
    // L5    X :hid 0x00          : sendKey and hidCode
    // -----01234567890123456789---------------------------
    // ****************************************************
    if (Lx >= N_ROWS)
        return;

    M5Cardputer.Display.fillRect(0, Lx * H_CHR, X_WIDTH, H_CHR, TFT_BLACK);
    M5Cardputer.Display.setCursor(0, Lx * H_CHR);
    M5Cardputer.Display.print(msg);
}

void dispModsKeys(const char *msg)
{ // line4 : modifiers keys(Shift/Ctrl/Alt/Opt/Fn)
    dispLx(4, msg);
}

void dispModsCls()
{ // line4 : modifiers keys disp clear
    // dispLx(4, "");
    M5Cardputer.Display.fillRect(0, LINE4, X_WIDTH, H_CHR, TFT_BLACK);
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
    char buffer[N_COLS + 1]; // Ensure buffer is large enough
    snprintf(buffer, sizeof(buffer), " %s", msg);
    dispLx(5, buffer);
#ifdef DEBUG
    Serial.println(msg);
#endif
}

void dispFnState()
{ // Line3 : fn1 to fn3 state display
    //-------- 01234567890123456789---
    //_____L2 "fn1:Cap 2:CurM      "__
    //_____L3_" unlock   off       "__
    //-------- 01234567890123456789---
    const char *StCaps[] = {"unlock", " lock"};
    const char *StEditMode[] = {"off", " on"};

    M5Cardputer.Display.fillRect(0, LINE3, X_WIDTH, H_CHR, TFT_BLACK);

    // capsLock state
    M5Cardputer.Display.setCursor(W_CHR * COL_CAPSLOCK, LINE3);
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

    // Cursor movement mode state
    M5Cardputer.Display.setCursor(W_CHR * COL_CURSORMODE, LINE3);
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
    // M5Cardputer.Display.setBrightness(BRIGHT_NORMAL);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setCursor(0, 0);

    //  L0 : software title -----------------
    //-------------------01234567890123456789--
    const char *L0Str = "- tiny usbKeyboard -";
    //-------------------01234567890123456789--
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    dispLx(0, L0Str);

    //  L1 : software title -----------------
    dispLx(1, "");

    //  L2 : Fn1 to Fn3 title -----------------------------------
    //-------------------"01234567890123456789"------------------;
    //           L2Str = "fn1:Cap 2:CurM 3:Apo";
    //-------------------"01234567890123456789"------------------;
    M5Cardputer.Display.setCursor(0, LINE2);
    M5Cardputer.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5Cardputer.Display.print("fn1:");
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.print("Cap ");

    M5Cardputer.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5Cardputer.Display.print("2:");
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.print("CurM ");

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
    LINE0 = 0 * H_CHR;
    LINE1 = 1 * H_CHR;
    LINE2 = 2 * H_CHR;
    LINE3 = 3 * H_CHR;
    LINE4 = 4 * H_CHR;
    LINE5 = 5 * H_CHR;

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
// https://github.com/NoRi-230401/tiny-bleKeyboard-Cardputer
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
    int i = 0;
    while (!SD.begin(M5.getPin(m5::pin_name_t::sd_spi_ss), SPI2) && i < 10)
    {
        delay(500);
        i++;
    }
    if (i >= 10)
    {
        Serial.println("ERR: SD begin fail...");
        SD.end();
        return false;
    }
    return true;
}
