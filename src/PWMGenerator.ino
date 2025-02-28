#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Encoder.h>
#include <Arduino.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define ENCODER_CLK 3
#define ENCODER_DT 4
#define BUTTON_PIN 2

// Определение пинов для каналов PWM и инжекторов
const int pwmPins[5] = {5, 6, 7, 8, 9};
const int injectorPins[4] = {5, 7, 8, 9}; // Пины для управления инжекторами

// Структура для хранения параметров канала
struct Channel {
    int frequency = 0;
    int dutyCycle = 50;
    int timer = 0; // Timer in seconds
    int pulseLimit = 0; // Pulse count limit, 0 means no limit
    unsigned long startTime = 0; // Start time for the timer
    int pulseCount = 0; // Current pulse count
};

Channel channels[5];
int selectedChannel = 0; // Выбранный канал (0-4) или 5 для "Channel All"
int menuOffset = 0;
bool menuActive = false;
bool settingActive = false;
int settingIndex = 0; // Index of the current setting being adjusted
int settingOffset = 0; // Смещение меню настроек
bool inSettingsMenu = false;
bool settingSelected = false;
bool injectorMenuActive = false;
int injectorMode = 0; // Текущий режим промывки инжекторов
bool injectorActive = false; // Состояние активности режима промывки
int injectorMenuOffset = 0; // Смещение для меню инжекторов
bool injectorPinsSetLow = false; // Флаг для отслеживания выполнения
bool pwmPinsSetLow = false; // Флаг для отслеживания выполнения
unsigned long constantModeStartTime = 0; // Время активации режима Constant Open
bool constantModeActive = false; // Флаг активности режима Constant Open

Encoder myEnc(ENCODER_CLK, ENCODER_DT);
unsigned long buttonPressTime = 0;
unsigned long lastDisplayUpdate = 0;

// Переменные для отслеживания скорости вращения энкодера
unsigned long lastEncoderChangeTime = 0;
int encoderChangeCount = 0;
const int fastRotationThreshold = 2; // Порог для быстрого вращения (количество изменений за 100 мс)

void setup() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    for (int i = 0; i < 5; i++) {
        pinMode(pwmPins[i], OUTPUT);
    }
    for (int i = 0; i < 4; i++) {
        pinMode(injectorPins[i], OUTPUT);
    }

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        for (;;);
    }
    display.clearDisplay();
    display.display();
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(1, 1);
    display.print("PWM");
    display.setCursor(65, 11);
    display.print("Clear");

    display.setTextSize(1);
    display.setCursor(1, 16);
    display.print("Generator");
    display.setCursor(63, 1);
    display.print("Injector");

    display.display();

    for (int i = 0; i <= SCREEN_WIDTH; i += 4) {
        display.drawRect(0, SCREEN_HEIGHT - 5, i, 3, SSD1306_WHITE);
        display.display();
        delay(50);
    }

    delay(3000);
    display.clearDisplay();
    display.display();
    updateDisplay();
}

