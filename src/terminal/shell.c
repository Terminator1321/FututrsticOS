#include "shell.h"
#include "../drivers/ata/ata.h"
#include "../drivers/timer/timer.h"
#include "../fs/fs.h"
#include "../gui/gui.h"
#include "../gui/wallpaper.h"
#include "../io.h"
#include "../memory/kmalloc.h"
#include "../memory/pmm.h"
#include "../process/process.h"
#include "../system/system.h"
#include "terminal.h"

#define CMD_MAX 256

static char cmd_buf[CMD_MAX];
static int cmd_len = 0;

static void print_hex8(uint8_t value) {
    const char hex[] = "0123456789ABCDEF";
    terminal_putchar(hex[(value >> 4) & 0xF]);
    terminal_putchar(hex[value & 0xF]);
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

// Reads one unsigned decimal number starting at *s (skipping leading
// spaces), advances *s past it, and clamps the result to 0-255 so it can
// feed straight into an RGB() color channel. Returns 0 if there's no digit
// there to read.
static int parse_color_channel(const char **s, int *out) {
    while (**s == ' ')
        (*s)++;

    if (**s < '0' || **s > '9')
        return 0;

    int v = 0;
    while (**s >= '0' && **s <= '9') {
        v = v * 10 + (**s - '0');
        (*s)++;
    }

    *out = (v > 255) ? 255 : v;
    return 1;
}

static int parse_rgb(const char *args, int *r, int *g, int *b) {
    const char *p = args;
    return parse_color_channel(&p, r) &&
           parse_color_channel(&p, g) &&
           parse_color_channel(&p, b);
}

static void show_prompt(void) {
    terminal_print(fs_get_pwd());
    terminal_print("> ");
}

static void execute_command(void) {
    cmd_buf[cmd_len] = '\0';
    char *args = 0;
    for (int i = 0; cmd_buf[i]; i++) {
        if (cmd_buf[i] == ' ') {
            cmd_buf[i] = '\0';
            args = &cmd_buf[i + 1];
            break;
        }
    }

    terminal_putchar('\n');
    if (str_eq(cmd_buf, "ver")) {
        terminal_print("Nano OS v0.2  |  NanoFS v2\n");

    } else if (str_eq(cmd_buf, "help")) {
        terminal_print("Commands:\n");
        terminal_print("  System  : ver, help, clear, uptime, meminfo, usage, reboot, shutdown\n");
        terminal_print("  Disk    : diskread, diskwrite, format, mount\n");
        terminal_print("  Files   : touch <name>, ls, cat <file>, write <file> <text>\n");
        terminal_print("            append <file> <text>, rm <file>, mv <old> <new>\n");
        terminal_print("            stat <name>\n");
        terminal_print("  Dirs    : mkdir <name>, cd <dir>, pwd\n");
        terminal_print("  Other   : say <text>, alloc\n");
        terminal_print("  Desktop : bg <r> <g> <b>, bg default, bg image\n");
        terminal_print("  Process : run <program>\n");
        terminal_print("  Window  : show\n");
    } else if (str_eq(cmd_buf, "clear")) {
        terminal_clear();
    } else if (str_eq(cmd_buf, "say")) {
        if (args && *args) {
            terminal_print(args);
            terminal_putchar('\n');
        } else {
            terminal_print("Usage: say <text>\n");
        }
    } else if (str_eq(cmd_buf, "uptime")) {
        uint64_t seconds = timer_get_ticks() / 100;
        terminal_print("Uptime: ");
        print_uint(seconds);
        terminal_print(" seconds\n");
    } else if (str_eq(cmd_buf, "reboot")) {
        terminal_print("Rebooting...\n");
        reboot_system();
    } else if (str_eq(cmd_buf, "meminfo")) {
        terminal_print("Framebuffer : ");
        print_uint(system_width);
        terminal_putchar('x');
        print_uint(system_height);
        terminal_putchar('\n');
        terminal_print("Timer       : ");
        print_uint(system_timer_hz);
        terminal_print(" Hz\n");
        terminal_print("Ticks       : ");
        print_uint(timer_get_ticks());
        terminal_putchar('\n');
        terminal_print("Heap        : ");
        print_uint(kmalloc_used());
        terminal_print(" / ");
        print_uint(kmalloc_total());
        terminal_print(" bytes\n");
    } else if (str_eq(cmd_buf, "usage")) {
        uint64_t total_ticks = timer_get_ticks();
        uint64_t busy_ticks = process_get_busy_ticks();
        uint64_t cpu_percent = total_ticks ? (busy_ticks * 100) / total_ticks : 0;

        uint64_t total_pages = pmm_get_total_pages();
        uint64_t free_pages = pmm_get_free_pages();
        uint64_t used_pages = total_pages - free_pages;
        uint64_t ram_percent = total_pages ? (used_pages * 100) / total_pages : 0;

        uint32_t used_sectors, total_sectors;
        fs_disk_stats(&used_sectors, &total_sectors);
        uint64_t disk_free_kb = ((uint64_t)(total_sectors - used_sectors) * 512) / 1024;
        uint64_t disk_total_kb = ((uint64_t)total_sectors * 512) / 1024;
        uint64_t disk_percent = total_sectors ? ((uint64_t)used_sectors * 100) / total_sectors : 0;

        terminal_print("CPU  : ");
        print_uint(cpu_percent);
        terminal_print("%\n");

        terminal_print("RAM  : ");
        print_uint(used_pages * 4);
        terminal_print(" / ");
        print_uint(total_pages * 4);
        terminal_print(" KB (");
        print_uint(ram_percent);
        terminal_print("%)\n");

        terminal_print("Disk : ");
        print_uint(disk_free_kb);
        terminal_print(" KB free / ");
        print_uint(disk_total_kb);
        terminal_print(" KB (");
        print_uint(disk_percent);
        terminal_print("% used)\n");
    } else if (str_eq(cmd_buf, "alloc")) {
        void *p = kmalloc(64);
        if (p)
            terminal_print("Allocated 64 bytes\n");
        else
            terminal_print("Out of memory\n");
    } else if (str_eq(cmd_buf, "diskread")) {
        uint8_t sector[512];
        if (ata_read_sector(0, sector) != 0) {
            terminal_print("Read failed\n");
        } else {
            for (int i = 0; i < 16; i++) {
                print_hex8(sector[i]);
                terminal_putchar(' ');
            }
            terminal_putchar('\n');
        }
    } else if (str_eq(cmd_buf, "diskwrite")) {
        uint8_t sector[512] = {0};
        sector[0] = 'N';
        sector[1] = 'A';
        sector[2] = 'N';
        sector[3] = 'O';
        sector[4] = 'O';
        sector[5] = 'S';
        terminal_print("Writing sector 0...\n");
        int r = ata_write_sector(0, sector);
        terminal_print("ATA result: ");
        if (r == 0)
            terminal_print("SUCCESS\n");
        else if (r == -1)
            terminal_print("TIMEOUT\n");
        else if (r == -2)
            terminal_print("ATA ERROR\n");
        else if (r == -3)
            terminal_print("DEVICE FAULT\n");
        else
            terminal_print("UNKNOWN ERROR\n");
    } else if (str_eq(cmd_buf, "format")) {
        fs_format();
        terminal_print("Disk formatted\n");
    } else if (str_eq(cmd_buf, "mount")) {
        if (fs_mount())
            terminal_print("Filesystem mounted\n");
        else
            terminal_print("No filesystem found\n");
    } else if (str_eq(cmd_buf, "touch")) {
        if (!args || !*args) {
            terminal_print("Usage: touch <name>\n");
        } else {
            int r = fs_create(args);
            if (r == 0)
                terminal_print("File created\n");
            else if (r == -1)
                terminal_print("Already exists\n");
            else
                terminal_print("Inode table full\n");
        }
    } else if (str_eq(cmd_buf, "ls")) {
        fs_inode_t files[FS_MAX_INODES];
        int count = fs_list(files, FS_MAX_INODES);
        if (count == 0) {
            terminal_print("(empty)\n");
        } else {
            for (int i = 0; i < count; i++) {
                if (files[i].is_directory) {
                    terminal_print("[DIR]  ");
                    terminal_print(files[i].name);
                } else {
                    terminal_print("[FILE] ");
                    terminal_print(files[i].name);
                    terminal_print("  (");
                    print_uint(files[i].size);
                    terminal_print(" B)");
                }
                terminal_putchar('\n');
            }
        }
    } else if (str_eq(cmd_buf, "write")) {
        if (!args || !*args) {
            terminal_print("Usage: write <file> <text>\n");
        } else {
            char *text = 0;
            for (int i = 0; args[i]; i++) {
                if (args[i] == ' ') {
                    args[i] = '\0';
                    text = &args[i + 1];
                    break;
                }
            }
            if (!text || !*text) {
                terminal_print("Usage: write <file> <text>\n");
            } else {
                int r = fs_write(args, text);
                if (r == 0)
                    terminal_print("Written\n");
                else if (r == -2)
                    terminal_print("Is a directory\n");
                else
                    terminal_print("File not found\n");
            }
        }
    } else if (str_eq(cmd_buf, "append")) {
        if (!args || !*args) {
            terminal_print("Usage: append <file> <text>\n");
        } else {
            char *text = 0;
            for (int i = 0; args[i]; i++) {
                if (args[i] == ' ') {
                    args[i] = '\0';
                    text = &args[i + 1];
                    break;
                }
            }
            if (!text || !*text) {
                terminal_print("Usage: append <file> <text>\n");
            } else {
                int r = fs_append(args, text);
                if (r == 0)
                    terminal_print("Appended\n");
                else if (r == -2)
                    terminal_print("Is a directory\n");
                else
                    terminal_print("File not found\n");
            }
        }
    } else if (str_eq(cmd_buf, "cat")) {
        if (!args || !*args) {
            terminal_print("Usage: cat <file>\n");
        } else {
            char buffer[FS_MAX_BLOCKS * 512];
            int r = fs_read(args, buffer, sizeof(buffer));
            if (r >= 0) {
                terminal_print(buffer);
                terminal_putchar('\n');
            } else if (r == -2)
                terminal_print("Is a directory\n");
            else
                terminal_print("File not found\n");
        }
    } else if (str_eq(cmd_buf, "rm")) {
        if (!args || !*args) {
            terminal_print("Usage: rm <file>\n");
        } else {
            int r = fs_delete(args);
            if (r == 0)
                terminal_print("Deleted\n");
            else if (r == -2)
                terminal_print("Is a directory – use rmdir\n");
            else
                terminal_print("File not found\n");
        }
    } else if (str_eq(cmd_buf, "mv")) {
        if (!args || !*args) {
            terminal_print("Usage: mv <old> <new>\n");
        } else {
            char *newname = 0;
            for (int i = 0; args[i]; i++) {
                if (args[i] == ' ') {
                    args[i] = '\0';
                    newname = &args[i + 1];
                    break;
                }
            }
            if (!newname || !*newname) {
                terminal_print("Usage: mv <old> <new>\n");
            } else {
                int r = fs_rename(args, newname);
                if (r == 0)
                    terminal_print("Renamed\n");
                else if (r == -2)
                    terminal_print("Name already taken\n");
                else
                    terminal_print("File not found\n");
            }
        }
    } else if (str_eq(cmd_buf, "stat")) {
        if (!args || !*args) {
            terminal_print("Usage: stat <name>\n");
        } else {
            fs_inode_t node;
            if (fs_stat(args, &node) == 0) {
                terminal_print("Name : ");
                terminal_print(node.name);
                terminal_putchar('\n');
                terminal_print("Type : ");
                terminal_print(node.is_directory ? "directory" : "file");
                terminal_putchar('\n');
                terminal_print("Size : ");
                print_uint(node.size);
                terminal_print(" bytes\n");
            } else {
                terminal_print("Not found\n");
            }
        }
    } else if (str_eq(cmd_buf, "mkdir")) {
        if (!args || !*args) {
            terminal_print("Usage: mkdir <name>\n");
        } else {
            int r = fs_mkdir(args);
            if (r == 0)
                terminal_print("Directory created\n");
            else if (r == -1)
                terminal_print("Already exists\n");
            else
                terminal_print("Inode table full\n");
        }
    } else if (str_eq(cmd_buf, "cd")) {
        if (!args || !*args) {
            terminal_print("Usage: cd <directory>\n");
        } else {
            int r = fs_change_dir(args);
            if (r == -1)
                terminal_print("Not found\n");
            else if (r == -2)
                terminal_print("Not a directory\n");
        }
    } else if (str_eq(cmd_buf, "pwd")) {
        terminal_print(fs_get_pwd());
        terminal_putchar('\n');
    } else if (str_eq(cmd_buf, "shutdown")) {
        shutdown_system();
    } else if (str_eq(cmd_buf, "bg")) {
        if (!args || !*args) {
            terminal_print("Usage: bg <r> <g> <b>  (each 0-255)\n");
            terminal_print("       bg default\n");
            terminal_print("       bg image\n");
        } else if (str_eq(args, "default")) {
            gui_set_bg_color(RGB(20, 20, 20));
            terminal_print("Background reset\n");
        } else if (str_eq(args, "image")) {
            int r = wallpaper_load();
            if (r == 0)
                terminal_print("Wallpaper loaded from disk\n");
            else if (r == -1)
                terminal_print("No wallpaper on disk (pack one with wallpaper_pack first)\n");
            else
                terminal_print("Wallpaper header on disk is invalid\n");
        } else {
            int r, g, b;
            if (parse_rgb(args, &r, &g, &b)) {
                gui_set_bg_color(RGB((uint8_t)r, (uint8_t)g, (uint8_t)b));
                terminal_print("Background updated\n");
            } else {
                terminal_print("Usage: bg <r> <g> <b>  (each 0-255)\n");
            }
        }
    } else if (str_eq(cmd_buf, "show")) {
        gui_restore_terminal();
        terminal_print("Terminal shown\n");
    } else if (str_eq(cmd_buf, "run")) {
        if (!args || !*args) {
            terminal_print("Usage: run <program>\n");
        } else {
            int r = process_exec(args);

            if (r == -2)
                terminal_print("Process already running\n");
            else if (r != 0)
                terminal_print("Failed to start process\n");
        }
    } else if (cmd_len != 0) {
        terminal_print("Unknown command: ");
        terminal_print(cmd_buf);
        terminal_putchar('\n');
    }

    // Every command (or empty Enter) ends here: clear the line buffer and
    // show a fresh prompt. Previously this only happened inside the "run"
    // branch, so cmd_len was never reset after any other command - the
    // buffer just kept growing with every keystroke and no new prompt
    // ever appeared, which looks exactly like the keyboard stopped
    // responding.
    cmd_len = 0;
    show_prompt();
}

    void shell_init(void) { show_prompt(); }

    void shell_input(char c) {
        if (c == '\n') {
            execute_command();
            return;
        }
        if (c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                terminal_putchar('\b');
            }
            return;
        }
        if (cmd_len >= CMD_MAX - 1)
            return;
        cmd_buf[cmd_len++] = c;
        terminal_putchar(c);
    }

    void print_uint(uint64_t value) {
        char buf[21];
        int i = 0;
        if (value == 0) {
            terminal_putchar('0');
            return;
        }
        while (value > 0) {
            buf[i++] = '0' + (value % 10);
            value /= 10;
        }
        while (i > 0)
            terminal_putchar(buf[--i]);
    }

    void reboot_system(void) {
        while (inb(0x64) & 0x02)
            ;
        outb(0x64, 0xFE);
        for (;;)
            __asm__ volatile("hlt");
    }

    void shutdown_system(void) {
        terminal_print("Shutting down...\n");
        outw(0xB004, 0x2000);
        outw(0x604, 0x2000);
        outw(0x4004, 0x3400);
        for (;;)
            __asm__ volatile("hlt");
    }