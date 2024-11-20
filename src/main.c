#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
static void clear_screen(void)
{
    system("cls");
}

static int get_char(void)
{
    return _getch();
}

static void set_text_color(WORD attributes)
{
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attributes);
}

#else
#include <termios.h>
static void clear_screen(void)
{
    system("clear");
}

static int get_char(void)
{
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

static void set_text_color(int color)
{
    printf("\033[%dm", color);
}
#endif

#define MAX_NAME_LEN 255 // Probably can shorten or make dynamic
#define MAX_COMMAND_LEN 255
#define MAX_MENU_ITEMS 10

typedef enum
{
    WIN,
    MACOS,
    LINUX
} OperatingSystem;

typedef struct
{
    OperatingSystem os;
    char *package_manager;
} MachineInfo;

typedef struct
{
    char *name;
    char *check_command;
    char *install_command;
} Dependency;

typedef struct
{
    char *name;
    char *run_command;
    Dependency *dependencies;
    int dependency_count;
} Tool;

typedef struct
{
    char *name;
    Tool *tools;
    int tool_count;
} Framework;

typedef struct
{
    char *name;
    Framework *frameworks;
    int framework_count;
} Category;

typedef struct
{
    Category *categories;
    int category_count;
    Dependency *dependencies;
    int dependency_count;
} Configuration;

static void print_masthead(void)
{
    static const char *masthead[] = {
        " ____  ____  ____     _  _____ ____  _____    _      _  ____ ",
        "/  __\\/  __\\/  _ \\   / |/  __//   _\\/__ __\\  / \\  /|/ \\/_   \\",
        "|  \\/||  \\/|| / \\|   | ||  \\  |  /    / \\    | |  ||| | /   /",
        "|  __/|    /| \\_/|/\\_| ||  /_ |  \\_   | |    | |/\\||| |/   /_",
        "\\_/   \\_/\\_\\\\____/\\____/\\____\\\\____/  \\_/    \\_/  \\|\\_/\\____/",
        ""};
    int size = sizeof(masthead) / sizeof(masthead[0]);
    for (size_t i = 0; i < size; i++)
    {
        puts(masthead[i]);
    }
}

void get_parent_directory(char *buffer, size_t size)
{
#ifdef _WIN32
    // Get the path of the executable on Windows
    GetModuleFileName(NULL, buffer, (DWORD)size);
    // Remove the executable name to get the directory
    char *last_slash = strrchr(buffer, '\\');
    if (last_slash != NULL)
    {
        *last_slash = '\0';
        // Find the parent directory by truncating at the next-to-last slash
        char *second_last_slash = strrchr(buffer, '\\');
        if (second_last_slash != NULL)
        {
            *second_last_slash = '\0';
        }
    }
#else
    // Get the path of the executable on Linux/macOS
    ssize_t len = readlink("/proc/self/exe", buffer, size - 1);
    if (len != -1)
    {
        buffer[len] = '\0';
        // Remove the executable name to get the directory
        char *last_slash = strrchr(buffer, '/');
        if (last_slash != NULL)
        {
            *last_slash = '\0';
            // Find the parent directory by truncating at the next-to-last slash
            char *second_last_slash = strrchr(buffer, '/');
            if (second_last_slash != NULL)
            {
                *second_last_slash = '\0';
            }
        }
    }
    else
    {
        perror("readlink");
        exit(EXIT_FAILURE);
    }
#endif
}
int parse_json_file(const char *filename, Configuration *configuration, MachineInfo *machine_info)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("Error opening file");
        return 0;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *json_string = malloc(file_size + 1);
    if (json_string == NULL)
    {
        printf("Configuration file could not be read.\n");
        return 0;
    }
    fread(json_string, 1, file_size, file);
    json_string[file_size] = '\0';
    fclose(file);

    cJSON *json = cJSON_Parse(json_string);
    free(json_string);

    if (!json)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        fprintf(stderr, "JSON parsing error: %s\n", error_ptr);
        return 0;
    }

    // Parse dependencies first
    cJSON *dependencies = cJSON_GetObjectItemCaseSensitive(json, "dependencies");
    if (!cJSON_IsArray(dependencies))
    {
        fprintf(stderr, "Invalid JSON schema: 'dependencies' should be an array.\n");
        cJSON_Delete(json);
        return 0;
    }
    configuration->dependency_count = cJSON_GetArraySize(dependencies);
    configuration->dependencies = malloc(configuration->dependency_count * sizeof(Dependency));
    if (configuration->dependencies == NULL)
    {
        printf("Memory allocation failed for configuration obj\n");
        return 0;
    }
    for (int i = 0; i < configuration->dependency_count; i++)
    {
        cJSON *dep = cJSON_GetArrayItem(dependencies, i);
        cJSON *name = cJSON_GetObjectItemCaseSensitive(dep, "name");
        cJSON *check_command = cJSON_GetObjectItemCaseSensitive(dep, "check_command");
        cJSON *install_commands = cJSON_GetObjectItemCaseSensitive(dep, "install_commands");
        cJSON *install_command = NULL;
        switch (machine_info->os)
        {
        case WIN:
            install_command = cJSON_GetObjectItemCaseSensitive(install_commands, "windows");
            break;
        case MACOS:
            install_command = cJSON_GetObjectItemCaseSensitive(install_commands, "macos");
            break;
        case LINUX:
            install_command = cJSON_GetObjectItemCaseSensitive(install_commands, machine_info->package_manager);
            break;
        }
        if (install_command == NULL || !cJSON_IsString(install_command))
        {
            printf("Cannot find installation command for dependency: %s\n", name->valuestring);
            cJSON_Delete(json);
            return 0;
        }
        configuration->dependencies[i].install_command = _strdup(install_command->valuestring);
        configuration->dependencies[i].name = _strdup(name->valuestring);
        configuration->dependencies[i].check_command = _strdup(check_command->valuestring);
    }

    // Parse categories
    cJSON *categories_json = cJSON_GetObjectItemCaseSensitive(json, "categories");
    configuration->category_count = cJSON_GetArraySize(categories_json);
    configuration->categories = malloc(configuration->category_count * sizeof(Category));
    if (configuration->categories == NULL)
    {
        printf("Memory allocation failed for configuration obj\n");
        return 0;
    }
    for (int i = 0; i < configuration->category_count; i++)
    {
        cJSON *category_json = cJSON_GetArrayItem(categories_json, i);
        cJSON *category_name = cJSON_GetObjectItemCaseSensitive(category_json, "name");
        configuration->categories[i].name = _strdup(category_name->valuestring);

        cJSON *frameworks_json = cJSON_GetObjectItemCaseSensitive(category_json, "frameworks");
        configuration->categories[i].framework_count = cJSON_GetArraySize(frameworks_json);
        configuration->categories[i].frameworks = malloc(configuration->categories[i].framework_count * sizeof(Framework));
        ;
        if (configuration->categories[i].frameworks == NULL)
        {
            printf("Memory allocation failed for configuration obj\n");
            return 0;
        }
        for (int j = 0; j < configuration->categories[i].framework_count; j++)
        {
            cJSON *framework_json = cJSON_GetArrayItem(frameworks_json, j);
            cJSON *framework_name = cJSON_GetObjectItemCaseSensitive(framework_json, "name");
            configuration->categories[i].frameworks[j].name = _strdup(framework_name->valuestring);

            cJSON *tools_json = cJSON_GetObjectItemCaseSensitive(framework_json, "tools");
            configuration->categories[i].frameworks[j].tool_count = cJSON_GetArraySize(tools_json);
            configuration->categories[i].frameworks[j].tools = malloc(configuration->categories[i].frameworks[j].tool_count * sizeof(Tool));
            if (configuration->categories[i].frameworks[j].tools == NULL)
            {
                printf("Memory allocation failed for configuration obj\n");
                return 0;
            }
            for (int k = 0; k < configuration->categories[i].frameworks[j].tool_count; k++)
            {
                cJSON *tool_json = cJSON_GetArrayItem(tools_json, k);
                cJSON *tool_name = cJSON_GetObjectItemCaseSensitive(tool_json, "name");
                cJSON *tool_command = cJSON_GetObjectItemCaseSensitive(tool_json, "command");
                cJSON *tool_dependencies = cJSON_GetObjectItemCaseSensitive(tool_json, "dependencies");
                configuration->categories[i].frameworks[j].tools[k].dependency_count = cJSON_GetArraySize(tool_dependencies);
                configuration->categories[i].frameworks[j].tools[k].dependencies = malloc(configuration->categories[i].frameworks[j].tools[k].dependency_count * sizeof(Dependency));
                if (configuration->categories[i].frameworks[j].tools[k].dependencies == NULL)
                {
                    printf("Memory allocation failed for configuration obj\n");
                    return 0;
                }
                for (int l = 0; l < configuration->categories[i].frameworks[j].tools[k].dependency_count; l++)
                {
                    cJSON *tool_dep_json = cJSON_GetArrayItem(tool_dependencies, l);
                    char *dep_key = _strdup(tool_dep_json->valuestring);
                    for (int m = 0; m < configuration->dependency_count; m++)
                    {
                        if (strcmp(dep_key, configuration->dependencies[m].name) == 0)
                        {
                            configuration->categories[i].frameworks[j].tools[k].dependencies[l].check_command = configuration->dependencies[m].check_command;
                            configuration->categories[i].frameworks[j].tools[k].dependencies[l].install_command = configuration->dependencies[m].install_command;
                            configuration->categories[i].frameworks[j].tools[k].dependencies[l].name = configuration->dependencies[m].name;
                        }
                    }
                }
                configuration->categories[i].frameworks[j].tools[k].name = _strdup(tool_name->valuestring);
                configuration->categories[i].frameworks[j].tools[k].run_command = _strdup(tool_command->valuestring);
            }
        }
    }

    cJSON_Delete(json);
    return 1;
}