void loop() {
    static long oldPosition = 0;
    long newPosition = myEnc.read() / 4;

    if (newPosition != oldPosition) {
        int change = (newPosition - oldPosition);
        oldPosition = newPosition;

        // Отслеживание скорости вращения энкодера
        encoderChangeCount++;
        unsigned long currentTime = millis();
        if (currentTime - lastEncoderChangeTime >= 100) {
            if (encoderChangeCount >= fastRotationThreshold) {
                change *= 10; // Увеличиваем изменение в 10 раз при быстром вращении
            }
            encoderChangeCount = 0;
            lastEncoderChangeTime = currentTime;
        }

        if (settingActive) {
            if (!settingSelected) {
                // Навигация по списку настроек
                settingIndex = (settingIndex + change + 4) % 4;
                if (settingIndex < settingOffset) {
                    settingOffset = settingIndex;
                } else if (settingIndex >= settingOffset + 3) {
                    settingOffset = settingIndex - 2;
                }
            } else {
                // Изменение значения выбранного пункта
                switch (settingIndex) {
                    case 0: // Частота
                        if (selectedChannel < 5) {
                            channels[selectedChannel].frequency = constrain(channels[selectedChannel].frequency + change, 0, 1000);
                            if (channels[selectedChannel].frequency > 0) {
                                channels[selectedChannel].startTime = millis();
                                channels[selectedChannel].pulseCount = 0;
                            }
                        } else {
                            for (int i = 0; i < 5; i++) {
                                channels[i].frequency = constrain(channels[i].frequency + change, 0, 1000);
                                if (channels[i].frequency > 0) {
                                    channels[i].startTime = millis();
                                    channels[i].pulseCount = 0;
                                }
                            }
                        }
                        break;
                    case 1: // Скважность
                        if (selectedChannel < 5) {
                            channels[selectedChannel].dutyCycle = constrain(channels[selectedChannel].dutyCycle + change, 1, 99);
                        } else {
                            for (int i = 0; i < 5; i++) {
                                channels[i].dutyCycle = constrain(channels[i].dutyCycle + change, 1, 99);
                            }
                        }
                        break;
                    case 2: // Таймер
                        if (selectedChannel < 5) {
                            channels[selectedChannel].timer = constrain(channels[selectedChannel].timer + change, 0, 360);
                            if (channels[selectedChannel].timer > 0) {
                                channels[selectedChannel].startTime = millis();
                            }
                        } else {
                            for (int i = 0; i < 5; i++) {
                                channels[i].timer = constrain(channels[i].timer + change, 0, 360);
                                if (channels[i].timer > 0) {
                                    channels[i].startTime = millis();
                                }
                            }
                        }
                        break;
                    case 3: // Ограничение количества импульсов
                        if (selectedChannel < 5) {
                            channels[selectedChannel].pulseLimit = constrain(channels[selectedChannel].pulseLimit + change, 0, 10000);
                            if (channels[selectedChannel].pulseLimit > 0) {
                                channels[selectedChannel].pulseCount = 0;
                            }
                        } else {
                            for (int i = 0; i < 5; i++) {
                                channels[i].pulseLimit = constrain(channels[i].pulseLimit + change, 0, 10000);
                                if (channels[i].pulseLimit > 0) {
                                    channels[i].pulseCount = 0;
                                }
                            }
                        }
                        break;
                }
            }
            updateDisplay();
        } else if (injectorMenuActive) {
            // Навигация по режимам промывки инжекторов
            injectorMode = (injectorMode + change + 8) % 8;
            if (injectorMode < injectorMenuOffset) {
                injectorMenuOffset = injectorMode;
            } else if (injectorMode >= injectorMenuOffset + 4) {
                injectorMenuOffset = injectorMode - 3;
            }
            updateDisplay();
        } else {
            selectedChannel = (selectedChannel + change + 7) % 7;
            if (selectedChannel < menuOffset) {
                menuOffset = selectedChannel;
            } else if (selectedChannel >= menuOffset + 4) {
                menuOffset = selectedChannel - 3;
            }
            updateDisplay();
        }
    }

    if (digitalRead(BUTTON_PIN) == LOW) {
        if (buttonPressTime == 0) buttonPressTime = millis();
    } else {
        if (buttonPressTime > 0) {
            if (millis() - buttonPressTime > 1000) {
                menuActive = !menuActive;
                settingActive = false;
                injectorMenuActive = false;
            } else {
                if (!menuActive) {
                    menuActive = true;
                    if (selectedChannel == 6) {
                        injectorMenuActive = true; // Активируем меню инжекторов
                    } else {
                        settingActive = true;
                        settingSelected = true;
                    }
                } else if (injectorMenuActive) {
                    injectorActive = !injectorActive; // Активируем или деактивируем режим промывки
                } else if (!settingActive) {
                    settingActive = true;
                    settingSelected = true;
                } else {
                    settingSelected = !settingSelected;
                    if (!settingSelected) {
                        settingIndex = (settingIndex + 1) % 4;
                    }
                }
            }
            buttonPressTime = 0;
            updateDisplay();
        }
    }

    generatePWM();
    if (injectorActive) {
        activateInjectorMode();
    }

    // Единоразовое выполнение digitalWrite(injectorPins[i], LOW) при активном меню режимов промывки инжекторов
    if (!injectorActive) {
        if (!injectorPinsSetLow) {
            for (int i = 0; i < 4; i++) {
                digitalWrite(injectorPins[i], LOW);
                updateDisplay();
            }
            injectorPinsSetLow = true; // Устанавливаем флаг, чтобы код не выполнялся повторно
        }
    } else {
        injectorPinsSetLow = false; // Сбрасываем флаг, если меню не активно
    }

    // Единоразовое выполнение digitalWrite(pwmPins[i], LOW) при активном меню настройки выбранного канала
    if (settingActive) {
        if (!pwmPinsSetLow) {
            for (int i = 0; i < 5; i++) {
                // Проверяем, что frequency <= 0, чтобы не нарушить работу PWM
                if (channels[i].frequency <= 0) {
                    digitalWrite(pwmPins[i], LOW);
                }
            }
            pwmPinsSetLow = true; // Устанавливаем флаг, чтобы код не выполнялся повторно
        }
    } else {
        pwmPinsSetLow = false; // Сбрасываем флаг, если меню не активно
    }
}

void updateDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    if (injectorMenuActive) {
        // Отображение режимов промывки инжекторов
        const char* injectorModes[] = {"100 RPM", "300 RPM", "500 RPM", "800 RPM", "1500 RPM", "3600 RPM", "5000 RPM", "Open 5sec"};
        for (int i = 0; i < 8; i++) {
            if (i >= injectorMenuOffset && i < injectorMenuOffset + 4) {
                display.setCursor(0, (i - injectorMenuOffset) * 8);
                if (injectorMode == i) display.print("> "); else display.print("  ");
                display.print(injectorModes[i]);
                if (injectorMode == i && injectorActive) {
                    display.setCursor(120, (i - injectorMenuOffset) * 8);
                    display.print("\x10"); // Символ молнии
                }
            }
        }
    } else if (!settingActive) {
        // Обновление меню выбора каналов
        for (int i = 0; i < 7; i++) {
            if (i >= menuOffset && i < menuOffset + 4) {
                display.setCursor(0, (i - menuOffset) * 8);
                if (selectedChannel == i) display.print("> "); else display.print("  ");
                if (i < 5) {
                    display.print("Channel "); display.print(i + 1);
                } else if (i == 5) {
                    display.print("Channel All");
                } else if (i == 6) {
                    display.print("Injector Clear");
                }
            }
        }
    } else {
        display.setCursor(0, 0);
        display.print("Channel ");
        if (selectedChannel < 5) display.print(selectedChannel + 1);
        else display.print("All");

        // Обновление списка настроек
        const char* settings[] = {"Freq: ", "Duty: ", "Timer: ", "Pulses: "};
        for (int i = 0; i < 4; i++) {
            if (i >= settingOffset && i < settingOffset + 3) {
                display.setCursor(0, ((i - settingOffset) + 1) * 8);
                if (settingIndex == i) display.print("> "); else display.print("  ");
                display.print(settings[i]);
                if (selectedChannel < 5) {
                    switch (i) {
                        case 0: display.print(channels[selectedChannel].frequency); display.print(" Hz"); break;
                        case 1: display.print(channels[selectedChannel].dutyCycle); display.print(" %"); break;
                        case 2: display.print(channels[selectedChannel].timer); display.print(" s"); break;
                        case 3: display.print(channels[selectedChannel].pulseLimit); break;
                    }
                } else {
                    switch (i) {
                        case 0: display.print(channels[0].frequency); display.print(" Hz"); break;
                        case 1: display.print(channels[0].dutyCycle); display.print(" %"); break;
                        case 2: display.print(channels[0].timer); display.print(" s"); break;
                        case 3: display.print(channels[0].pulseLimit); break;
                    }
                }
            }
        }
    }

    // Простое отображение оставшегося времени и количества импульсов
    if (selectedChannel < 5) {
        if (channels[selectedChannel].timer > 0) {
            display.setCursor(0, 32);
            unsigned long elapsedTime = millis() - channels[selectedChannel].startTime;
            unsigned long remainingTime = (channels[selectedChannel].timer * 1000) - elapsedTime;
            display.print("Time left: ");
            display.print(remainingTime / 1000);
            display.print(" s");
        }

        if (channels[selectedChannel].pulseLimit > 0) {
            display.setCursor(0, 40);
            display.print("Pulses left: ");
            display.print(max(channels[selectedChannel].pulseLimit - channels[selectedChannel].pulseCount, 0));
        }
    }

    // Отображение точек активного канала
    for (int i = 0; i < 5; i++) {
        if (channels[i].frequency > 0) {
            display.setCursor(120, i * 6);
            display.print(".");
        }
    }

    display.display();
}

