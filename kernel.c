typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

enum {
    VGA_W = 80,
    VGA_H = 25,
    VGA_CELLS = VGA_W * VGA_H,
    LINE_MAX = 160,

    FS_LBA_START = 128,
    FS_LBA_SECTORS = 16,

    FS_MAX_DIRS = 8,
    FS_MAX_FILES = 32,
    FS_NAME_MAX = 16,
    FS_PATH_MAX = 96,
    FS_DATA_MAX = 96,

    FS_MAGIC = 0x53464C43u, /* CLFS */
    FS_VERSION = 2
};

typedef struct __attribute__((packed)) {
    u8 used;
    u8 parent;
    char name[FS_NAME_MAX];
} FsDir;

typedef struct __attribute__((packed)) {
    u8 used;
    u8 dir_index;
    u16 size;
    char name[FS_NAME_MAX];
    char data[FS_DATA_MAX];
} FsFile;

typedef struct __attribute__((packed)) {
    u32 magic;
    u16 version;
    u16 reserved;
    FsDir dirs[FS_MAX_DIRS];
    FsFile files[FS_MAX_FILES];
    u32 checksum;
} CatLoafFs;

static volatile u16* const VGA = (volatile u16*)0xB8000;
static u16 cursor = 0;
static u16 prompt_start = 0;

static char line_buf[LINE_MAX];
static int line_len = 0;

static CatLoafFs g_fs;
static u8 sector_buf[512];
static char path_buf[FS_PATH_MAX];
static char arg_buf[FS_NAME_MAX];
static int g_cwd = 0;