int get_machine_info(MachineInfo *machine)
{
#if _WIN32
    machine->os = WIN;
#elif __APPLE__
    machine.os = MACOS;
#elif __linux__
    machine.os = LINUX;
    const char *managers[] = {"apt", "pacman", "dnf"};
    const char *paths[] = {"/usr/bin/", "/bin/"};
    int num_managers = sizeof(managers) / sizeof(managers[0]);
    int num_paths = sizeof(paths) / sizeof(paths[0]);

    for (int i = 0; i < num_managers; i++)
    {
        for (int j = 0; j < num_paths; j++)
        {
            char full_path[256];
            snprintf(full_path, sizeof(full_path), "%s%s", paths[j], managers[i]);
            if (access(full_path, X_OK) == 0)
            {
                machine->package_manager = managers[i];
                return 1;
            }
        }
    }

    return 0;
#else
    printf("This operating system is not supported.\n") return 0;
#endif
}

int handle_dependencies(Tool *tool)
{
    for (size_t i = 0; i < tool->dependency_count; i++)
    {
        if (system(tool->dependencies[i].check_command) != 0)
        {
            system(tool->dependencies[i].install_command);
        }
    }
}

char *replace_substring(const char *str, const char *old_sub, const char *new_sub)
{
    if (!str || !old_sub || !new_sub)
        return NULL;

    size_t str_len = strlen(str);
    size_t old_sub_len = strlen(old_sub);
    size_t new_sub_len = strlen(new_sub);

    // If the substring to replace is empty, return a copy of the original string
    if (old_sub_len == 0)
        return _strdup(str);

    // Count occurrences of old_sub in str
    size_t count = 0;
    const char *tmp = str;
    while ((tmp = strstr(tmp, old_sub)))
    {
        count++;
        tmp += old_sub_len;
    }

    // Calculate the length of the new string
    size_t new_len = str_len + (new_sub_len - old_sub_len) * count;

    // Allocate memory for the new string
    char *result = (char *)malloc(new_len + 1);
    if (!result)
    {
        perror("malloc");
        return NULL;
    }

    // Replace old_sub with new_sub
    const char *current = str;
    char *dest = result;
    while ((tmp = strstr(current, old_sub)))
    {
        // Copy characters before the old_sub
        size_t segment_len = tmp - current;
        memcpy(dest, current, segment_len);
        dest += segment_len;

        // Copy the new_sub
        memcpy(dest, new_sub, new_sub_len);
        dest += new_sub_len;

        // Move past the old_sub
        current = tmp + old_sub_len;
    }

    // Copy the remaining part of the original string
    strcpy(dest, current);

    return result;
}

