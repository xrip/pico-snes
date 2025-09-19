#include <stdio.h>
#include "snes9x.h"


#if PICO_ON_DEVICE
#include "pico.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "../sparkfun_pico/sfe_pico_alloc.h"
#include "audio.h"
#include "graphics.h"

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/structs/qmi.h"
#include "pico/multicore.h"
#include "pico/stdio.h"
#else
#include <windows.h>
#include <stdalign.h>
#include "win32/MiniFB.h"
#endif

#include "rom.h"

#define AUDIO_SAMPLE_RATE   (22050)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)

int16_t __attribute__((aligned (4))) audioBuffer[AUDIO_BUFFER_LENGTH * 2];
uint16_t __attribute__((aligned (4))) SCREEN[2][SNES_WIDTH * SNES_HEIGHT_EXTENDED];
// uint16_t __attribute__((aligned (4))) SubScreen[SNES_WIDTH * SNES_HEIGHT_EXTENDED];
uint8_t __attribute__((aligned (4))) ZBuffer[SNES_WIDTH * SNES_HEIGHT_EXTENDED];
uint8_t __attribute__((aligned (4))) SubZBuffer[SNES_WIDTH * SNES_HEIGHT_EXTENDED];

volatile uint32_t current_buffer = 0;
bool S9xInitDisplay(void) {
    GFX.Pitch = SNES_WIDTH * sizeof(uint16_t);
    GFX.ZPitch = SNES_WIDTH;
    GFX.SubScreen  = GFX.Screen = (uint8_t *) SCREEN[current_buffer];
    // GFX.SubScreen = (uint8_t *) SubScreen;
    GFX.ZBuffer = (uint8_t *) ZBuffer;
    GFX.SubZBuffer = (uint8_t *) SubZBuffer;

    return true;
}

void S9xDeinitDisplay(void) {
}

uint32_t S9xReadJoypad(const int32_t port) {
    if (port != 0)
        return 0;

    uint32_t joypad = 0;

    return joypad;
}

bool S9xReadMousePosition(int32_t which1, int32_t *x, int32_t *y, uint32_t *buttons) {
    return false;
}

bool S9xReadSuperScopePosition(int32_t *x, int32_t *y, uint32_t *buttons) {
    return false;
}

bool JustifierOffscreen(void) {
    return true;
}

void JustifierButtons(uint32_t *justifiers) {
    (void) justifiers;
}

static inline void snes9x_init() {
    Settings.CyclesPercentage = 100;
    Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.ControllerOption = SNES_JOYPAD;
    Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;
    Settings.SoundPlaybackRate = AUDIO_SAMPLE_RATE;
    Settings.DisableSoundEcho = true;
    Settings.InterpolatedSound = true;

    S9xInitDisplay();

    S9xInitMemory();

    S9xInitAPU();

    S9xInitSound(0, 0);

    S9xInitGFX();
    // GFX.SubScreen = malloc(GFX.Pitch * SNES_HEIGHT_EXTENDED);
    S9xSetPlaybackRate(Settings.SoundPlaybackRate);
    IPPU.RenderThisFrame = 1;
}
#if PICO_ON_DEVICE

static void memory_stats()
{
    size_t mem_size = sfe_mem_size();
    size_t mem_used = sfe_mem_used();
    printf("\tMemory pool - Total: 0x%X (%u)  Used: 0x%X (%u) - %3.2f%%\n", mem_size, mem_size, mem_used, mem_used,
           (float)mem_used / (float)mem_size * 100.0);

    size_t max_block = sfe_mem_max_free_size();
    printf("\tMax free block size: 0x%X (%u) \n", max_block, max_block);
}