static u8 inb(u16 port) {
    u8 v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static u16 inw(u16 port) {
    u16 v;
    __asm__ __volatile__("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void outb(u16 port, u8 v) {
    __asm__ __volatile__("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void outw(u16 port, u16 v) {
    __asm__ __volatile__("outw %0, %1" : : "a"(v), "Nd"(port));
}

static void putc_col(char c, u8 col);
static void print_col(const char* s, u8 col);
static void println_col(const char* s, u8 col);
static void screen_clear(void);
static void fs_format(void);
static int fs_save(void);
static int str_len(const char* s);
static void mem_set(void* p, u8 v, int n);
static void mem_copy(void* dst, const void* src, int n);
static int streq(const char* a, const char* b);
static int starts_ci(const char* s, const char* pref);
static const char* skip_spaces(const char* s);
static void print_u32(u32 value, u8 col);
static void ps2_wait_input(void);
static void ps2_wait_output(void);
static void ps2_mouse_write(u8 data);
static u8 ps2_mouse_read(void);
static int mouse_init(void);

static u8 cmos_read(u8 reg) {
    outb(0x70, (u8)(reg | 0x80));
    return inb(0x71);
}

static int bcd_to_bin(u8 value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}

static void check_birthday_message(void) {
    int day = bcd_to_bin(cmos_read(0x07));
    int month = bcd_to_bin(cmos_read(0x08));
    int hour = bcd_to_bin(cmos_read(0x04));
    int minute = bcd_to_bin(cmos_read(0x02));

    if (day == 27 && month == 4 && hour == 16 && minute == 5) {
        println_col("Iyi ki dogdun bana", 0x0E);
    }
}

static void set_vga_cell(int row, int col, char c, u8 attr) {
    if (row < 0 || row >= VGA_H || col < 0 || col >= VGA_W) return;
    VGA[row * VGA_W + col] = ((u16)attr << 8) | (u8)c;
}

static void draw_hline(int row, int x1, int x2, u8 attr) {
    int c;
    for (c = x1; c <= x2; c++) set_vga_cell(row, c, '-', attr);
}

static void draw_vline(int col, int y1, int y2, u8 attr) {
    int r;
    for (r = y1; r <= y2; r++) set_vga_cell(r, col, '|', attr);
}

static void draw_box(int x, int y, int w, int h, u8 attr) {
    int x2 = x + w - 1;
    int y2 = y + h - 1;
    set_vga_cell(y, x, '+', attr);
    set_vga_cell(y, x2, '+', attr);
    set_vga_cell(y2, x, '+', attr);
    set_vga_cell(y2, x2, '+', attr);
    draw_hline(y, x + 1, x2 - 1, attr);
    draw_hline(y2, x + 1, x2 - 1, attr);
    draw_vline(x, y + 1, y2 - 1, attr);
    draw_vline(x2, y + 1, y2 - 1, attr);
}

static void draw_box_text(int x, int y, const char* text, u8 attr) {
    int i = 0;
    while (text[i] && x + i < VGA_W) {
        set_vga_cell(y, x + i, text[i], attr);
        i++;
    }
}

static u16 get_vga_cell(int row, int col) {
    if (row < 0 || row >= VGA_H || col < 0 || col >= VGA_W) return 0;
    return VGA[row * VGA_W + col];
}

static void ps2_wait_input(void) {
    while (inb(0x64) & 0x02) {
    }
}

static void ps2_wait_output(void) {
    while (!(inb(0x64) & 0x01)) {
    }
}

static void ps2_mouse_write(u8 data) {
    ps2_wait_input();
    outb(0x64, 0xD4);
    ps2_wait_input();
    outb(0x60, data);
}

static u8 ps2_mouse_read(void) {
    while (!(inb(0x64) & 0x01)) {
    }
    return inb(0x60);
}

static int mouse_init(void) {
    u8 status;
    ps2_wait_input();
    outb(0x64, 0xA8);
    ps2_wait_input();
    outb(0x64, 0x20);
    status = ps2_mouse_read();
    ps2_wait_input();
    outb(0x64, 0x60);
    outb(0x60, status | 0x02);
    ps2_mouse_write(0xF6);
    if (ps2_mouse_read() != 0xFA) return -1;
    ps2_mouse_write(0xF4);
    if (ps2_mouse_read() != 0xFA) return -1;
    return 0;
}

static void gui_draw_window(void) {
    int x = 10;
    int y = 3;
    int w = 60;
    int h = 16;
    draw_box(x, y, w, h, 0x1F);
    draw_box_text(x + 3, y + 1, "CatLoaf GUI v0.4.0", 0x1F);
    draw_box_text(x + 3, y + 3, "1) Dosyalar", 0x2F);
    draw_box_text(x + 3, y + 5, "2) Paket yoneticisi", 0x3F);
    draw_box_text(x + 3, y + 7, "3) Ayarlar", 0x5F);
    draw_box_text(x + 3, y + 9, "Q) Cikis", 0x4F);
    draw_box_text(x + 3, y + 11, "[Secmek icin tus basiniz]", 0x0F);
}

static void gui_draw_menu(int selection) {
    int x = 10;
    int y = 3;
    int w = 60;
    int h = 18;
    const char* items[5] = {"Dosyalar", "Paket yoneticisi", "Ayarlar", "Hakkinda", "Ozel Tesekkurler"};
    int i;

    draw_box(x, y, w, h, 0x1F);
    draw_box_text(x + 3, y + 1, "CatLoaf GUI v0.4.0", 0x1F);
    for (i = 0; i < 5; i++) {
        u8 attr = (i == selection) ? 0x70 : 0x2F;
        draw_box_text(x + 3, y + 3 + i * 2, (i == selection) ? "> " : "  ", attr);
        draw_box_text(x + 5, y + 3 + i * 2, items[i], attr);
    }
    draw_box_text(x + 3, y + 14, "W / A: yukari / asagi, Enter: sec, Q: cikis", 0x0F);
}

static void gui_draw_page(const char* title, const char* lines[], int count, int offset, int show_cat) {
    int x = 12;
    int y = 5;
    int i;
    draw_box(x - 2, y - 2, 56, 14, 0x1F);
    draw_box_text(x, y, title, 0x1E);
    for (i = 0; i < 10; i++) {
        int idx = i + offset;
        if (idx < count) draw_box_text(x, y + 2 + i, lines[idx], 0x07);
        else draw_box_text(x, y + 2 + i, "", 0x07);
    }
    if (show_cat) {
        draw_box_text(x, y + 13, "  /\\_/\\", 0x0D);
        draw_box_text(x, y + 14, " ( o.o )", 0x0D);
        draw_box_text(x, y + 15, "  > ^ <", 0x0D);
    }
    draw_box_text(x, y + 12, "W/A: kaydir, Q: don", 0x0F);
}

static void gui_draw_simple(const char* title, const char* body) {
    const char* lines[4] = {body, "", "[Herhangi tusa basarak donunuz]", ""};
    gui_draw_page(title, lines, 4, 0, 0);
}

static void cmd_gui(void) {
    char choice = 0;
    int selection = 0;
    int offset = 0;
    int show_cat = 0;
    int about_press_count = 0;
    int active_page = 0;
    const char* about_lines[] = {
        "CatLoaf OS v0.4.0-dev",
        "Sistem surumu gizli bir tusla acilabilir.",
        "Kaydirma: W / A",
        ""
    };
    const char* thanks_lines[] = {
        "Tesekkurler: ben, Maxwell Catloaf (isim secme konusunda yardimci oldu)",
        "CatLoaf ismi Maxwell Catloaftan gelmektedir.",
        "test eden herkese ve tabii ki sana tesekkurler :3",
        "Gizli yardimlar icin saklanilmis komutlar var.",
        "ben tarafindan yazilmistir.",
        ""
    };
    const char* settings_lines[] = {
        "1) Tema: yukari / asagi ile secilebilir.",
        "2) Ses: kapali / acik", 
        "3) Tarih/Saat goruntuleme", 
        ""
    };
    const char* pkg_lines[] = {
        "CatLoaf Paket Yonetici v0.4.0-dev",
        "pkg install <isim> - paket yukler.",
        "pkg remove <isim> - paket siler.",
        "pkg list - yüklü paketleri gosterir.",
        "ML paketleri .ml paket uzantisi olabilir.",
        ""
    };
    const char* file_lines[] = {
        "Dosyalar ve dizinler yakinda gorunur.",
        "kedi mamasi /ornek", 
        "Ton baligi /ornek/dosya.txt", 
        ""
    };

    screen_clear();
    gui_draw_menu(selection);

    while (!choice) {
        if (inb(0x64) & 1) {
            u8 sc = inb(0x60);
            if (sc == 0x11) {
                if (selection > 0) selection--; /* W */
                gui_draw_menu(selection);
                continue;
            }
            if (sc == 0x1E) {
                if (selection < 4) selection++; /* A */
                gui_draw_menu(selection);
                continue;
            }
            if (sc == 0x1C) {
                choice = '1' + selection;
                if (selection == 4) choice = '5';
                continue;
            }
            if (sc == 0x10) {
                choice = 'Q';
                continue;
            }
        }
    }

    if (choice == '1') active_page = 1;
    else if (choice == '2') active_page = 2;
    else if (choice == '3') active_page = 3;
    else if (choice == '4') active_page = 4;
    else if (choice == '5') active_page = 5;
    else return;

    offset = 0;
    show_cat = 0;
    about_press_count = 0;

    screen_clear();
    if (active_page == 1) {
        gui_draw_page("Dosyalar", file_lines, 4, offset, 0);
    } else if (active_page == 2) {
        gui_draw_page("Paket yoneticisi", pkg_lines, 5, offset, 0);
    } else if (active_page == 3) {
        gui_draw_page("Ayarlar", settings_lines, 4, offset, 0);
    } else if (active_page == 4) {
        gui_draw_page("Hakkinda", about_lines, 4, offset, show_cat);
    } else if (active_page == 5) {
        gui_draw_page("Ozel Tesekkurler", thanks_lines, 4, offset, 0);
    }

    while (1) {
        if (inb(0x64) & 1) {
            u8 sc = inb(0x60);
            int need_redraw = 0;
            
            if (sc == 0x11) {
                if (offset > 0) offset--;
                need_redraw = 1;
            } else if (sc == 0x1E) {
                offset++;
                need_redraw = 1;
            } else if (sc == 0x2F && active_page == 4) {
                about_press_count++;
                if (about_press_count >= 9) show_cat = 1;
                need_redraw = 1;
            } else if (sc == 0x10) {
                break;
            }
            
            if (offset < 0) offset = 0;
            {
                int page_count = 0;
                if (active_page == 1) page_count = 4;
                else if (active_page == 2) page_count = 5;
                else if (active_page == 3) page_count = 4;
                else if (active_page == 4) page_count = 4;
                else if (active_page == 5) page_count = 4;
                int max_offset = page_count > 10 ? page_count - 10 : 0;
                if (offset > max_offset) offset = max_offset;
            }
            
            if (need_redraw) {
                screen_clear();
                if (active_page == 1) {
                    gui_draw_page("Dosyalar", file_lines, 4, offset, 0);
                } else if (active_page == 2) {
                    gui_draw_page("Paket yoneticisi", pkg_lines, 5, offset, 0);
                } else if (active_page == 3) {
                    gui_draw_page("Ayarlar", settings_lines, 4, offset, 0);
                } else if (active_page == 4) {
                    gui_draw_page("Hakkinda", about_lines, 4, offset, show_cat);
                } else if (active_page == 5) {
                    gui_draw_page("Ozel Tesekkurler", thanks_lines, 4, offset, 0);
                }
            }
        }
    }
}

#define ML_MAX_VARS 8
static char ml_var_name[ML_MAX_VARS][FS_NAME_MAX];
static int ml_var_value[ML_MAX_VARS];

static const char* ml_skip_spaces(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int ml_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int ml_find_var(const char* name) {
    int i;
    for (i = 0; i < ML_MAX_VARS; i++) {
        if (ml_var_name[i][0] && streq(ml_var_name[i], name)) return i;
    }
    return -1;
}

static int ml_alloc_var(const char* name) {
    int i;
    for (i = 0; i < ML_MAX_VARS; i++) {
        if (!ml_var_name[i][0]) {
            mem_set(ml_var_name[i], 0, FS_NAME_MAX);
            mem_copy(ml_var_name[i], name, str_len(name));
            ml_var_value[i] = 0;
            return i;
        }
    }
    return -1;
}

static int ml_set_var(const char* name, int value) {
    int idx = ml_find_var(name);
    if (idx < 0) idx = ml_alloc_var(name);
    if (idx < 0) return -1;
    ml_var_value[idx] = value;
    return 0;
}

static int ml_get_var(const char* name, int* out) {
    int idx = ml_find_var(name);
    if (idx < 0) return -1;
    *out = ml_var_value[idx];
    return 0;
}

static int ml_parse_number(const char** p) {
    int result = 0;
    while (**p >= '0' && **p <= '9') {
        result = result * 10 + (**p - '0');
        (*p)++;
    }
    return result;
}

static int ml_parse_ident(const char** p, char* out, int max) {
    int i = 0;
    while (ml_ident_char(**p) && i < max - 1) {
        out[i++] = **p;
        (*p)++;
    }
    out[i] = '\0';
    return i;
}

static int ml_parse_string(const char** p, char* out, int max) {
    if (**p != '"') return -1;
    (*p)++;
    int i = 0;
    while (**p && **p != '"' && i < max - 1) {
        out[i++] = **p;
        (*p)++;
    }
    if (**p != '"') return -1;
    (*p)++;
    out[i] = '\0';
    return i;
}

static int ml_parse_expr(const char** p, int* out);

static int ml_parse_factor(const char** p, int* out) {
    const char* s = ml_skip_spaces(*p);
    if (*s == '(') {
        s++;
        if (ml_parse_expr(&s, out) != 0) return -1;
        s = ml_skip_spaces(s);
        if (*s != ')') return -1;
        s++;
        *p = s;
        return 0;
    }
    if (*s >= '0' && *s <= '9') {
        *out = ml_parse_number(&s);
        *p = s;
        return 0;
    }
    if (ml_ident_char(*s)) {
        char name[FS_NAME_MAX];
        ml_parse_ident(&s, name, FS_NAME_MAX);
        if (ml_get_var(name, out) == 0) {
            *p = s;
            return 0;
        }
        return -1;
    }
    return -1;
}

static int ml_parse_term(const char** p, int* out) {
    if (ml_parse_factor(p, out) != 0) return -1;
    while (1) {
        const char* s = ml_skip_spaces(*p);
        if (*s == '*' || *s == '/') {
            char op = *s;
            s++;
            int rhs;
            if (ml_parse_factor(&s, &rhs) != 0) return -1;
            if (op == '*') *out = (*out) * rhs;
            else {
                if (rhs == 0) return -1;
                *out = (*out) / rhs;
            }
            *p = s;
            continue;
        }
        break;
    }
    return 0;
}

static int ml_parse_expr(const char** p, int* out) {
    if (ml_parse_term(p, out) != 0) return -1;
    while (1) {
        const char* s = ml_skip_spaces(*p);
        if (*s == '+' || *s == '-') {
            char op = *s;
            s++;
            int rhs;
            if (ml_parse_term(&s, &rhs) != 0) return -1;
            if (op == '+') *out = (*out) + rhs;
            else *out = (*out) - rhs;
            *p = s;
            continue;
        }
        break;
    }
    return 0;
}

static int ml_execute_ml(const char* input) {
    const char* p = ml_skip_spaces(input);
    if (starts_ci(p, "let ")) {
        p += 4;
        char name[FS_NAME_MAX];
        if (ml_parse_ident(&p, name, FS_NAME_MAX) <= 0) return -1;
        p = ml_skip_spaces(p);
        if (*p != '=') return -1;
        p++;
        int value;
        if (ml_parse_expr(&p, &value) != 0) return -1;
        if (ml_set_var(name, value) != 0) return -1;
        println_col("ok", 0x0A);
        return 0;
    }
    if (starts_ci(p, "print")) {
        p += 5;
        p = ml_skip_spaces(p);
        if (*p != '(') return -1;
        p++;
        p = ml_skip_spaces(p);
        if (*p == '"') {
            char text[FS_PATH_MAX];
            if (ml_parse_string(&p, text, FS_PATH_MAX) < 0) return -1;
            p = ml_skip_spaces(p);
            if (*p != ')') return -1;
            println_col(text, 0x07);
            return 0;
        }
        int value;
        if (ml_parse_expr(&p, &value) != 0) return -1;
        p = ml_skip_spaces(p);
        if (*p != ')') return -1;
        p++;
        print_u32(value, 0x07);
        putc_col('\n', 0x07);
        return 0;
    }
    if (starts_ci(p, "pkg ")) {
        println_col("ML paket yoneticisi kismi calisiyor...", 0x0D);
        return 0;
    }
    int value;
    if (ml_parse_expr(&p, &value) != 0) return -1;
    p = ml_skip_spaces(p);
    if (*p != '\0') return -1;
    print_u32(value, 0x07);
    putc_col('\n', 0x07);
    return 0;
}

static void cmd_ml(const char* arg_line) {
    if (skip_spaces(arg_line)[0] == '\0') {
        println_col("Kullan: ml <komut>", 0x0C);
        return;
    }
    if (ml_execute_ml(arg_line) != 0) {
        println_col("ML: gecersiz ifade.", 0x0C);
    }
}

static int boot_help_scan(u8 sc) {
    if (sc == 0x23) return 1; /* H */
    if (sc == 0x12) return 2; /* E */
    if (sc == 0x26) return 4; /* L */
    if (sc == 0x19) return 8; /* P */
    return 0;
}

static void draw_loader_frame(int frame, int help_flags) {
    int cx = VGA_W / 2;
    int cy = VGA_H / 2;
    int pos_x[4] = {0, 2, 0, -2};
    int pos_y[4] = {-1, 0, 1, 0};
    u8 colors[4] = {0x0A, 0x0C, 0x09, 0x0E};
    int i;

    screen_clear();
    print_col("CatLoaf loading...", 0x0F);
    putc_col('\n', 0x0F);
    println_col("   H E L P basili tutarsan recovery acilir.", 0x0B);
    putc_col('\n', 0x0F);

    set_vga_cell(cy, cx, '0', 0x0F);
    for (i = 0; i < 4; i++) {
        int idx = (i + frame) & 3;
        set_vga_cell(cy + pos_y[idx], cx + pos_x[idx], '0', colors[i]);
    }

    print_col("[", 0x07);
    print_col((help_flags == 15) ? "HELP READY" : "HELP: ", 0x0F);
    if (help_flags & 1) print_col("H", 0x0A);
    else print_col("-", 0x07);
    print_col(" ", 0x07);
    if (help_flags & 2) print_col("E", 0x0A);
    else print_col("-", 0x07);
    print_col(" ", 0x07);
    if (help_flags & 4) print_col("L", 0x0A);
    else print_col("-", 0x07);
    print_col(" ", 0x07);
    if (help_flags & 8) print_col("P", 0x0A);
    else print_col("-", 0x07);
    print_col("]", 0x07);
}

static void show_recovery_menu(void) {
    char choice = 0;

    screen_clear();
    println_col("=== CatLoaf Recovery ===", 0x0E);
    println_col("1) FS sifirla", 0x0B);
    println_col("2) Paket yoneticisi (ML/apt benzeri)", 0x0B);
    println_col("3) Devam et", 0x0B);
    putc_col('\n', 0x07);
    print_col("Secim: ", 0x0F);

    while (!choice) {
        if (inb(0x64) & 1) {
            u8 sc = inb(0x60);
            if (sc == 0x02) choice = '1';
            if (sc == 0x03) choice = '2';
            if (sc == 0x04) choice = '3';
        }
    }

    putc_col(choice, 0x0F);
    putc_col('\n', 0x0F);

    if (choice == '1') {
        screen_clear();
        println_col("EMIN MISIN? BU TUM DOSYALARINI SILECEKTIR!", 0x0C);
        println_col("E = EVET, H = HAYIR", 0x0E);
        while (1) {
            if (inb(0x64) & 1) {
                u8 sc = inb(0x60);
                if (sc == 0x12) {
                    fs_format();
                    if (fs_save() == 0) println_col("FS sifirlandi.", 0x0A);
                    else println_col("Disk yazma hatasi.", 0x0C);
                    break;
                }
                if (sc == 0x23) {
                    println_col("FS sifirlama iptal edildi.", 0x0E);
                    break;
                }
            }
        }
    } else if (choice == '2') {
        println_col("CatLoaf paket yoneticisi gelistirme asamasinda.", 0x0D);
        println_col("MeowLine paketleri ML dilinde olusturulacak.", 0x0D);
        println_col("Apt/cacalib benzeri sistem hedefi.", 0x0D);
    }

    println_col("Devam etmek icin bir tusa bas.", 0x0F);
    while (1) {
        if (inb(0x64) & 1) {
            u8 sc = inb(0x60);
            if (!(sc & 0x80)) break;
        }
    }
}

static int show_boot_loader(void) {
    int help_flags = 0;
    int frame = 0;
    int elapsed = 0;
    int last_sec = bcd_to_bin(cmos_read(0x00));

    while (elapsed < 7) {
        draw_loader_frame(frame, help_flags);
        while (bcd_to_bin(cmos_read(0x00)) == last_sec) {
            if (inb(0x64) & 1) {
                u8 sc = inb(0x60);
                if (!(sc & 0x80)) help_flags |= boot_help_scan(sc);
            }
        }
        last_sec = bcd_to_bin(cmos_read(0x00));
        frame++;
        elapsed++;
    }
    return help_flags == 15;
}

static int str_len(const char* s) {
    int n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static void mem_set(void* p, u8 v, int n) {
    u8* d = (u8*)p;
    int i;
    for (i = 0; i < n; i++) d[i] = v;
}

static void mem_copy(void* dst, const void* src, int n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    int i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static char lower_ch(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int streq(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int streq_ci(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (lower_ch(a[i]) != lower_ch(b[i])) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int starts_ci(const char* s, const char* pref) {
    int i = 0;
    while (pref[i]) {
        if (lower_ch(s[i]) != lower_ch(pref[i])) return 0;
        i++;
    }
    return 1;
}

static int copy_token(const char* src, char* dst, int max) {
    int i = 0;
    while (src[i] && src[i] != ' ') {
        if (i >= max - 1) return -1;
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

static int copy_str_limit(const char* src, char* dst, int max) {
    int i = 0;
    while (src[i]) {
        if (i >= max - 1) return -1;
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

static const char* skip_spaces(const char* s) {
    while (*s == ' ') s++;
    return s;
}

static void scroll_if_needed(void) {
    int i;
    while (cursor >= VGA_CELLS) {
        for (i = 0; i < VGA_W * (VGA_H - 1); i++) VGA[i] = VGA[i + VGA_W];
        for (i = VGA_W * (VGA_H - 1); i < VGA_CELLS; i++) VGA[i] = 0x0720;
        cursor -= VGA_W;
        if (prompt_start >= VGA_W) prompt_start -= VGA_W;
        else prompt_start = 0;
    }
}

static void putc_col(char c, u8 col) {
    if (c == '\n') {
        cursor += VGA_W - (cursor % VGA_W);
        scroll_if_needed();
        return;
    }
    VGA[cursor] = ((u16)col << 8) | (u8)c;
    cursor++;
    scroll_if_needed();
}

static void print_col(const char* s, u8 col) {
    int i = 0;
    while (s[i]) {
        putc_col(s[i], col);
        i++;
    }
}

static void println_col(const char* s, u8 col) {
    print_col(s, col);
    putc_col('\n', col);
}

static void screen_clear(void) {
    int i;
    for (i = 0; i < VGA_CELLS; i++) VGA[i] = 0x0720;
    cursor = 0;
    prompt_start = 0;
}

static void prompt(void) {
    print_col("MeowLine> ", 0x0F);
    prompt_start = cursor;
}

static void print_header(void) {
    println_col("PurrfectOS CatLoaf FS", 0x0A);
    println_col("Maxwell Catloaf gondermesi", 0x07);
    putc_col('\n', 0x07);
}

static void ata_wait_not_busy(void) {
    while (inb(0x1F7) & 0x80) {
    }
}

static int ata_wait_drq(void) {
    u8 s;
    for (;;) {
        s = inb(0x1F7);
        if (s & 0x01) return -1;
        if (s & 0x08) return 0;
    }
}

static int ata_read_sector(u32 lba, u8* out) {
    int i;
    ata_wait_not_busy();
    outb(0x1F6, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(0x1F2, 1);
    outb(0x1F3, (u8)(lba & 0xFF));
    outb(0x1F4, (u8)((lba >> 8) & 0xFF));
    outb(0x1F5, (u8)((lba >> 16) & 0xFF));
    outb(0x1F7, 0x20);
    if (ata_wait_drq() != 0) return -1;
    for (i = 0; i < 256; i++) {
        u16 w = inw(0x1F0);
        out[i * 2] = (u8)(w & 0xFF);
        out[i * 2 + 1] = (u8)((w >> 8) & 0xFF);
    }
    return 0;
}

static int ata_write_sector(u32 lba, const u8* in) {
    int i;
    ata_wait_not_busy();
    outb(0x1F6, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(0x1F2, 1);
    outb(0x1F3, (u8)(lba & 0xFF));
    outb(0x1F4, (u8)((lba >> 8) & 0xFF));
    outb(0x1F5, (u8)((lba >> 16) & 0xFF));
    outb(0x1F7, 0x30);
    if (ata_wait_drq() != 0) return -1;
    for (i = 0; i < 256; i++) {
        u16 w = (u16)in[i * 2] | ((u16)in[i * 2 + 1] << 8);
        outw(0x1F0, w);
    }
    outb(0x1F7, 0xE7); /* flush */
    ata_wait_not_busy();
    return 0;
}

static u32 fs_checksum(const CatLoafFs* fs) {
    const u8* p = (const u8*)fs;
    u32 sum = 0;
    int i;
    int n = (int)sizeof(CatLoafFs) - 4;
    for (i = 0; i < n; i++) sum += p[i];
    return sum;
}

static void fs_format(void) {
    mem_set(&g_fs, 0, (int)sizeof(g_fs));
    g_fs.magic = FS_MAGIC;
    g_fs.version = FS_VERSION;
    g_fs.dirs[0].used = 1;
    g_fs.dirs[0].parent = 0;
    g_fs.dirs[0].name[0] = '/';
    g_fs.dirs[0].name[1] = '\0';
    g_fs.checksum = fs_checksum(&g_fs);
}

static int fs_save(void) {
    int s;
    u8* raw = (u8*)&g_fs;
    int total = (int)sizeof(g_fs);

    g_fs.checksum = fs_checksum(&g_fs);
    for (s = 0; s < FS_LBA_SECTORS; s++) {
        int off = s * 512;
        int chunk = 512;
        mem_set(sector_buf, 0, 512);
        if (off < total) {
            if (off + chunk > total) chunk = total - off;
            mem_copy(sector_buf, raw + off, chunk);
        }
        if (ata_write_sector(FS_LBA_START + (u32)s, sector_buf) != 0) return -1;
    }
    return 0;
}

static int fs_load(void) {
    int s;
    u8* raw = (u8*)&g_fs;
    int total = (int)sizeof(g_fs);

    mem_set(&g_fs, 0, total);
    for (s = 0; s < FS_LBA_SECTORS; s++) {
        int off = s * 512;
        int chunk = 512;
        if (ata_read_sector(FS_LBA_START + (u32)s, sector_buf) != 0) return -1;
        if (off < total) {
            if (off + chunk > total) chunk = total - off;
            mem_copy(raw + off, sector_buf, chunk);
        }
    }
    if (g_fs.magic != FS_MAGIC) return -1;
    if (g_fs.version != FS_VERSION) return -1;
    if (g_fs.checksum != fs_checksum(&g_fs)) return -1;
    return 0;
}

static int fs_repair(void) {
    int i;
    int changed = 0;

    if (!g_fs.dirs[0].used || g_fs.dirs[0].name[0] != '/') {
        g_fs.dirs[0].used = 1;
        g_fs.dirs[0].parent = 0;
        g_fs.dirs[0].name[0] = '/';
        g_fs.dirs[0].name[1] = '\0';
        changed = 1;
    }

    for (i = 1; i < FS_MAX_DIRS; i++) {
        if (g_fs.dirs[i].used) {
            u8 p = g_fs.dirs[i].parent;
            if (p >= FS_MAX_DIRS || !g_fs.dirs[p].used || p == (u8)i) {
                g_fs.dirs[i].used = 0;
                g_fs.dirs[i].name[0] = '\0';
                g_fs.dirs[i].parent = 0;
                changed = 1;
            }
        }
    }

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (g_fs.files[i].used) {
            u8 d = g_fs.files[i].dir_index;
            if (d >= FS_MAX_DIRS || !g_fs.dirs[d].used) {
                g_fs.files[i].used = 0;
                g_fs.files[i].name[0] = '\0';
                g_fs.files[i].data[0] = '\0';
                g_fs.files[i].size = 0;
                g_fs.files[i].dir_index = 0;
                changed = 1;
            }
        }
    }

    if (changed) {
        if (fs_save() != 0) return -1;
    }
    return changed;
}

static int fs_find_dir_child(int parent, const char* name) {
    int i;
    for (i = 1; i < FS_MAX_DIRS; i++) {
        if (g_fs.dirs[i].used &&
            g_fs.dirs[i].parent == (u8)parent &&
            streq(g_fs.dirs[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int fs_find_file(int dir, const char* name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (g_fs.files[i].used &&
            g_fs.files[i].dir_index == (u8)dir &&
            streq(g_fs.files[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int fs_resolve_dir(const char* path, int base, int* out_dir) {
    int cur = base;
    int i = 0;
    char seg[FS_NAME_MAX];

    if (!path || path[0] == '\0') {
        *out_dir = base;
        return 0;
    }

    if (path[0] == '/') {
        cur = 0;
        i = 1;
    }

    while (1) {
        int j = 0;
        int next;

        while (path[i] == '/') i++;
        if (path[i] == '\0') break;

        while (path[i] != '\0' && path[i] != '/') {
            if (j >= FS_NAME_MAX - 1) return -1;
            seg[j++] = path[i++];
        }
        seg[j] = '\0';

        if (streq(seg, ".") || seg[0] == '\0') {
            continue;
        }
        if (streq(seg, "..")) {
            if (cur != 0) cur = g_fs.dirs[cur].parent;
            continue;
        }

        next = fs_find_dir_child(cur, seg);
        if (next < 0) return -1;
        cur = next;
    }

    *out_dir = cur;
    return 0;
}

static int fs_resolve_parent_leaf(const char* raw_path, int base, int* out_parent, char* out_leaf) {
    int len;
    int last = -1;
    int i;
    char parent_path[FS_PATH_MAX];
    int leaf_len = 0;

    if (!raw_path || raw_path[0] == '\0') return -1;

    len = str_len(raw_path);
    while (len > 0 && raw_path[len - 1] == '/') len--;
    if (len <= 0) return -1;

    for (i = 0; i < len; i++) {
        if (raw_path[i] == '/') last = i;
    }

    if (last < 0) {
        if (len >= FS_NAME_MAX) return -1;
        for (i = 0; i < len; i++) out_leaf[i] = raw_path[i];
        out_leaf[len] = '\0';
        if (streq(out_leaf, ".") || streq(out_leaf, "..")) return -1;
        *out_parent = base;
        return 0;
    }

    if (last == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        if (last >= FS_PATH_MAX) return -1;
        for (i = 0; i < last; i++) parent_path[i] = raw_path[i];
        parent_path[last] = '\0';
    }

    for (i = last + 1; i < len; i++) {
        if (leaf_len >= FS_NAME_MAX - 1) return -1;
        out_leaf[leaf_len++] = raw_path[i];
    }
    out_leaf[leaf_len] = '\0';
    if (leaf_len == 0) return -1;
    if (streq(out_leaf, ".") || streq(out_leaf, "..")) return -1;

    return fs_resolve_dir(parent_path, base, out_parent);
}

static void fs_build_path(int dir, char* out, int max) {
    int stack[FS_MAX_DIRS];
    int top = 0;
    int i = 0;

    if (dir == 0) {
        if (max > 0) out[0] = '/';
        if (max > 1) out[1] = '\0';
        return;
    }

    while (dir > 0 && top < FS_MAX_DIRS) {
        stack[top++] = dir;
        dir = g_fs.dirs[dir].parent;
    }

    if (dir != 0 || top == FS_MAX_DIRS) {
        if (max > 0) out[0] = '?';
        if (max > 1) out[1] = '\0';
        return;
    }

    if (i < max - 1) out[i++] = '/';
    while (top > 0 && i < max - 1) {
        int idx = stack[--top];
        int j = 0;
        while (g_fs.dirs[idx].name[j] && i < max - 1) {
            out[i++] = g_fs.dirs[idx].name[j++];
        }
        if (top > 0 && i < max - 1) out[i++] = '/';
    }
    out[i] = '\0';
}

static void cmd_gozleme(void) {
    int i;
    fs_build_path(g_cwd, path_buf, FS_PATH_MAX);
    print_col("Konum: ", 0x0B);
    println_col(path_buf, 0x0B);

    println_col("Dizinler:", 0x0B);
    if (g_cwd != 0) println_col("- ..", 0x07);
    for (i = 1; i < FS_MAX_DIRS; i++) {
        if (g_fs.dirs[i].used && g_fs.dirs[i].parent == (u8)g_cwd) {
            print_col("- ", 0x07);
            println_col(g_fs.dirs[i].name, 0x07);
        }
    }
    putc_col('\n', 0x07);
    println_col("Dosyalar:", 0x0B);
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (g_fs.files[i].used && g_fs.files[i].dir_index == (u8)g_cwd) {
            print_col("- ", 0x07);
            println_col(g_fs.files[i].name, 0x07);
        }
    }
}

static void cmd_pwd(void) {
    fs_build_path(g_cwd, path_buf, FS_PATH_MAX);
    println_col(path_buf, 0x0B);
}

static void cmd_cd(const char* arg) {
    int target;
    int tok;
    const char* p = skip_spaces(arg);

    if (*p == '\0') {
        g_cwd = 0;
        return;
    }

    tok = copy_token(p, path_buf, FS_PATH_MAX);
    if (tok < 0 || path_buf[0] == '\0') {
        println_col("Kullan: cd <dizin>", 0x0C);
        return;
    }

    if (fs_resolve_dir(path_buf, g_cwd, &target) != 0) {
        println_col("Dizin bulunamadi.", 0x0C);
        return;
    }

    g_cwd = target;
}

static void cmd_kedi_mamasi(const char* arg) {
    int i;
    int parent;
    int free_idx = -1;
    int tok;

    arg = skip_spaces(arg);
    tok = copy_token(arg, path_buf, FS_PATH_MAX);
    if (tok <= 0 || path_buf[0] == '\0') {
        println_col("Kullan: kedi mamasi <dizin>", 0x0C);
        return;
    }

    if (fs_resolve_parent_leaf(path_buf, g_cwd, &parent, arg_buf) != 0) {
        println_col("Yol hatasi.", 0x0C);
        return;
    }
    if (fs_find_dir_child(parent, arg_buf) >= 0) {
        println_col("Dizin zaten var.", 0x0C);
        return;
    }
    for (i = 1; i < FS_MAX_DIRS; i++) {
        if (!g_fs.dirs[i].used) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        println_col("Dizin limiti dolu.", 0x0C);
        return;
    }
    g_fs.dirs[free_idx].used = 1;
    g_fs.dirs[free_idx].parent = (u8)parent;
    mem_set(g_fs.dirs[free_idx].name, 0, FS_NAME_MAX);
    mem_copy(g_fs.dirs[free_idx].name, arg_buf, str_len(arg_buf));
    if (fs_save() != 0) println_col("Disk yazma hatasi.", 0x0C);
    else println_col("Dizin olusturuldu.", 0x0A);
}

static void cmd_pencele(const char* arg) {
    int tok;
    int d;
    int i;
    u8 dead[FS_MAX_DIRS];
    int changed = 1;
    arg = skip_spaces(arg);
    tok = copy_token(arg, path_buf, FS_PATH_MAX);
    if (tok <= 0 || path_buf[0] == '\0') {
        println_col("Kullan: Pencele <dizin>", 0x0C);
        return;
    }
    if (fs_resolve_dir(path_buf, g_cwd, &d) != 0) {
        println_col("Dizin bulunamadi.", 0x0C);
        return;
    }
    if (d <= 0) {
        println_col("Dizin bulunamadi.", 0x0C);
        return;
    }
    for (i = 0; i < FS_MAX_DIRS; i++) dead[i] = 0;
    dead[d] = 1;
    while (changed) {
        changed = 0;
        for (i = 1; i < FS_MAX_DIRS; i++) {
            if (g_fs.dirs[i].used && !dead[i] && dead[g_fs.dirs[i].parent]) {
                dead[i] = 1;
                changed = 1;
            }
        }
    }

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (g_fs.files[i].used && dead[g_fs.files[i].dir_index]) {
            g_fs.files[i].used = 0;
            g_fs.files[i].name[0] = '\0';
            g_fs.files[i].data[0] = '\0';
            g_fs.files[i].size = 0;
            g_fs.files[i].dir_index = 0;
        }
    }
    for (i = 1; i < FS_MAX_DIRS; i++) {
        if (dead[i]) {
            g_fs.dirs[i].used = 0;
            g_fs.dirs[i].name[0] = '\0';
            g_fs.dirs[i].parent = 0;
        }
    }
    if (dead[g_cwd]) g_cwd = 0;

    if (fs_save() != 0) println_col("Disk yazma hatasi.", 0x0C);
    else println_col("Dizin pence ile silindi.", 0x0A);
}

static void cmd_ton_baligi(const char* arg) {
    int i;
    int free_idx = -1;
    int d;
    int tok;
    char file_name[FS_NAME_MAX];

    arg = skip_spaces(arg);
    tok = copy_token(arg, path_buf, FS_PATH_MAX);
    if (tok <= 0 || path_buf[0] == '\0') {
        println_col("Kullan: Ton baligi <dosya>", 0x0C);
        return;
    }
    if (fs_resolve_parent_leaf(path_buf, g_cwd, &d, file_name) != 0) {
        println_col("Yol hatasi.", 0x0C);
        return;
    }
    if (fs_find_file(d, file_name) >= 0) {
        println_col("Dosya zaten var.", 0x0C);
        return;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!g_fs.files[i].used) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        println_col("Dosya limiti dolu.", 0x0C);
        return;
    }

    g_fs.files[free_idx].used = 1;
    g_fs.files[free_idx].dir_index = (u8)d;
    g_fs.files[free_idx].size = 0;
    mem_set(g_fs.files[free_idx].name, 0, FS_NAME_MAX);
    mem_set(g_fs.files[free_idx].data, 0, FS_DATA_MAX);
    mem_copy(g_fs.files[free_idx].name, file_name, str_len(file_name));
    if (fs_save() != 0) println_col("Disk yazma hatasi.", 0x0C);
    else println_col("Ton baligi ile dosya olustu.", 0x0A);
}

static void cmd_yirt(const char* arg) {
    int d;
    int f;
    char file_name[FS_NAME_MAX];
    int tok;

    arg = skip_spaces(arg);
    tok = copy_token(arg, path_buf, FS_PATH_MAX);
    if (tok <= 0 || path_buf[0] == '\0') {
        println_col("Kullan: Yirt <dosya>", 0x0C);
        return;
    }
    if (fs_resolve_parent_leaf(path_buf, g_cwd, &d, file_name) != 0) {
        println_col("Yol hatasi.", 0x0C);
        return;
    }
    f = fs_find_file(d, file_name);
    if (f < 0) {
        println_col("Dosya bulunamadi.", 0x0C);
        return;
    }
    g_fs.files[f].used = 0;
    if (fs_save() != 0) println_col("Disk yazma hatasi.", 0x0C);
    else println_col("Dosya yirtildi.", 0x0A);
}

static void cmd_avla(const char* arg_line) {
    const char* p = skip_spaces(arg_line);
    const char* content;
    int tok_len;
    int d;
    int f;
    int n;
    char file_name[FS_NAME_MAX];

    if (*p == '\0') {
        println_col("Kullan: avla <dosya> <icerik>", 0x0C);
        return;
    }

    tok_len = copy_token(p, path_buf, (int)sizeof(path_buf));
    if (tok_len < 0) {
        println_col("Dosya adi cok uzun.", 0x0C);
        return;
    }
    p += tok_len;
    content = skip_spaces(p);
    if (*content == '\0') {
        println_col("Icerik bos olamaz.", 0x0C);
        return;
    }

    if (fs_resolve_parent_leaf(path_buf, g_cwd, &d, file_name) != 0) {
        println_col("Yol hatasi.", 0x0C);
        return;
    }
    f = fs_find_file(d, file_name);
    if (f < 0) {
        println_col("Dosya yok, once Ton baligi kullan.", 0x0C);
        return;
    }

    n = str_len(content);
    if (n >= FS_DATA_MAX) {
        println_col("Icerik cok uzun.", 0x0C);
        return;
    }
    mem_set(g_fs.files[f].data, 0, FS_DATA_MAX);
    mem_copy(g_fs.files[f].data, content, n);
    g_fs.files[f].size = (u16)n;
    if (fs_save() != 0) println_col("Disk yazma hatasi.", 0x0C);
    else println_col("Dosya avlandi ve guncellendi.", 0x0A);
}

static void cmd_cat(const char* arg) {
    int d;
    int f;
    char file_name[FS_NAME_MAX];
    int tok;

    arg = skip_spaces(arg);
    tok = copy_token(arg, path_buf, FS_PATH_MAX);
    if (tok <= 0 || path_buf[0] == '\0') {
        println_col("Kullan: cat <dosya>", 0x0C);
        return;
    }
    if (fs_resolve_parent_leaf(path_buf, g_cwd, &d, file_name) != 0) {
        println_col("Yol hatasi.", 0x0C);
        return;
    }
    f = fs_find_file(d, file_name);
    if (f < 0) {
        println_col("Dosya bulunamadi.", 0x0C);
        return;
    }
    println_col(g_fs.files[f].data, 0x07);
}

static int fs_count_dirs(void) {
    int i;
    int count = 0;
    for (i = 0; i < FS_MAX_DIRS; i++) {
        if (g_fs.dirs[i].used) count++;
    }
    return count;
}

static int fs_count_files(void) {
    int i;
    int count = 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (g_fs.files[i].used) count++;
    }
    return count;
}

static int fs_count_data(void) {
    int i;
    int total = 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (g_fs.files[i].used) total += g_fs.files[i].size;
    }
    return total;
}

static void print_u32(u32 value, u8 col) {
    char buf[11];
    int pos = 0;
    if (value == 0) {
        putc_col('0', col);
        return;
    }
    while (value > 0 && pos < (int)sizeof(buf) - 1) {
        buf[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (pos > 0) {
        putc_col(buf[--pos], col);
    }
}

static void cpuid(u32 leaf, u32 subleaf, u32* eax, u32* ebx, u32* ecx, u32* edx) {
    u32 a = leaf;
    u32 c = subleaf;
    u32 b, d;
    
    __asm__ __volatile__(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(a), "c"(c)
    );
    
    *eax = a;
    *ebx = b;
    *ecx = c;
    *edx = d;
}

static void get_cpu_brand(char* brand_str) {
    u32 eax, ebx, ecx, edx;
    int i;
    
    mem_set(brand_str, 0, 48);
    
    cpuid(0x80000002, 0, &eax, &ebx, &ecx, &edx);
    mem_copy(brand_str, &eax, 4);
    mem_copy(brand_str + 4, &ebx, 4);
    mem_copy(brand_str + 8, &ecx, 4);
    mem_copy(brand_str + 12, &edx, 4);
    
    cpuid(0x80000003, 0, &eax, &ebx, &ecx, &edx);
    mem_copy(brand_str + 16, &eax, 4);
    mem_copy(brand_str + 20, &ebx, 4);
    mem_copy(brand_str + 24, &ecx, 4);
    mem_copy(brand_str + 28, &edx, 4);
    
    cpuid(0x80000004, 0, &eax, &ebx, &ecx, &edx);
    mem_copy(brand_str + 32, &eax, 4);
    mem_copy(brand_str + 36, &ebx, 4);
    mem_copy(brand_str + 40, &ecx, 4);
    mem_copy(brand_str + 44, &edx, 4);
    
    for (i = 0; i < 48; i++) {
        if (brand_str[i] < 32 || brand_str[i] > 126) brand_str[i] = ' ';
    }
}

static u32 get_cpu_features(void) {
    u32 eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return edx;
}

static int get_cpu_cores(void) {
    u32 eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return ((ebx >> 16) & 0xFF);
}

static void cmd_top(void) {
    int used_dirs = fs_count_dirs();
    int used_files = fs_count_files();
    int used_bytes = fs_count_data();
    char brand[48];
    u32 features;
    int cores;

    println_col("=== CatLoaf Sistem Bilgileri v0.4.0 ===", 0x0E);
    putc_col('\n', 0x07);
    
    println_col("-- Kernel --", 0x0B);
    print_col("Derleme: ", 0x0B);
    println_col(__DATE__ " " __TIME__, 0x07);
    print_col("Versiyon: ", 0x0B);
    println_col("CatLoaf OS v0.4.0-dev", 0x07);
    
    putc_col('\n', 0x07);
    println_col("-- CPU Bilgileri --", 0x0B);
    
    get_cpu_brand(brand);
    print_col("Islemci: ", 0x0B);
    println_col(brand, 0x07);
    
    cores = get_cpu_cores();
    print_col("Cekirdek Sayisi: ", 0x0B);
    if (cores > 0) print_u32(cores, 0x07);
    else println_col("Tespit edilemedi", 0x07);
    putc_col('\n', 0x07);
    
    features = get_cpu_features();
    print_col("FPU: ", 0x0B);
    println_col((features & 0x01) ? "Var" : "Yok", 0x07);
    
    print_col("MMU: ", 0x0B);
    println_col((features & 0x08) ? "Var" : "Yok", 0x07);
    
    print_col("SSE: ", 0x0B);
    println_col((features & (1 << 25)) ? "Var" : "Yok", 0x07);
    
    print_col("SSE2: ", 0x0B);
    println_col((features & (1 << 26)) ? "Var" : "Yok", 0x07);
    
    putc_col('\n', 0x07);
    println_col("-- Bellek Bilgileri --", 0x0B);
    
    print_col("CatLoaf FS Boyutu: ", 0x0B);
    print_u32((FS_LBA_SECTORS * 512) / 1024, 0x07);
    println_col(" KB", 0x07);
    
    print_col("Kernel Yaklasik: ", 0x0B);
    println_col("32 KB", 0x07);
    
    putc_col('\n', 0x07);
    println_col("-- Dosya Sistemi Kullanimi --", 0x0B);
    
    print_col("Dizinler: ", 0x0B);
    print_u32(used_dirs, 0x07);
    print_col(" / ", 0x07);
    print_u32(FS_MAX_DIRS, 0x07);
    putc_col('\n', 0x07);
    
    print_col("Dosyalar: ", 0x0B);
    print_u32(used_files, 0x07);
    print_col(" / ", 0x07);
    print_u32(FS_MAX_FILES, 0x07);
    putc_col('\n', 0x07);
    
    print_col("Veri Kullanimi: ", 0x0B);
    print_u32(used_bytes, 0x07);
    print_col(" / ", 0x07);
    print_u32(FS_MAX_FILES * FS_DATA_MAX, 0x07);
    println_col(" byte", 0x07);
    
    putc_col('\n', 0x07);
    println_col("-- Sistem --", 0x0B);
    print_col("Calisma Modu: ", 0x0B);
    println_col("Protected Mode (32-bit)", 0x07);
    
    print_col("Calisma Konumu (CWD): ", 0x0B);
    fs_build_path(g_cwd, path_buf, FS_PATH_MAX);
    println_col(path_buf, 0x07);
    
    putc_col('\n', 0x07);
    println_col("[ Herhangi tusa bas ]", 0x0F);
    
    while (1) {
        if (inb(0x64) & 1) {
            inb(0x60);
            break;
        }
    }
}

static void cmd_veteriner(void) {
    int r;
    if (fs_load() == 0) {
        r = fs_repair();
        if (r < 0) {
            println_col("Veteriner: onarim yazma hatasi.", 0x0C);
        } else if (r == 0) {
            println_col("Veteriner: CatLoaf FS saglikli.", 0x0A);
        } else {
            println_col("Veteriner: bozuk kayitlar onarildi.", 0x0A);
        }
        return;
    }
    fs_format();
    if (fs_save() == 0) println_col("Veteriner: FS onarildi (sifirlandi).", 0x0A);
    else println_col("Veteriner: disk yazma hatasi.", 0x0C);
}

static void cmd_uyu(void) {
    println_col("Uykuya geciliyor...", 0x0E);
    outw(0x604, 0x2000); /* qemu acpi */
    outw(0xB004, 0x2000);
    println_col("Kapatma desteklenmedi.", 0x0C);
}

static void cmd_miyav(void) {
    println_col("Miyav! Gizli komut acildi.", 0x0D);
    println_col("Benimle oyun oynamak mi istiyorsun? :3", 0x0D);
}

static void cmd_esne(void) {
    println_col("Esneme reboot...", 0x0E);
    while (inb(0x64) & 0x02) {
    }
    outb(0x64, 0xFE);
}

static void cmd_yardim(void) {
    println_col("Komutlar:", 0x0B);
    println_col("- Gozleme", 0x07);
    println_col("- pwd", 0x07);
    println_col("- cd <dizin>", 0x07);
    println_col("- Yirt <dosya>", 0x07);
    println_col("- Pencele <dizin>", 0x07);
    println_col("- Ton baligi <dosya>", 0x07);
    println_col("- kedi mamasi <dizin>", 0x07);
    println_col("- avla <dosya> <icerik>", 0x07);
    println_col("- cat <dosya>", 0x07);
    println_col("- ml <ifade>", 0x07);
    println_col("- gui", 0x07);
    println_col("- top", 0x07);
    println_col("- veteriner", 0x07);
    println_col("- uyu", 0x07);
    println_col("- esne", 0x07);
}

static void handle_command(const char* line) {
    const char* s = skip_spaces(line);

    if (*s == '\0') return;

    if (streq_ci(s, "gozleme")) {
        cmd_gozleme();
        return;
    }
    if (streq_ci(s, "pwd")) {
        cmd_pwd();
        return;
    }
    if (streq_ci(s, "cd")) {
        cmd_cd("");
        return;
    }
    if (streq_ci(s, "yardim")) {
        cmd_yardim();
        return;
    }
    if (streq_ci(s, "veteriner")) {
        cmd_veteriner();
        return;
    }
    if (streq_ci(s, "uyu")) {
        cmd_uyu();
        return;
    }
    if (streq_ci(s, "esne")) {
        cmd_esne();
        return;
    }
    if (starts_ci(s, "kedi mamasi ")) {
        cmd_kedi_mamasi(skip_spaces(s + 12));
        return;
    }
    if (starts_ci(s, "cd ")) {
        cmd_cd(skip_spaces(s + 3));
        return;
    }
    if (starts_ci(s, "ton baligi ")) {
        cmd_ton_baligi(skip_spaces(s + 11));
        return;
    }
    if (starts_ci(s, "yirt ")) {
        cmd_yirt(skip_spaces(s + 5));
        return;
    }
    if (starts_ci(s, "pencele ")) {
        cmd_pencele(skip_spaces(s + 8));
        return;
    }
    if (starts_ci(s, "avla ")) {
        cmd_avla(s + 5);
        return;
    }
    if (starts_ci(s, "cat ")) {
        cmd_cat(skip_spaces(s + 4));
        return;
    }
    if (starts_ci(s, "ml ")) {
        cmd_ml(skip_spaces(s + 3));
        return;
    }
    if (streq_ci(s, "gui")) {
        cmd_gui();
        return;
    }
    if (streq_ci(s, "miyav")) {
        cmd_miyav();
        return;
    }
    if (streq_ci(s, "top")) {
        cmd_top();
        return;
    }

    println_col("Bilinmeyen komut. yardim yaz.", 0x0C);
}

static char scan_to_ascii(u8 sc, int shift) {
    switch (sc) {
    case 0x02: return shift ? '!' : '1';
    case 0x03: return shift ? '@' : '2';
    case 0x04: return shift ? '#' : '3';
    case 0x05: return shift ? '$' : '4';
    case 0x06: return shift ? '%' : '5';
    case 0x07: return shift ? '^' : '6';
    case 0x08: return shift ? '&' : '7';
    case 0x09: return shift ? '*' : '8';
    case 0x0A: return shift ? '(' : '9';
    case 0x0B: return shift ? ')' : '0';
    case 0x0C: return shift ? '_' : '-';
    case 0x0D: return shift ? '+' : '=';
    case 0x10: return shift ? 'Q' : 'q';
    case 0x11: return shift ? 'W' : 'w';
    case 0x12: return shift ? 'E' : 'e';
    case 0x13: return shift ? 'R' : 'r';
    case 0x14: return shift ? 'T' : 't';
    case 0x15: return shift ? 'Y' : 'y';
    case 0x16: return shift ? 'U' : 'u';
    case 0x17: return shift ? 'I' : 'i';
    case 0x18: return shift ? 'O' : 'o';
    case 0x19: return shift ? 'P' : 'p';
    case 0x1E: return shift ? 'A' : 'a';
    case 0x1F: return shift ? 'S' : 's';
    case 0x20: return shift ? 'D' : 'd';
    case 0x21: return shift ? 'F' : 'f';
    case 0x22: return shift ? 'G' : 'g';
    case 0x23: return shift ? 'H' : 'h';
    case 0x24: return shift ? 'J' : 'j';
    case 0x25: return shift ? 'K' : 'k';
    case 0x26: return shift ? 'L' : 'l';
    case 0x27: return shift ? ':' : ';';
    case 0x28: return shift ? '"' : '\'';
    case 0x2B: return shift ? '|' : '\\';
    case 0x2C: return shift ? 'Z' : 'z';
    case 0x2D: return shift ? 'X' : 'x';
    case 0x2E: return shift ? 'C' : 'c';
    case 0x2F: return shift ? 'V' : 'v';
    case 0x30: return shift ? 'B' : 'b';
    case 0x31: return shift ? 'N' : 'n';
    case 0x32: return shift ? 'M' : 'm';
    case 0x33: return shift ? '<' : ',';
    case 0x34: return shift ? '>' : '.';
    case 0x35: return shift ? '?' : '/';
    case 0x39: return ' ';
    default: return 0;
    }
}

void kmain(void) {
    int shift = 0;
    int ext = 0;

    if ((int)sizeof(CatLoafFs) > (FS_LBA_SECTORS * 512)) {
        screen_clear();
        println_col("CatLoaf FS boyutu sektor alanini asti.", 0x0C);
        for (;;) {
        }
    }

    screen_clear();
    if (show_boot_loader()) {
        show_recovery_menu();
    }
    print_header();
    if (fs_load() != 0) {
        fs_format();
        if (fs_save() == 0) println_col("CatLoaf FS ilk kez hazirlandi.", 0x0A);
        else println_col("CatLoaf FS disk yazma hatasi.", 0x0C);
    } else {
        if (fs_repair() < 0) {
            println_col("CatLoaf FS onarim yazma hatasi.", 0x0C);
        }
        println_col("CatLoaf FS diskten yuklendi.", 0x0A);
    }
    check_birthday_message();
    if (g_cwd < 0 || g_cwd >= FS_MAX_DIRS || !g_fs.dirs[g_cwd].used) g_cwd = 0;
    prompt();

    for (;;) {
        u8 status = inb(0x64);
        u8 sc;
        char ch;

        if ((status & 1) == 0) continue;

        sc = inb(0x60);
        if (sc == 0xE0) {
            ext = 1;
            continue;
        }
        if (ext) {
            ext = 0;
            continue;
        }

        if (sc == 0x2A || sc == 0x36) {
            shift = 1;
            continue;
        }
        if (sc == 0xAA || sc == 0xB6) {
            shift = 0;
            continue;
        }
        if (sc & 0x80) continue;

        if (sc == 0x1C) {
            putc_col('\n', 0x0F);
            line_buf[line_len] = '\0';
            handle_command(line_buf);
            prompt();
            line_len = 0;
            continue;
        }

        if (sc == 0x0E) {
            if (line_len > 0 && cursor > prompt_start) {
                cursor--;
                VGA[cursor] = 0x0720;
                line_len--;
            }
            continue;
        }

        ch = scan_to_ascii(sc, shift);
        if (ch && line_len < LINE_MAX - 1) {
            line_buf[line_len++] = ch;
            putc_col(ch, 0x0F);
        }
    }
}