void print_menu(Configuration *conf)
{
    int selection = 0;
    while (1)
    {
        clear_screen();
        print_masthead();
        printf("Main Menu:\n");
        for (int i = 0; i < conf->category_count; i++)
        {
            if (i == selection)
            {
#ifdef _WIN32
                set_text_color(FOREGROUND_BLUE | FOREGROUND_INTENSITY);
#else
                set_text_color(34);
#endif
                printf("> %s\n", conf->categories[i].name);
#ifdef _WIN32
                set_text_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
                set_text_color(0);
#endif
            }
            else
            {
                printf("  %s\n", conf->categories[i].name);
            }
        }
        printf("\nUse arrow keys or 'w'/'s' to navigate. Press Enter to select, or 'q' to quit.\n");

        int key = get_char();
#ifdef _WIN32
        if (key == 224)
            key = get_char(); // Handle arrow keys
#else
        if (key == '\033')
        { // Arrow keys start with escape sequence
            get_char();
            key = get_char();
        }
#endif

        if (key == 'q')
        {
            break;
        }
        else if (key == 72 || key == 'w')
        { // Up
            if (selection > 0)
                selection--;
        }
        else if (key == 80 || key == 's')
        { // Down
            if (selection < conf->category_count - 1)
                selection++;
        }
        else if (key == '\r' || key == '\n')
        { // Enter
            // Submenu for the selected category
            int sub_selection = 0;
            while (1)
            {
                clear_screen();
                print_masthead();
                printf("%s:\n", conf->categories[selection].name);
                for (int j = 0; j < conf->categories[selection].framework_count; j++)
                {
                    if (j == sub_selection)
                    {
#ifdef _WIN32
                        set_text_color(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
#else
                        set_text_color(32);
#endif
                        printf("> %s\n", conf->categories[selection].frameworks[j].name);
#ifdef _WIN32
                        set_text_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
                        set_text_color(0);
#endif
                    }
                    else
                    {
                        printf("  %s\n", conf->categories[selection].frameworks[j].name);
                    }
                }
                printf("\nUse arrow keys or 'w'/'s' to navigate. Press Enter to select, or 'b' to go back.\n");

                key = get_char();
#ifdef _WIN32
                if (key == 224)
                    key = get_char();
#else
                if (key == '\033')
                {
                    get_char();
                    key = get_char();
                }
#endif

                if (key == 'b')
                {
                    break;
                }
                else if (key == 72 || key == 'w')
                { // Up
                    if (sub_selection > 0)
                        sub_selection--;
                }
                else if (key == 80 || key == 's')
                { // Down
                    if (sub_selection < conf->categories[selection].framework_count - 1)
                        sub_selection++;
                }
                else if (key == '\r' || key == '\n')
                { // Enter
                    // Submenu for the selected framework
                    int tool_selection = 0;
                    while (1)
                    {
                        clear_screen();
                        print_masthead();
                        printf("%s -> %s:\n", conf->categories[selection].name, conf->categories[selection].frameworks[sub_selection].name);
                        for (int k = 0; k < conf->categories[selection].frameworks[sub_selection].tool_count; k++)
                        {
                            if (k == tool_selection)
                            {
#ifdef _WIN32
                                set_text_color(FOREGROUND_RED | FOREGROUND_INTENSITY);
#else
                                set_text_color(31);
#endif
                                printf("> %s\n", conf->categories[selection].frameworks[sub_selection].tools[k].name);
#ifdef _WIN32
                                set_text_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
                                set_text_color(0);
#endif
                            }
                            else
                            {
                                printf("  %s\n", conf->categories[selection].frameworks[sub_selection].tools[k].name);
                            }
                        }
                        printf("\nUse arrow keys or 'w'/'s' to navigate. Press Enter to select, or 'b' to go back.\n");

                        key = get_char();
#ifdef _WIN32
                        if (key == 224)
                            key = get_char();
#else
                        if (key == '\033')
                        {
                            get_char();
                            key = get_char();
                        }
#endif

                        if (key == 'b')
                        {
                            break;
                        }
                        else if (key == 72 || key == 'w')
                        { // Up
                            if (tool_selection > 0)
                                tool_selection--;
                        }
                        else if (key == 80 || key == 's')
                        { // Down
                            if (tool_selection < conf->categories[selection].frameworks[sub_selection].tool_count - 1)
                                tool_selection++;
                        }
                        else if (key == '\r' || key == '\n')
                        { // Enter
                            clear_screen();
                            print_masthead();
                            if (strstr(conf->categories[selection].frameworks[sub_selection].tools[tool_selection].run_command, "{}") != NULL)
                            {
                                char proj_name[255];
                                printf("Enter the project name (max 255 characters): ");
                                fgets(proj_name, 255, stdin);
                                size_t len = strlen(proj_name);
                                if (len > 0 && proj_name[len - 1] == '\n')
                                {
                                    proj_name[len - 1] = '\0';
                                }
                                clear_screen();
                                print_masthead();
                                if (!handle_dependencies(&conf->categories[selection].frameworks[sub_selection].tools[tool_selection]))
                                {
                                    printf("Could not install dependencies. Exiting...\n");
                                    return;
                                }

                                if (system(replace_substring(conf->categories[selection].frameworks[sub_selection].tools[tool_selection].run_command, "{}", proj_name)) != 0)
                                {
                                }
                            }
                            else
                            {
                                if (system(conf->categories[selection].frameworks[sub_selection].tools[tool_selection].run_command) != 0)
                                {
                                }
                            }
                            printf("\nPress any key to go back.\n");
                            get_char();
                        }
                    }
                }
            }
        }
    }
}

int main(void)
{
    MachineInfo machineInfo = {0};
    get_machine_info(&machineInfo);
    Configuration configuration = {0};

    char exe_dir[255];
    get_parent_directory(exe_dir, sizeof(exe_dir));
    char config_path[255];
    printf("%s\n", exe_dir);
    snprintf(config_path, sizeof(config_path), "%s/config.json", exe_dir);
    if (!parse_json_file(config_path, &configuration, &machineInfo))
    {
        printf("Could not parse config.json\n");
        return 0;
    };
    print_menu(&configuration);
    return 0;
}