void generatePWM() {
    for (int i = 0; i < 5; i++) {
        if (channels[i].frequency == 0) {
            pwmPinsSetLow = false;
            continue;
        }

        float period = 1000000.0 / channels[i].frequency;
        float highTime = (period * channels[i].dutyCycle) / 100.0;
        float lowTime = period - highTime;

        static unsigned long lastChange[5] = {0};
        static bool state[5] = {0};

        if (micros() - lastChange[i] >= (state[i] ? highTime : lowTime)) {
            lastChange[i] = micros();
            state[i] = !state[i];
            digitalWrite(pwmPins[i], state[i]);

            // Проверка таймера и ограничения количества импульсов
            if (state[i]) {
                if (static_cast<unsigned long>(channels[i].timer) > 0 && millis() - channels[i].startTime >= static_cast<unsigned long>(channels[i].timer) * 1000) {
                    channels[i].frequency = 0; // Остановить ШИМ
                    channels[i].timer = 0; // Сбросить таймер
                    updateDisplay();
                }
                if (channels[i].pulseLimit > 0 && channels[i].pulseCount >= channels[i].pulseLimit) {
                    channels[i].frequency = 0; // Остановить ШИМ
                    channels[i].pulseLimit = 0; // Сбросить ограничение количества импульсов
                    updateDisplay();
                }
                channels[i].pulseCount++;
            }
        }
    }
}

void activateInjectorMode() {
    // Пример кода для различных режимов промывки инжекторов
    switch (injectorMode) {
        case 0:
            // Режим 5: Промывка с частотой 300 оборотов в минуту
            pulseInjectors(100);
            break;
        case 1:
            // Режим 5: Промывка с частотой 300 оборотов в минуту
            pulseInjectors(300);
            break;
        case 2:
            // Режим 6: Промывка с частотой 500 оборотов в минуту
            pulseInjectors(500);
            break;
        case 3:
            // Режим 1: Промывка с частотой 800 оборотов в минуту
            pulseInjectors(800);
            break;
        case 4:
            // Режим 2: Промывка с частотой 1500 оборотов в минуту
            pulseInjectors(1500);
            break;
        case 5:
            // Режим 3: Промывка с частотой 3600 оборотов в минуту
            pulseInjectors(3600);
            break;
        case 6:
            // Режим 4: Промывка с частотой 5000 оборотов в минуту
            pulseInjectors(5000);
            break;
        case 7:
            // Режим 7: Постоянное открытие инжекторов на 5 секунд
            if (!constantModeActive) {
                // Активируем режим Constant Open
                for (int i = 0; i < 4; i++) {
                    digitalWrite(injectorPins[i], HIGH); // Открываем все инжекторы
                }
                constantModeStartTime = millis(); // Запоминаем время активации
                constantModeActive = true; // Устанавливаем флаг активности
            } else {
                // Проверяем, прошло ли 5 секунд
                if (millis() - constantModeStartTime >= 5000) {
                    // Деактивируем режим Constant Open
                    for (int i = 0; i < 4; i++) {
                        injectorPinsSetLow = false;   // Закрываем все инжекторы
                        
                    }
                    constantModeActive = false; // Сбрасываем флаг активности
                    injectorActive = false; // Деактивируем режим промывки
                }
            }
            break;
    }
}

void pulseInjectors(int rpm) {
    // Конвертация оборотов в минуту (RPM) в частоту (Hz)
    float frequency = rpm / 60.0;
    float period = 1000.0 / frequency; // Период в миллисекундах
    float onTime = period * 0.03; // Время открытия инжектора (50% рабочего цикла)
    float offTime = period * 0.97; // Время закрытия инжектора (50% рабочего цикла)

    static unsigned long previousMillis = 0;
    static int injectorState = LOW;

    unsigned long currentMillis = millis();

    if ((injectorState == LOW) && (currentMillis - previousMillis >= offTime)) {
        injectorState = HIGH;
        previousMillis = currentMillis;
        for (int i = 0; i < 4; i++) {
            digitalWrite(injectorPins[i], injectorState);
        }
    } else if ((injectorState == HIGH) && (currentMillis - previousMillis >= onTime)) {
        injectorState = LOW;
        previousMillis = currentMillis;
        for (int i = 0; i < 4; i++) {
            digitalWrite(injectorPins[i], injectorState);
        }
    }
}