/* Renderer loop on Pico's second core */
void __time_critical_func(render_core)() {
    i2s_config_t i2s_config = i2s_get_default_config();
    i2s_config.sample_freq = AUDIO_SAMPLE_RATE;
    i2s_config.dma_trans_count = AUDIO_SAMPLE_RATE / 60;
    i2s_volume(&i2s_config, 0);
    i2s_init(&i2s_config);


    graphics_init();

    graphics_set_buffer((uint8_t *) SCREEN, SNES_WIDTH, SNES_HEIGHT_EXTENDED);
    graphics_set_textbuffer((uint8_t *) SCREEN);
    graphics_set_bgcolor(0x000000);
    graphics_set_offset(32, 0);

    graphics_set_flashmode(false, false);
    graphics_set_mode(GRAPHICSMODE_DEFAULT);

    uint32_t old_buffer = current_buffer;
    while (true) {
        // TODO: Всю обработку звука вынести на второе ядро
        if (old_buffer != current_buffer) {
            // APU_EXECUTE();
            S9xMixSamples((void *) audioBuffer, AUDIO_BUFFER_LENGTH * 2);
            i2s_dma_write(&i2s_config, (const int16_t *) audioBuffer);
            old_buffer = current_buffer;
        }

        tight_loop_contents();
    }
}
static inline void flash_timings() {
    qmi_hw->m[0].timing = 0x60007305;
}
void main(){
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_70);
    flash_timings();
    sleep_ms(100);
    set_sys_clock_hz(504 * MHZ, true); // fallback to failsafe clocks
    sleep_ms(100);

        // stdio_init_all();

    // Initialize onboard LED
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    if (!sfe_pico_alloc_init()) {
        while (true) {
            sleep_ms(15);
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            sleep_ms(15);
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            printf("PSRAM ERROR!\n");
        }
    }

    // LED startup sequence
    for (int i = 20; i--;) {
        sleep_ms(25);
        gpio_put(PICO_DEFAULT_LED_PIN, i ^ 1);
        printf("%d...\n", i);
    }

    const size_t rom_size = sizeof(rom);
    Memory.ROM_AllocSize = rom_size;
    // Memory.ROM = (uint8_t *) malloc(rom_size);
    Memory.ROM = (uint8_t * )&rom;
    // if (!Memory.ROM) {
        // while (1) printf("Allocation failed!");
    // }
    // memcpy(Memory.ROM, rom, rom_size);

    snes9x_init();
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    // memcpy(Memory.ROM, rom, rom_size);
    LoadROM(NULL);

    multicore_launch_core1(render_core);
    int i = 0;
    while (true) {

        S9xMainLoop();
        current_buffer = !current_buffer;
        // GFX.Screen = (uint8_t *) &SCREEN[current_buffer][0];
        GFX.SubScreen  = GFX.Screen = (uint8_t *) SCREEN[current_buffer];
        // sleep_ms(16);
        // gpio_put(PICO_DEFAULT_LED_PIN, i++ & 1);
        // printf("%i\n", i);
        tight_loop_contents();
    }
}
#else
void HandleInput(WPARAM wParam, BOOL isKeyDown) {
}

DWORD WINAPI SoundThread(LPVOID lpParam) {
    WAVEHDR waveHeaders[4];

    WAVEFORMATEX format = { 0 };
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = AUDIO_SAMPLE_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HANDLE waveEvent = CreateEvent(NULL, 1, 0, NULL);

    HWAVEOUT hWaveOut;
    waveOutOpen(&hWaveOut, WAVE_MAPPER, &format, (DWORD_PTR) waveEvent, 0, CALLBACK_EVENT);

    for (size_t i = 0; i < 4; i++) {
        int16_t audio_buffers[4][AUDIO_BUFFER_LENGTH * 2];
        waveHeaders[i] = (WAVEHDR) {
            .lpData = (char *) audio_buffers[i],
            .dwBufferLength = AUDIO_BUFFER_LENGTH * 2,
    };
        waveOutPrepareHeader(hWaveOut, &waveHeaders[i], sizeof(WAVEHDR));
        waveHeaders[i].dwFlags |= WHDR_DONE;
    }
    WAVEHDR *currentHeader = waveHeaders;


    while (true) {
        if (WaitForSingleObject(waveEvent, INFINITE)) {
            fprintf(stderr, "Failed to wait for event.\n");
            return 1;
        }

        if (!ResetEvent(waveEvent)) {
            fprintf(stderr, "Failed to reset event.\n");
            return 1;
        }

        // Wait until audio finishes playing
        while (currentHeader->dwFlags & WHDR_DONE) {
            S9xMixSamples((void *) currentHeader->lpData, AUDIO_BUFFER_LENGTH * 2);
            waveOutWrite(hWaveOut, currentHeader, sizeof(WAVEHDR));

            currentHeader++;
            if (currentHeader == waveHeaders + 4) { currentHeader = waveHeaders; }
        }
    }
    return 0;
}


int main(const int argc, char **argv) {
    const int scale = argc > 2 ? atoi(argv[2]) : 4;

    if (!argv[1]) {
        printf("Usage: dendy.exe <rom.bin> [scale_factor]\n");
        return EXIT_FAILURE;
    }
    if (!mfb_open("SNES", SNES_WIDTH, SNES_HEIGHT, scale, MFB_FORMAT_RGB565))
        return EXIT_FAILURE;

    Memory.ROM = malloc(8 << 20);
    // Memory.ROM = (uint8_t *) rom;
    // memcpy(Memory.ROM, rom, sizeof(rom));
    Memory.ROM_AllocSize = 8 << 20;
    snes9x_init();

    // LoadROM(NULL);

    if (!LoadROM(argv[1]))
        printf("ROM loading failed!");




    CreateThread(NULL, 0, SoundThread, NULL, 0, NULL);

    do {
        S9xMainLoop();
    } while (mfb_update(SCREEN, 60));

    return EXIT_SUCCESS;
}
#endif