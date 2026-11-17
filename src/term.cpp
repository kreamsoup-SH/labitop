// labitop: terminal layer. ANSI/VT output on both platforms, native key input.
#include "sys.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static HANDLE g_in = INVALID_HANDLE_VALUE, g_out = INVALID_HANDLE_VALUE;
static DWORD g_in_mode = 0, g_out_mode = 0;
static UINT g_old_cp = 0;
static bool g_in_console = false, g_out_console = false, g_active = false;

static void handles() {
    if (g_out != INVALID_HANDLE_VALUE) return;
    g_in = GetStdHandle(STD_INPUT_HANDLE);
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    g_in_console = GetConsoleMode(g_in, &g_in_mode) != 0;
    g_out_console = GetConsoleMode(g_out, &g_out_mode) != 0;
    g_old_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    if (g_out_console)
        SetConsoleMode(g_out, g_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

bool term_is_tty() {
    handles();
    return g_out_console;
}

void term_init() {
    handles();
    if (g_in_console) SetConsoleMode(g_in, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);
    g_active = true;
    term_write("\x1b[?1049h\x1b[?25l");
}

void term_restore() {
    if (g_out == INVALID_HANDLE_VALUE) return;
    if (g_active) {
        term_write("\x1b[0m\x1b[?25h\x1b[?1049l");
        g_active = false;
        if (g_in_console) SetConsoleMode(g_in, g_in_mode);
    }
    if (g_out_console) SetConsoleMode(g_out, g_out_mode);
    if (g_old_cp) SetConsoleOutputCP(g_old_cp);
}

void term_size(int& w, int& h) {
    handles();
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (g_out_console && GetConsoleScreenBufferInfo(g_out, &info)) {
        w = info.srWindow.Right - info.srWindow.Left + 1;
        h = info.srWindow.Bottom - info.srWindow.Top + 1;
    } else {
        w = 120;
        h = 45;
    }
}

void term_write(const std::string& s) {
    handles();
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        DWORD n = 0;
        if (!WriteFile(g_out, p, (DWORD)left, &n, nullptr) || n == 0) break;
        p += n;
        left -= n;
    }
}

int term_read_key(int timeout_ms) {
    if (timeout_ms < 0) timeout_ms = 0;
    if (!g_in_console) {
        Sleep(timeout_ms);
        return K_NONE;
    }
    ULONGLONG end = GetTickCount64() + (ULONGLONG)timeout_ms;
    for (;;) {
        DWORD pending = 0;
        while (GetNumberOfConsoleInputEvents(g_in, &pending) && pending > 0) {
            INPUT_RECORD r;
            DWORD got = 0;
            if (!ReadConsoleInputW(g_in, &r, 1, &got) || got == 0) break;
            if (r.EventType == WINDOW_BUFFER_SIZE_EVENT) return K_RESIZE;
            if (r.EventType != KEY_EVENT || !r.Event.KeyEvent.bKeyDown) continue;
            const KEY_EVENT_RECORD& k = r.Event.KeyEvent;
            switch (k.wVirtualKeyCode) {
            case VK_UP: return K_UP;
            case VK_DOWN: return K_DOWN;
            case VK_PRIOR: return K_PGUP;
            case VK_NEXT: return K_PGDN;
            case VK_HOME: return K_HOME;
            case VK_END: return K_END;
            }
            if ((k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) && k.wVirtualKeyCode == 'C')
                return 'q';
            wchar_t ch = k.uChar.UnicodeChar;
            if (ch > 0 && ch < 128) return (int)ch;
        }
        ULONGLONG now = GetTickCount64();
        if (now >= end) return K_NONE;
        WaitForSingleObject(g_in, (DWORD)(end - now));
    }
}

#else // POSIX

#include <cerrno>
#include <csignal>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static termios g_old_tio;
static bool g_active = false, g_have_tio = false;

static void on_winch(int) {}

bool term_is_tty() { return isatty(STDOUT_FILENO) != 0; }

void term_init() {
    if (tcgetattr(STDIN_FILENO, &g_old_tio) == 0) {
        g_have_tio = true;
        termios t = g_old_tio;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    struct sigaction sa {};
    sa.sa_handler = on_winch; // no SA_RESTART: select() returns EINTR on resize
    sigaction(SIGWINCH, &sa, nullptr);
    g_active = true;
    term_write("\x1b[?1049h\x1b[?25l");
}

void term_restore() {
    if (!g_active) return;
    g_active = false;
    term_write("\x1b[0m\x1b[?25h\x1b[?1049l");
    if (g_have_tio) tcsetattr(STDIN_FILENO, TCSANOW, &g_old_tio);
}

void term_size(int& w, int& h) {
    winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        w = ws.ws_col;
        h = ws.ws_row;
    } else {
        w = 120;
        h = 45;
    }
}

void term_write(const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = write(STDOUT_FILENO, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        p += n;
        left -= (size_t)n;
    }
}

int term_read_key(int timeout_ms) {
    if (timeout_ms < 0) timeout_ms = 0;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval tv { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int r = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    if (r < 0) return errno == EINTR ? K_RESIZE : K_NONE;
    if (r == 0) return K_NONE;
    char buf[16];
    ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
    if (n <= 0) {
        // stdin closed or not readable: avoid a busy loop
        usleep((useconds_t)timeout_ms * 1000);
        return K_NONE;
    }
    if (buf[0] == '\x1b' && n >= 3 && buf[1] == '[') {
        switch (buf[2]) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'H': return K_HOME;
        case 'F': return K_END;
        case '5': return K_PGUP;
        case '6': return K_PGDN;
        case '1': return K_HOME;
        case '4': return K_END;
        }
        return K_NONE;
    }
    // Other escape sequences (F-keys, Alt+key) must not be mistaken for a lone ESC, which quits.
    if (buf[0] == '\x1b' && n > 1) return K_NONE;
    return (unsigned char)buf[0];
}

#endif
